// Median's dungeon lockout (D2DrlgWarp.cpp 0x5d810 / 0x5cee0, D2Dungeon.cpp 0x5d9d0) on D2R's level changes.
//
//   0x491ff0  warp action (game, player, tile): stairs, ladders, trapdoors and tile warps. Detoured; the destination
//             level is worked out the way the function does (linked tile 0x2efee0 -> its room -> level id). A refusal
//             returns without calling the original, D2R's own silent refusal for an unfinished quest (plus the
//             "failed" unit message 0x14, see Allowed).
//   0x58f680  portal object operate function (ctx = {game, portal, player, ?, object class}): town portals, red
//             portals and, if objects.txt gives it the same operate function, the one-way portal. Detoured; the
//             destination is the portal's level word (0x34ad40). A refusal sends the "failed" unit message 0x14 and
//             returns 0, exactly the function's own failure exit.
//
// Allowed entries that did move the player (his room's level differs afterwards) start the level's clock, as D2Sigma's
// warp (0x5cf90) and portal (0x96e80) functions call 0x5d9d0 after the move. State: dungeon::Lockout, per game
// pointer, reset when the game ends (game_end.cpp) and, as a fallback, when the pointer or the frame counter
// (game+0x170) changes. Sigma's quest-table condition in 0x5d810 (leveldefs QuestFlagEx, Entry::questLock) is a third
// refusal, see Allowed. Not ported: the 0x7fb70 item teleport (the cherry item spell, docs/unported.md), the client
// DungeonWidget's art and the 0x88 state packet.
//
// The countdown (Median's DungeonWidget timer): the server part sends the plugin message 'T' (level, ms left;
// client_messages_logic.h) to a player when he enters a level, to everybody when the clock starts, and the client part draws
// "Closing in: mm:ss" and the warning with D2RLoader's OverlayService while the local player stands in that level. The chat
// lines stay as well.
#include "dungeon_lockout.h"

#include "client_messages.h"
#include "client_messages_logic.h"
#include "d2r.h"
#include "dungeon_logic.h"
#include "game_end.h"
#include "quests_common.h"
#include "strings.h"

#define NOMINMAX
#include <windows.h>

#include <D2RLPlugin/overlay.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t WarpRva = 0x491ff0;         // (game, player, tile)
constexpr uint64_t PortalRva = 0x58f680;       // (int64_t* ctx) -> 1 moved, 0 failed
constexpr uint64_t FindTileRva = 0x2efee0;     // (dataFlag, room, class, &out) -> linked tile unit or null
constexpr uint64_t RoomLevelRva = 0x2efc10;    // (room) -> level id
constexpr uint64_t UnitRoomRva = 0x34b440;     // (unit) -> room
constexpr uint64_t PortalLevelRva = 0x34ad40;  // (portal object) -> destination level (word)
constexpr uint64_t UnitMessageRva = 0x491960;  // (player, message, subject)
constexpr uint64_t GetStatRva = 0x2f5020;      // (unit, stat, layer)

// 0x491ff0: mov rax,rsp / push rbx / push rbp / push rdi / push r14 / sub rsp,0x68 / mov rbx,r8 (15 bytes; dump of
// build 93847, timestamp 0x6ab3782c)
constexpr uint8_t WarpEntry[] = {0x48, 0x8b, 0xc4, 0x53, 0x55, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x68, 0x49, 0x8b, 0xd8};
// 0x58f680: mov [rsp+8],rcx / push rbp rbx rsi rdi r14 r15 / mov rbp,rsp (16 bytes)
constexpr uint8_t PortalEntry[] = {0x48, 0x89, 0x4c, 0x24, 0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8b, 0xec};

constexpr int GameDifficulty = 0x104;  // byte
constexpr int GameFrame = 0x170;       // u32
constexpr int UnitClass = 0x04;        // u32: player class / object class
constexpr int32_t StatLevel = 12;
constexpr int32_t MessageFailed = 0x14;  // the portal function's own failure message
constexpr uint64_t LocalPlayerSlotRva = 0x2a23704;  // client: u32, the local player's slot
constexpr uint64_t UnitBySlotRva = 0x9a480;         // client: (slot < 8) -> that player's unit or null

using WarpFn = void (*)(Game* game, Unit* player, Unit* tile);
using PortalFn = int64_t (*)(int64_t* ctx);
using FindTileFn = Unit* (*)(uint8_t dataFlag, void* room, uint32_t cls, void** out);
using RoomLevelFn = int32_t (*)(void* room);
using UnitRoomFn = void* (*)(Unit* unit);
using PortalLevelFn = uint16_t (*)(Unit* portal);
using UnitMessageFn = void (*)(Unit* player, int32_t message, Unit* subject);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using QuestBitFn = int32_t (*)(void* record, int32_t quest, int32_t bit);
using UnitBySlotFn = Unit* (*)(uint32_t slot);

UnitBySlotFn g_unitBySlot = nullptr;
WarpFn g_warpOriginal = nullptr;
PortalFn g_portalOriginal = nullptr;
FindTileFn g_findTile = nullptr;
RoomLevelFn g_roomLevel = nullptr;
UnitRoomFn g_unitRoom = nullptr;
PortalLevelFn g_portalLevel = nullptr;
UnitMessageFn g_unitMessage = nullptr;
GetStatFn g_getStat = nullptr;
QuestBitFn g_questBit = nullptr;

std::mutex g_mutex;
dungeon::Lockout g_lockout;
const D2RL::PluginContext* g_context = nullptr;
int g_refusals = 0;
int g_started = 0;

auto Now() noexcept -> uint64_t {
	return GetTickCount64();
}

auto IsPlayer(const Unit* unit) noexcept -> bool {
	return unit != nullptr && d2r::At<int32_t>(unit, d2r::UnitType) == 0;
}

auto LevelOf(Unit* unit) noexcept -> int32_t {
	void* room = unit != nullptr ? g_unitRoom(unit) : nullptr;
	return room != nullptr ? g_roomLevel(room) : 0;
}

// "Closing in: mm:ss" (Median string 11191) in the active language.
void ClosingLineText(uint32_t seconds, char* out, size_t outSize) noexcept {
	char tmpl[64];
	StringText(mxlstr::KeyClosingIn, clientmsg::ClosingText, tmpl, sizeof(tmpl));
	clientmsg::ClosingLine(seconds, out, outSize, tmpl);
}

void Log(const char* what, int32_t a, int32_t b) noexcept {
	if (g_context != nullptr) {
		char line[160];
		std::snprintf(line, sizeof(line), "mxl-core: lockout: %s (%d -> %d)", what, a, b);
		g_context->LogInfo(line);
	}
}

// Median's refusal texts (string table ids 11189 / 11192) as lines in the player's message log: D2R has no popup for
// the lockout, the vanilla chat line stands in for it (the popup art is custom art, docs/unported.md).
void TellRefusal(Unit* player, const dungeon::Entry& e, const dungeon::Who& who, dungeon::Verdict v) noexcept {
	if (v == dungeon::Verdict::Quest) {
		SendClientMessage(player, clientmsg::QuestLine, clientmsg::Red);
		return;
	}
	if (v == dungeon::Verdict::Sealed) {
		char sealed[256];
		StringText(mxlstr::KeySealed, clientmsg::SealedText, sealed, sizeof(sealed));
		mxlstr::ForEachLine(sealed, clientmsg::ChatTextMax, [player](const char* line, int) {
			SendClientMessage(player, line, clientmsg::Red);
		});
		return;
	}
	char lines[3][64];
	char levelTmpl[64];
	StringText(mxlstr::KeyRequiredLevel, clientmsg::RequiredLevelText, levelTmpl, sizeof(levelTmpl));
	const int n = clientmsg::RequirementLines(e.levelReq[who.difficulty < 3 ? who.difficulty : 2], e.minDifficulty,
	                                          e.classLock, who.level, who.difficulty, who.cls, lines, levelTmpl);
	for (int i = 0; i < n; ++i) {
		SendClientMessage(player, lines[i], clientmsg::Red);
	}
}

// The player's bits of quest `quest` for the quest-table condition (no record, no refusal).
auto QuestBitsOf(Game* game, Unit* player, int quest) noexcept -> dungeon::QuestBits {
	dungeon::QuestBits q;
	void* record = questeng::Record(game, player);
	if (record == nullptr || quest <= 0) {
		return q;
	}
	q.haveRecord = true;
	q.pending = g_questBit(record, quest, 1) != 0;
	q.granted = g_questBit(record, quest, 0) != 0;
	q.completedNow = g_questBit(record, quest, 0xe) != 0;
	q.completedBefore = g_questBit(record, quest, 0xf) != 0;
	return q;
}

// True when the player may enter `dst`; otherwise sends the failure message (D2R's generic "failed" reaction, the one
// the portal function itself uses) and the reason as text. Median's own message is unit message 0x13 and its popup.
auto Allowed(Game* game, Unit* player, int32_t src, int32_t dst) noexcept -> bool {
	const dungeon::Entry* entry = dst > 0 ? dungeon::Find(dst) : nullptr;
	if (entry == nullptr) {
		return true;
	}
	const dungeon::Who who{g_getStat(player, StatLevel, 0), static_cast<int32_t>(d2r::At<uint32_t>(player, UnitClass)),
	                       d2r::At<uint8_t>(game, GameDifficulty)};
	const dungeon::QuestBits quest = QuestBitsOf(game, player, entry->questLock);
	dungeon::Verdict v;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_lockout.Touch(game, d2r::At<uint32_t>(game, GameFrame));
		v = dungeon::Check(g_lockout, who, dst, Now(), quest);
		if (v != dungeon::Verdict::Allow) {
			++g_refusals;
		}
	}
	if (v == dungeon::Verdict::Allow) {
		return true;
	}
	Log(v == dungeon::Verdict::Sealed ? "level sealed, entry refused"
	    : v == dungeon::Verdict::Quest ? "quest not done, entry refused"
	                                   : "entry requirements not met, refused",
	    src, dst);
	g_unitMessage(player, MessageFailed, player);
	TellRefusal(player, *entry, who, v);
	return false;
}

// After an allowed move: the clock of the level the player now stands in.
void Entered(Game* game, Unit* player, int32_t src) noexcept {
	const int32_t now = LevelOf(player);
	if (now <= 0 || now == src) {
		return;
	}
	uint8_t payload[clientmsg::TimerPayloadSize];
	if (dungeon::Find(now) == nullptr) {
		clientmsg::BuildTimerPayload(payload, static_cast<uint16_t>(now), 0);  // the clock of the level he left goes off the screen
		SendPluginMessage(player, clientmsg::PluginKindTimer, payload, sizeof(payload));
		return;
	}
	bool started;
	uint64_t remaining;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_lockout.Touch(game, d2r::At<uint32_t>(game, GameFrame));
		started = g_lockout.Start(now, Now());
		if (started) {
			++g_started;
		}
		remaining = g_lockout.RemainingMs(now, Now());
	}
	// The countdown on the screen: everybody when the clock just started, else the player who came in.
	clientmsg::BuildTimerPayload(payload, static_cast<uint16_t>(now),
	                             static_cast<uint32_t>(remaining > 0xffffffffull ? 0xffffffffull : remaining));
	if (started) {
		SendGamePluginMessage(game, clientmsg::PluginKindTimer, payload, sizeof(payload));
	} else {
		SendPluginMessage(player, clientmsg::PluginKindTimer, payload, sizeof(payload));
	}
	if (started) {
		Log("clock started", src, now);
		// Median's DungeonWidget (packet 0x88) shows the clock to everybody in the game: a log line with the time
		// instead ("Closing in: mm:ss", string 11191) and the warning of string 11193.
		const dungeon::Entry* entry = dungeon::Find(now);
		char line[64];
		ClosingLineText(entry->lockSeconds, line, sizeof(line));
		char warning[96];
		SendGameMessage(game, line, clientmsg::Orange);
		SendGameMessage(game, StringText(mxlstr::KeyExitWarning, clientmsg::ExitWarning, warning, sizeof(warning)), clientmsg::Orange);
	}
}

// --- client: the countdown on the screen -----------------------------------------------------------------------------
const D2RL::OverlayService* g_overlay = nullptr;
std::atomic<uint32_t> g_timerLevel{0};
std::atomic<uint64_t> g_timerEnd{0};  // GetTickCount64 when the clock runs out, 0 = none

// The level the local player stands in; 0 when it cannot be told (no game, no room).
auto ClientLevel() noexcept -> int32_t {
	__try {
		const uint32_t slot = *reinterpret_cast<const uint32_t*>(g_context->exeBase + LocalPlayerSlotRva);
		Unit* unit = slot < 8 ? g_unitBySlot(slot) : nullptr;
		void* room = unit != nullptr ? g_unitRoom(unit) : nullptr;
		return room != nullptr ? g_roomLevel(room) : 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

void OnTimerMessage(const uint8_t* data, size_t n) noexcept {
	if (n != clientmsg::TimerPayloadSize) {
		return;
	}
	const uint32_t ms = clientmsg::Get32(data + 4);
	g_timerLevel.store(clientmsg::Get16(data));
	g_timerEnd.store(ms == 0 ? 0 : Now() + ms);
}

// Text centred on `centerX`, with a black shadow.
void DrawCentred(const D2RL::PluginContext* context, const D2RL::Overlay::Frame* frame, const char* text, float centerX, float y,
                 float size) noexcept {
	const auto length = static_cast<uint32_t>(std::strlen(text));
	D2RL::Overlay::MeasureTextRequest measure{};
	measure.structSize = D2RL::Overlay::MeasureTextRequestSize;
	measure.canvas = frame->canvas;
	measure.textSize = size;
	measure.text = text;
	measure.textLength = length;
	D2RL::Overlay::TextMetrics metrics{};
	metrics.structSize = D2RL::Overlay::TextMetricsSize;
	float width = 0.0F;
	if (g_overlay->measureText(context, &measure, &metrics) == D2RL::Overlay::Result::Success) {
		width = metrics.width;
	}
	for (int pass = 0; pass < 2; ++pass) {
		D2RL::Overlay::TextRequest req{};
		req.structSize = D2RL::Overlay::TextRequestSize;
		req.canvas = frame->canvas;
		const float shadow = pass == 0 ? 1.5F : 0.0F;
		req.position = {centerX - width / 2.0F + shadow, y + shadow};
		req.color = pass == 0 ? D2RL::Overlay::Color{0.0F, 0.0F, 0.0F, 0.9F} : D2RL::Overlay::Color{1.0F, 0.55F, 0.1F, 1.0F};
		req.textSize = size;
		req.text = text;
		req.textLength = length;
		(void)g_overlay->drawText(context, &req);
	}
}

void __cdecl DrawTimer(const D2RL::PluginContext* context, const D2RL::Overlay::Frame* frame, void*) noexcept {
	const uint64_t end = g_timerEnd.load();
	if (end == 0 || frame == nullptr) {
		return;
	}
	const uint64_t now = Now();
	if (now >= end) {
		g_timerEnd.store(0);
		return;
	}
	if (ClientLevel() != static_cast<int32_t>(g_timerLevel.load())) {
		return;  // another level (or no game): the clock belongs to the level it was started for
	}
	char line[64];
	ClosingLineText(clientmsg::SecondsLeft(end - now), line, sizeof(line));
	const float size = frame->defaultTextSize * 1.4F;
	const float x = frame->screenWidth / 2.0F;
	const float y = frame->screenHeight * 0.09F;
	DrawCentred(context, frame, line, x, y, size);
	char warning[96];
	DrawCentred(context, frame, StringText(mxlstr::KeyExitWarning, clientmsg::ExitWarning, warning, sizeof(warning)), x,
	            y + size * 1.3F, frame->defaultTextSize);
}

auto InstallTimerOverlay(const D2RL::PluginContext* context) noexcept -> bool {
	if (!PluginMessagesOn() || context->QueryService(&g_overlay) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasOverlayServiceField(g_overlay, D2RL::OverlayServiceRequiredSize)) {
		g_overlay = nullptr;
		return false;
	}
	g_unitBySlot = reinterpret_cast<UnitBySlotFn>(context->exeBase + UnitBySlotRva);
	SetPluginMessageHandler(clientmsg::PluginKindTimer, &OnTimerMessage);
	D2RL::Overlay::CallbackRegistration reg{};
	reg.structSize = D2RL::Overlay::CallbackRegistrationSize;
	reg.phase = D2RL::Overlay::Phase::AfterGameUi;
	reg.callback = &DrawTimer;
	D2RL::Overlay::CallbackHandle handle = D2RL::Overlay::InvalidCallbackHandle;
	return g_overlay->registerFrameCallback(context, &reg, &handle) == D2RL::Overlay::Result::Success;
}

void WarpHook(Game* game, Unit* player, Unit* tile) {
	if (game == nullptr || !IsPlayer(player) || tile == nullptr) {
		g_warpOriginal(game, player, tile);
		return;
	}
	void* room = g_unitRoom(tile);
	void* out = nullptr;
	Unit* linked = room != nullptr ? g_findTile(d2r::At<uint8_t>(game, d2r::GameDataFlag), room,
	                                            d2r::At<uint32_t>(tile, UnitClass), &out)
	                               : nullptr;
	void* dstRoom = linked != nullptr ? g_unitRoom(linked) : nullptr;
	const int32_t dst = dstRoom != nullptr ? g_roomLevel(dstRoom) : 0;
	const int32_t src = room != nullptr ? g_roomLevel(room) : 0;
	if (dst > 0 && !Allowed(game, player, src, dst)) {
		return;
	}
	g_warpOriginal(game, player, tile);
	if (dst > 0) {
		Entered(game, player, src);
	}
}

int64_t PortalHook(int64_t* ctx) {
	auto* game = reinterpret_cast<Game*>(ctx != nullptr ? ctx[0] : 0);
	auto* portal = reinterpret_cast<Unit*>(ctx != nullptr ? ctx[1] : 0);
	auto* player = reinterpret_cast<Unit*>(ctx != nullptr ? ctx[2] : 0);
	if (game == nullptr || portal == nullptr || !IsPlayer(player) || d2r::At<int32_t>(portal, d2r::UnitType) != 2) {
		return g_portalOriginal(ctx);
	}
	const int32_t dst = g_portalLevel(portal);
	const int32_t src = LevelOf(player);
	if (dst > 0 && !Allowed(game, player, src, dst)) {
		return 0;
	}
	const int64_t moved = g_portalOriginal(ctx);
	if (moved != 0) {
		Entered(game, player, src);
	}
	return moved;
}

void ForgetGame(const void* game) noexcept {
	std::lock_guard<std::mutex> lock(g_mutex);
	g_lockout.Forget(game);
}

}  // namespace

auto DungeonLockoutProbe(void* gameArg, void* playerArg, int dst) noexcept -> int {
	auto* game = static_cast<Game*>(gameArg);
	auto* player = static_cast<Unit*>(playerArg);
	const dungeon::Entry* entry = dst > 0 ? dungeon::Find(dst) : nullptr;
	if (game == nullptr || !IsPlayer(player) || entry == nullptr) {
		return -1;
	}
	const dungeon::Who who{g_getStat(player, StatLevel, 0), static_cast<int32_t>(d2r::At<uint32_t>(player, UnitClass)),
	                       d2r::At<uint8_t>(game, GameDifficulty)};
	const dungeon::QuestBits quest = QuestBitsOf(game, player, entry->questLock);
	std::lock_guard<std::mutex> lock(g_mutex);
	g_lockout.Touch(game, d2r::At<uint32_t>(game, GameFrame));
	return static_cast<int>(dungeon::Check(g_lockout, who, dst, Now(), quest));
}

void DungeonLockoutCounts(int* refused, int* started) noexcept {
	std::lock_guard<std::mutex> lock(g_mutex);
	*refused = g_refusals;
	*started = g_started;
}

auto InstallDungeonLockout(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	RegisterGameEnd(&ForgetGame);
	const uint64_t base = context->exeBase;
	g_findTile = reinterpret_cast<FindTileFn>(base + FindTileRva);
	g_roomLevel = reinterpret_cast<RoomLevelFn>(base + RoomLevelRva);
	g_unitRoom = reinterpret_cast<UnitRoomFn>(base + UnitRoomRva);
	g_portalLevel = reinterpret_cast<PortalLevelFn>(base + PortalLevelRva);
	g_unitMessage = reinterpret_cast<UnitMessageFn>(base + UnitMessageRva);
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	g_questBit = reinterpret_cast<QuestBitFn>(base + questeng::QuestBitTestRva);
	const bool warp = context->InstallInlineHook(WarpRva, WarpEntry, sizeof(WarpEntry), reinterpret_cast<void*>(&WarpHook),
	                                             reinterpret_cast<void**>(&g_warpOriginal));
	const bool portal = context->InstallInlineHook(PortalRva, PortalEntry, sizeof(PortalEntry),
	                                               reinterpret_cast<void*>(&PortalHook),
	                                               reinterpret_cast<void**>(&g_portalOriginal));
	const bool timer = InstallTimerOverlay(context);
	std::snprintf(report, reportSize, "%s: %d levels; tile warps %s, portals %s, countdown %s", warp && portal ? "on" : "partly off",
	              static_cast<int>(mxl_dungeons::EntryCount), warp ? "on" : "hook failed", portal ? "on" : "hook failed",
	              timer ? "on screen" : "in chat lines only");
	return warp && portal;
}

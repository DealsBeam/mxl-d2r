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
// pointer, reset when the pointer or the frame counter (game+0x170) changes; no game-destroy hook (another lane may
// use 0x4057c0). Not ported: Sigma's quest-table condition in 0x5d810 (levelDef +4), the 0x7fb70 item teleport, the
// client DungeonWidget and the 0x88 state packet.
#include "dungeon_lockout.h"

#include "d2r.h"
#include "dungeon_logic.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
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

using WarpFn = void (*)(Game* game, Unit* player, Unit* tile);
using PortalFn = int64_t (*)(int64_t* ctx);
using FindTileFn = Unit* (*)(uint8_t dataFlag, void* room, uint32_t cls, void** out);
using RoomLevelFn = int32_t (*)(void* room);
using UnitRoomFn = void* (*)(Unit* unit);
using PortalLevelFn = uint16_t (*)(Unit* portal);
using UnitMessageFn = void (*)(Unit* player, int32_t message, Unit* subject);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);

WarpFn g_warpOriginal = nullptr;
PortalFn g_portalOriginal = nullptr;
FindTileFn g_findTile = nullptr;
RoomLevelFn g_roomLevel = nullptr;
UnitRoomFn g_unitRoom = nullptr;
PortalLevelFn g_portalLevel = nullptr;
UnitMessageFn g_unitMessage = nullptr;
GetStatFn g_getStat = nullptr;

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

void Log(const char* what, int32_t a, int32_t b) noexcept {
	if (g_context != nullptr) {
		char line[160];
		std::snprintf(line, sizeof(line), "mxl-core: lockout: %s (%d -> %d)", what, a, b);
		g_context->LogInfo(line);
	}
}

// True when the player may enter `dst`; otherwise sends the failure message. Median shows its own message (code 0x13,
// D2Sigma 0x100b15d0) and the client's DungeonWidget; D2R has no text for that, so the player gets D2R's generic
// "failed" reaction (the one the portal function itself uses) and nothing else.
auto Allowed(Game* game, Unit* player, int32_t src, int32_t dst) noexcept -> bool {
	if (dst <= 0 || dungeon::Find(dst) == nullptr) {
		return true;
	}
	const dungeon::Who who{g_getStat(player, StatLevel, 0), static_cast<int32_t>(d2r::At<uint32_t>(player, UnitClass)),
	                       d2r::At<uint8_t>(game, GameDifficulty)};
	dungeon::Verdict v;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_lockout.Touch(game, d2r::At<uint32_t>(game, GameFrame));
		v = dungeon::Check(g_lockout, who, dst, Now());
		if (v != dungeon::Verdict::Allow) {
			++g_refusals;
		}
	}
	if (v == dungeon::Verdict::Allow) {
		return true;
	}
	Log(v == dungeon::Verdict::Sealed ? "level sealed, entry refused" : "entry requirements not met, refused", src, dst);
	g_unitMessage(player, MessageFailed, player);
	return false;
}

// After an allowed move: the clock of the level the player now stands in.
void Entered(Game* game, Unit* player, int32_t src) noexcept {
	const int32_t now = LevelOf(player);
	if (now <= 0 || now == src || dungeon::Find(now) == nullptr) {
		return;
	}
	bool started;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_lockout.Touch(game, d2r::At<uint32_t>(game, GameFrame));
		started = g_lockout.Start(now, Now());
		if (started) {
			++g_started;
		}
	}
	if (started) {
		Log("clock started", src, now);
	}
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

}  // namespace

void DungeonLockoutCounts(int* refused, int* started) noexcept {
	std::lock_guard<std::mutex> lock(g_mutex);
	*refused = g_refusals;
	*started = g_started;
}

auto InstallDungeonLockout(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	const uint64_t base = context->exeBase;
	g_findTile = reinterpret_cast<FindTileFn>(base + FindTileRva);
	g_roomLevel = reinterpret_cast<RoomLevelFn>(base + RoomLevelRva);
	g_unitRoom = reinterpret_cast<UnitRoomFn>(base + UnitRoomRva);
	g_portalLevel = reinterpret_cast<PortalLevelFn>(base + PortalLevelRva);
	g_unitMessage = reinterpret_cast<UnitMessageFn>(base + UnitMessageRva);
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	const bool warp = context->InstallInlineHook(WarpRva, WarpEntry, sizeof(WarpEntry), reinterpret_cast<void*>(&WarpHook),
	                                             reinterpret_cast<void**>(&g_warpOriginal));
	const bool portal = context->InstallInlineHook(PortalRva, PortalEntry, sizeof(PortalEntry),
	                                               reinterpret_cast<void*>(&PortalHook),
	                                               reinterpret_cast<void**>(&g_portalOriginal));
	std::snprintf(report, reportSize, "%s: %d levels; tile warps %s, portals %s", warp && portal ? "on" : "partly off",
	              static_cast<int>(mxl_dungeons::EntryCount), warp ? "on" : "hook failed", portal ? "on" : "hook failed");
	return warp && portal;
}

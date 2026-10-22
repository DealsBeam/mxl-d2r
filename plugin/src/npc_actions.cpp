// Median's NPC menu actions on D2R's NPC dialog (D2Sigma D2NpcActions.cpp / D2NPC.cpp; docs/subsystems/npc-hirelings.md 1.5).
//
// Median gives an NPC up to 16 extra menu lines (npcext slots); a line is shown while its requirement row holds and runs an
// npcactions row on the server. D2R builds the NPC menu from a table of 48 records (0x48 bytes, at the heap pointer
// 0x2a48830, at most five lines each) and sends packet 0x38 (kind u32, NPC unit id u32) for a clicked line.
//
//   client  0x1147a0 (menu builder): in front of the original, the lines that Median's slots add and the server can do are
//           written into the record of the NPC (string id + click handler, before "Identify" when the record has it). A
//           handler sends packet 0x38 with kind 0x10 + npcRow * 16 + slot and closes the menu like Cancel (0x112c30).
//   server  0x4b0470 (handler of packet 0x38): kinds >= 0x10 are Median's. The NPC must be a unit within 50 of the player
//           (0x53b8f0, as the original does), its class must own the slot, then the requirement rows decide
//           (requirements.cpp) and the action runs (npc_actions_logic.h). Other kinds go to the original untouched.
//
// Cain's "crafting materials" is Median's text page: a click does not give anything, it opens a submenu of the four
// materials whose requirement holds. The server evaluates the requirements for the click and answers with the plugin
// message 'N' (page kind, NPC class, mask of the open entries; client_messages_logic.h); the client then closes its list and
// builds it again with the record's lines swapped for the entries (the record is put back afterwards, so ESC and the next
// talk show the normal menu). Each entry is a line of its own: it sends its own kind, the server runs that slot.
//
// A slot with npcext PromptConfirm (Akara's respec) asks first: the click closes the list and shows D2R's own confirmation
// page, the one its Repair All builds (a list widget at 0x2a48798 with the prompt text as a header line and the stock Yes /
// No lines); only Yes sends the kind, No and Escape close the menu like Cancel.
//
// Realm rule: the client only draws the lines and sends the kind; requirements and effects are server code, in the
// separate section below. Actions: respec (Akara) and the free crafting materials (the five Cains) are done here. The
// travel packages (with their waypoint grant) and Imbue / Add sockets / Personalize are D2R's own: it shows its lines under
// the same quest bits and its server code spends the reward, so no line is drawn and nothing is hooked; npc_services.cpp
// reads the game's tables at load and reports whether they match Median's rows (section 1.6 of npc-hirelings.md).
//
// Entry bytes checked against the dump of build 93847; whole instructions, no rip-relative.
#include "npc_actions.h"

#include "client_messages.h"
#include "client_messages_logic.h"
#include "d2r.h"
#include "mxl_requirements.h"
#include "npc_actions_logic.h"
#include "npc_services.h"
#include "strings.h"

#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {

using d2r::Game;
using d2r::Unit;

// --- shared addresses ---------------------------------------------------------------------------------------------
constexpr uint64_t MenuBuilderRva = 0x1147a0;   // client: void (uint8 dataFlag)
constexpr uint64_t MenuTableRva = 0x2a48830;    // client: pointer to the 48 records
constexpr uint64_t MenuCountRva = 0x2a48838;    // client: record count (qword)
constexpr uint64_t MenuNpcIdRva = 0x2a4875c;    // client: unit id of the NPC in the open menu (u32)
constexpr uint64_t MenuCancelRva = 0x112c30;    // client: the Cancel line's handler (closes list and panel)
constexpr uint64_t ListWidgetRva = 0x2a487b0;   // client: pointer to the open list widget (qword)
constexpr uint64_t CloseListRva = 0x1e9390;     // client: void (widget): closes the list widget
constexpr uint64_t MenuOpenFlagRva = 0x2a486a4; // client: u32, 1 while the menu is up
constexpr uint64_t MenuNpcClassRva = 0x2a48764; // client: class of the NPC in the open menu (u16)
constexpr uint64_t CloseAllListsRva = 0x110670; // client: void (): closes every NPC list widget and clears their globals
constexpr uint64_t CreateListRva = 0x1e9200;    // client: (cancel handler, 0, 1) -> list widget (centred)
constexpr uint64_t AddListLineRva = 0x1e9030;   // client: (widget, text, flags, 1, handler, selectable): adds a line
constexpr uint64_t StringRva = 0x3e2b730;       // client: D2RCore thunk (u16 string id) -> text
constexpr uint64_t ConfirmListRva = 0x2a48798;  // client: pointer to the confirmation list widget (qword)
constexpr uint64_t SendPacketRva = 0xec730;     // client: (uint8 id, u32 a, u32 b) builds and sends a 9-byte packet
constexpr uint64_t MenuPacketRva = 0x4b0470;    // server: (game, player, packet*, size): packet 0x38 handler
constexpr uint64_t FindNpcRva = 0x53b8f0;       // server: (player, unit id, max distance) -> NPC unit or null
constexpr uint64_t CreateQuestItemRva = 0x517530;  // (game, player, code, ilvl, quality, dropIfFull) -> item or null
constexpr uint64_t FindItemRva = 0x441980;      // (game, player, code) -> item or null
constexpr uint64_t QuestBitTestRva = 0x325c50;  // (record, quest, bit)
constexpr uint64_t RespecRva = 0x580f20;        // server: (game, player, 0): stat + skill reset, equipment re-check
constexpr uint64_t RespecDoneRva = 0x5d9ae0;    // server: (game, player): quest 41 bookkeeping after a respec

// 0x4b0470: push rbp / push rbx / push rsi / push rdi / push r12 / push r13 / lea rbp,[rsp-0x2f] / sub rsp,0xc8
constexpr uint8_t PacketEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x48, 0x8d, 0x6c, 0x24, 0xd1,
                                   0x48, 0x81, 0xec, 0xc8, 0x00, 0x00, 0x00};
// 0x1147a0: push rbp / push rbx / push rdi / push r12 / push r14 / lea rbp,[rsp-0x3f0] / sub rsp,0x4f0
constexpr uint8_t BuilderEntry[] = {0x40, 0x55, 0x53, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x8d, 0xac, 0x24, 0x10, 0xfc,
                                    0xff, 0xff, 0x48, 0x81, 0xec, 0xf0, 0x04, 0x00, 0x00};

// Entries of the client functions the confirmation calls (whole instructions, up to the first rip-relative operand).
// 0x110670: push rbx / sub rsp,0x20
constexpr uint8_t CloseAllEntry[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20};
// 0x1e9200: mov [rsp+8],rbx / mov [rsp+0x10],rbp / push rsi / push rdi / push r14 / sub rsp,0x40 / mov esi,r8d / mov rbp,rdx /
// mov r14,rcx
constexpr uint8_t CreateListEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x56, 0x57, 0x41, 0x56,
                                       0x48, 0x83, 0xec, 0x40, 0x41, 0x8b, 0xf0, 0x48, 0x8b, 0xea, 0x4c, 0x8b, 0xf1};
// 0x1e9030: the same saves, sub rsp,0x20 / cmp dword [rcx+0x1c],10 / mov ebx,r9d / mov ebp,r8d / mov r14,rdx
constexpr uint8_t AddLineEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
                                    0xec, 0x20, 0x83, 0x79, 0x1c, 0x0a, 0x41, 0x8b, 0xd9, 0x41, 0x8b, 0xe8, 0x4c, 0x8b, 0xf2};
// 0x3e2b730: jmp [rip+disp32], the thunk into D2RCore's string lookup
constexpr uint8_t StringEntry[] = {0xff, 0x25};

constexpr int GameDifficulty = 0x104;  // byte
constexpr int UnitClass = 0x04;        // u32
constexpr int UnitPlayerData = 0x10;   // player: data block; +0x40 + difficulty * 8 -> quest record
constexpr int NpcRange = 0x32;         // the original's distance for the menu packet
constexpr uint8_t MenuPacketId = 0x38;
constexpr int MaxMenuRecords = 256;

using MenuBuilderFn = void (*)(uint8_t dataFlag);
using MenuPacketFn = void (*)(Game* game, Unit* player, uint8_t* packet, int size);
using SendPacketFn = void (*)(uint8_t id, uint32_t a, uint32_t b);
using MenuCancelFn = uint8_t (*)();
using CloseListFn = void (*)(void* widget);
using FindNpcFn = Unit* (*)(Unit* player, uint32_t id, uint32_t maxDistance);
using CreateItemFn = Unit* (*)(Game* game, Unit* player, uint32_t code, int32_t itemLevel, int32_t quality, int32_t dropIfFull);
using FindItemFn = Unit* (*)(Game* game, Unit* player, uint32_t code);
using BitFn = int32_t (*)(void* record, int32_t quest, int32_t bit);
using RespecFn = void (*)(Game* game, Unit* player, int32_t zero);
using RespecDoneFn = void (*)(Game* game, Unit* player);
using CloseAllFn = void (*)();
using CreateListFn = void* (*)(void* cancelHandler, void* zero, int32_t one);
using AddLineFn = void (*)(void* widget, const char* text, uint32_t flags, uint32_t one, void* handler, int32_t selectable);
using StringFn = const char* (*)(uint32_t id);

const D2RL::PluginContext* g_context = nullptr;
uint64_t g_base = 0;
MenuBuilderFn g_builderOriginal = nullptr;
MenuPacketFn g_packetOriginal = nullptr;
SendPacketFn g_send = nullptr;
MenuCancelFn g_cancel = nullptr;
CloseListFn g_closeList = nullptr;
FindNpcFn g_findNpc = nullptr;
CreateItemFn g_createItem = nullptr;
FindItemFn g_findItem = nullptr;
BitFn g_bitTest = nullptr;
RespecFn g_respec = nullptr;
RespecDoneFn g_respecDone = nullptr;
CloseAllFn g_closeAll = nullptr;
CreateListFn g_createList = nullptr;
AddLineFn g_addLine = nullptr;
StringFn g_string = nullptr;
bool g_confirmOk = false;  // the confirmation's functions are in the expected image; else a confirm line sends at once

void Log(const char* what, int a, int b) noexcept {
	if (g_context == nullptr) {
		return;
	}
	char line[200];
	std::snprintf(line, sizeof(line), "mxl-core: npc actions: %s (%d, %d)", what, a, b);
	g_context->LogInfo(line);
}

// --- server -------------------------------------------------------------------------------------------------------
struct Ctx {
	Game* game;
	Unit* player;
};

auto QuestRecord(const Ctx& c, int difficulty) noexcept -> void* {
	const auto* data = c.player != nullptr ? d2r::At<const uint8_t*>(c.player, UnitPlayerData) : nullptr;
	if (data == nullptr) {
		return nullptr;
	}
	const int d = difficulty >= 0 ? difficulty : d2r::At<uint8_t>(c.game, GameDifficulty);
	return d2r::At<void*>(data, 0x40 + d * 8);
}

bool QueryQuestBit(void* user, int quest, int bit, int difficulty) noexcept {
	void* record = QuestRecord(*static_cast<Ctx*>(user), difficulty);
	return record != nullptr && g_bitTest(record, quest, bit) != 0;
}

int QueryPlayerClass(void* user) noexcept {
	return static_cast<int>(d2r::At<uint32_t>(static_cast<Ctx*>(user)->player, UnitClass));
}

bool QueryHoldsItem(void* user, const char* code) noexcept {
	const auto* c = static_cast<Ctx*>(user);
	uint32_t word = 0;
	std::memcpy(&word, code, 4);
	return g_findItem(c->game, c->player, word) != nullptr;
}

bool ActionCreateItem(void* user, uint32_t code, int itemLevel, int quality) noexcept {
	const auto* c = static_cast<Ctx*>(user);
	return g_createItem(c->game, c->player, code, itemLevel, quality, 1) != nullptr;
}

// What the vanilla respec packet (0x4b2530) does after its checks: the reset, then the quest 41 bookkeeping.
void ActionPage(void* user, int npcClass, uint32_t pageKind, uint32_t mask) noexcept {
	const auto* c = static_cast<Ctx*>(user);
	uint8_t payload[npc::PagePayloadSize];
	npc::BuildPagePayload(payload, pageKind, static_cast<uint16_t>(npcClass), static_cast<uint16_t>(mask));
	if (!SendPluginMessage(c->player, clientmsg::PluginKindNpcPage, payload, sizeof(payload))) {
		Log("page answer could not be sent (npc class, mask)", npcClass, static_cast<int>(mask));
	}
}

bool ActionRespec(void* user) noexcept {
	const auto* c = static_cast<Ctx*>(user);
	g_respec(c->game, c->player, 0);
	g_respecDone(c->game, c->player);
	return true;
}

void ActionNote(void*, const char* what, int a, int b) noexcept {
	Log(what, a, b);
}

void HandleKind(Game* game, Unit* player, uint32_t kind, uint32_t npcId) noexcept {
	Unit* npcUnit = g_findNpc(player, npcId, NpcRange);
	if (npcUnit == nullptr) {
		Log("kind from a player with no NPC in range", static_cast<int>(kind), static_cast<int>(npcId));
		return;
	}
	const int npcClass = static_cast<int>(d2r::At<uint32_t>(npcUnit, UnitClass));
	Ctx ctx{game, player};
	npc::Host host;
	host.user = &ctx;
	host.queries = {&ctx, QueryQuestBit, QueryPlayerClass, QueryHoldsItem};
	host.createItem = ActionCreateItem;
	host.respec = ActionRespec;
	host.page = ActionPage;
	host.note = ActionNote;
	const npc::Result r = npc::RunSlot(kind, npcClass, host);
	switch (r) {
	case npc::Result::NotFound: Log("no slot of this NPC has the kind (kind, npc class)", static_cast<int>(kind), npcClass); break;
	case npc::Result::Denied: Log("requirement not met (kind, npc class)", static_cast<int>(kind), npcClass); break;
	case npc::Result::Done: Log("done (kind, npc class)", static_cast<int>(kind), npcClass); break;
	case npc::Result::Nothing: Log("nothing to do (kind, npc class)", static_cast<int>(kind), npcClass); break;
	case npc::Result::Page: Log("page opened (kind, npc class)", static_cast<int>(kind), npcClass); break;
	case npc::Result::Native: Log("D2R runs this service itself (kind, npc class)", static_cast<int>(kind), npcClass); break;
	}
}

void PacketHook(Game* game, Unit* player, uint8_t* packet, int size) {
	if (size == 9 && packet != nullptr && player != nullptr) {
		uint32_t kind = 0;
		uint32_t npcId = 0;
		std::memcpy(&kind, packet + 1, 4);
		std::memcpy(&npcId, packet + 5, 4);
		if (kind >= npc::KindBase) {
			HandleKind(game, player, kind, npcId);
			return;
		}
	}
	g_packetOriginal(game, player, packet, size);
}

// --- client -------------------------------------------------------------------------------------------------------
constexpr int MaxClientLines = 16;

struct ClientLine {
	uint16_t npcClass;
	mxlstr::Ref label;
	uint32_t kind;
	bool page;  // a text-page line: sends the kind and keeps the menu open for the answer
	bool confirm;  // asks Yes / No first (npcext PromptConfirm)
	mxlstr::Ref prompt;
};
ClientLine g_lines[MaxClientLines];
int g_lineCount = 0;
int g_nativeSlots = 0;

// The pending Yes / No of a confirm line (one at a time; npc_actions_logic.h).
npc::Confirmation g_confirm;

auto OnConfirmAnswer(bool yes) noexcept -> uint8_t {
	const uint32_t kind = g_confirm.Answer(yes);
	const uint32_t npcId = *reinterpret_cast<const uint32_t*>(g_base + MenuNpcIdRva);
	g_closeAll();  // the confirmation list (and any other); the Cancel step below closes the panel
	if (kind != 0) {
		g_send(MenuPacketId, kind, npcId);
	}
	Log(yes ? "confirmed (kind)" : "declined (kind)", static_cast<int>(kind), 0);
	return g_cancel();
}
auto OnConfirmYes() noexcept -> uint8_t { return OnConfirmAnswer(true); }
auto OnConfirmNo() noexcept -> uint8_t { return OnConfirmAnswer(false); }

// D2R's confirmation page (what Repair All builds, 0x110b60): the NPC list is closed and a new centred list gets the prompt
// as its header line (flag 4) and the stock Yes / No lines. False when it cannot be built (nothing was changed then, the
// caller sends the kind as before).
bool OpenConfirm(uint32_t kind, uint16_t text) noexcept {
	if (!g_confirmOk) {
		return false;
	}
	g_closeAll();
	void* list = g_createList(reinterpret_cast<void*>(&OnConfirmNo), nullptr, 1);
	if (list == nullptr) {
		return false;
	}
	*(static_cast<uint8_t*>(list) + 8) = 1;
	*reinterpret_cast<void**>(g_base + ConfirmListRva) = list;
	*reinterpret_cast<uint32_t*>(g_base + MenuOpenFlagRva) = 3;  // as the original does for its confirmation page
	g_confirm.Open(kind, text);
	g_addLine(list, g_string(text), 4, 1, nullptr, 0);
	g_addLine(list, g_string(npc::StringYes), 0, 1, reinterpret_cast<void*>(&OnConfirmYes), 1);
	g_addLine(list, g_string(npc::StringNo), 0, 1, reinterpret_cast<void*>(&OnConfirmNo), 1);
	Log("confirmation shown (kind, text id)", static_cast<int>(kind), text);
	return true;
}

auto OnLine(int index) noexcept -> uint8_t {
	bool page = false;
	if (index >= 0 && index < g_lineCount) {
		if (g_lines[index].confirm && OpenConfirm(g_lines[index].kind, StringId(g_lines[index].prompt))) {
			return 1;
		}
		const uint32_t npcId = *reinterpret_cast<const uint32_t*>(g_base + MenuNpcIdRva);
		g_send(MenuPacketId, g_lines[index].kind, npcId);
		page = g_lines[index].page;
	}
	if (page) {
		return 1;  // the server's answer (message 'N') replaces the list
	}
	return g_cancel();  // close the menu as the Cancel line does (returns 1)
}

// The entries of the open submenu (at most 8); their lines send their kind and close the menu.
constexpr int MaxSubLines = 8;
npc::PageEntry g_sub[MaxSubLines];
int g_subCount = 0;

auto OnSubLine(int index) noexcept -> uint8_t {
	if (index >= 0 && index < g_subCount) {
		const uint32_t npcId = *reinterpret_cast<const uint32_t*>(g_base + MenuNpcIdRva);
		g_send(MenuPacketId, g_sub[index].kind, npcId);
	}
	return g_cancel();
}

template <size_t I>
auto SubHandler() noexcept -> uint8_t {
	return OnSubLine(static_cast<int>(I));
}

template <size_t I>
auto LineHandler() noexcept -> uint8_t {
	return OnLine(static_cast<int>(I));
}

using LineFn = uint8_t (*)();

template <size_t... I>
constexpr auto MakeHandlers(std::index_sequence<I...>) noexcept -> std::array<LineFn, sizeof...(I)> {
	return {&LineHandler<I>...};
}
const auto g_handlers = MakeHandlers(std::make_index_sequence<MaxClientLines>{});

template <size_t... I>
constexpr auto MakeSubHandlers(std::index_sequence<I...>) noexcept -> std::array<LineFn, sizeof...(I)> {
	return {&SubHandler<I>...};
}
const auto g_subHandlers = MakeSubHandlers(std::make_index_sequence<MaxSubLines>{});

uint8_t* FindRecord(uint16_t npcClass) noexcept {
	auto* table = *reinterpret_cast<uint8_t**>(g_base + MenuTableRva);
	const uint64_t count = *reinterpret_cast<const uint64_t*>(g_base + MenuCountRva);
	if (table == nullptr || count == 0 || count > MaxMenuRecords) {
		return nullptr;
	}
	for (uint64_t r = 0; r < count; ++r) {
		if (*reinterpret_cast<const uint16_t*>(table + r * npc::RecordSize) == npcClass) {
			return table + r * npc::RecordSize;
		}
	}
	return nullptr;
}

uint8_t g_lastDataFlag = 0;

// Builds the list again with the entries in place of the NPC record's lines (the record is restored at the next build).
void BuildSubmenu(uint16_t npcClass) noexcept;

void PatchMenuTable() noexcept {
	auto* table = *reinterpret_cast<uint8_t**>(g_base + MenuTableRva);
	const uint64_t count = *reinterpret_cast<const uint64_t*>(g_base + MenuCountRva);
	if (table == nullptr || count == 0 || count > MaxMenuRecords) {
		return;
	}
	for (uint64_t r = 0; r < count; ++r) {
		uint8_t* record = table + r * npc::RecordSize;
		const uint16_t cls = *reinterpret_cast<const uint16_t*>(record);
		uint16_t ids[npc::RecMaxLines];
		uint64_t fns[npc::RecMaxLines];
		int n = 0;
		for (int i = 0; i < g_lineCount && n < npc::RecMaxLines; ++i) {
			const uint16_t id = g_lines[i].npcClass == cls ? StringId(g_lines[i].label) : 0;
			if (id != 0) {  // no id (a namespaced string with no service / lookup hook): no line, a 0 would end the record
				ids[n] = id;
				fns[n] = reinterpret_cast<uint64_t>(g_handlers[static_cast<size_t>(i)]);
				++n;
			}
		}
		if (n > 0 && npc::AddMenuLines(record, ids, fns, n) < 0) {
			Log("menu record is full, a line was left out (npc class)", cls, 0);
		}
	}
}

// The builder lowers the record's line count when it passes the Identify line (0xfb4); the lines are drawn by then, but
// the record would show one line less than the list (and the harness `menu list` would miss the plugin line).
void RestoreCounts() noexcept {
	auto* table = *reinterpret_cast<uint8_t**>(g_base + MenuTableRva);
	const uint64_t count = *reinterpret_cast<const uint64_t*>(g_base + MenuCountRva);
	if (table == nullptr || count == 0 || count > MaxMenuRecords) {
		return;
	}
	for (uint64_t r = 0; r < count; ++r) {
		uint8_t* record = table + r * npc::RecordSize;
		const uint16_t cls = *reinterpret_cast<const uint16_t*>(record);
		bool ours = false;
		for (int i = 0; i < g_lineCount; ++i) {
			ours = ours || g_lines[i].npcClass == cls;
		}
		if (ours) {
			int lines = 0;
			while (lines < npc::RecMaxLines && *reinterpret_cast<const uint16_t*>(record + npc::RecIdsOffset + lines * 2) != 0) {
				++lines;
			}
			*reinterpret_cast<uint32_t*>(record + npc::RecCountOffset) = static_cast<uint32_t>(lines + 1);
		}
	}
}

// The record a submenu was built from: it keeps the entries until the next build (so the table shows what the list shows,
// which the harness `menu list` / `menu <line>` read) and its own bytes are put back then.
uint8_t g_savedRecord[npc::RecordSize];
uint16_t g_savedClass = 0;
bool g_savedValid = false;

void RestoreSavedRecord() noexcept {
	if (g_savedValid) {
		g_savedValid = false;
		uint8_t* record = FindRecord(g_savedClass);  // the table may be gone after the game ended: only a live record is written
		if (record != nullptr) {
			std::memcpy(record, g_savedRecord, sizeof(g_savedRecord));
		}
	}
}

void BuilderHook(uint8_t dataFlag) {
	g_lastDataFlag = dataFlag;
	RestoreSavedRecord();
	const bool patch = g_packetOriginal != nullptr;  // lines the server cannot answer are not drawn
	if (patch) {
		PatchMenuTable();
	}
	g_builderOriginal(dataFlag);
	if (patch) {
		RestoreCounts();
	}
}

void BuildSubmenu(uint16_t npcClass) noexcept {
	uint8_t* record = FindRecord(npcClass);
	if (record == nullptr || g_subCount > npc::RecMaxLines - 1) {
		return;
	}
	if (!g_savedValid) {  // a second answer builds on the first one's saved bytes
		std::memcpy(g_savedRecord, record, sizeof(g_savedRecord));
		g_savedClass = npcClass;
		g_savedValid = true;
	}
	auto* ids = reinterpret_cast<uint16_t*>(record + npc::RecIdsOffset);
	auto* fns = reinterpret_cast<uint64_t*>(record + npc::RecHandlersOffset);
	for (int i = 0; i < g_subCount; ++i) {
		ids[i] = StringId(g_sub[i].label);
		fns[i] = reinterpret_cast<uint64_t>(g_subHandlers[static_cast<size_t>(i)]);
	}
	*reinterpret_cast<uint32_t*>(record + npc::RecCountOffset) = static_cast<uint32_t>(g_subCount + 1);
	g_builderOriginal(g_lastDataFlag);
}

// Plugin message 'N': the server's answer to a click on a text-page line.
void OnPageMessage(const uint8_t* data, size_t n) noexcept {
	uint32_t pageKind = 0;
	uint16_t npcClass = 0;
	uint16_t mask = 0;
	if (!npc::ParsePagePayload(data, n, &pageKind, &npcClass, &mask)) {
		return;
	}
	void* widget = *reinterpret_cast<void**>(g_base + ListWidgetRva);
	const bool open = *reinterpret_cast<const uint32_t*>(g_base + MenuOpenFlagRva) == 1;
	if (!open || widget == nullptr || *reinterpret_cast<const uint16_t*>(g_base + MenuNpcClassRva) != npcClass) {
		Log("page answer with no matching menu open (npc class, mask)", npcClass, mask);
		return;
	}
	g_subCount = npc::PageEntries(npcClass, pageKind, mask, g_sub, MaxSubLines);
	g_closeList(widget);
	*reinterpret_cast<void**>(g_base + ListWidgetRva) = nullptr;
	BuildSubmenu(npcClass);
	Log("submenu shown (entries, mask)", g_subCount, mask);
}

// Lines the client shows: the top-level slots whose action the server can run and that have a label.
void CollectLines() noexcept {
	g_lineCount = 0;
	g_nativeSlots = 0;
	for (int i = 0; i < mxl_npc::SlotCount; ++i) {
		const mxl_npc::Slot& s = mxl_npc::Slots[i];
		if (s.sub != 0 || npc::LabelOf(s).IsNone()) {
			continue;
		}
		const npc::Support sup = npc::SlotSupport(i);
		if (sup == npc::Support::Native) {
			++g_nativeSlots;
		} else if (sup == npc::Support::Run && g_lineCount < MaxClientLines) {
			g_lines[g_lineCount++] = {s.npcClass, npc::LabelOf(s), npc::KindOf(s.npcRow, s.slot), npc::IsPage(i), npc::NeedsConfirm(i),
			                          npc::ConfirmText(i)};
		}
	}
}

}  // namespace

auto InstallNpcActions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	g_base = context->exeBase;
	g_send = reinterpret_cast<SendPacketFn>(g_base + SendPacketRva);
	g_cancel = reinterpret_cast<MenuCancelFn>(g_base + MenuCancelRva);
	g_closeList = reinterpret_cast<CloseListFn>(g_base + CloseListRva);
	g_findNpc = reinterpret_cast<FindNpcFn>(g_base + FindNpcRva);
	g_createItem = reinterpret_cast<CreateItemFn>(g_base + CreateQuestItemRva);
	g_findItem = reinterpret_cast<FindItemFn>(g_base + FindItemRva);
	g_bitTest = reinterpret_cast<BitFn>(g_base + QuestBitTestRva);
	g_respec = reinterpret_cast<RespecFn>(g_base + RespecRva);
	g_respecDone = reinterpret_cast<RespecDoneFn>(g_base + RespecDoneRva);
	g_closeAll = reinterpret_cast<CloseAllFn>(g_base + CloseAllListsRva);
	g_createList = reinterpret_cast<CreateListFn>(g_base + CreateListRva);
	g_addLine = reinterpret_cast<AddLineFn>(g_base + AddListLineRva);
	g_string = reinterpret_cast<StringFn>(g_base + StringRva);
	g_confirmOk = std::memcmp(reinterpret_cast<const void*>(g_base + CloseAllListsRva), CloseAllEntry, sizeof(CloseAllEntry)) == 0 &&
	              std::memcmp(reinterpret_cast<const void*>(g_base + CreateListRva), CreateListEntry, sizeof(CreateListEntry)) == 0 &&
	              std::memcmp(reinterpret_cast<const void*>(g_base + AddListLineRva), AddLineEntry, sizeof(AddLineEntry)) == 0 &&
	              std::memcmp(reinterpret_cast<const void*>(g_base + StringRva), StringEntry, sizeof(StringEntry)) == 0;
	CollectLines();
	int ok = 0;
	ok += context->InstallInlineHook(MenuPacketRva, PacketEntry, sizeof(PacketEntry), reinterpret_cast<void*>(&PacketHook),
	                                 reinterpret_cast<void**>(&g_packetOriginal)) ? 1 : 0;
	ok += context->InstallInlineHook(MenuBuilderRva, BuilderEntry, sizeof(BuilderEntry), reinterpret_cast<void*>(&BuilderHook),
	                                 reinterpret_cast<void**>(&g_builderOriginal)) ? 1 : 0;
	if (ok == 2) {
		SetPluginMessageHandler(clientmsg::PluginKindNpcPage, &OnPageMessage);
		const int travelGates = FixTravelGates(context);
		char native[160];
		VerifyNativeServices(g_base, native, sizeof(native));
		std::snprintf(report, reportSize,
		              "on: %d menu lines (Akara, Cain; Cain's page is a submenu%s; respec confirm %s); %d slots are D2R's own, %s; travel gates moved %d",
		              g_lineCount, PluginMessagesOn() ? "" : ", answer unreadable: plugin messages off",
		              g_confirmOk ? "on" : "off: image mismatch", g_nativeSlots, native, travelGates);
	} else {
		std::snprintf(report, reportSize, "partial: %d of 2 hooks installed (menu packet 0x4b0470, menu builder 0x1147a0)", ok);
	}
	return ok == 2;
}

auto NpcConfirmOpen() noexcept -> bool {
	return g_confirm.open;
}

auto NpcConfirmAnswer(bool yes) noexcept -> bool {
	if (!g_confirm.open) {
		return false;
	}
	OnConfirmAnswer(yes);
	return true;
}

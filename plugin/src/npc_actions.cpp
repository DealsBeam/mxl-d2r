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
// Realm rule: the client only draws the lines and sends the kind; requirements and effects are server code, in the
// separate section below. Actions: respec (Akara) and the free crafting materials (the five Cains) are done; the travel
// packages (D2R has its own travel lines), Imbue / Add sockets / Personalize (D2R's ImbueItemsPanel is bound to quest
// state) are logged only and their lines are not shown.
//
// Entry bytes checked against the dump of build 93847; whole instructions, no rip-relative.
#include "npc_actions.h"

#include "d2r.h"
#include "mxl_requirements.h"
#include "npc_actions_logic.h"

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
using FindNpcFn = Unit* (*)(Unit* player, uint32_t id, uint32_t maxDistance);
using CreateItemFn = Unit* (*)(Game* game, Unit* player, uint32_t code, int32_t itemLevel, int32_t quality, int32_t dropIfFull);
using FindItemFn = Unit* (*)(Game* game, Unit* player, uint32_t code);
using BitFn = int32_t (*)(void* record, int32_t quest, int32_t bit);
using RespecFn = void (*)(Game* game, Unit* player, int32_t zero);
using RespecDoneFn = void (*)(Game* game, Unit* player);

const D2RL::PluginContext* g_context = nullptr;
uint64_t g_base = 0;
MenuBuilderFn g_builderOriginal = nullptr;
MenuPacketFn g_packetOriginal = nullptr;
SendPacketFn g_send = nullptr;
MenuCancelFn g_cancel = nullptr;
FindNpcFn g_findNpc = nullptr;
CreateItemFn g_createItem = nullptr;
FindItemFn g_findItem = nullptr;
BitFn g_bitTest = nullptr;
RespecFn g_respec = nullptr;
RespecDoneFn g_respecDone = nullptr;

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
	host.note = ActionNote;
	const npc::Result r = npc::RunSlot(kind, npcClass, host);
	switch (r) {
	case npc::Result::NotFound: Log("no slot of this NPC has the kind (kind, npc class)", static_cast<int>(kind), npcClass); break;
	case npc::Result::Denied: Log("requirement not met (kind, npc class)", static_cast<int>(kind), npcClass); break;
	case npc::Result::Done: Log("done (kind, npc class)", static_cast<int>(kind), npcClass); break;
	case npc::Result::Nothing: Log("nothing to do (kind, npc class)", static_cast<int>(kind), npcClass); break;
	case npc::Result::Stubbed: Log("not implemented (kind, npc class)", static_cast<int>(kind), npcClass); break;
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
	uint16_t label;
	uint32_t kind;
};
ClientLine g_lines[MaxClientLines];
int g_lineCount = 0;
int g_stubSlots = 0;

auto OnLine(int index) noexcept -> uint8_t {
	if (index >= 0 && index < g_lineCount) {
		const uint32_t npcId = *reinterpret_cast<const uint32_t*>(g_base + MenuNpcIdRva);
		g_send(MenuPacketId, g_lines[index].kind, npcId);
	}
	return g_cancel();  // close the menu as the Cancel line does (returns 1)
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
			if (g_lines[i].npcClass == cls) {
				ids[n] = g_lines[i].label;
				fns[n] = reinterpret_cast<uint64_t>(g_handlers[static_cast<size_t>(i)]);
				++n;
			}
		}
		if (n > 0 && npc::AddMenuLines(record, ids, fns, n) < 0) {
			Log("menu record is full, a line was left out (npc class)", cls, 0);
		}
	}
}

void BuilderHook(uint8_t dataFlag) {
	if (g_packetOriginal != nullptr) {  // lines the server cannot answer are not drawn
		PatchMenuTable();
	}
	g_builderOriginal(dataFlag);
}

// Lines the client shows: the top-level slots whose action the server can run and that have a label.
void CollectLines() noexcept {
	g_lineCount = 0;
	g_stubSlots = 0;
	for (int i = 0; i < mxl_npc::SlotCount; ++i) {
		const mxl_npc::Slot& s = mxl_npc::Slots[i];
		if (s.sub != 0 || s.label == 0) {
			continue;
		}
		const npc::Support sup = npc::SlotSupport(i);
		if (sup == npc::Support::Stub) {
			++g_stubSlots;
		} else if (sup == npc::Support::Run && g_lineCount < MaxClientLines) {
			g_lines[g_lineCount++] = {s.npcClass, s.label, npc::KindOf(s.npcRow, s.slot)};
		}
	}
}

}  // namespace

auto InstallNpcActions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	g_base = context->exeBase;
	g_send = reinterpret_cast<SendPacketFn>(g_base + SendPacketRva);
	g_cancel = reinterpret_cast<MenuCancelFn>(g_base + MenuCancelRva);
	g_findNpc = reinterpret_cast<FindNpcFn>(g_base + FindNpcRva);
	g_createItem = reinterpret_cast<CreateItemFn>(g_base + CreateQuestItemRva);
	g_findItem = reinterpret_cast<FindItemFn>(g_base + FindItemRva);
	g_bitTest = reinterpret_cast<BitFn>(g_base + QuestBitTestRva);
	g_respec = reinterpret_cast<RespecFn>(g_base + RespecRva);
	g_respecDone = reinterpret_cast<RespecDoneFn>(g_base + RespecDoneRva);
	CollectLines();
	int ok = 0;
	ok += context->InstallInlineHook(MenuPacketRva, PacketEntry, sizeof(PacketEntry), reinterpret_cast<void*>(&PacketHook),
	                                 reinterpret_cast<void**>(&g_packetOriginal)) ? 1 : 0;
	ok += context->InstallInlineHook(MenuBuilderRva, BuilderEntry, sizeof(BuilderEntry), reinterpret_cast<void*>(&BuilderHook),
	                                 reinterpret_cast<void**>(&g_builderOriginal)) ? 1 : 0;
	if (ok == 2) {
		std::snprintf(report, reportSize, "on: %d menu lines (Akara respec, Cain crafting materials); %d stubbed services", g_lineCount,
		              g_stubSlots);
	} else {
		std::snprintf(report, reportSize, "partial: %d of 2 hooks installed (menu packet 0x4b0470, menu builder 0x1147a0)", ok);
	}
	return ok == 2;
}

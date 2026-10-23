// Median's cube output effects on D2R (docs/plugin-spec.md, "Cube conditions"; docs/d2r-addresses.md, section 1).
//
// D2Sigma runs a recipe's output slots through a small handler table (byte 6 pay gold, 7 / 9 set a player / game craft
// flag). D2R's parser drops those bytes, so the plugin carries them in mxl_cube_effects.h (tools/cube_effects.py) and
// detours the recipe executor 0x5269c0 (game, player, record, ctx -> first output item or null): when the original made
// an output, the recipe's effects are applied. Flags are memory only, per game pointer (reset when it changes) and per
// character name (as Median does); the game-end hook (game_end.cpp) clears them with their game; gold comes from
// carried gold first, then the bank (D2Craft.cpp 0x10059120).
//
// Output byte 1 (a portal) is D2R's own Cow Level handler, which ignores the level: Median's five portal recipes (rows 2,
// 3, 85, 4962, 7453; mxl_cube_effects.h Portals) name a level each. Entry 1 of the handler table (0x1d3bfc0) is replaced
// by a handler that opens the recipe's level (item_spells.cpp CubeLevelPortal) and passes everything else on.
#include "craft_flags.h"

#include "craft_state.h"
#include "d2r.h"
#include "game_end.h"
#include "item_spells.h"
#include "mxl_cube.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t ExecRva = 0x5269c0;     // (game, player, record, ctx) -> first output item or null
constexpr uint64_t RecordRva = 0x3e3f60;   // (dataFlag, index) -> cubemain record (stride 0x16c)
constexpr uint64_t GetStatRva = 0x2f5020;  // (unit, stat, layer)
constexpr uint64_t SetStatRva = 0x2f7d10;  // (unit, stat, value, layer)
constexpr uint64_t UnitIdRva = 0x34a330;   // (unit, file, line) -> unit +8, the unit id (0x34ab60 is +0xc, the mode)
constexpr uint64_t PlayerDataRva = 0x34b240;  // (player) -> player data
constexpr int PlayerNameOffset = 0x00;        // char[16] in the player data (1.10: PlayerData.szName)
constexpr int PlayerNameSize = 16;
constexpr int RecordSize = 0x16c;
constexpr uint64_t OutputTableRva = 0x1d3bfc0;     // qword handlers (game, player, slot); where byte 1 sits: OutputHandlerSlot
constexpr uint64_t CowHandlerRva = 0x52a9d0;       // entry for byte 1 (jmp 0x5da2a0): always the Cow Level
constexpr int32_t CowLevelMedian = 39;
constexpr int32_t StatGold = 14, StatGoldBank = 15;

// 0x5269c0: push rbp rbx rsi rdi r12 r13 r14 r15 / lea rbp,[rsp-0x15f8] (21 bytes, whole instructions; dump of build
// 93847, timestamp 0x6ab3782c)
constexpr uint8_t ExecEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                 0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0x08, 0xea, 0xff, 0xff};

using ExecFn = void* (*)(Game* game, Unit* player, const uint8_t* record, void* ctx);
using RecordFn = const uint8_t* (*)(uint32_t dataFlag, int32_t index);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using SetStatFn = void (*)(Unit* unit, int32_t stat, int32_t value, int32_t layer);
using UnitIdFn = uint32_t (*)(Unit* unit, const char* file, int32_t line);

using OutputFn = int64_t (*)(Game* game, Unit* player, const uint8_t* slot);

ExecFn g_execOriginal = nullptr;
OutputFn g_portalOriginal = nullptr;
uint64_t* g_byteOneEntry = nullptr;  // where byte 1 sits (set when the portal handler is swapped; the scan needs the original)
RecordFn g_record = nullptr;
GetStatFn g_getStat = nullptr;
SetStatFn g_setStat = nullptr;
UnitIdFn g_unitId = nullptr;
const D2RL::PluginContext* g_context = nullptr;
craft::State g_state;
bool g_warned = false;

using PlayerDataFn = const uint8_t* (*)(Unit* player);
PlayerDataFn g_playerData = nullptr;

// The character name keys the player flags, as in Median (D2Craft.cpp looks the flags up by szName). A unit without a
// readable name falls back to "#<unit id>" so it still gets its own flags.
auto PlayerKey(const void* player) noexcept -> std::string {
	if (player == nullptr || g_playerData == nullptr) {
		return std::string();
	}
	auto* unit = const_cast<Unit*>(static_cast<const Unit*>(player));
	const uint8_t* data = g_playerData(unit);
	if (data != nullptr) {
		char name[PlayerNameSize + 1] = {};
		std::memcpy(name, data + PlayerNameOffset, PlayerNameSize);
		if (name[0] != '\0') {
			return std::string(name);
		}
	}
	return "#" + std::to_string(g_unitId != nullptr ? g_unitId(unit, "mxl-core craft_flags", 0) : 0U);
}

void PayGold(Unit* player, int64_t cost) noexcept {
	const craft::Gold now = craft::Pay({g_getStat(player, StatGold, 0), g_getStat(player, StatGoldBank, 0)}, cost);
	g_setStat(player, StatGold, static_cast<int32_t>(now.carried), 0);
	g_setStat(player, StatGoldBank, static_cast<int32_t>(now.bank), 0);
}

auto RecipeIndex(Game* game, const uint8_t* record) noexcept -> int32_t {
	const uint32_t flag = d2r::DataFlag(game);
	const uint8_t* first = g_record(flag, 0);
	if (first == nullptr || record == nullptr || record < first) {
		return -1;
	}
	const auto delta = static_cast<uint64_t>(record - first);
	const auto index = static_cast<int32_t>(delta / RecordSize);
	if (delta % RecordSize != 0 || index >= mxl_cube::RecipeCount || g_record(flag, index) != record) {
		return -1;
	}
	return index;
}

// Byte 1 (portal): the level of the recipe the slot belongs to. Row 2 and any slot this table does not know stay D2R's.
// D2R's portal creator 0x432ce0 returns at once when the destination level is in another act than the room (`cmp bl, al` / `jnz` after
// the two act lookups 0x143e2b5c8); the Cow Level (an act 1 level) has to be reachable from every town (audit CR6), so the jump is
// replaced by a 6-byte nop. Every other caller makes same-act portals only (town portals, the cube's Red Portal, the plugin's own
// level portals check the act before they call).
constexpr uint64_t PortalActCheckRva = 0x432dd5;
constexpr uint8_t PortalActCheckOriginal[] = {0x3a, 0xd8, 0x0f, 0x85, 0x8a, 0x01, 0x00, 0x00};
constexpr uint8_t PortalActCheckPatched[] = {0x3a, 0xd8, 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00};
bool g_actCheckPatched = false;

int64_t PortalHook(Game* game, Unit* player, const uint8_t* slot) {
	int32_t recipe = 0, index = 0;
	const uint8_t* first = g_record != nullptr && game != nullptr ? g_record(d2r::DataFlag(game), 0) : nullptr;
	if (first != nullptr &&
	    craft::SlotOf(reinterpret_cast<uintptr_t>(first), reinterpret_cast<uintptr_t>(slot), mxl_cube::RecipeCount, &recipe, &index)) {
		const int32_t median = craft::PortalLevel(recipe, index);
		if (median == CowLevelMedian) {
			const int cow = g_actCheckPatched ? CubeCowPortal(game, player) : -1;  // any town but Rogue Encampment (audit CR6); there D2R's own handler
			if (cow >= 0) {
				return cow;
			}
		} else if (median != 0) {
			return CubeLevelPortal(game, player, median) ? 1 : 0;
		}
	}
	return g_portalOriginal(game, player, slot);
}

// Swaps entry 1 of the output handler table; only when it still holds the Cow handler of this build.
auto InstallPortalHandler(uint64_t exeBase) noexcept -> bool {
	uint64_t* entry = OutputHandlerSlot(exeBase, 1);
	if (entry == nullptr) {
		return false;
	}
	DWORD old = 0;
	if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old)) {
		return false;
	}
	g_portalOriginal = reinterpret_cast<OutputFn>(*entry);
	g_byteOneEntry = entry;
	*entry = reinterpret_cast<uint64_t>(&PortalHook);
	VirtualProtect(entry, sizeof(*entry), old, &old);
	return true;
}

void* ExecHook(Game* game, Unit* player, const uint8_t* record, void* ctx) {
	g_state.Touch(game);
	void* item = g_execOriginal(game, player, record, ctx);
	if (item == nullptr || player == nullptr) {
		return item;
	}
	const int32_t recipe = RecipeIndex(game, record);
	if (recipe < 0) {
		if (!g_warned && g_context != nullptr) {
			g_warned = true;
			g_context->LogInfo("mxl-core: craft flags: record pointer is not 0x3e3f60(dataFlag, index) + n*0x16c; "
			                   "effects not applied");
		}
		return item;
	}
	const craft::Range r = craft::EffectsFor(recipe);
	for (const mxl_cube::Effect* e = r.first; e != r.last; ++e) {
		switch (e->kind) {
		case 6: PayGold(player, static_cast<int64_t>(e->param) * 100); break;
		case 7: case 8: g_state.Set(false, PlayerKey(player), e->param, e->kind == 7); break;
		case 9: case 10: g_state.Set(true, std::string(), e->param, e->kind == 9); break;
		default: break;
		}
	}
	return item;
}

}  // namespace

void CraftBind(uint64_t exeBase) noexcept {
	g_record = reinterpret_cast<RecordFn>(exeBase + RecordRva);
	g_getStat = reinterpret_cast<GetStatFn>(exeBase + GetStatRva);
	g_setStat = reinterpret_cast<SetStatFn>(exeBase + SetStatRva);
	g_unitId = reinterpret_cast<UnitIdFn>(exeBase + UnitIdRva);
	g_playerData = reinterpret_cast<PlayerDataFn>(exeBase + PlayerDataRva);
}

auto OutputHandlerSlot(uint64_t exeBase, int byte) noexcept -> uint64_t* {
	if (g_byteOneEntry != nullptr) {
		return g_byteOneEntry + (byte - 1);
	}
	// the dump's table starts at 0x1d3bfc0 with a null entry 0 (index = byte), the dispatch code indexes it with byte - 1;
	// whichever it is, byte 1 is the entry that holds the Cow handler
	const auto* table = reinterpret_cast<const uint64_t*>(exeBase + OutputTableRva);
	for (int i = 0; i < 4; ++i) {
		if (table[i] == exeBase + CowHandlerRva) {
			return const_cast<uint64_t*>(table) + i + (byte - 1);
		}
	}
	return nullptr;
}

void CraftForgetGame(const void* game) noexcept {
	g_state.Forget(game);
}

void CraftTouchGame(const void* game) noexcept {
	g_state.Touch(game);
}

auto CraftFlagTest(const void* player, int flag, bool gameWide) noexcept -> bool {
	return g_state.Test(gameWide, gameWide ? std::string() : PlayerKey(player), flag);
}

void SetCraftFlag(const void* player, int flag, bool game, bool on) noexcept {
	g_state.Set(game, game ? std::string() : PlayerKey(player), flag, on);
}

auto InstallCraftFlags(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	CraftBind(context->exeBase);
	RegisterGameEnd(&CraftForgetGame);
	const bool ok = context->InstallInlineHook(ExecRva, ExecEntry, sizeof(ExecEntry), reinterpret_cast<void*>(&ExecHook),
	                                           reinterpret_cast<void**>(&g_execOriginal));
	const bool portals = ok && InstallPortalHandler(context->exeBase);
	g_actCheckPatched = portals && context->PatchBytes(PortalActCheckRva, PortalActCheckOriginal, sizeof(PortalActCheckOriginal), PortalActCheckPatched,
	                                                   sizeof(PortalActCheckPatched));
	std::snprintf(report, reportSize, ok ? "on: %d output effects (gold, player and game flags), %s%s" : "off: hook failed",
	              static_cast<int>(sizeof(mxl_cube::Effects) / sizeof(mxl_cube::Effects[0])),
	              portals ? "portal outputs by level" : "portal outputs left to D2R (handler table differs)",
	              g_actCheckPatched ? ", portals cross acts" : ", portals stay in their act");
	return ok;
}

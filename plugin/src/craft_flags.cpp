// Median's cube output effects on D2R (docs/plugin-spec.md, "Cube conditions"; docs/d2r-addresses.md, section 1).
//
// D2Sigma runs a recipe's output slots through a small handler table (byte 6 pay gold, 7 / 9 set a player / game craft
// flag). D2R's parser drops those bytes, so the plugin carries them in mxl_cube_effects.h (tools/cube_effects.py) and
// detours the recipe executor 0x5269c0 (game, player, record, ctx -> first output item or null): when the original made
// an output, the recipe's effects are applied. Flags are memory only, per game pointer (reset when it changes) and per
// player unit id; gold comes from carried gold first, then the bank (D2Craft.cpp 0x10059120).
#include "craft_flags.h"

#include "craft_state.h"
#include "d2r.h"
#include "mxl_cube.h"

#include <cstdint>
#include <cstdio>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t ExecRva = 0x5269c0;     // (game, player, record, ctx) -> first output item or null
constexpr uint64_t RecordRva = 0x3e3f60;   // (dataFlag, index) -> cubemain record (stride 0x16c)
constexpr uint64_t GetStatRva = 0x2f5020;  // (unit, stat, layer)
constexpr uint64_t SetStatRva = 0x2f7d10;  // (unit, stat, value, layer)
constexpr uint64_t UnitIdRva = 0x34ab60;   // (unit) -> unit id
constexpr int RecordSize = 0x16c;
constexpr int32_t StatGold = 14, StatGoldBank = 15;

// 0x5269c0: push rbp rbx rsi rdi r12 r13 r14 r15 / lea rbp,[rsp-0x15f8] (21 bytes, whole instructions; dump of build
// 93847, timestamp 0x6ab3782c)
constexpr uint8_t ExecEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                 0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0x08, 0xea, 0xff, 0xff};

using ExecFn = void* (*)(Game* game, Unit* player, const uint8_t* record, void* ctx);
using RecordFn = const uint8_t* (*)(uint32_t dataFlag, int32_t index);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using SetStatFn = void (*)(Unit* unit, int32_t stat, int32_t value, int32_t layer);
using UnitIdFn = int32_t (*)(Unit* unit);

ExecFn g_execOriginal = nullptr;
RecordFn g_record = nullptr;
GetStatFn g_getStat = nullptr;
SetStatFn g_setStat = nullptr;
UnitIdFn g_unitId = nullptr;
const D2RL::PluginContext* g_context = nullptr;
craft::State g_state;
bool g_warned = false;

auto PlayerKey(const void* player) noexcept -> uint32_t {
	return player != nullptr && g_unitId != nullptr ? static_cast<uint32_t>(g_unitId(const_cast<Unit*>(static_cast<const Unit*>(player))))
	                                                : 0;
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
		case 9: case 10: g_state.Set(true, 0, e->param, e->kind == 9); break;
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
}

void CraftTouchGame(const void* game) noexcept {
	g_state.Touch(game);
}

auto CraftFlagTest(const void* player, int flag, bool gameWide) noexcept -> bool {
	return g_state.Test(gameWide, gameWide ? 0 : PlayerKey(player), flag);
}

void SetCraftFlag(const void* player, int flag, bool game, bool on) noexcept {
	g_state.Set(game, game ? 0 : PlayerKey(player), flag, on);
}

auto InstallCraftFlags(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	CraftBind(context->exeBase);
	const bool ok = context->InstallInlineHook(ExecRva, ExecEntry, sizeof(ExecEntry), reinterpret_cast<void*>(&ExecHook),
	                                           reinterpret_cast<void**>(&g_execOriginal));
	std::snprintf(report, reportSize, ok ? "on: %d output effects (gold, player and game flags)" : "off: hook failed",
	              static_cast<int>(sizeof(mxl_cube::Effects) / sizeof(mxl_cube::Effects[0])));
	return ok;
}

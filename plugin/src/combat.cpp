// Median's limits on avoiding and blocking hits, on D2R's avoid check (docs/subsystems/combat-stats.md, section 2).
//
// D2R decides whether a hit is avoided in one function, 0x44ef70 (game, defender, missile flag): evade (stat 340) while
// walking or running, otherwise block (the block chance 0x450070 while in the block mode), then avoid (339, missiles) or
// dodge (338, melee); each is a percent roll on the raw stat. It returns 8 / 0x10 / 2 / 4 for the one that succeeded, else 0.
// D2Sigma's twin (D2Combat.cpp 0x10056e20, same results) caps the three passive stats at min(75, 60 + the item stat
// "maximum avoid chance") and its block chance (0x100bd8d0) at min(80, 50 + "maximum block chance"), 50 with the skill
// Indomitable. D2R has neither limit, so a stack of passives or a high block would exceed Median's.
//
// The hook runs D2R's check, then, for a player and a success whose stat is above its cap, keeps the success with
// probability cap / stat: the overall chance is then stat/100 * cap/stat = cap/100, as if the roll had been made on the
// cap. The extra roll uses the unit's own seed, like D2R's. Block compares D2R's own block chance (0x450070, which
// already stops at its own ceiling) with the cap, so Median's 80 cannot be reached, only its lower caps.
//
// Ethereal items: D2R's 0x3d3510 makes an item ethereal and multiplies its weapon damage (or defense) by 3/2; D2Sigma's
// equivalent (0x1007fdd0) adds 25%. The hook does D2R's steps with Median's factor.
//
// Not ported: the same caps for mercenaries (D2Sigma applies them to hirelings too; the player test here is unit type 0)
// and D2Sigma's small difference of letting a walking unit with no evade still try block/dodge/avoid (D2R returns 0).
//
// Entry bytes checked against the dump of build 93847; whole instructions, no rip-relative.
#include "combat.h"

#include "combat_logic.h"
#include "d2r.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t AvoidCheckRva = 0x44ef70;  // (game, defender, missile flag) -> 0 / 2 / 4 / 8 / 0x10
constexpr uint64_t BlockChanceRva = 0x450070;  // (unit) -> D2R's block chance in percent (a jmp [rip+x] thunk)
constexpr uint64_t GetStatRva = 0x2f5020;      // (unit, stat, layer)
constexpr uint64_t CalcCodeRva = 0x33b3b0;     // (dataFlag, unit, code, skillId, level)
constexpr uint64_t CalcCodeBlvl = 41;          // D2R's skill calc code: base level
constexpr uint64_t BaseStatRva = 0x2f48c0;     // (unit, stat, layer): base value
constexpr uint64_t SetStatRva = 0x2f7d10;      // (unit, stat, value, layer)
constexpr uint64_t SetItemFlagRva = 0x36d8f0;  // (item, mask, on)
constexpr uint64_t IsItemTypeRva = 0x373890;   // (item, type or class id): non-zero when the item is of it
constexpr int32_t ItemTypeWeaponClass = 0x2d;

// mov [rsp+8],rbx / mov [rsp+0x10],rbp / push rsi / push rdi / push r14 / sub rsp,0x30
constexpr uint8_t AvoidCheckEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24,
                                       0x10, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x30};
// jmp qword ptr [rip+disp32]
constexpr uint8_t BlockChanceEntry[] = {0xff, 0x25};
// mov [rsp+0x10],rbx / push rdi / sub rsp,0x20 / mov rbx,rcx

using AvoidCheckFn = int32_t (*)(Game* game, Unit* defender, int32_t missile);
using BlockChanceFn = int32_t (*)(Unit* unit);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using CalcCodeFn = uint64_t (*)(uint64_t dataFlag, Unit* unit, uint64_t code, int32_t skillId, uint32_t level);
using SetStatFn = void (*)(Unit* unit, int32_t stat, int32_t value, int32_t layer);
using SetItemFlagFn = void (*)(Unit* item, int32_t mask, int32_t on);
using IsItemTypeFn = int32_t (*)(Unit* item, int32_t type);

const D2RL::PluginContext* g_context = nullptr;
AvoidCheckFn g_original = nullptr;
BlockChanceFn g_blockChance = nullptr;
GetStatFn g_getStat = nullptr;
CalcCodeFn g_calcCode = nullptr;
GetStatFn g_baseStat = nullptr;
SetStatFn g_setStat = nullptr;
SetItemFlagFn g_setItemFlag = nullptr;
IsItemTypeFn g_isItemType = nullptr;
int g_thinned = 0;
int g_logged = 0;

auto Matches(uint64_t base, uint64_t rva, const uint8_t* bytes, size_t size) noexcept -> bool {
	return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes, size) == 0;
}

auto HasIndomitable(Game* game, Unit* unit) noexcept -> bool {
	const uint32_t flag = d2r::DataFlag(game);
	if (d2r::api.getSkillRecord(flag, mxl_combat::SkillIndomitable) == nullptr) {
		return false;
	}
	return static_cast<int32_t>(g_calcCode(flag, unit, CalcCodeBlvl, mxl_combat::SkillIndomitable, 1)) > 0;
}

auto AvoidCheckHook(Game* game, Unit* defender, int32_t missile) -> int32_t {
	const int32_t result = g_original(game, defender, missile);
	if (result == 0 || defender == nullptr || d2r::At<int32_t>(defender, d2r::UnitType) != 0) {
		return result;
	}
	int32_t value = 0;
	int32_t cap = 0;
	if (result == mxl_combat::ResultBlock) {
		value = g_blockChance(defender);
		cap = mxl_combat::BlockCap(g_getStat(defender, mxl_combat::StatMaxBlock, 0),
		                           value > mxl_combat::IndomitableBlock && HasIndomitable(game, defender));
	} else if (const int32_t stat = mxl_combat::StatOfResult(result); stat != 0) {
		value = g_getStat(defender, stat, 0);
		cap = mxl_combat::AvoidCap(g_getStat(defender, mxl_combat::StatMaxAvoid, 0));
	}
	if (value <= cap) {
		return result;
	}
	uint32_t* seed = d2r::api.unitSeed(defender);
	if (seed == nullptr || mxl_combat::KeepsSuccess(value, cap, d2r::RandomBelow(seed, value))) {
		return result;
	}
	++g_thinned;
	if (g_logged < 6 && g_context != nullptr) {
		++g_logged;
		char line[160];
		std::snprintf(line, sizeof(line), "mxl-core: combat: result 0x%x thinned (%d%% above the cap of %d%%)", result, value, cap);
		g_context->LogInfo(line);
	}
	return 0;
}

}  // namespace

auto InstallCombat(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	const uint64_t base = context->exeBase;
	if (!Matches(base, AvoidCheckRva, AvoidCheckEntry, sizeof(AvoidCheckEntry)) ||
	    !Matches(base, BlockChanceRva, BlockChanceEntry, sizeof(BlockChanceEntry))) {
		std::snprintf(report, reportSize, "off: entry bytes of 0x44ef70 / 0x450070 differ");
		return false;
	}
	d2r::Bind(base);
	g_baseStat = reinterpret_cast<GetStatFn>(base + BaseStatRva);
	g_setStat = reinterpret_cast<SetStatFn>(base + SetStatRva);
	g_setItemFlag = reinterpret_cast<SetItemFlagFn>(base + SetItemFlagRva);
	g_isItemType = reinterpret_cast<IsItemTypeFn>(base + IsItemTypeRva);
	g_blockChance = reinterpret_cast<BlockChanceFn>(base + BlockChanceRva);
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	g_calcCode = reinterpret_cast<CalcCodeFn>(base + CalcCodeRva);
	const bool hooked = context->InstallInlineHook(AvoidCheckRva, AvoidCheckEntry, sizeof(AvoidCheckEntry),
	                                               reinterpret_cast<void*>(&AvoidCheckHook),
	                                               reinterpret_cast<void**>(&g_original));
	// The ethereal +25% lives in item_gen.cpp (one owner of 0x3d3510: Median's ethereal roll and +25% everywhere).
	std::snprintf(report, reportSize, "%s: avoid/dodge/evade cap 60+stat (max 75) and block cap 50+stat (max 80, Indomitable 50) %s",
	              hooked ? "on" : "off", hooked ? "hooked on 0x44ef70" : "NOT hooked");
	return hooked;
}

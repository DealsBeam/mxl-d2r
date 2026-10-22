// Median's limits on avoiding and blocking hits, on D2R's avoid check (docs/subsystems/combat-stats.md, section 2).
//
// D2R decides whether a hit is avoided in one function, 0x44ef70 (attacker, defender, missile flag): evade (stat 340) while
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
// Resistances: D2R's per-hit resist step 0x4523e0 clamps 75 + the element's max-resistance stat to 95 (`mov eax,0x5f` at
// 0x4524dd); Median stops at 90. It is plain code in the main image (D2RLoader's stat engine lives in D2RCore.dll, so the
// stat getter is a thunk, but this function is not), so the immediate is patched to 0x5a. The character sheet (0x14e6e70)
// and the hireling panel (0x14f3400) show the same clamp; they are patched as well so the number shown is the number used.
// The context flag the step tests (ctx+0x24, "defender is a non-hireling monster") is 0 for hirelings, so mercenaries
// already take the player branch there: the resist cap covers them with no extra work.
//
// Mercenaries: D2Sigma applies the avoid / dodge / evade caps to hirelings too (the block cap is for players only), so the
// hook accepts a hireling (a monster for which 0x3af240 is non-zero) like a player for those three.
//
// Absorb: D2R's absorb step 0x450670 (ctx, table entry, damage -> damage left) cuts the damage by the defender's absorb
// percent (at most 40) and flat stat and credits the cut amount to the damage block's absorbed total (+0x130), which heals
// the defender. D2Sigma's twin (0x10057a20) does the same cuts but runs every credited amount through the defender's
// Median stats 189 (divisor), 184 (percent off) and 417 (percent off per damage type; D2R 373 / 369 / 446); the damage
// cut itself is unchanged. Those are monster stats (monprop rows) and no item rolls them, so the hook only matters for
// monsters that absorb.
//
// Damage reduction on the incoming damage (off word `damagered`): D2Sigma's other helper, 0x10057c10, is live. Its stub
// (0x1009b7e0, D2Game rva 0xdc4ea) replaces the call of the per-type step 6fcfbd00 in the loop of SUnitDmg 6fcfc0b0 (the
// twelve entries of the damage table), and the helper starts by calling the original step through the adapter 0x10041e00 (so
// flat reduction, resistance and absorb are untouched). When the entry's field is still non-zero afterwards it applies the
// defender's stats 189 (divisor: damage / divisor; a cold, freeze or poison length is halved, at least 5), 184 (percent off a
// damage field) and 417 (percent off a damage field, layer = the damage type) to it, one after the other on the updated field.
// D2R's twin of the step is 0x4523e0 (ctx, table entry, flag) -> the field's new value; entry + 0 points at the field and
// entry + 0x30 at the entry's name ("Phys", "Fire", "Ligt", "Cold", "Magc", "CLen", "FLen", "PLen", "Pois", "Life", "Mana",
// "Stam"), which tells the types apart. The hook runs D2R's step, then Median's stats on the field it left (combat_logic.h).
// The absorb credit was computed inside the step, from the damage before these stats, exactly as in Median.
//
// Not ported: D2Sigma's small difference of letting a walking unit with no evade still try block/dodge/avoid (D2R returns 0).
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

constexpr uint64_t AvoidCheckRva = 0x44ef70;  // (attacker unit, defender unit, missile flag) -> 0 / 2 / 4 / 8 / 0x10
constexpr uint64_t BlockChanceRva = 0x450070;  // (unit) -> D2R's block chance in percent (a jmp [rip+x] thunk)
constexpr uint64_t GetStatRva = 0x2f5020;      // (unit, stat, layer)
constexpr uint64_t CalcCodeRva = 0x33b3b0;     // (dataFlag, unit, code, skillId, level)
constexpr uint64_t CalcCodeBlvl = 41;          // D2R's skill calc code: base level
constexpr uint64_t BaseStatRva = 0x2f48c0;     // (unit, stat, layer): base value
constexpr uint64_t SetStatRva = 0x2f7d10;      // (unit, stat, value, layer)
constexpr uint64_t SetItemFlagRva = 0x36d8f0;  // (item, mask, on)
constexpr uint64_t IsItemTypeRva = 0x373890;   // (item, type or class id): non-zero when the item is of it
constexpr int32_t ItemTypeWeaponClass = 0x2d;
constexpr uint64_t AbsorbRva = 0x450670;       // (ctx, table entry, damage) -> damage left: absorb percent, then flat
constexpr uint64_t UnitDataFlagRva = 0x34a0e0;  // (unit) -> data bank flag byte
constexpr uint64_t IsHirelingRva = 0x3af240;   // (monster) -> non-zero for a mercenary
constexpr int CtxDefender = 0x18;              // damage context: the defending unit
constexpr int CtxDamage = 0x28;                // damage context: the damage block
constexpr int DamageAbsorbed = 0x130;          // damage block: life the defender gets back from absorbing
constexpr int EntryResistStat = 0x08;          // table entry: the resistance stat of the element
constexpr int EntryAbsorbPercent = 0x18;       // table entry: the absorb-percent stat or -1
constexpr int EntryAbsorbFlat = 0x1c;          // table entry: the flat absorb stat
constexpr uint64_t HitStepRva = 0x4523e0;      // (ctx, table entry, flag) -> the entry's damage field after resist and absorb
constexpr int EntryField = 0x00;               // table entry: pointer to the damage field (int32)
constexpr int EntryName = 0x30;                // table entry: pointer to the name ("Phys", "Fire", ...)

// mov [rsp+8],rbx / mov [rsp+0x10],rbp / push rsi / push rdi / push r14 / sub rsp,0x30
constexpr uint8_t AvoidCheckEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24,
                                       0x10, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x30};
// jmp qword ptr [rip+disp32]
constexpr uint8_t BlockChanceEntry[] = {0xff, 0x25};
// mov [rsp+8],rbx / mov [rsp+0x10],rsi / push rdi / sub rsp,0x20
constexpr uint8_t AbsorbEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20};
// mov [rsp+0x18],rbp / push rsi / push r12 / push r13 / push r14 / push r15 / sub rsp,0x50
constexpr uint8_t HitStepEntry[] = {0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x41, 0x54, 0x41, 0x55,
                                    0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x50};
// push rsi / sub rsp,0x20 / mov rsi,rcx / call rel32
constexpr uint8_t IsHirelingEntry[] = {0x40, 0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf1};
// Resist ceiling immediates: the bytes of the instruction (`mov r32, 0x5f`) and the offset of its immediate.
struct ResistClamp {
	uint64_t rva;
	uint8_t bytes[5];
	const char* what;
};
constexpr ResistClamp ResistClamps[] = {
    {0x4524dd, {0xb8, 0x5f, 0x00, 0x00, 0x00}, "per-hit resist step 0x4523e0"},
    {0x14e7294, {0xb9, 0x5f, 0x00, 0x00, 0x00}, "character sheet 0x14e6e70"},
    {0x14f4686, {0xba, 0x5f, 0x00, 0x00, 0x00}, "hireling panel 0x14f3400"},
};

using AvoidCheckFn = int32_t (*)(Unit* attacker, Unit* defender, int32_t missile);
using BlockChanceFn = int32_t (*)(Unit* unit);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using CalcCodeFn = uint64_t (*)(uint64_t dataFlag, Unit* unit, uint64_t code, int32_t skillId, uint32_t level);
using SetStatFn = void (*)(Unit* unit, int32_t stat, int32_t value, int32_t layer);
using SetItemFlagFn = void (*)(Unit* item, int32_t mask, int32_t on);
using IsItemTypeFn = int32_t (*)(Unit* item, int32_t type);
using AbsorbFn = int64_t (*)(void* ctx, const uint8_t* entry, int32_t damage);
using HitStepFn = int32_t (*)(void* ctx, const uint8_t* entry, int32_t flag);
using IsHirelingFn = int32_t (*)(Unit* monster);
using UnitDataFlagFn = uint8_t (*)(Unit* unit);

const D2RL::PluginContext* g_context = nullptr;
AvoidCheckFn g_original = nullptr;
BlockChanceFn g_blockChance = nullptr;
GetStatFn g_getStat = nullptr;
CalcCodeFn g_calcCode = nullptr;
GetStatFn g_baseStat = nullptr;
SetStatFn g_setStat = nullptr;
SetItemFlagFn g_setItemFlag = nullptr;
IsItemTypeFn g_isItemType = nullptr;
AbsorbFn g_absorbOriginal = nullptr;
HitStepFn g_hitStepOriginal = nullptr;
IsHirelingFn g_isHireling = nullptr;
UnitDataFlagFn g_unitDataFlag = nullptr;
int g_thinned = 0;
int g_absorbed = 0;
int g_reduced = 0;
int g_logged = 0;

auto Matches(uint64_t base, uint64_t rva, const uint8_t* bytes, size_t size) noexcept -> bool {
	return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes, size) == 0;
}

// The data bank flag comes from the unit itself (0x34a0e0): the avoid check has no Game argument.
auto HasIndomitable(Unit* unit) noexcept -> bool {
	const uint32_t flag = g_unitDataFlag(unit);
	if (d2r::api.getSkillRecord(flag, mxl_combat::SkillIndomitable) == nullptr) {
		return false;
	}
	return static_cast<int32_t>(g_calcCode(flag, unit, CalcCodeBlvl, mxl_combat::SkillIndomitable, 1)) > 0;
}

// A player, or (for the avoid / dodge / evade caps) a mercenary.
auto IsPlayerOrHireling(Unit* unit) noexcept -> bool {
	const int32_t type = d2r::At<int32_t>(unit, d2r::UnitType);
	return type == 0 || (type == 1 && g_isHireling(unit) != 0);
}

auto AvoidCheckHook(Unit* attacker, Unit* defender, int32_t missile) -> int32_t {
	const int32_t result = g_original(attacker, defender, missile);
	if (result == 0 || defender == nullptr || !IsPlayerOrHireling(defender)) {
		return result;
	}
	if (result == mxl_combat::ResultBlock && d2r::At<int32_t>(defender, d2r::UnitType) != 0) {
		return result;  // Median's block cap is for players only
	}
	int32_t value = 0;
	int32_t cap = 0;
	if (result == mxl_combat::ResultBlock) {
		value = g_blockChance(defender);
		cap = mxl_combat::BlockCapFor(g_getStat(defender, mxl_combat::StatMaxBlock, 0), value,
		                              [defender] { return HasIndomitable(defender); });
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

// One absorbed amount as the defender is healed by it: D2R credits it as is, Median runs it through stats 189 / 184 / 417.
auto Credit(Unit* defender, int32_t amount, int32_t layer) noexcept -> int32_t {
	if (amount == 0) {
		return amount;
	}
	return mxl_combat::AbsorbCredit(amount, g_getStat(defender, mxl_combat::StatAbsorbDivisor, 0),
	                                g_getStat(defender, mxl_combat::StatAbsorbPercent, 0),
	                                g_getStat(defender, mxl_combat::StatAbsorbTypePercent, layer));
}

// D2R's 0x450670 with Median's credit: the damage cut is exactly D2R's (percent capped at 40, then flat * 256, both limited
// to what is left), only what is added to the absorbed total goes through Credit().
auto AbsorbHook(void* ctx, const uint8_t* entry, int32_t damage) -> int64_t {
	const int32_t percentStat = d2r::At<int32_t>(entry, EntryAbsorbPercent);
	if (percentStat == -1) {
		return static_cast<int64_t>(static_cast<uint32_t>(damage));
	}
	Unit* defender = d2r::At<Unit*>(ctx, CtxDefender);
	auto* absorbed = reinterpret_cast<int32_t*>(d2r::At<uint8_t*>(ctx, CtxDamage) + DamageAbsorbed);
	const int32_t layer = mxl_combat::AbsorbLayer(d2r::At<int32_t>(entry, EntryResistStat));
	int32_t left = damage;
	if (const int32_t cut = mxl_combat::AbsorbPercentCut(left, g_getStat(defender, percentStat, 0)); cut != 0) {
		const int32_t credit = Credit(defender, cut, layer);
		*absorbed += credit;
		left -= cut;
		if (credit != cut && g_absorbed < 6 && g_context != nullptr) {
			++g_absorbed;
			char line[160];
			std::snprintf(line, sizeof(line), "mxl-core: combat: absorb of %d healed %d (layer %d)", cut, credit, layer);
			g_context->LogInfo(line);
		}
	}
	const int32_t flat = g_getStat(defender, d2r::At<int32_t>(entry, EntryAbsorbFlat), 0) * 0x100;
	if (flat > 0) {
		const int32_t take = flat < left ? flat : left;
		*absorbed += Credit(defender, take, layer);
		left -= take;
	}
	return static_cast<int64_t>(static_cast<uint32_t>(left));
}

// D2R's per-type step, then Median's stats 189 / 184 / 417 on what it left in the entry's field (D2Sigma 0x10057c10).
auto HitStepHook(void* ctx, const uint8_t* entry, int32_t flag) -> int32_t {
	const int32_t result = g_hitStepOriginal(ctx, entry, flag);
	auto* field = d2r::At<int32_t*>(entry, EntryField);
	const char* name = d2r::At<const char*>(entry, EntryName);
	Unit* defender = d2r::At<Unit*>(ctx, CtxDefender);
	if (field == nullptr || name == nullptr || defender == nullptr || *field == 0) {
		return result;
	}
	uint32_t word = 0;
	std::memcpy(&word, name, sizeof(word));
	const mxl_combat::DamageType type = mxl_combat::DamageTypeOfName(word);
	if (type.kind == mxl_combat::FieldKind::None) {
		return result;
	}
	const int32_t divisor = g_getStat(defender, mxl_combat::StatAbsorbDivisor, 0);
	const int32_t percent = g_getStat(defender, mxl_combat::StatAbsorbPercent, 0);
	const int32_t typePercent = g_getStat(defender, mxl_combat::StatAbsorbTypePercent, type.layer);
	if (divisor == 0 && percent == 0 && typePercent == 0) {
		return result;
	}
	const int32_t before = *field;
	const int32_t after = mxl_combat::ReduceField(before, type, divisor, percent, typePercent);
	if (after == before) {
		return result;
	}
	*field = after;
	if (g_reduced < 6 && g_context != nullptr) {
		++g_reduced;
		char line[200];
		std::snprintf(line, sizeof(line), "mxl-core: combat: %.4s %d -> %d (189 %d, 184 %d%%, 417 layer %d %d%%)", name, before, after,
		              divisor, percent, type.layer, typePercent);
		g_context->LogInfo(line);
	}
	return after;
}

}  // namespace

auto InstallCombat(const D2RL::PluginContext* context, bool damageReduction, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	const uint64_t base = context->exeBase;
	if (!Matches(base, AvoidCheckRva, AvoidCheckEntry, sizeof(AvoidCheckEntry)) ||
	    !Matches(base, BlockChanceRva, BlockChanceEntry, sizeof(BlockChanceEntry)) ||
	    !Matches(base, AbsorbRva, AbsorbEntry, sizeof(AbsorbEntry)) ||
	    !Matches(base, IsHirelingRva, IsHirelingEntry, sizeof(IsHirelingEntry)) ||
	    (damageReduction && !Matches(base, HitStepRva, HitStepEntry, sizeof(HitStepEntry)))) {
		std::snprintf(report, reportSize, "off: entry bytes of 0x44ef70 / 0x450070 / 0x450670 / 0x3af240 / 0x4523e0 differ");
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
	g_isHireling = reinterpret_cast<IsHirelingFn>(base + IsHirelingRva);
	g_unitDataFlag = reinterpret_cast<UnitDataFlagFn>(base + UnitDataFlagRva);
	const bool hooked = context->InstallInlineHook(AvoidCheckRva, AvoidCheckEntry, sizeof(AvoidCheckEntry),
	                                               reinterpret_cast<void*>(&AvoidCheckHook),
	                                               reinterpret_cast<void**>(&g_original));
	// The ethereal +25% lives in item_gen.cpp (one owner of 0x3d3510: Median's ethereal roll and +25% everywhere).
	const bool absorbHooked = context->InstallInlineHook(AbsorbRva, AbsorbEntry, sizeof(AbsorbEntry),
	                                                     reinterpret_cast<void*>(&AbsorbHook),
	                                                     reinterpret_cast<void**>(&g_absorbOriginal));
	// Same entry shape as the absorb hook; D2RLoader's wrapper of the step calls the original, so this hook sees every call.
	const bool reductionHooked =
	    !damageReduction || context->InstallInlineHook(HitStepRva, HitStepEntry, sizeof(HitStepEntry), reinterpret_cast<void*>(&HitStepHook),
	                                                    reinterpret_cast<void**>(&g_hitStepOriginal));
	int resistPatched = 0;
	for (const ResistClamp& clamp : ResistClamps) {
		if (Matches(base, clamp.rva, clamp.bytes, sizeof(clamp.bytes)) &&
		    context->PatchWriteU8(clamp.rva + 1, &clamp.bytes[1], 1, static_cast<uint8_t>(mxl_combat::ResistMaxMedian))) {
			++resistPatched;
		}
	}
	const bool resistOk = resistPatched == static_cast<int>(sizeof(ResistClamps) / sizeof(ResistClamps[0]));
	const int resistTotal = static_cast<int>(sizeof(ResistClamps) / sizeof(ResistClamps[0]));
	const bool all = hooked && resistOk && absorbHooked && reductionHooked;
	std::snprintf(report, reportSize,
	              "%s: avoid/dodge/evade cap 60+stat (max 75, mercenaries too), block cap 50+stat (max 80), resist cap 75+max (max 90, %d/%d "
	              "clamps), absorb credit and damage reduction stats 184/189/417; hooks 0x44ef70 %s, 0x450670 %s, 0x4523e0 %s",
	              all ? "on" : "partial", resistPatched, resistTotal, hooked ? "ok" : "NOT hooked", absorbHooked ? "ok" : "NOT hooked",
	              !damageReduction ? "off (damagered)" : reductionHooked ? "ok" : "NOT hooked");
	return all;
}

// D2Sigma's skill calc codes (docs/plugin-spec.md, "Still to specify": skill calc codes).
//
// tools/txt_migrate.py appends Median's calc code names to skillcalc.txt (rows mxl_ids::FirstCalcCode.., the same in
// both banks), so formulas naming them compile. D2R evaluates a code through 0x33b3b0 (dataFlag, unit, code,
// skillId, level), whose switch answers unknown rows with 0. The hook answers the D2Sigma codes implemented here and
// passes everything else on, so a code without an implementation keeps reading 0.
//
//   syn1-syn6  the unit's synergy stats with the skill as their parameter (D2Sigma 0x100a6340 cases 74-77, 104-105:
//              D2Common 10973 (unit, stat, skill)); items grant them as "stat with a skill parameter" properties
//   skcp       the unit's "+N to maximum level" stat with the skill as its parameter (case 89, Median stat 449)
//   aura, pass 1 if the unit has the skill's aurastate / passivestate, else 0 (cases 90, 91)
//   bl12-bl78  linear in the skill's base level: blvl > 0 ? parA + (blvl-1)*parB : 0 (cases 78-81, 0x100a5c40); the
//   blz1-blz7  "z" forms (93-96) give parA instead of 0 at base level 0
//   bd12-bd78  diminishing in the base level, like vanilla's dm12: parA + (110*blvl/(blvl+6))*(parB-parA)/100
//   bdz1-bdz7  (cases 82-85, 97-100, 0x100a5be0)
//   wdm        the skill's weapon damage percent, SrcDam*100/128 (case 86)
//   area       the unit's current level id (case 106, 0x100bdf40: room -> level -> id)
//   skcd       the skill's cooldown: its localdelay formula (D2R record +0x238) minus the unit's cooldown-reduction
//              stat for that skill (case 73, 0x100b0ed0, Median stat 309)
//   bmax       the skill's current maximum base level: max(0, min(mlvl + skcn + skcp, clvl + 1 - reqlevel)) (case 88,
//              0x100a6e00); mlvl and skcn come from calc9/calc10, where tools/txt_migrate.py stores them
//   maxe       1 when the skill's base level (> 0) equals bmax (case 92)
//   mxfn       D2Sigma's formula functions, called through hidden proxy skills (tools/formula.py, txt_migrate.py):
//              the formula reads skill('mxf_N'.mxfn) and mxf_N's Param1..4 hold the function and its arguments.
//              1 hasstate(state): the unit has the state; 2 statlayer(stat, mode, layer): the stat at that layer,
//              mode 0 total / 1 base / 2 the third D2R getter, as D2R's own stat(); 3 itemstat and 4 tabpoints: 0
//              for now
//
// Missile formulas: D2R's missile code switch 0x3b9dc0 (dataFlag, missile, owner, code, missileId, level) is hooked the
// same way for D2Sigma's two missile codes (0x10088fd0, codes 43-44):
//   msyn       the owner's synergy stat with the missile's skill as parameter (Median stat 382)
//   blvl       the base level of the missile's skill on the owner (players uncapped; other owners at most 1, as
//              D2Sigma's cap 0x100bdff0 gives 1 to units that are not players or hirelings)
// Not yet: (max-level checks), mnhp/mnar
// (Median's minion scaling), mode, pdmn/pdmx, mlvl/skcn (written into skpoints instead).
#include "calc_codes.h"

#include "d2r.h"
#include "mxl_ids.h"

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t CalcCodeRva = 0x33b3b0;
constexpr uint64_t GetUnitStatRva = 0x2f5020;  // (unit, stat, layer) -> total value
constexpr uint64_t HasStateRva = 0x3351b0;     // (unit, state) -> non-zero when set
constexpr int SkAuraState = 0xa0;              // int16 in the skills record
constexpr int SkPassiveState = 0xb4;
constexpr int SkParam1 = 0x1b8;                // int32 Param1..Param8, 4 bytes apart
constexpr int SkSrcDam = 0x24d;                // uint8
constexpr int SkLocalDelayCalc = 0x238;        // compiled localdelay formula (-1 none)
constexpr int SkCalc9 = 0x1b0;                 // mlvl (written by txt_migrate.py)
constexpr int SkCalc10 = 0x1b4;                // skcn
constexpr int SkReqLevel = 0x214;              // uint16
constexpr int32_t StatLevel = 12;
constexpr uint64_t GetUnitBaseStatRva = 0x2f48c0;  // stat() mode 1
constexpr uint64_t GetUnitStatMode2Rva = 0x2f5c60; // stat() mode 2
constexpr uint64_t MissCodeRva = 0x3b9dc0;
// MOV [RSP+0x8],RBX / MOV [RSP+0x18],RBP / PUSH RSI / PUSH RDI / PUSH R14
constexpr uint8_t MissCodeEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56};
constexpr int MsSkill = 0x1b6;  // int16 in the missiles record
constexpr uint64_t D2RCodeBlvl = 41;           // D2R's own "blvl" row
constexpr uint64_t UnitRoomRva = 0x34b440;     // (unit) -> room
constexpr uint64_t RoomLevelIdRva = 0x2efc10;  // (room) -> level id
// MOV [RSP+0x10],RBX / MOV [RSP+0x18],RBP / MOV [RSP+0x20],RSI
constexpr uint8_t CalcCodeEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c,
                                     0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};

using CalcCodeFn = uint64_t (*)(uint64_t dataFlag, void* unit, uint64_t code, int32_t skillId, uint32_t level) noexcept;
using GetUnitStatFn = int32_t (*)(void* unit, int32_t stat, int32_t layer);
using HasStateFn = uint64_t (*)(void* unit, int32_t state);
using UnitRoomFn = void* (*)(void* unit);
using RoomLevelIdFn = int32_t (*)(void* room);

CalcCodeFn g_original = nullptr;
using MissCodeFn = uint64_t (*)(uint64_t dataFlag, void* missile, void* owner, uint64_t code, int32_t missileId,
                                uint32_t level) noexcept;
MissCodeFn g_missOriginal = nullptr;
GetUnitStatFn g_getUnitStat = nullptr;
GetUnitStatFn g_getUnitBaseStat = nullptr;
GetUnitStatFn g_getUnitStatMode2 = nullptr;
HasStateFn g_hasState = nullptr;
UnitRoomFn g_unitRoom = nullptr;
RoomLevelIdFn g_roomLevelId = nullptr;

auto LayerStat(void* unit, int32_t stat, int32_t skillId) noexcept -> uint64_t {
	if (unit == nullptr) {
		return 0;
	}
	return static_cast<uint64_t>(static_cast<int64_t>(g_getUnitStat(unit, stat, skillId)));
}

auto SkillState(uint64_t dataFlag, void* unit, int32_t skillId, int field) noexcept -> uint64_t {
	const uint8_t* skill = d2r::api.getSkillRecord(static_cast<uint32_t>(dataFlag & 0xff), skillId);
	if (unit == nullptr || skill == nullptr) {
		return 0;
	}
	const int32_t state = d2r::At<int16_t>(skill, field);
	return state > 0 && g_hasState(unit, state) != 0 ? 1 : 0;
}

auto Param(const uint8_t* skill, int n) noexcept -> int32_t {  // n = 1..8
	return d2r::At<int32_t>(skill, SkParam1 + (n - 1) * 4);
}

// bl/blz (diminishing = false) and bd/bdz (true) on Param a / a+1.
auto BaseLevelProgression(uint64_t dataFlag, void* unit, uint64_t code, int32_t skillId, uint32_t level, int a,
                          bool diminishing, bool z) noexcept -> uint64_t {
	const uint8_t* skill = d2r::api.getSkillRecord(static_cast<uint32_t>(dataFlag & 0xff), skillId);
	if (skill == nullptr) {
		return 0;
	}
	(void)code;
	const int32_t lo = Param(skill, a), hi = Param(skill, a + 1);
	const int32_t blvl = static_cast<int32_t>(g_original(dataFlag, unit, D2RCodeBlvl, skillId, level));
	if (blvl <= 0) {
		return static_cast<uint64_t>(static_cast<int64_t>(z ? lo : 0));
	}
	if (!diminishing) {
		return static_cast<uint64_t>(static_cast<int64_t>(lo + (blvl - 1) * hi));
	}
	const int64_t scale = (110LL * blvl) / (blvl + 6);
	return static_cast<uint64_t>(lo + (scale * (hi - lo)) / 100);
}

auto MaxBaseLevel(uint64_t dataFlag, void* unit, int32_t skillId, uint32_t level) noexcept -> int32_t {
	const uint32_t flag = static_cast<uint32_t>(dataFlag & 0xff);
	const uint8_t* skill = d2r::api.getSkillRecord(flag, skillId);
	if (skill == nullptr || unit == nullptr) {
		return 0;
	}
	auto* u = static_cast<d2r::Unit*>(unit);
	const int32_t lvl = static_cast<int32_t>(level);
	const int32_t cap = d2r::api.evalSkillCalc(flag, u, d2r::At<int32_t>(skill, SkCalc9), skillId, lvl) +
	                    d2r::api.evalSkillCalc(flag, u, d2r::At<int32_t>(skill, SkCalc10), skillId, lvl) +
	                    g_getUnitStat(unit, mxl_ids::StatSkillCapBonus, skillId);
	const int32_t byLevel = g_getUnitStat(unit, StatLevel, 0) + 1 - d2r::At<uint16_t>(skill, SkReqLevel);
	const int32_t v = cap < byLevel ? cap : byLevel;
	return v > 0 ? v : 0;
}

auto FormulaFunction(uint64_t dataFlag, void* unit, int32_t proxySkill) noexcept -> uint64_t {
	const uint8_t* proxy = d2r::api.getSkillRecord(static_cast<uint32_t>(dataFlag & 0xff), proxySkill);
	if (proxy == nullptr || unit == nullptr) {
		return 0;
	}
	const int32_t a = Param(proxy, 2), b = Param(proxy, 3), c = Param(proxy, 4);
	switch (Param(proxy, 1)) {
	case 1: return a >= 0 && g_hasState(unit, a) != 0 ? 1 : 0;
	case 2: {
		if (a < 0) {
			return 0;
		}
		const GetUnitStatFn get = b == 1 ? g_getUnitBaseStat : b == 2 ? g_getUnitStatMode2 : g_getUnitStat;
		return static_cast<uint64_t>(static_cast<int64_t>(get(unit, a, c)));
	}
	default: return 0;
	}
}

auto CalcCode(uint64_t dataFlag, void* unit, uint64_t code, int32_t skillId, uint32_t level) noexcept -> uint64_t {
	switch (static_cast<int32_t>(code & 0xff)) {
	case mxl_ids::Calc_syn1: return LayerStat(unit, mxl_ids::StatSyn1, skillId);
	case mxl_ids::Calc_syn2: return LayerStat(unit, mxl_ids::StatSyn2, skillId);
	case mxl_ids::Calc_syn3: return LayerStat(unit, mxl_ids::StatSyn3, skillId);
	case mxl_ids::Calc_syn4: return LayerStat(unit, mxl_ids::StatSyn4, skillId);
	case mxl_ids::Calc_syn5: return LayerStat(unit, mxl_ids::StatSyn5, skillId);
	case mxl_ids::Calc_syn6: return LayerStat(unit, mxl_ids::StatSyn6, skillId);
	case mxl_ids::Calc_skcp: return LayerStat(unit, mxl_ids::StatSkillCapBonus, skillId);
	case mxl_ids::Calc_aura: return SkillState(dataFlag, unit, skillId, SkAuraState);
	case mxl_ids::Calc_pass: return SkillState(dataFlag, unit, skillId, SkPassiveState);
	case mxl_ids::Calc_bl12: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 1, false, false);
	case mxl_ids::Calc_bl34: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 3, false, false);
	case mxl_ids::Calc_bl56: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 5, false, false);
	case mxl_ids::Calc_bl78: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 7, false, false);
	case mxl_ids::Calc_blz1: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 1, false, true);
	case mxl_ids::Calc_blz3: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 3, false, true);
	case mxl_ids::Calc_blz5: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 5, false, true);
	case mxl_ids::Calc_blz7: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 7, false, true);
	case mxl_ids::Calc_bd12: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 1, true, false);
	case mxl_ids::Calc_bd34: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 3, true, false);
	case mxl_ids::Calc_bd56: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 5, true, false);
	case mxl_ids::Calc_bd78: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 7, true, false);
	case mxl_ids::Calc_bdz1: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 1, true, true);
	case mxl_ids::Calc_bdz3: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 3, true, true);
	case mxl_ids::Calc_bdz5: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 5, true, true);
	case mxl_ids::Calc_bdz7: return BaseLevelProgression(dataFlag, unit, code, skillId, level, 7, true, true);
	case mxl_ids::Calc_skcd: {
		const uint8_t* skill = d2r::api.getSkillRecord(static_cast<uint32_t>(dataFlag & 0xff), skillId);
		if (skill == nullptr || unit == nullptr) {
			return 0;
		}
		const int32_t delay = d2r::api.evalSkillCalc(static_cast<uint32_t>(dataFlag & 0xff),
		                                             static_cast<d2r::Unit*>(unit),
		                                             d2r::At<int32_t>(skill, SkLocalDelayCalc), skillId,
		                                             static_cast<int32_t>(level));
		const int32_t reduction = g_getUnitStat(unit, mxl_ids::StatCooldownReduction, skillId);
		return static_cast<uint64_t>(static_cast<int64_t>(reduction != 0 ? delay - reduction : delay));
	}
	case mxl_ids::Calc_mxfn: return FormulaFunction(dataFlag, unit, skillId);
	case mxl_ids::Calc_bmax: return static_cast<uint64_t>(MaxBaseLevel(dataFlag, unit, skillId, level));
	case mxl_ids::Calc_maxe: {
		const int32_t blvl = static_cast<int32_t>(g_original(dataFlag, unit, D2RCodeBlvl, skillId, level));
		return blvl > 0 && blvl == MaxBaseLevel(dataFlag, unit, skillId, level) ? 1 : 0;
	}
	case mxl_ids::Calc_wdm: {
		const uint8_t* skill = d2r::api.getSkillRecord(static_cast<uint32_t>(dataFlag & 0xff), skillId);
		return skill == nullptr ? 0 : static_cast<uint64_t>(d2r::At<uint8_t>(skill, SkSrcDam) * 100 / 128);
	}
	case mxl_ids::Calc_area: {
		void* room = unit == nullptr ? nullptr : g_unitRoom(unit);
		return room == nullptr ? 0 : static_cast<uint64_t>(static_cast<int64_t>(g_roomLevelId(room)));
	}
	default: return g_original(dataFlag, unit, code, skillId, level);
	}
}

auto MissCode(uint64_t dataFlag, void* missile, void* owner, uint64_t code, int32_t missileId,
              uint32_t level) noexcept -> uint64_t {
	const int32_t c = static_cast<int32_t>(code & 0xff);
	if (c != mxl_ids::MissCalc_msyn && c != mxl_ids::MissCalc_blvl) {
		return g_missOriginal(dataFlag, missile, owner, code, missileId, level);
	}
	if (owner == nullptr) {
		return 0;
	}
	const uint8_t* rec = d2r::api.missileRecord(static_cast<uint32_t>(dataFlag & 0xff), missileId);
	const int32_t skill = rec == nullptr ? -1 : d2r::At<int16_t>(rec, MsSkill);
	if (skill < 0) {
		return 0;
	}
	if (c == mxl_ids::MissCalc_msyn) {
		return LayerStat(owner, mxl_ids::StatMissileSyn, skill);
	}
	int32_t blvl = static_cast<int32_t>(g_original(dataFlag, owner, D2RCodeBlvl, skill, level));
	if (d2r::At<int32_t>(owner, d2r::UnitType) != 0 && blvl > 1) {
		blvl = 1;
	}
	return static_cast<uint64_t>(static_cast<int64_t>(blvl));
}

}  // namespace

auto InstallCalcCodes(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	d2r::Bind(context->exeBase);
	g_getUnitStat = reinterpret_cast<GetUnitStatFn>(context->exeBase + GetUnitStatRva);
	g_getUnitBaseStat = reinterpret_cast<GetUnitStatFn>(context->exeBase + GetUnitBaseStatRva);
	g_getUnitStatMode2 = reinterpret_cast<GetUnitStatFn>(context->exeBase + GetUnitStatMode2Rva);
	g_hasState = reinterpret_cast<HasStateFn>(context->exeBase + HasStateRva);
	g_unitRoom = reinterpret_cast<UnitRoomFn>(context->exeBase + UnitRoomRva);
	g_roomLevelId = reinterpret_cast<RoomLevelIdFn>(context->exeBase + RoomLevelIdRva);
	const bool ok = context->InstallInlineHook(CalcCodeRva, CalcCodeEntry, sizeof(CalcCodeEntry), CalcCode, &g_original) &&
	                context->InstallInlineHook(MissCodeRva, MissCodeEntry, sizeof(MissCodeEntry), MissCode, &g_missOriginal);
	std::snprintf(report, reportSize, ok ? "on: syn1-6, skcp, aura, pass, bl/blz/bd/bdz, wdm, area, skcd, bmax, maxe, mxfn (hasstate, statlayer); missiles msyn, blvl (rows %d..)" : "off: calc code hook failed (rows %d..)",
	              mxl_ids::FirstCalcCode);
	return ok;
}

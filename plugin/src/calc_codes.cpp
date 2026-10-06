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
#include "calc_codes.h"

#include "mxl_ids.h"

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t CalcCodeRva = 0x33b3b0;
constexpr uint64_t GetUnitStatRva = 0x2f5020;  // (unit, stat, layer) -> total value
// MOV [RSP+0x10],RBX / MOV [RSP+0x18],RBP / MOV [RSP+0x20],RSI
constexpr uint8_t CalcCodeEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c,
                                     0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};

using CalcCodeFn = uint64_t (*)(uint64_t dataFlag, void* unit, uint64_t code, int32_t skillId, uint32_t level) noexcept;
using GetUnitStatFn = int32_t (*)(void* unit, int32_t stat, int32_t layer);

CalcCodeFn g_original = nullptr;
GetUnitStatFn g_getUnitStat = nullptr;

auto LayerStat(void* unit, int32_t stat, int32_t skillId) noexcept -> uint64_t {
	if (unit == nullptr) {
		return 0;
	}
	return static_cast<uint64_t>(static_cast<int64_t>(g_getUnitStat(unit, stat, skillId)));
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
	default: return g_original(dataFlag, unit, code, skillId, level);
	}
}

}  // namespace

auto InstallCalcCodes(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_getUnitStat = reinterpret_cast<GetUnitStatFn>(context->exeBase + GetUnitStatRva);
	const bool ok = context->InstallInlineHook(CalcCodeRva, CalcCodeEntry, sizeof(CalcCodeEntry), CalcCode, &g_original);
	std::snprintf(report, reportSize, ok ? "on: syn1-syn6, skcp (rows %d..)" : "off: calc code hook failed (rows %d..)",
	              mxl_ids::FirstCalcCode);
	return ok;
}

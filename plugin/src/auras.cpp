// D2Sigma's aura function (srvdofunc 165, D2SkillDo.cpp 0x100ab190) -> slot 184.
//
// D2Sigma's version is vanilla's aura routine: the aurastats (calcs evaluated) go onto the caster under aurastate and
// onto the units its finder picks (aurarangecalc, aurafilter) under auratargetstate, refreshed on every aura pulse.
// Its one addition (callback 0x100bbbe0) is a layer per aurastat, read from D2Sigma's skills2 table: Median uses it
// for the stat `state` (which state), packed skill-on-event stats, +skill/synergy stats and counter slots, and some
// auras list one stat several times with different layers.
//
// The slot runs D2R's own aura function (srvdofunc 65, 0x563d90), which handles states, duration, pulses and
// targets. D2R writes every aurastat at layer 0, so a stat listed several times keeps only its last value; for the
// auras in mxl_auras.h (tools/txt_migrate.py aura_tables) the caster's aura stat lists are then rewritten: each
// layered aurastat gets its own calc value at its layer, and layer 0 of that stat the value of its last unlayered
// listing (or 0). Units other than the caster keep D2R's layer-0 stats (no Median aura with layers has a range).
#include "auras.h"

#include "d2r.h"
#include "mxl_auras.h"

#include <cstdint>

namespace {

constexpr uint64_t D2RAuraRva = 0x563d90;       // D2R srvdofunc 65 (game, unit, skill, level)
constexpr uint64_t StatListByStateRva = 0x2f5b80;  // (unit, state, 0) -> the unit's stat list for that state
constexpr uint64_t StatListSetRva = 0x2f7c00;   // (dataFlag, list, stat, value, layer)
constexpr int SkAuraStat = 0x74;                // 6 x int16 aurastat1-6
constexpr int SkAuraStatCalc = 0x88;            // 6 x int32 compiled aurastatcalc1-6
constexpr int SkAuraState = 0xa0;
constexpr int SkAuraTargetState = 0xa2;

using AuraFn = void (*)(d2r::Game* game, d2r::Unit* unit, int32_t skill, int32_t level);
using StatListByStateFn = void* (*)(d2r::Unit* unit, int32_t state, uint32_t flags);
using StatListSetFn = void (*)(uint32_t dataFlag, void* list, int32_t stat, int32_t value, int32_t layer);

AuraFn g_d2rAura = nullptr;
StatListByStateFn g_statListByState = nullptr;
StatListSetFn g_statListSet = nullptr;

auto LayeredAuraOf(int32_t skillId) noexcept -> const mxl_auras::LayeredAura* {
	for (const auto& aura : mxl_auras::Auras) {
		if (aura.skill == skillId) {
			return &aura;
		}
	}
	return nullptr;
}

void ApplyLayers(const mxl_auras::LayeredAura& aura, uint32_t dataFlag, void* list, const int32_t (&values)[6]) noexcept {
	for (int k = 0; k < 6; ++k) {
		const mxl_auras::Slot& slot = aura.slots[k];
		if (slot.stat < 0) {
			continue;
		}
		if (slot.layer != 0) {
			g_statListSet(dataFlag, list, slot.stat, values[k], slot.layer);
			continue;
		}
	}
	// layer 0 of each stat that also has layered listings: its last unlayered value, else 0
	for (int k = 0; k < 6; ++k) {
		const int32_t stat = aura.slots[k].stat;
		if (stat < 0 || aura.slots[k].layer == 0) {
			continue;
		}
		int32_t plain = 0;
		for (int j = 0; j < 6; ++j) {
			if (aura.slots[j].stat == stat && aura.slots[j].layer == 0) {
				plain = values[j];
			}
		}
		g_statListSet(dataFlag, list, stat, plain, 0);
	}
}

}  // namespace

auto SrvDoAura(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr || g_d2rAura == nullptr) {
		return 0;
	}
	g_d2rAura(game, unit, skillId, level);
	const mxl_auras::LayeredAura* aura = LayeredAuraOf(skillId);
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (aura == nullptr || skill == nullptr) {
		return 1;
	}
	int32_t values[6] = {};
	for (int k = 0; k < 6; ++k) {
		if (aura->slots[k].stat >= 0 && d2r::At<int16_t>(skill, SkAuraStat + 2 * k) >= 0) {
			values[k] = d2r::api.evalSkillCalc(dataFlag, unit, d2r::At<int32_t>(skill, SkAuraStatCalc + 4 * k), skillId,
			                                   level);
		}
	}
	const int16_t states[2] = {d2r::At<int16_t>(skill, SkAuraState), d2r::At<int16_t>(skill, SkAuraTargetState)};
	for (int i = 0; i < 2; ++i) {
		if (states[i] <= 0 || (i == 1 && states[1] == states[0])) {
			continue;
		}
		if (void* list = g_statListByState(unit, states[i], 0)) {
			ApplyLayers(*aura, dataFlag, list, values);
		}
	}
	return 1;
}

void InstallAuras(uint64_t exeBase) noexcept {
	g_d2rAura = reinterpret_cast<AuraFn>(exeBase + D2RAuraRva);
	g_statListByState = reinterpret_cast<StatListByStateFn>(exeBase + StatListByStateRva);
	g_statListSet = reinterpret_cast<StatListSetFn>(exeBase + StatListSetRva);
}

// D2Sigma's aura function (srvdofunc 165, D2SkillDo.cpp 0x100ab190) -> slot 184, and its versions of the self buff
// (18, 0x100ab710) -> slot 174 and the timed storm (124, 0x100a9510) -> slot 180, which add the same per-aurastat layer
// to D2R's functions 18 (0x553e60) and 124 (0x575de0). Only the Median skills that use a layer move to 174/180.
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
constexpr uint64_t D2RBuffRva = 0x553e60;       // D2R srvdofunc 18, self buff (Frozen Armor)
constexpr uint64_t D2RStormRva = 0x575de0;      // D2R srvdofunc 124, timed storm (Hurricane, Armageddon)
constexpr uint64_t StatListByStateRva = 0x2f5b80;  // (unit, state, 0) -> the unit's stat list for that state
constexpr uint64_t StatListSetRva = 0x2f7c00;   // (dataFlag, list, stat, value, layer)
constexpr int SkAuraStat = 0x74;                // 6 x int16 aurastat1-6
constexpr int SkAuraStatCalc = 0x88;            // 6 x int32 compiled aurastatcalc1-6
constexpr int SkAuraState = 0xa0;
constexpr int SkAuraTargetState = 0xa2;

using AuraFn = int64_t (*)(d2r::Game* game, d2r::Unit* unit, int32_t skill, int32_t level);
using StatListByStateFn = void* (*)(d2r::Unit* unit, int32_t state, uint32_t flags);
using StatListSetFn = void (*)(uint32_t dataFlag, void* list, int32_t stat, int32_t value, int32_t layer);

AuraFn g_d2rAura = nullptr;
AuraFn g_d2rBuff = nullptr;
AuraFn g_d2rStorm = nullptr;
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

// The caster's stat lists for aurastate and auratargetstate: each layered aurastat at its layer.
void RewriteLayers(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept {
	const mxl_auras::LayeredAura* aura = LayeredAuraOf(skillId);
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (aura == nullptr || skill == nullptr) {
		return;
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
}

auto RunThenLayer(AuraFn d2rFunction, d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept
	-> int64_t {
	if (game == nullptr || unit == nullptr || d2rFunction == nullptr) {
		return 0;
	}
	// D2R's aura (65) returns nothing; its buff (18) and storm (124) return whether they started
	const int64_t result = d2rFunction == g_d2rAura ? (d2rFunction(game, unit, skillId, level), 1) :
	                                                  d2rFunction(game, unit, skillId, level);
	if (result != 0) {
		RewriteLayers(game, unit, skillId, level);
	}
	return result;
}

}  // namespace

auto SrvDoAura(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	return RunThenLayer(g_d2rAura, game, unit, skillId, level);
}

auto SrvDoLayeredBuff(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	return RunThenLayer(g_d2rBuff, game, unit, skillId, level);
}

auto SrvDoLayeredStorm(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	return RunThenLayer(g_d2rStorm, game, unit, skillId, level);
}

void InstallAuras(uint64_t exeBase) noexcept {
	g_d2rAura = reinterpret_cast<AuraFn>(exeBase + D2RAuraRva);
	g_d2rBuff = reinterpret_cast<AuraFn>(exeBase + D2RBuffRva);
	g_d2rStorm = reinterpret_cast<AuraFn>(exeBase + D2RStormRva);
	g_statListByState = reinterpret_cast<StatListByStateFn>(exeBase + StatListByStateRva);
	g_statListSet = reinterpret_cast<StatListSetFn>(exeBase + StatListSetRva);
}

// Median XL's skill damage scaling: BaseSynergy and StaticDamage skills, physical mastery (skill_damage.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

#include "d2r.h"

auto InstallSkillDamage(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// One skill's damage as the port computes it and as D2R's own functions would (harness command skilldmg).
struct SkillDamageSample {
	int32_t flags;         // mxl_skill_flags: 1 BaseSynergy, 2 StaticDamage
	int32_t calcElem;      // the skill's elemental synergy percent column, evaluated for the caster (0 without one)
	int32_t calcPhys;      // ... and the physical one
	int32_t baseSynergy;   // BaseSynergyPercent for the caster (0 when the skill has no BaseSynergy)
	int32_t masteryElem;   // mastery stat of the skill's damage type (D2R 0x337f40)
	int32_t masteryMagic;  // stat 357, the physical mastery
	int32_t eleMin, eleMax, physMin, physMax;                  // through the hooks
	int32_t origEleMin, origEleMax, origPhysMin, origPhysMax;  // D2R's functions
};
auto SampleSkillDamage(d2r::Unit* unit, uint32_t dataFlag, int32_t skill, int32_t level, int32_t withMastery,
                       SkillDamageSample* out) noexcept -> bool;

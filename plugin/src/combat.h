// Median's avoid/dodge/evade and block limits (combat.cpp; docs/subsystems/combat-stats.md).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks D2R's avoid check 0x44ef70 (result 2 avoid, 4 dodge, 8 evade, 0x10 block) and thins out the successes a
// player gets above Median's caps (combat_logic.h), the absorb step 0x450670 (Median's credit stats) and, unless
// `damageReduction` is false (off word "damagered"), the per-hit step 0x4523e0 (Median's stats 184 / 189 / 417 on the damage
// left in each type entry). True when every hook asked for is in.
auto InstallCombat(const D2RL::PluginContext* context, bool damageReduction, char* report, size_t reportSize) noexcept -> bool;

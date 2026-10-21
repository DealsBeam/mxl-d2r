// Median's avoid/dodge/evade and block limits (combat.cpp; docs/subsystems/combat-stats.md).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks D2R's avoid check 0x44ef70 (result 2 avoid, 4 dodge, 8 evade, 0x10 block) and thins out the successes a
// player gets above Median's caps (combat_logic.h). True when hooked.
auto InstallCombat(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

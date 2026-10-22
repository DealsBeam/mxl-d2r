// Three small Median engine rules on D2R (engine_gaps.cpp, rules in engine_gaps_logic.h).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// hitFloor: missile hit 20 floors its damage calc at 0 (off word hitfloor). recast: srvdofunc 47 can be recast while its state
// runs (off word recast). slowFloor: players' slow floor by difficulty (off word slowfloor). Each flag false = left alone.
// Returns the number installed; the report line says which.
auto InstallEngineGaps(const D2RL::PluginContext* context, bool hitFloor, bool recast, bool slowFloor, char* report, size_t reportSize) noexcept -> int;

// Harness (`gaps`): the player slow floor as the hooked function returns it for `unit`, through the game's own entry.
auto EngineGapsSlowFloor(void* game, void* unit) noexcept -> int;

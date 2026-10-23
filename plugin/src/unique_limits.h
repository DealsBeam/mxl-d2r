// Median's unique carry limits (uniqueitems2) on D2R (unique_limits.cpp, rules in unique_limits_logic.h). Off word: uniquelimits.
#pragma once

#include <D2RLPlugin/api.h>

#include "unique_limits_logic.h"

#include <cstddef>

// Hooks D2R's item move (0x471e90, the funnel of the container packets): a limited unique does not enter the backpack when
// the limit is reached; the move fails the way a move without room does and the player gets Median's line. True when hooked.
auto InstallUniqueLimits(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Counters for the `mxl` command and the harness: false when the part is not installed.
auto UniqueLimitCounts(unique_limits::Counts* out) noexcept -> bool;

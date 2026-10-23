// Required level without the +6 oskill penalty (required_level.cpp; audit common1-013, Median / D2Sigma D2Common 0x26667).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Patches D2R's required-level function (D2RCore.dll, behind the executable's thunk 0x376de0) so a bonus to a skill of another
// class (the item property `oskill`, stat 97) no longer adds 6 to the item's required level. Off word `reqlevel`.
auto InstallRequiredLevel(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Harness: how many of the two `add 6` sites currently hold the patched bytes (0..2), -1 when D2RCore.dll is not patched.
auto RequiredLevelPatchedSites() noexcept -> int;

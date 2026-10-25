// Median's NPC healing and town portal lock on D2R (town_rules.cpp, rules in town_rules_logic.h).
// Off words: npcheal, tplock, mediantowns.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// healAll: every NPC table entry heals (writes byte +6 of the 43 entries of 0x237ea60). townPortalLock: hooks the town portal
// skill function (srvdofunc 109, 0x5822b0): in a level with Median's Tplock flag it fails the way the function itself does when
// there is no room for a portal. Each flag false = left alone. Returns the number of parts installed; the report says which.
auto InstallTownRules(const D2RL::PluginContext* context, bool healAll, bool townPortalLock, char* report, size_t reportSize) noexcept -> int;

// Number of town portal casts refused in a locked level since load (for the `mxl` command); -1 when the hook is not installed.
auto TownPortalRefusals() noexcept -> int;

// Replace D2R's five-id town classifier with Median's IsTown flags. Off word: mediantowns.
auto InstallMedianTowns(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

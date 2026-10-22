// Median XL's skill cooldowns on D2R: stat 309 shortens the real wait, skills of one cooldown group share one timer
// (cooldowns_logic.h, docs/sigma-patch-audit.md section 4).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks D2R's local cooldown start on the server (0x439500: game, unit, delay, skill) and on the client (0x217b90: unit, delay,
// skill; the client predicts the cooldown itself). For a player unit the delay becomes delay - stat 309 (layer = skill) and the
// cooldown starts for every skill of the used skill's group (mxl_cooldowns.h, from D2Sigma's skills2); a non-player unit
// (monster, pet, hireling) gets no cooldown at all, as in Median where those units are always ready. True when both hooks are
// installed. Off word: cooldowns.
auto InstallCooldowns(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Counters since the plugin loaded; false while the hooks are not installed. For the `mxl` command and the harness `state`.
auto CooldownCounts(long* starts, long* reduced, long* shared, long* dropped, long* monsters) noexcept -> bool;

// Skills that are in a cooldown group, and how many groups (the generated table of D2Sigma's skills2).
auto CooldownMemberCount() noexcept -> size_t;
auto CooldownGroupCount() noexcept -> size_t;

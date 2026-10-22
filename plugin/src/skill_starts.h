// D2Sigma's skill start functions D2R lacks (skill_starts.cpp, skill_starts_logic.h; docs/sigma-patch-audit.md item 20).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Fills the free slots 71 and 72 of D2R's server start-function table (RVA 0x238e710) with Median's srvstfunc 30 (timed state
// on the caster) and 66 (the same, refused while the state is on). Returns how many were installed (0-2); the report says which.
auto InstallSkillStarts(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;

// "slot 71 N casts, slot 72 M casts, refused R" (for the harness and the mxl line).
auto SkillStartStats(char* out, size_t outSize) noexcept -> void;

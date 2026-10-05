// D2Sigma's added skill server functions (sigma_skills.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Writes the functions into their empty srvdofunc slots. Call only on the expected game image. Returns how many
// were installed; report gets one "srvdofunc <slot> <name>[ FAILED]" entry per function.
auto InstallSigmaSkills(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto SigmaSkillCount() noexcept -> int;

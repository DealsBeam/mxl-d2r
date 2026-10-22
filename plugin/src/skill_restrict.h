// Median's skill restriction modes 3 to 7 (skill_restrict.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallSkillRestrict(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Level types above D2R's 36 (level_types.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallLevelTypeGuards(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

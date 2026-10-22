// The Fire Sorceress's Heat: mana regeneration with the heat drain of stat 297 (heat.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallHeat(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

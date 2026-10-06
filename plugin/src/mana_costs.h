// Mana costs with D2Sigma's formulas and mana cost % (mana_costs.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallManaCosts(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

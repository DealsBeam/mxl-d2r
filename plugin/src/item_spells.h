// D2Sigma's item spells: mystic orbs, dyes (item_spells.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallItemSpells(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

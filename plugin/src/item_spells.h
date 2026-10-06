// D2Sigma's item spells: mystic orbs, dyes (item_spells.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

auto InstallItemSpells(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

namespace d2r {
struct Game;
struct Unit;
}
// D2Sigma srvdofunc 154 -> slot 173: a portal to the Median level the skill's calc1 gives.
auto SrvDoLevelPortal(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

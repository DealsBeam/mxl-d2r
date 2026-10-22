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
// D2Sigma missile hit 30: a portal at the missile to Median level `median` (missile_hits.cpp).
auto HitLevelPortal(d2r::Game* game, d2r::Unit* owner, d2r::Unit* missile, int32_t median, bool once) noexcept -> bool;
// D2Sigma cube output byte 1: a red portal next to the player to Median level `median` (false when D2R cannot link it).
auto CubeLevelPortal(d2r::Game* game, d2r::Unit* player, int32_t median) noexcept -> bool;
auto SrvDoLevelPortal(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

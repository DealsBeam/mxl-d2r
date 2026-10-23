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
// The Cow Level recipe outside Rogue Encampment (D2R's handler only works there): 1 opened, 0 refused (once per game, Baal quest), -1 = not
// applicable (level 1 or no data): D2R's own handler runs. CowPortalCounts: portals opened outside act 1 / refusals there (of them: Baal quest not done, once-per-game flag set, portal not created).
auto CubeCowPortal(d2r::Game* game, d2r::Unit* player) noexcept -> int;
auto CowPortalCounts(uint32_t* outsideAct1, uint32_t* refused, uint32_t* noQuest, uint32_t* flagSet, uint32_t* noPortal) noexcept -> void;
auto SrvDoLevelPortal(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

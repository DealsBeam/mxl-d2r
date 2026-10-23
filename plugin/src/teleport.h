// D2Sigma's teleport, srvdofunc 27 with aurarangecalc -> slot 179 (teleport.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace d2r {
struct Game;
struct Unit;
}

void InstallTeleport(uint64_t exeBase) noexcept;

// Gold pickup after a player's teleport (audit GS2; off word `teleportgold`): the pickup of slot 179 is switched on and the
// table entry of slot 27 (D2R's teleport) is wrapped. `TeleportGoldStats`: teleports that looked, piles in reach, piles taken.
auto InstallTeleportGold(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;
void TeleportGoldStats(unsigned* teleports, unsigned* seen, unsigned* piles) noexcept;
auto SrvDoRangedTeleport(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

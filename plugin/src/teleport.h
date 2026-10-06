// D2Sigma's teleport, srvdofunc 27 with aurarangecalc -> slot 179 (teleport.cpp).
#pragma once

#include <cstdint>

namespace d2r {
struct Game;
struct Unit;
}

void InstallTeleport(uint64_t exeBase) noexcept;
auto SrvDoRangedTeleport(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

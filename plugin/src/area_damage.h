// D2Sigma's area damage, srvdofunc 171 -> slot 190 (area_damage.cpp).
#pragma once

#include <cstdint>

namespace d2r {
struct Game;
struct Unit;
}

void InstallAreaDamage(uint64_t exeBase) noexcept;
auto SrvDoAreaDamage(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

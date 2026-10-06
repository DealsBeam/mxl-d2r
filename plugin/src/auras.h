// D2Sigma's aura function, srvdofunc 165 -> slot 184 (auras.cpp).
#pragma once

#include <cstdint>

namespace d2r {
struct Game;
struct Unit;
}

void InstallAuras(uint64_t exeBase) noexcept;
auto SrvDoAura(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

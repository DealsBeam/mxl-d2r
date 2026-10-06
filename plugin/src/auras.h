// D2Sigma's aura function, srvdofunc 165 -> slot 184, and its layered self buff (18 -> 174) and storm (124 -> 175).
#pragma once

#include <cstdint>

namespace d2r {
struct Game;
struct Unit;
}

void InstallAuras(uint64_t exeBase) noexcept;
auto SrvDoAura(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;
auto SrvDoLayeredBuff(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;
auto SrvDoLayeredStorm(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

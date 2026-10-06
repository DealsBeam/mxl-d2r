// D2Sigma's minion scaling for multi-summon pets (pet_scaling.cpp).
#pragma once

#include <cstdint>

namespace d2r {
struct Game;
struct Unit;
}

void InstallPetScaling(uint64_t exeBase) noexcept;
auto MinionLifeCode(uint32_t dataFlag, d2r::Unit* owner, int32_t skillId, int32_t level) noexcept -> int32_t;
auto MinionAttackRatingCode(uint32_t dataFlag, d2r::Unit* owner, int32_t skillId, int32_t level) noexcept -> int32_t;
void ScalePet(d2r::Game* game, d2r::Unit* owner, d2r::Unit* pet, int32_t skillId, int32_t level) noexcept;

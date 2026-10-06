// D2Sigma's missile hit functions (missile_hits.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallMissileHits(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto MissileHitCount() noexcept -> int;

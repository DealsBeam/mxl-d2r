// D2Sigma's missile hit functions (missile_hits.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// clientDoStubs false (mxl-core-off.txt word "cltdostub"): leave D2R's empty client do slots empty (bug #24b diagnosis)
auto InstallMissileHits(const D2RL::PluginContext* context, char* report, size_t reportSize, bool clientDoStubs = true) noexcept -> int;
auto MissileHitCount() noexcept -> int;

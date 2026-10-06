// D2Sigma's skill calc codes (calc_codes.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallCalcCodes(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// D2Sigma's skill calc codes (calc_codes.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallCalcCodes(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Diagnosis (bug #21): itemstat and tabpoints answer 0, as before mxl-core 0.31.0.
void SetItemFunctionsOff(bool off) noexcept;

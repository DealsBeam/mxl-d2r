// Diagnosis trace of unit states (state_trace.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallStateTrace(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

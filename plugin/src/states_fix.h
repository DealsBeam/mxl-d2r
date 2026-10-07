// Room for more than 512 states (states_fix.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Patches the game image (all or nothing). Call only on the expected image, before any game unit exists.
auto InstallStatesFix(const D2RL::PluginContext* context, bool keepCurrent, char* report, size_t reportSize) noexcept -> bool;

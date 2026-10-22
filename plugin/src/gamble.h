// Median's gamble window offers on D2R (gamble.cpp, rules in gamble_logic.h). Off word: gamble.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Raises the loop bound of the gamble offer generator 0x541880 from 14 to 32 offers. True when patched.
auto InstallGamble(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

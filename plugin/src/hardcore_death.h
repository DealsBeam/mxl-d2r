// A dead Hardcore character becomes Softcore in Single Player (hardcore_death.cpp; audit DC9 / HC1, Median 1.6.0).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks D2R's player death handler 0x42a5e0. Off word `hcdeath`.
auto InstallHardcoreDeath(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Conversions so far (the `mxl` line and the harness): deaths seen of Hardcore characters, and how many were converted.
void HardcoreDeathStats(unsigned* hardcoreDeaths, unsigned* converted) noexcept;

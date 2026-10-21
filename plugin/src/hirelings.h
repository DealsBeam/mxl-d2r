// Median's mercenary class bonuses and hire gate (hirelings.cpp; docs/subsystems/npc-hirelings.md section 2).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks D2R's mercenary stat setup 0x544f60 (run on hire, load and level-up) to set D2Sigma's class bonuses afterwards,
// and clears Kashya's level-8 hire alternative so that, as in Median, only the quest opens her hire. True when hooked.
auto InstallHirelings(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

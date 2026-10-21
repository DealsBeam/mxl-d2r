// Median's Act 1 quest changes (quests_act1.cpp; docs/subsystems/quests-requirements.md section 2.2).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks three quest handlers (a1q4 event 11, a1q6 events 8 and 0) and the item generator's request field. True when
// all four installed.
auto InstallQuestsAct1(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

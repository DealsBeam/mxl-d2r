// Median's quest changes of Acts 2-5 (quests_act2_5.cpp; docs/subsystems/quests-requirements.md section 5c).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks nine quest handlers (a2q5 events 8 and 0, a2q6, a3q3, a3q5, a4q1 events 8 and 0, a5q2, a5q6) and the respec
// bookkeeping 0x5d9ae0. True when all ten installed. clearBuffs (off word "respecbuffs") makes the respec remove the player's
// timed buffs (audit DC7).
auto InstallQuestsAct2To5(const D2RL::PluginContext* context, bool clearBuffs, char* report, size_t reportSize) noexcept -> bool;

// Median's Act 1 quest changes (quests_act1.cpp; docs/subsystems/quests-requirements.md section 2.2).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks four quest handlers (a1q4 event 11, a1q6 events 8 and 0, a1q5 event 8) and the item generator's request field.
// True when all five installed.
auto InstallQuestsAct1(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Test harness: the next item generated on this thread becomes the unique with this index + 1 (0 clears it).
void QuestsWantUnique(int indexPlusOne) noexcept;

// Kills of the Countess for which the vanilla handler was skipped because the killer had finished quest 5.
auto QuestsTowerSkips() noexcept -> int;

// Test harness (`towerprobe`): whether the quest 5 hook would skip the vanilla handler for this kill (no counting, nothing
// run). A unit the harness spawned is not on quest 5's unit list, so the hook itself only sees the natural Countess.
auto QuestsTowerProbe(void* game, void* victim, void* player) noexcept -> bool;

// D2Sigma's item event 14, "the unit dies", raised on the victim of every death (death_event.cpp, docs/plugin-spec.md
// "Death event 14").
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks D2R's unit death function 0x44db20: before it runs, the victim's item events of stat 271 (Median's cast on death, 248
// monprop rows such as the Night Hawks' explosion) are raised. Off word: deathevent.
auto InstallDeathEvent(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Deaths seen, events raised and the event id in use (-1 until the first death read it); returns whether the hook is installed.
// For the `mxl` command and the harness `state`.
auto DeathEventCounts(long* deaths, long* raised, long* eventId) noexcept -> bool;

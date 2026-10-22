// Median's travel conveniences on D2R: waypoint auto-activation, the map reveal hotkey, the X / R / C hotkeys.
// travel_logic.h has the rules and the evidence (Median's D2AutoPickup.cpp, hotkey table, stash page text); travel.cpp the game side.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Off words (mxl-core-off.txt): wpauto, mapreveal, hotkeys. A part that is left out is not installed; `report` says which are on.
// Waypoint auto-activation needs the server frame (goblins.cpp calls TravelGameFrame from its hook of 0x406550), the hotkeys need
// D2RLoader's InputService and ThreadService.
auto InstallTravel(const D2RL::PluginContext* context, bool wpauto, bool mapreveal, bool hotkeys, char* report, size_t reportSize) noexcept -> bool;

// Game thread, after the server frame (every frame; the scan runs every fifth).
void TravelGameFrame(void* game, uint32_t frame) noexcept;

// UI thread: the R hotkey's work without the key (harness `reveal`). -1 = off or no local player, 0 = done.
auto TravelRevealNow() noexcept -> int;

// "<n> waypoints activated, ..." for the mxl console command.
void TravelStatus(char* out, size_t outSize) noexcept;

// Harness (harness_cmds.cpp, docs/testing.md "Harness commands").
auto TravelAutomap(uint64_t* cells, int* layer, int* revealFlag, int* layers) noexcept -> bool;  // client thread; false = no automap yet
void TravelHotkeyReport(char* out, size_t outSize) noexcept;                                      // bindings and counters
void TravelUiMessages(char* out, size_t outSize) noexcept;                                         // distinct UI messages seen (target:command)
auto TravelStashProbe(int page, char* out, size_t outSize) noexcept -> int;                       // 1 queued, 0 not possible; result by TravelStashResult
auto TravelStashResult(char* out, size_t outSize) noexcept -> int;                                // 1 done ok, 0 done failed, -1 pending, -2 never queued

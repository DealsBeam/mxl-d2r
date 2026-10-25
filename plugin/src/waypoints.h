// Waypoint numbers 39..111 and the waypoint panel (waypoints.cpp; docs/subsystems/areas-drlg.md "Waypoints").
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Check, writes nothing: the engine's 112-entry waypoint table is the layout the save and Median's numbers (up to 90) rely on, and
// the entry bytes of the record functions 0x3d6670 / 0x3d6890 are the ones analysed. Writes a one-line summary with the number
// of levels the waypoint panel (actinfo.txt, data) has no button for. Hooks and writes nothing.
// True when the table is as expected and Median's numbers fit it.
auto VerifyWaypoints(uint64_t exeBase, char* report, size_t reportSize) noexcept -> bool;

// The waypoint panel: lists every waypoint level of mxl_waypoints.h under its own act, more than the nine slots of an actinfo.txt
// row (hooks the tab fill 0x1ea9b0 and points the button table's three readers at a longer table; see waypoints.cpp). Everything
// is verified first; on any difference nothing changes and the engine's panel (actinfo.txt) stays. True when installed.
auto InstallWaypointPanel(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// "panel on, N tab fills, last tab T with B buttons" (for the harness).
auto WaypointPanelStats(char* out, size_t outSize) noexcept -> void;

// The plugin's button table (wp::ButtonEntry[count]); false while the panel patch is not installed.
auto WaypointPanelTable(int* count, const void** entries) noexcept -> bool;

// Select the waypoint object rather than the default entry in Median preset areas.
auto InstallWaypointArrival(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

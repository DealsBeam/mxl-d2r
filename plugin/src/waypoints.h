// Waypoint numbers 39..111 and the waypoint panel (waypoints.cpp; docs/subsystems/areas-drlg.md "Waypoints").
#pragma once

#include <cstddef>
#include <cstdint>

// Read-only check: the engine's 112-entry waypoint table is the layout the save and Median's numbers (up to 90) rely on, and
// the entry bytes of the record functions 0x3d6670 / 0x3d6890 are the ones analysed. Writes a one-line summary with the number
// of levels the waypoint panel (actinfo.txt, data) has no button for. Hooks and writes nothing.
// True when the table is as expected and Median's numbers fit it.
auto VerifyWaypoints(uint64_t exeBase, char* report, size_t reportSize) noexcept -> bool;

// Missile CollideType 9 and 10 (collide_types.cpp, collide_types_logic.h; docs/sigma-patch-audit.md item 14).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Copies D2R's two collide-type tables (server RVA 0x2391370, client 0x23589d0; nine 16-byte rows each) into 256-row tables
// with Median's rows 9 and 10 added, and points the nine `lea reg, [rip + table]` readers at them. Everything is verified first
// (the rows, the nine instructions); on any difference nothing changes. True when installed.
auto InstallCollideTypes(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// The row of `type` (0-255) the engine reads now: check function RVA (0 = none) and mask. False when the patch is not installed.
auto CollideTypeRow(bool server, int type, uint64_t* fnRva, uint32_t* mask) noexcept -> bool;

// Table loads seen by the data-table listener and records it wrote back (type above 8 restored from the clamp).
auto CollideRecordStats(long* loads, long* restored) noexcept -> void;

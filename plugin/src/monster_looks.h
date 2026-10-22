// Median's monster looks on D2R: name colour by rank, automap marker and name of flagged monsters, spawn overlays.
// monster_looks_logic.h has the rules, monster_looks.cpp the hooks.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Hooks the hover name builder (0xcea50) and its colour conversion (0x908690), the automap unit classifier (0xd78f0), the
// automap unit drawing (0xd76e0) and its name drawing (0xd6b20), and the monster creation (0x493cc0). Each part reports
// separately; true when at least one is in place.
auto InstallMonsterLooks(const D2RL::PluginContext* context, bool nameColours, bool automap, bool spawnOverlays, char* report,
                         size_t reportSize) noexcept -> bool;

// "<n> names recoloured, ..." for the mxl console command.
void MonsterLooksStatus(char* out, size_t outSize) noexcept;

// The unit the hover name builder last ran for (harness verb `hover`): id, unit type, class, and the milliseconds since (all ones: never).
auto MonsterLooksHovered(uint32_t* id, uint32_t* type, uint32_t* cls) noexcept -> uint64_t;

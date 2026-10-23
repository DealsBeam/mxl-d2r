// Median's dungeon lockout and level entry requirements on D2R (dungeon_lockout.cpp, rules in dungeon_logic.h).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks the tile / stair warp action 0x491ff0 and the portal object's operate function 0x58f680. True when both
// installed. The report line says what is on.
auto InstallDungeonLockout(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Counters for the `mxl` console command (this session): refused entries, clocks started.
void DungeonLockoutCounts(int* refused, int* started) noexcept;

// Portals into another act's level taken through the NPC travel function 0x4f2e40 this session (the Cow Level portal made in
// another act's town, and the way back out of it).
auto DungeonActTravels() noexcept -> int;

// Test harness (`lockcheck <level>`): what the warp test says about the player entering `dst`, without sending anything or
// counting: 0 allowed, 1 requirements, 2 quest-table condition, 3 sealed, -1 no row for the level / no player.
auto DungeonLockoutProbe(void* game, void* player, int dst) noexcept -> int;

// An item teleport (the cherries, item_spells.cpp; D2Sigma 0x1007fb70 -> 0x1005d9d0): the player has arrived from level `from`; starts
// the destination's clock like a stair or portal entry does. Nothing happens when the lockout is off.
void DungeonLockoutItemEntered(void* game, void* player, int from) noexcept;

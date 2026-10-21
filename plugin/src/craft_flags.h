// Median's cube output effects: pay gold (byte 6), set a player flag (7) or a game flag (9); craft_flags.cpp.
// docs/plugin-spec.md, "Cube conditions". Flags live in memory only, as in Median.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Detours the recipe executor 0x5269c0; after it made an output, applies the recipe's effects (mxl_cube_effects.h).
auto InstallCraftFlags(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// For cube_conditions.cpp (ops 38-41). Touch is called with the game of every recipe search.
void CraftTouchGame(const void* game) noexcept;
auto CraftFlagTest(const void* player, int flag, bool gameWide) noexcept -> bool;
// Sets or clears one flag (player flags are per player id; game flags per game).
void SetCraftFlag(const void* player, int flag, bool game, bool on) noexcept;
// Binds the unit-id getter and stat functions (cheap, no hooks); cube_conditions calls it from its installer.
void CraftBind(uint64_t exeBase) noexcept;
// The game with this pointer ended: its flags are dropped (registered with game_end.cpp by InstallCraftFlags).
void CraftForgetGame(const void* game) noexcept;

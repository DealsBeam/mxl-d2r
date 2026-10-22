// Game-end event for the per-game state of the plugin's modules (game_end.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// A callback gets the Game* of the game that is being destroyed (before its clients are freed). Modules register theirs
// from their installer; registering twice is harmless. At most 8.
using GameEndFn = void (*)(const void* game);
void RegisterGameEnd(GameEndFn fn) noexcept;

// Hooks the game's client-list teardown 0x486be0 (called only by the game destroy 0x4057c0, which has a rip-relative
// prologue and cannot be detoured). True when installed.
auto InstallGameEnd(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Games destroyed since the plugin loaded, for the `mxl` command.
auto GameEndCount() noexcept -> int;

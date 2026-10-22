// Trophy challenge windows: the stat 202 code is only good for the trophy's time limit, in the area it was earned, and
// until the player dies (trophy_timer_logic.h, docs/plugin-spec.md "Trophy timer").
#pragma once

#include <D2RLPlugin/api.h>

#include "d2r.h"

#include <cstddef>
#include <cstdint>

// Nothing is hooked here: the game-frame hook (goblins.cpp) calls TrophyTimerFrame and the cube conditions
// (cube_conditions.cpp) ask TrophyWindowOpen. The report line says whether the part is on.
auto InstallTrophyTimer(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Once per server frame (game thread): samples every player's challenge code every few frames.
void TrophyTimerFrame(d2r::Game* game) noexcept;

// May a cube recipe on challenge code `code` match for `player` now? True when the part is off.
auto TrophyWindowOpen(d2r::Game* game, d2r::Unit* player, int32_t code) noexcept -> bool;

// "<opened> windows opened, <voided> voided, <refused> cube checks refused" for the mxl console command.
void TrophyTimerStatus(char* out, size_t outSize) noexcept;

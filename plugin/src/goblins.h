// Median's treasure goblins (D2Sigma D2Goblins.cpp) on D2R: a 15 s per-player roll that may spawn a goblin next to
// a fighting player. docs/subsystems/ubers-events.md section 4; goblins.cpp.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks the server game frame (0x406550, the tick) and the kill wrapper (0x515640, the last-kill stamp).
// Drops are data (monstats treasure classes "Goblin", "Goblin (H)", ...), nothing is hooked for them.
auto InstallGoblins(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Sends the "<variant name> appears!" line for goblin class `monster` (D2R monstats id) to `player` and returns the text in `out`
// (harness verb goblinsay). Game (server) thread.
auto GoblinAnnounce(void* player, int monster, char* out, size_t outSize) noexcept -> bool;

// "<spawned> goblins spawned, <rolls> rolls, ..." for the mxl console command.
void GoblinsStatus(char* out, size_t outSize) noexcept;

// No corpse on player death and a 5 second respawn wait, as in Median XL (no_corpse_logic.h, docs/subsystems/death.md).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Replaces D2R's corpse creator 0x42c110 and death penalty 0x424ac0 by functions that return at once. True when both
// hooks are installed (report says which). Off word: nocorpse.
auto InstallNoCorpse(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Holds the respawn request (client packet 0x41, handler 0x4b6160) back until 5 s after the death. Needs the death stamps
// of InstallNoCorpse's hooks. Off word: respawnwait.
auto InstallRespawnWait(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Calls swallowed since the plugin loaded (corpse creator, death penalty) and respawn requests held back / let through;
// returns whether the no-corpse hooks are installed. For the `mxl` command and the harness `state`.
auto NoCorpseCounts(long* corpses, long* penalties, long* respawnsHeld = nullptr, long* respawns = nullptr) noexcept -> bool;

// No corpse on player death and a 5 second respawn wait, as in Median XL (no_corpse_logic.h, docs/subsystems/death.md).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Remembers where the game image is, for the harness readers (StoredCorpseIds) when the parts are left out.
void NoCorpseRememberImage(const D2RL::PluginContext* context) noexcept;

// Replaces D2R's corpse creator 0x42c110 and death penalty 0x424ac0 by functions that return at once. True when both
// hooks are installed (report says which). Off word: nocorpse.
auto InstallNoCorpse(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Soulstealer bosses (the monsters of mxl_soulstealer.h) heal 1/10 of their max life when they kill a player (audit DC5): the
// death penalty hook of InstallNoCorpse calls it with the killer. Needs those hooks. Off word: soulstealer.
auto InstallSoulstealer(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Heals done since the plugin loaded; returns whether the part is on. For the `mxl` command and the harness `state`.
auto SoulstealerHeals(long* heals, long* deaths, long* lastType, long* lastClass, long* lastLife, long* lastMax) noexcept -> bool;

// Holds the respawn request (client packet 0x41, handler 0x4b6160) back until 5 s after the death. Needs the death stamps
// of InstallNoCorpse's hooks. Off word: respawnwait.
auto InstallRespawnWait(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Calls swallowed since the plugin loaded (corpse creator, death penalty) and respawn requests held back / let through;
// returns whether the no-corpse hooks are installed. For the `mxl` command and the harness `state`.
auto NoCorpseCounts(long* corpses, long* penalties, long* respawnsHeld = nullptr, long* respawns = nullptr) noexcept -> bool;

// Picks up a corpse that is already stored in a character's save (made by D2R's own death before `nocorpse`) when the
// character enters a level: D2R's own pickup (the one a click on the body runs) puts the items back and the body is removed
// from the room, the clients and the character's corpse list. Hook on the level-entry function 0x485350, which is also the
// function that places the stored corpse. Needs the `nocorpse` hooks (with the corpse creator on, bodies are D2R's again).
// Off word: corpsepickup.
auto InstallCorpsePickup(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Pickups since the plugin loaded; returns whether the hook is installed. For the `mxl` command and the harness `state`.
auto CorpsePickupCounts(long* enters, long* picked, long* kept, long* faults) noexcept -> bool;

// Ids of the corpses (list entries with flag 1) in a player unit's corpse list, at most `max`; the number found.
// Harness `state` / `corpses`: after the pickup the list must be empty. Server unit only.
auto StoredCorpseIds(void* player, unsigned* ids, int max) noexcept -> int;

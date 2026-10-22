// Median's monster level, players-count scaling, treasure-class level step, summon leash and Baal throne delay
// (monster_rules.cpp; docs/sigma-patch-audit.md items 17-19, docs/subsystems/monsters-ai.md).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Each Install* checks the bytes it changes against the build's dump, changes all or none of its sites, and writes one report
// line. True when the part is on.

// monlevel: Normal monsters (and mercenary classes) take the area level too, and the monstats flag 0x40 no longer opts out.
auto InstallMonsterLevel(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// playersx: hooks the monster setup 0x543e90 so the players-count record carries Median's life percentage and no experience
// percentage.
auto InstallPlayersTable(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// normaltc: the treasure-class executor 0x441300 steps a class up by the monster's level in Normal too. Also traces the level
// it hands the class getter 0x397290 (NormalTcTrace).
auto InstallNormalTc(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// leash: owned units warp to their owner past 40 sub-tiles (six compares).
auto InstallLeash(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// throne: Baal's throne AI waits 125 frames instead of 250 before the first wave.
auto InstallThrone(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

struct PlayersTrace {
	int32_t records;  // monster setups seen since start
	int32_t players;  // the last record's players count
	int32_t hpIn, expIn;    // the percentages D2R handed in
	int32_t hpOut, expOut;  // what the hook left in the record
};
auto PlayersTraceLast() noexcept -> PlayersTrace;

struct TcTrace {
	int32_t calls;    // calls of the class getter 0x397290 since the last reset
	int32_t leveled;  // of them with a level above 0
	int32_t lastClass;
	int32_t lastLevel;
};
// The class getter calls since start (or since `reset`).
auto NormalTcTrace(bool reset) noexcept -> TcTrace;

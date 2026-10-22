// Per-level map memory for Levels.txt layers above 99 (map_memory.cpp, map_memory_logic.h).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

auto InstallMapMemory(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// For the test harness (client thread): the automap layer of the current level and what the character's map file holds for it.
struct MapMemoryInfo {
	bool haveLayer = false;      // an automap layer record is current
	int32_t layer = 0;
	int bucket = 0;
	uint64_t cells[4] = {};      // the four room sets of the record (sizes)
	bool haveFile = false;       // the map file was read (needs a level switch since the hooks went in)
	uint32_t fileSize = 0;
	int nodes = 0;               // nodes of this layer in its chain
	int foreign = 0;             // nodes of other layers sharing the chain
	bool clean = true;
	long loads = 0, saves = 0, views = 0, resets = 0;  // hook counters
};
auto MapMemoryProbe(MapMemoryInfo* info) noexcept -> bool;

// Reading every layer of a stat on a unit (stat_list.cpp).
#pragma once

#include <cstdint>

namespace d2r {
struct Unit;
}

struct StatEntry {
	uint16_t layer;
	uint16_t stat;
	int32_t value;
};

void InstallStatList(uint64_t exeBase) noexcept;
// The unit's stats with ids in `stats` (all layers, totals as the stat getter returns them), up to `max` entries.
// Returns how many were written; 0 also when the stat array could not be found safely.
auto UnitStatsById(d2r::Unit* unit, const int32_t* stats, int statCount, StatEntry* out, int max) noexcept -> int;

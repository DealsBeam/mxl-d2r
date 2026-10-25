// Unique charms and other carry1 uniques: one copy of each, not one of all (unique_carry.cpp, unique_carry_logic.h). Off word: uniquecarry (the off list keeps letters only, so no digits).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Registers a listener for D2R's data-table load; after every load each unique record with a carry1 number gets its own number
// (the row + 1), so that D2R's carry test compares unique with unique. True when the listener is registered.
auto InstallUniqueCarry(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Table loads seen, records rewritten and carry1 records of the last load, for the `mxl` command: false when the part is not installed.
auto UniqueCarryStats(long* loads, long* rewritten, long* carry) noexcept -> bool;

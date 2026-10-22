// Level types above D2R's 36 (level_types.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

auto InstallLevelTypeGuards(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Harness verb `automap` (docs/testing.md): the hooked lookup called by hand, a Median type's row range and a row of the live table,
// and how many real lookups the game made per level type.
auto LevelTypeProbe(int32_t levelType, int32_t tileType, uint32_t style, int32_t sequence) noexcept -> uint64_t;
auto LevelTypeRows(int32_t levelType, int32_t* first, int32_t* end) noexcept -> bool;
auto LevelTypeRowTable(int32_t index, int32_t* tileType, uint32_t* style, int32_t* startSeq, int32_t* celCount, uint32_t* cel0) noexcept -> bool;
void LevelTypeCounters(int32_t levelType, uint32_t* calls, uint32_t* hits) noexcept;
// The first distinct (tile type, style) pairs of the game's lookups that found no Median row (diagnosis of a type without hits).
auto LevelTypeMiss(int32_t levelType, int index, int32_t* tileType, int32_t* style) noexcept -> bool;

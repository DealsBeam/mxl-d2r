// Median's item and experience rules on D2R's functions (items_ext.cpp; docs/subsystems/items-xp.md).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks, each switchable in mxl-core-off.txt: "runewords" (properties 8-13 of Median's runewords, D2R's record holds
// seven: the function that applies a runeword's properties, 0x3d44c0; and Median's Jewel padding in the matcher
// 0x372260) and "xp" (Median's kill-experience penalty for the
// level gap between player and monster instead of 1.13c's tables, 0x44fef0) and "partyxp" (Median's party award: the whole experience for every member in range, which is 179 instead of 80
// units, in place of 1.13c's pool-and-split; the party iterator 0x506f90 and the range constant 0x44fe7b). True when every hook asked for installed.
auto InstallItemsExt(const D2RL::PluginContext* context, bool runewords, bool xp, bool party, char* report, size_t reportSize) noexcept
	-> bool;

// Counters for the "mxl" console line: runewords completed with extra properties / refused because the live record did
// not match the generated table, kills whose experience was changed / all kills seen, runewords found only through the
// Jewel padding.
void ItemsExtCounts(int* extended, int* refused, int* xpChanged, int* xpSeen, int* padded) noexcept;

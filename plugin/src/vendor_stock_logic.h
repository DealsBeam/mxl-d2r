// Median's vendor stock rules on D2R as pure data (vendor_stock.cpp patches the game; tests/vendor_stock_test.cpp).
//
// Median's stock builder (D2Sigma D2NPC 0x10093a30) and D2R's (0x53c9f0) have the same shape: the item level is the player level
// + 5 capped by the act (cap function 0x53cfa0), every item needs its base level <= that level, normal copies are made only while
// the level is below 25 (both `cmp r13d, 0x19` / `cmp ebp, 0x19`), magic copies are rolled in [MagicMin, MagicMax] (3 at most in
// the data). D2R adds, from item level 25 on, one or two extra magic copies of every stocked item (the compare at 0x53cd2b); that
// rule has no counterpart in Median's builder, so the compare is moved out of reach.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace vendor_stock {

inline constexpr uint64_t CapRva = 0x53cfa0;  // int cap(game, npc, ilvl): lowers the item level by the act's limit (Normal only)
inline constexpr uint8_t CapEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20};

// 41 83 fd 19 / 7c 13: cmp r13d, 0x19 / jl 0x53cd44 (0x53cd2b); the immediate is at 0x53cd2e. At or above it the count of magic
// copies of the item becomes 1 + rand(1, 2) instead of 1.
inline constexpr uint64_t ExtraCopiesRva = 0x53cd2b;
inline constexpr uint8_t ExtraCopiesOriginal[] = {0x41, 0x83, 0xfd, 0x19, 0x7c, 0x13};
inline constexpr uint8_t ExtraCopiesPatched[] = {0x41, 0x83, 0xfd, 0x7f, 0x7c, 0x13};
static_assert(sizeof(ExtraCopiesOriginal) == sizeof(ExtraCopiesPatched));

inline constexpr int NormalCopiesBelow = 25;  // Median and D2R agree (0x53caec)

// The item level cap. D2R applies actinfo `maxnpcitemlevel` (by the NPC's act, the byte at +10 of its monstats-like record) in Normal
// only; txt_migrate writes Median's Normal values there (MonLvl1Ex + 5 of the act's last waypoint area: 20 31 40 43 53). Median caps
// Nightmare too, at MonLvl2Ex + 5 of the same areas (levels.txt rows 35 46 101 107 129: 63 75 83 88 97); Hell is not capped.
inline constexpr int Acts = 5;
inline constexpr int NightmareCap[Acts] = {68, 80, 88, 93, 102};
inline constexpr int DifficultyNightmare = 1;

constexpr auto NightmareLevel(int act, int ilvl) noexcept -> int {
	return act >= 0 && act < Acts && NightmareCap[act] < ilvl ? NightmareCap[act] : ilvl;
}

}  // namespace vendor_stock

// Two Median town rules on D2R (town_rules.cpp): NPC healing and the town portal lock (docs/unported.md).
#pragma once

#include "dungeon_logic.h"

#include <cstdint>

namespace town {

// D2R's NPC packet handler (0x4c2410) calls the heal function 0x53fc50 on every talk; the function heals only when the NPC table
// entry's byte +6 is set, which D2R has for the act healers (Akara, Fara, Ormus, Malah, ...) alone. Median heals at every NPC
// that has a dialog (D2Sigma's NPC talk handler); so every entry gets the flag.
inline constexpr int HealFlagOffset = 6;

inline auto NeedsHealFlag(uint8_t current) noexcept -> bool {
	return current == 0;
}

// Median's levels.txt column Tplock (levels.bin +0x155): the town portal skill does not open a portal in these levels.
inline auto TownPortalLocked(int32_t level) noexcept -> bool {
	const dungeon::Entry* e = level > 0 ? dungeon::Find(level) : nullptr;
	return e != nullptr && e->unk155 != 0;
}

}  // namespace town

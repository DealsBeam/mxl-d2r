// Pure rules of Median's monster looks: the name colour by rank (D2MonDesc.cpp FUN_1008b570), the automap marker and name
// of flagged monsters (D2Automap.cpp FUN_1004b620) and the overlay that plays when a monster spawns (D2Monsters.cpp
// FUN_1008c3f0). The data is monstats3 (mxl_monster_looks.h); no engine access, so mxl_tests can run it.
#pragma once

#include "mxl_monster_looks.h"

#include <cstdint>

namespace looks {

// Monster-data type flags (unit +0x10 -> u16 at +0x1a, D2R and 1.13 alike).
constexpr uint16_t TypeSuperUnique = 0x02, TypeChampion = 0x04, TypeUnique = 0x08;

// The row of a monster (D2R id) or null: Looks is sorted by id.
inline auto Find(int32_t monster) noexcept -> const mxl_looks::Look* {
	int32_t lo = 0, hi = mxl_looks::LookCount;
	while (lo < hi) {
		const int32_t mid = lo + (hi - lo) / 2;
		if (mxl_looks::Looks[mid].monster < monster) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return lo < mxl_looks::LookCount && mxl_looks::Looks[lo].monster == monster ? &mxl_looks::Looks[lo] : nullptr;
}

// FUN_1008b570: the colour index of a monster's name. A unique that is not a champion and a super unique are 4; a champion
// is 9; any other monster takes the first rank bit it has (Elite 9, Veteran 12, Uber 8, Heroic 2, Boss 4) and else the
// row's NameColor (0 = none).
constexpr auto NameColour(const mxl_looks::Look& look, uint16_t type) noexcept -> int {
	const bool champion = (type & TypeChampion) != 0;
	if (((type & TypeUnique) != 0 && !champion) || (type & TypeSuperUnique) != 0) {
		return 4;
	}
	if (champion) {
		return 9;
	}
	if (look.flags & mxl_looks::RankElite) {
		return 9;
	}
	if (look.flags & mxl_looks::RankVeteran) {
		return 12;
	}
	if (look.flags & mxl_looks::RankUber) {
		return 8;
	}
	if (look.flags & mxl_looks::RankHeroic) {
		return 2;
	}
	if (look.flags & mxl_looks::RankBoss) {
		return 4;
	}
	return look.nameColor;
}

// D2R builds a monster's hover name with its own colour: 0 white, 3 champion, 4 unique, 1 a corpse, 0x1d a possessed
// monster (monster-data flag 0x200). Median's colour takes over from 0, 3 and 4; the others keep D2R's, and Median's 0
// (no colour) leaves D2R's alone.
constexpr auto MergeNameColour(int d2r, int median) noexcept -> int {
	if (median == 0 || (d2r != 0 && d2r != 3 && d2r != 4)) {
		return d2r;
	}
	return median;
}

// D2R's automap names a monster in this colour for monsters it marks itself (town NPCs, the player's pets).
constexpr int DefaultAutomapNameColour = 4;

// Monster modes that do not show on the automap: death (0) and dead (12).
constexpr auto MonsterGone(int32_t mode) noexcept -> bool {
	return mode == 0 || mode == 12;
}

// FUN_1004b620: a monster with the Automap bit gets a marker (and, with AutomapShowName, its name in AutomapNameColor)
// while it lives.
constexpr auto AutomapMarked(const mxl_looks::Look& look, int32_t mode) noexcept -> bool {
	return (look.flags & mxl_looks::FlagAutomap) != 0 && !MonsterGone(mode);
}
constexpr auto AutomapNamed(const mxl_looks::Look& look) noexcept -> bool {
	return (look.flags & mxl_looks::FlagAutomapName) != 0;
}

// The two overlays played on a monster that has just spawned: a unique or super unique plays BossOverlay1/2, any other
// monster Overlay1/2. -1 = none.
struct Overlays {
	int32_t first;
	int32_t second;
};
constexpr auto SpawnOverlays(const mxl_looks::Look& look, uint16_t type) noexcept -> Overlays {
	if ((type & (TypeSuperUnique | TypeUnique)) != 0) {
		return {look.overlay[2], look.overlay[3]};
	}
	return {look.overlay[0], look.overlay[1]};
}

}  // namespace looks

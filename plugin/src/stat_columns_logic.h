// Median's character sheet page 2: the Strength / Dexterity damage bonus rows and the total / % bonus / base columns
// (pure rules; stat_columns.cpp feeds them to D2R's Advanced Stats panel, docs/subsystems/stat-screen.md).
//
// D2Sigma ..\ui\StatScreen.cpp draws a type 0x17 row with the result of D2Damage.cpp 0x1005c290: for the unit's wielded
// weapon, `stat * bonus / 100` with the stat the unit's total Strength (stat 0) or Dexterity (stat 2) and the bonus the
// weapon record's StrBonus / DexBonus (u16 at +0x106 / +0x108); a unit without a weapon gets 0 (the function asserts
// and returns 0, 0). It is the term D2R's damage code (0x44d710) adds to the physical damage percent. The three
// columns of the attribute rows are the total (GetStat), the "% bonus" stat (to_strength ...) and the base (GetBaseStat).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace stat_cols {

// stat * weaponBonus / 100, truncated toward zero like the 64-bit divide of D2Sigma 0x10088e50; 0 for a zero operand.
constexpr auto DamageBonus(int32_t stat, int32_t weaponBonus) noexcept -> int32_t {
	if (stat == 0 || weaponBonus == 0) {
		return 0;
	}
	return static_cast<int32_t>(static_cast<int64_t>(stat) * weaponBonus / 100);
}

// A stat as the list shows it: the raw value shifted right by the record's ValShift (life, mana, stamina are stored
// times 256). The shift is masked to 5 bits like the game's `shr`.
constexpr auto Shifted(int32_t raw, uint32_t shift) noexcept -> int32_t {
	return raw >> (shift & 0x1f);
}

// The part appended to an attribute row: " (+20% bonus, 190 base)" (no bonus column: " (190 base)"). Returns the
// length written (snprintf's), the text is empty when the buffer is too small for one character.
inline auto ColumnsText(char* out, size_t size, bool hasBonus, int32_t bonusPercent, int32_t base) noexcept -> int {
	if (size == 0) {
		return 0;
	}
	if (hasBonus) {
		return std::snprintf(out, size, " (%+d%% bonus, %d base)", bonusPercent, base);
	}
	return std::snprintf(out, size, " (%d base)", base);
}

}  // namespace stat_cols

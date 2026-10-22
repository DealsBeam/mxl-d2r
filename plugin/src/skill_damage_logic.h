// Median XL's skill damage arithmetic, without any game access (docs/plugin-spec.md, "Skill damage scaling").
//
// D2Sigma replaces the four D2Common skill damage functions (D2SkillCalc 0x100a5d00 / 0x100a5de0 elemental max /
// min, 0x100a5f40 / 0x100a60a0 physical max / min, addresses of the 32-bit DLL's code). They are D2's own
//   (per-level steps + base) << shift, then + that * (DmgSymPerCalc) / 100, then + that * mastery / 100
// with three Median changes:
//   1. BaseSynergy skills (skills2 byte +0x2d) add BaseSynergyPercent() to the calc percent.
//   2. StaticDamage skills (skills2 byte +0x2e) get no mastery bonus.
//   3. Physical damage also takes the magic mastery (stat 357) bonus (it took none).
// Every percent is applied in 64 bits and truncated toward zero (D2Sigma 0x10088e50); D2R's own versions split
// the product by value ranges instead.
#pragma once

#include <algorithm>
#include <cstdint>

namespace skill_damage {

// Damage bonus of the five level ranges 2-8, 9-16, 17-22, 23-28, 29+ (skills.txt EDmgLevel / MinLevDam1-5 columns):
// the step of each range applies to the levels inside it (D2Sigma 0x100a6c60).
inline auto Steps(const int32_t* steps, int32_t level) noexcept -> int32_t {
	if (level <= 1 || steps == nullptr) {
		return 0;
	}
	const auto at = [steps](int i) { return static_cast<int64_t>(steps[i]); };
	int64_t sum = 0;
	if (level <= 8) {
		sum = (level - 1) * at(0);
	} else if (level <= 16) {
		sum = 7 * at(0) + (level - 8) * at(1);
	} else if (level <= 22) {
		sum = 7 * at(0) + 8 * at(1) + (level - 16) * at(2);
	} else if (level <= 28) {
		sum = 7 * at(0) + 8 * at(1) + 6 * at(2) + (level - 22) * at(3);
	} else {
		sum = 7 * at(0) + 8 * at(1) + 6 * at(2) + 6 * at(3) + (level - 28) * at(4);
	}
	return static_cast<int32_t>(sum);
}

// value * percent / 100 in 64 bits, truncated toward zero, 0 for either factor 0 (D2Sigma 0x10088e50).
inline auto MulDiv100(int32_t value, int32_t percent) noexcept -> int32_t {
	if (value == 0 || percent == 0) {
		return 0;
	}
	return static_cast<int32_t>(static_cast<int64_t>(value) * percent / 100);
}

// The built-in damage percent of a BaseSynergy skill (D2Sigma 0x100a6c70): rises with the caster's energy and
// level (capped at 120) and with stat 485 (/ 10, capped at 100). About -42% at level 1, +1000% at level 100 with
// 300 energy.
inline auto BaseSynergyPercent(int32_t level, int32_t energy, int32_t stat485) noexcept -> int32_t {
	const int32_t extra = std::min(stat485 / 10, 100);
	const int32_t capped = std::min(level, 120);
	const int32_t denominator = energy + 500;
	const int64_t spread = denominator != 0 ? (static_cast<int64_t>(energy) + 20) * 130 / denominator : 0;
	const int64_t scaled = (spread + 50 + extra) * (static_cast<int64_t>(capped) + 5) * 10 / 100;
	return static_cast<int32_t>(scaled - 75);
}

// Elemental damage (min or max): `percent` is the calc percent plus the BaseSynergy one, `mastery` the mastery stat
// of the skill's damage type (0 for none, or when the skill is StaticDamage or the caller asked for no mastery).
inline auto Elemental(int32_t base, const int32_t* steps, int32_t level, uint8_t shift, int32_t percent,
                      int32_t mastery) noexcept -> int32_t {
	uint32_t value = static_cast<uint32_t>(Steps(steps, level) + base) << (shift & 31);
	if (percent != 0) {
		value += static_cast<uint32_t>(MulDiv100(static_cast<int32_t>(value), percent));
	}
	if (mastery != 0) {
		value += static_cast<uint32_t>(MulDiv100(static_cast<int32_t>(value), mastery));
	}
	return static_cast<int32_t>(value);
}

// Physical damage (min or max): the weapon part (the weapon's damage times the skill's hit shift / 128) and the
// skill's own part take the percent separately; mastery (stat 357) raises the skill's own part only.
inline auto Physical(int32_t weapon, int32_t base, const int32_t* steps, int32_t level, uint8_t shift,
                     int32_t percent, int32_t mastery) noexcept -> int32_t {
	int32_t own = Steps(steps, level) + base;
	if (percent != 0) {
		own += MulDiv100(own, percent);
		if (weapon != 0) {
			weapon += MulDiv100(weapon, percent);
		}
	}
	if (mastery != 0) {
		own += MulDiv100(own, mastery);
	}
	return static_cast<int32_t>(static_cast<uint32_t>(weapon + own) << (shift & 31));
}

// weapon damage * hit shift / 128 (arithmetic shift, as D2Sigma).
inline auto WeaponPart(int32_t hitShift, int32_t weaponDamage) noexcept -> int32_t {
	return (hitShift * weaponDamage) >> 7;
}

}  // namespace skill_damage

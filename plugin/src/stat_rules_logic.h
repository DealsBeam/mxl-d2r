// Median's stat rules that D2Sigma patches into the original game code (docs/sigma-patch-audit.md items 11 and 15). No
// engine access, so mxl_tests can run them.
//
//   Potion of Life     D2Game 0xd9eea (ItemMode 6fcf9bf0): the permanent life it gives is 0x3200 (50 life, x256), vanilla 0x1400.
//   Lam Esen's Tome    D2Game 0xe6496 (sub_6fd06420): 10 free stat points, vanilla 5.
//   Stamina recovery   D2Game 0x79b7c redirects the stamina step of the per-frame regeneration to D2Sigma 0x100a1f90.
//   Mana recovery      0x79b77 redirects the mana step to 0x100a1f60 (formula 0x100a1b50); the drain by stat 297 is heat_logic.h.
//   Stat "+" click     Median's StatScreen (D2Sigma 0x101151f0): Shift-click spends 25 stat points, a plain click 1. D2R's server
//                      handler of client packet 0x3a (0x4b3c70; [id u8][stat u16][count u16]) spends count + 1 points one after the
//                      other and stops at the first refusal; its own Shift-click sends count 4 (5 points), Ctrl-click the number
//                      of points left minus one.
#pragma once

#include <cstdint>

namespace statrules {

constexpr int32_t PotionOfLifeLife = 0x3200;  // stat 7 (max life) += this; 50 life in the x256 stat units
constexpr int32_t LamEsenStatPoints = 10;

constexpr int32_t D2RShiftClickCount = 4;       // D2R's Shift-click: the packet's count (points after the first)
constexpr int32_t MedianShiftClickCount = 24;   // Median's Shift-click: 25 points

// The count the server handler gets: Shift-click's 4 becomes Median's 24, every other count (plain click 0, Ctrl-click: all that
// is left, one point of 5 left is the same thing as Shift's) stays. The handler stops at the first refused point, so 25 never
// spends more than the unused points the player has.
constexpr auto StatClickCount(int32_t count) noexcept -> int32_t {
	return count == D2RShiftClickCount ? MedianShiftClickCount : count;
}

// Unit modes the stamina step tells apart (unit +0xc in D2R, +0x10 in 1.13c).
constexpr int32_t ModeNeutral = 1;
constexpr int32_t ModeWalk = 2;
constexpr int32_t ModeRun = 3;
constexpr int32_t ModeTownNeutral = 5;

// Percent of the standing recovery a mode gets: 100 standing (in the field or in a town), none while running, 75 in every
// other mode (walking, attacking, casting, being hit, dead). D2R's own step gives 50 walking and attacking, 100 running
// or anything else only with a stamina recovery bonus of 1000, and none otherwise.
constexpr auto StaminaRatePercent(int32_t mode) noexcept -> int32_t {
	if (mode == ModeNeutral || mode == ModeTownNeutral) {
		return 100;
	}
	if (mode == ModeRun) {
		return 0;
	}
	return 75;
}

struct Stamina {
	int32_t stamina = 0;     // stat 10 (x256)
	int32_t maxStamina = 0;  // stat 11 (x256)
	int32_t bonus = 0;       // stat 28, stamina recovery %
	int32_t mode = 0;        // unit mode
	bool infinite = false;   // state 136 (the stamina shrine)
};

// The stamina after one regeneration step. State 136 fills the bar whatever the mode. Otherwise a unit that runs gets
// nothing and the rest get max / 256 (the bar's whole points, as raw units per frame) times (100 + bonus)% times the
// mode's percent, never above the maximum. There is no "stamina already full" or "stamina below one point" test.
constexpr auto StaminaAfter(const Stamina& s) noexcept -> int32_t {
	if (s.infinite) {
		return s.maxStamina;
	}
	const int32_t rate = StaminaRatePercent(s.mode);
	if (rate == 0) {
		return s.stamina;
	}
	int64_t step = s.maxStamina >> 8;
	if (s.bonus != 0) {
		step += step * s.bonus / 100;
	}
	if (rate != 100) {
		step = step * rate / 100;
	}
	const int64_t next = static_cast<int64_t>(s.stamina) + step;
	return static_cast<int32_t>(next > s.maxStamina ? s.maxStamina : next);
}

// The base of the mana recovery: max mana / (the class's regeneration seconds x 25 frames, 7500 without one). D2R (like
// 1.13c) raises a base of 0 to 1; D2Sigma's formula (0x100a1b50) does not, so a pool smaller than the frame count has none.
constexpr auto ManaBase(int32_t maxMana, int32_t frames) noexcept -> int32_t {
	return frames > 0 ? maxMana / frames : 0;
}

// The mana step with no heat (stat 297 = 0): the base scaled by (100 + stat 27)%, plus stat 26, kept inside
// [-mana, maxMana - mana]. base and bonus are 0 under state 0x55 (no regeneration).
constexpr auto ManaDelta(int32_t mana, int32_t maxMana, int32_t base, int32_t bonus, int32_t recovery) noexcept -> int32_t {
	int64_t delta = static_cast<int64_t>(base) * (bonus + 100) / 100 + recovery;
	if (delta > maxMana - mana) {
		delta = maxMana - mana;
	}
	if (delta < -mana) {
		delta = -mana;
	}
	return static_cast<int32_t>(delta);
}

}  // namespace statrules

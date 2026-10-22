// Median's skill restriction modes 3 to 7 (D2Sigma D2SkillValidation.cpp 0x100b0c30), the part of the usability check that D2R
// lacks: D2R's 0x33f360 knows restrict 0 to 2 and lets every other value through. The skill's State1-3 (-1 = none) are
// tested on the caster. No engine access, so mxl_tests can run it.
#pragma once

#include <cstdint>

namespace skill_restrict {

constexpr int RestrictFirst = 3;  // D2R handles 0 to 2 itself
constexpr int RestrictLast = 7;

// True when the skill may be used. `has(state)` is whether the caster has the state; `devotion` and `cold` are
// Median's melee_devotion_state and progressive_cold (restrict 7 refuses the skill while either is set).
template <typename HasState>
constexpr auto Allowed(int restrict, const int16_t (&states)[3], int32_t devotion, int32_t cold, HasState has) noexcept
	-> bool {
	const auto set = [&](int i) { return states[i] >= 0 && has(states[i]); };
	switch (restrict) {
	case 3:  // none of the listed states
		return !set(0) && !set(1) && !set(2);
	case 4:  // every listed state
		return (states[0] < 0 || set(0)) && (states[1] < 0 || set(1)) && (states[2] < 0 || set(2));
	case 5:  // all three listed; the first set, the other two not
		return states[0] >= 0 && states[1] >= 0 && states[2] >= 0 && set(0) && !set(1) && !set(2);
	case 6:  // the first or the second
		return states[0] >= 0 && states[1] >= 0 && (set(0) || set(1));
	case 7:  // the first two listed; neither devotion nor progressive cold; any of the three
		return states[0] >= 0 && states[1] >= 0 && !has(devotion) && !has(cold) && (set(0) || set(1) || set(2));
	default:
		return true;
	}
}

}  // namespace skill_restrict

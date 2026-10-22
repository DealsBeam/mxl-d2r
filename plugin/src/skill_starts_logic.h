// D2Sigma's own skill start functions that D2R lacks (skill_starts.cpp; docs/sigma-patch-audit.md item 20). No engine access,
// so mxl_tests can run it.
//
// skills.txt srvstfunc picks a function that runs when the skill is cast, before srvdofunc; returning 0 stops the cast (no
// mana, no do function), a null table entry counts as success. D2Sigma moves the table (0x101df948) and fills 9 slots; D2R
// has its own functions in 6, 10, 28, 39 and 56 that are the same ones, and different ones in 30, 58 and 66-67 (D2R's 64-70
// are used by D2R's own skills). The two Median skills that use a slot D2R means otherwise are moved by tools/txt_migrate.py
// to free slots 71 and 72 (Median 30 -> 71, 66 -> 72) and implemented here; Median's 58 and 67 have no skill.
//
//   slot 30 (D2Sigma 0x100b00d0): Flash (sk454, state 182 for auralencalc frames) and "Move Only" (no state: the cast never
//       starts, so a shift-click to move does nothing else). Puts the skill's aurastate on the caster until
//       frame + auralencalc, and prolongs it when it is on already.
//   slot 66 (D2Sigma 0x100b01f0): sk851 (state 791, 25 * level frames). The same, but with the state on already the cast
//       is refused (returns 0), so it cannot be stacked or its time refreshed.
#pragma once

#include <cstdint>

namespace skillstart {

constexpr int SlotTimedState = 71;      // Median srvstfunc 30
constexpr int SlotTimedStateOnce = 72;  // Median srvstfunc 66

struct Input {
	int32_t duration;     // auralencalc evaluated for the caster
	int32_t state;        // aurastate
	int32_t stateCount;   // rows of states.txt
	int32_t frame;        // game frame now
	bool present;         // the caster has the state already
	bool refuseIfPresent; // slot 66
};

struct Decision {
	bool start;           // false: the cast is refused (the start function returns 0)
	int32_t expiry;       // game frame the state ends
};

// D2Sigma: the state must be a row (0 < state < count: 0x100b3ef0) and the length positive; slot 66 also needs the state off.
constexpr auto Decide(const Input& in) noexcept -> Decision {
	if (in.state <= 0 || in.state >= in.stateCount || in.duration <= 0) {
		return {false, 0};
	}
	if (in.refuseIfPresent && in.present) {
		return {false, 0};
	}
	return {true, in.frame + in.duration};
}

}  // namespace skillstart

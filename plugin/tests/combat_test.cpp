// Tests of Median's avoid/block caps and property functions 27-29 (plugin/src/combat_logic.h, properties_ext_logic.h).
#include "../src/combat_logic.h"
#include "../src/properties_ext_logic.h"

#include <cstdio>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: combat: %s\n", what);
	}
}

}  // namespace

int RunCombatTests() {
	using namespace mxl_combat;

	// avoid/dodge/evade: 60 + the stat, at most 75
	Check(AvoidCap(0) == 60, "avoid cap without items is 60");
	Check(AvoidCap(10) == 70 && AvoidCap(15) == 75 && AvoidCap(40) == 75, "avoid cap rises to 75");
	Check(AvoidCap(-80) == 0, "avoid cap does not go negative");

	// block: 50 + the stat, at most 80; 50 with Indomitable
	Check(BlockCap(0, false) == 50 && BlockCap(25, false) == 75 && BlockCap(30, false) == 80 && BlockCap(60, false) == 80,
	      "block cap");
	Check(BlockCap(0, true) == 50 && BlockCap(25, true) == 50 && BlockCap(-20, true) == 30, "Indomitable limits block to 50");

	// resistances: 75 + the max stat, at most 90 (D2R's own ceiling is 95)
	Check(ResistCap(0, ResistMaxMedian) == 75 && ResistCap(10, ResistMaxMedian) == 85 && ResistCap(15, ResistMaxMedian) == 90 &&
	          ResistCap(20, ResistMaxMedian) == 90,
	      "resist cap rises to 90");
	Check(ResistCap(20, ResistMaxD2R) == 95 && ResistCap(-10, ResistMaxMedian) == 65, "resist cap: D2R ceiling, negative max stat");

	// absorb credit: divisor, percent, per-type percent (D2Sigma 0x10057a20); 1 point (0x100) at the least
	Check(AbsorbCredit(5000, 0, 0, 0) == 5000, "no stats leave the credit alone");
	Check(AbsorbCredit(5000, 4, 0, 0) == 1250 && AbsorbCredit(5000, 0, 50, 0) == 2500 && AbsorbCredit(5000, 0, 0, 25) == 3750,
	      "each absorb stat on its own");
	Check(AbsorbCredit(10000, 2, 50, 50) == 1250, "the three stats compose in order");
	Check(AbsorbCredit(300, 60, 0, 0) == 0x100 && AbsorbCredit(300, 0, 90, 0) == 0x100, "floor of one life point");
	Check(AbsorbCredit(0, 5, 50, 50) == 0, "nothing absorbed, nothing credited");
	Check(AbsorbPercentCut(1000, 25) == 250 && AbsorbPercentCut(1000, 90) == 400 && AbsorbPercentCut(1000, 0) == 0 &&
	          AbsorbPercentCut(1000, -5) == 0,
	      "absorb percent is limited to 40");
	Check(AbsorbLayer(0x27) == 1 && AbsorbLayer(0x29) == 2 && AbsorbLayer(0x25) == 3 && AbsorbLayer(0x2b) == 4 &&
	          AbsorbLayer(0x2d) == 5 && AbsorbLayer(0x24) == 0,
	      "absorb layers by resistance stat");

	// ethereal: +25% (D2R's 3/2 is replaced), truncated toward zero
	Check(EtherealBonus(100) == 125 && EtherealBonus(7) == 8 && EtherealBonus(3) == 3 && EtherealBonus(0) == 0,
	      "ethereal bonus is 25%");
	Check(EtherealBonus(-8) == -10 && EtherealBonus(1000000000) == 1250000000, "ethereal bonus: negatives, no 32-bit overflow in the product");

	// result bits map to the stat that was rolled
	Check(StatOfResult(ResultAvoid) == StatAvoid && StatOfResult(ResultDodge) == StatDodge &&
	          StatOfResult(ResultEvade) == StatEvade,
	      "result bits to stats");
	Check(StatOfResult(ResultBlock) == 0 && StatOfResult(0) == 0 && StatOfResult(3) == 0, "other results have no stat");

	// thinning keeps exactly cap/value of the successes: over every roll of [0, value)
	for (const int32_t value : {61, 75, 90, 100}) {
		for (const int32_t cap : {0, 30, 60, 75}) {
			int kept = 0;
			for (int32_t roll = 0; roll < value; ++roll) {
				kept += KeepsSuccess(value, cap, roll) ? 1 : 0;
			}
			const int32_t expected = value <= cap ? value : cap;
			Check(kept == expected, "thinning keeps cap of value rolls");
		}
	}
	Check(KeepsSuccess(50, 60, 49) && KeepsSuccess(60, 60, 59), "success at or under the cap always stays");

	// property functions
	using namespace mxl_props;
	{
		// innate elemental damage: param 2, element 1, percent 16
		const Layered r = Compute(27, 2, 1, 16);
		Check(r.layer == 0x201 && r.value == 16, "func 27 layer is param << 8 | element, value max");
		const Layered wrap = Compute(27, 0x1ff, 0x1234, 7);
		Check(wrap.layer == static_cast<int32_t>(static_cast<uint16_t>(0x1ff * 256 + 0x1234)) && wrap.value == 7,
		      "func 27 layer is 16 bits");
	}
	{
		const Layered r = Compute(28, 1, 1, 1);
		Check(r.layer == 1 && r.value == 1, "func 28 amulet marker");
		const Layered q = Compute(28, 0x12345, 9, 3);
		Check(q.layer == 0x2345 && q.value == 9, "func 28 layer is the low 16 bits of param, value min");
	}
	{
		const Layered r = Compute(29, 620, 100, 1966);
		Check(r.layer == 620 && r.value == 1966, "func 29 synergy for the skill in param, value max");
	}
	Check(Compute(26, 1, 2, 3).layer == 0 && Compute(30, 1, 2, 3).value == 0, "other functions compute nothing");
	Check(FirstFunction == 27 && LastFunction == 29, "range");

	return failures;
}

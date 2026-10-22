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

	// the Indomitable lookup (a skill level read through the unit's own data bank) is only made above the limit of 50
	int lookups = 0;
	const auto has = [&lookups](bool v) { return [&lookups, v] { ++lookups; return v; }; };
	Check(BlockCapFor(30, 75, has(true)) == 50 && lookups == 1, "block 75 with Indomitable is capped to 50");
	Check(BlockCapFor(30, 75, has(false)) == 80 && lookups == 2, "block 75 without Indomitable keeps the cap of 80");
	Check(BlockCapFor(30, 50, has(true)) == 80 && lookups == 2, "block 50 or less does not look Indomitable up");

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

	// damage reduction on the incoming damage (D2Sigma 0x10057c10): divisor, percent, per-type percent on the field D2R's step left
	const DamageType fire = DamageTypeOfName(NameWord('F', 'i', 'r', 'e'));
	const DamageType phys = DamageTypeOfName(NameWord('P', 'h', 'y', 's'));
	const DamageType cold = DamageTypeOfName(NameWord('C', 'o', 'l', 'd'));
	const DamageType freeze = DamageTypeOfName(NameWord('F', 'L', 'e', 'n'));
	Check(fire.kind == FieldKind::Damage && fire.layer == 1 && phys.kind == FieldKind::Damage && phys.layer == 0 && cold.layer == 4,
	      "damage types by name: fire layer 1, physical layer 0, cold layer 4");
	Check(DamageTypeOfName(NameWord('L', 'i', 'g', 't')).layer == 2 && DamageTypeOfName(NameWord('M', 'a', 'g', 'c')).layer == 3 &&
	          DamageTypeOfName(NameWord('P', 'o', 'i', 's')).layer == 5 && DamageTypeOfName(NameWord('L', 'i', 'f', 'e')).layer == 6 &&
	          DamageTypeOfName(NameWord('M', 'a', 'n', 'a')).layer == 7 && DamageTypeOfName(NameWord('S', 't', 'a', 'm')).layer == 8,
	      "damage layers: lightning 2, magic 3, poison 5, life / mana / stamina leech 6 / 7 / 8");
	Check(freeze.kind == FieldKind::Duration && DamageTypeOfName(NameWord('C', 'L', 'e', 'n')).kind == FieldKind::Duration &&
	          DamageTypeOfName(NameWord('P', 'L', 'e', 'n')).kind == FieldKind::Duration,
	      "cold, freeze and poison lengths are durations");
	Check(DamageTypeOfName(NameWord('X', 'x', 'x', 'x')).kind == FieldKind::None, "an unknown name is left alone");
	Check(ReduceField(100000, fire, 0, 0, 0) == 100000, "no stats leave the damage alone");
	Check(ReduceField(100000, fire, 4, 0, 0) == 25000 && ReduceField(100000, fire, 0, 50, 0) == 50000 &&
	          ReduceField(100000, fire, 0, 0, 25) == 75000,
	      "each damage reduction stat on its own");
	Check(ReduceField(100000, fire, 2, 50, 50) == 12500, "divisor, then 184, then 417");
	Check(ReduceField(100001, fire, 3, 33, 17) == 18538, "the three stats compose with truncation at every step");
	Check(ReduceField(7, fire, 2, 0, 0) == 3 && ReduceField(1, fire, 4, 0, 0) == 0, "the divisor truncates and has no floor of one point");
	Check(ReduceField(99, fire, 0, 33, 0) == 99 - 99 * 33 / 100 && ReduceField(1, fire, 0, 50, 0) == 1, "percent truncates the cut");
	Check(ReduceField(1000, fire, 0, 150, 0) == 0, "a percent above 100 cannot turn damage into healing");
	Check(ReduceField(100000, phys, 0, 0, 30) == 70000, "physical damage takes stat 417 with layer 0 (the caller reads that layer)");
	Check(ReduceField(0, fire, 3, 50, 50) == 0, "an empty field stays empty");
	Check(ReduceField(-40, fire, 3, 50, 50) == -40, "a negative field is left alone");
	Check(ReduceField(100, freeze, 4, 0, 0) == 50 && ReduceField(101, freeze, 99, 0, 0) == 50 && ReduceField(8, freeze, 2, 0, 0) == 5 &&
	          ReduceField(3, freeze, 2, 0, 0) == 5,
	      "a duration is halved, at least 5, whatever the divisor");
	Check(ReduceField(100, freeze, 0, 50, 50) == 100, "percent stats do not shorten a duration");
	Check(ReduceField(100000, DamageTypeOfName(NameWord('X', 'x', 'x', 'x')), 4, 50, 50) == 100000, "unknown entries are not touched");
	Check(ReduceField(2000000000, fire, 0, 50, 0) == 1000000000, "no 32-bit overflow in the product");

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

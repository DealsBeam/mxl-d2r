// Tests of Median's skill damage arithmetic (plugin/src/skill_damage_logic.h). Expected values are worked out by hand from D2Sigma's code (see the headers).
#include "../src/skill_damage_logic.h"

#include <cstdio>
#include <set>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: skill damage: %s\n", what);
	}
}

}  // namespace

int RunSkillDamageTests() {
	using namespace skill_damage;

	// per-level steps: 2-8 first step, 9-16 second, 17-22 third, 23-28 fourth, 29+ fifth
	const int32_t steps[5] = {10, 20, 30, 40, 50};
	Check(Steps(steps, 0) == 0 && Steps(steps, 1) == 0, "no steps at level 1");
	Check(Steps(steps, 2) == 10 && Steps(steps, 8) == 70, "levels 2-8 use the first step");
	Check(Steps(steps, 9) == 90 && Steps(steps, 16) == 70 + 8 * 20, "levels 9-16 use the second step");
	Check(Steps(steps, 17) == 70 + 160 + 30 && Steps(steps, 22) == 70 + 160 + 6 * 30, "levels 17-22 use the third step");
	Check(Steps(steps, 23) == 70 + 160 + 180 + 40 && Steps(steps, 28) == 70 + 160 + 180 + 6 * 40, "levels 23-28");
	Check(Steps(steps, 29) == 70 + 160 + 180 + 240 + 50 && Steps(steps, 40) == 70 + 160 + 180 + 240 + 12 * 50, "level 29+");
	Check(Steps(nullptr, 20) == 0, "no step table");

	// percent: 64-bit product, truncated toward zero
	Check(MulDiv100(0, 50) == 0 && MulDiv100(50, 0) == 0, "zero factor");
	Check(MulDiv100(100, 50) == 50 && MulDiv100(99, 50) == 49 && MulDiv100(-99, 50) == -49, "truncates toward zero");
	Check(MulDiv100(2000000000, 300) == static_cast<int32_t>(6000000000LL), "64-bit product, 32-bit result");
	Check(MulDiv100(1000000, 2000000) == static_cast<int32_t>(20000000000LL), "large percent");

	// BaseSynergy: ((energy+20)*130/(energy+500) + 50 + min(stat/10,100)) * (min(level,120)+5) * 10 / 100 - 75
	Check(BaseSynergyPercent(1, 0, 0) == -42, "level 1, no energy: -42%");
	Check(BaseSynergyPercent(100, 300, 0) == 996, "level 100, 300 energy: +996%");
	Check(BaseSynergyPercent(100, 300, 1000) == 2046, "stat 485 adds up to 100 to the sum");
	Check(BaseSynergyPercent(200, 300, 0) == BaseSynergyPercent(120, 300, 0), "level capped at 120");
	Check(BaseSynergyPercent(50, 100, 5000) == BaseSynergyPercent(50, 100, 1000), "stat 485 capped at 100 after / 10");
	Check(BaseSynergyPercent(1, -500, 0) == -45, "zero denominator counts as no energy term");

	// elemental: (steps + base) << shift, + percent, + mastery on the sum
	Check(Elemental(100, steps, 1, 0, 0, 0) == 100, "plain base");
	Check(Elemental(100, steps, 9, 0, 0, 0) == 100 + 90, "steps added");
	Check(Elemental(100, steps, 1, 2, 0, 0) == 400, "shift");
	Check(Elemental(100, steps, 1, 0, 50, 0) == 150, "calc percent");
	Check(Elemental(100, steps, 1, 0, 50, 20) == 150 + 30, "mastery on the grown value");
	Check(Elemental(100, steps, 1, 0, -42, 0) == 100 - 42, "negative percent (BaseSynergy at level 1)");
	Check(Elemental(100, steps, 1, 1, 100, 10) == 200 + 200 + 40, "shift, percent and mastery together");
	Check(Elemental(0, steps, 1, 0, 500, 500) == 0, "nothing to grow");

	// physical: weapon part and own part take the percent separately, mastery raises the own part only
	Check(Physical(0, 100, steps, 1, 0, 0, 0) == 100, "plain physical");
	Check(Physical(40, 100, steps, 1, 0, 0, 0) == 140, "weapon part added");
	Check(Physical(40, 100, steps, 1, 0, 50, 0) == 150 + 60, "percent on both parts");
	Check(Physical(40, 100, steps, 1, 0, 50, 20) == 150 + 30 + 60, "mastery on the own part only");
	Check(Physical(40, 100, steps, 1, 1, 0, 0) == 280, "shift covers both parts");
	Check(Physical(33, 0, steps, 1, 0, 50, 0) == 33 + 16, "weapon part truncates");
	Check(WeaponPart(64, 200) == 100 && WeaponPart(127, 3) == 2 && WeaponPart(0, 99) == 0, "weapon part = hit shift / 128");
	return failures;
}

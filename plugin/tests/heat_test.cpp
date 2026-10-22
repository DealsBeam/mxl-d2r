// Tests of the heat regeneration (heat_logic.h).
#include "../src/heat_logic.h"

#include <cstdio>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: heat: %s\n", what);
	}
}
}  // namespace

int RunHeatTests() {
	using namespace heat;
	Check(RegenFrames(0) == DefaultRegenFrames && RegenFrames(6) == 150, "regeneration frames");
	Regen r;
	r.mana = 25600;
	r.maxMana = 25600;
	r.base = 170;
	r.bonus = 0;
	r.heat = 106;  // level 20
	// 170 * 140 / 100 = 238; * -106 / 100 = -252
	Check(HeatDelta(r) == -252, "full bar loses heat");
	r.recovery = 10;
	Check(HeatDelta(r) == -(248 * 106 / 100), "stat 26 counts before the rate");
	r.recovery = 0;
	r.bonus = 100;  // +100% mana regeneration: (170 * 240 / 100) = 408 * 1.06
	Check(HeatDelta(r) == -(408 * 106 / 100), "regeneration bonus drains faster");
	r.bonus = 0;
	r.mana = 100;
	Check(HeatDelta(r) == -100, "never below empty");
	r.mana = 0;
	Check(HeatDelta(r) == 0, "empty stays empty");
	r.mana = 20000;
	r.heat = -300;  // Emberstep: 3 x the rate, building
	Check(HeatDelta(r) == 714, "negative rate builds heat");
	r.mana = 25500;
	Check(HeatDelta(r) == 100, "build-up stops at the maximum");
	r.heat = 0;
	r.mana = 5000;
	Check(HeatDelta(r) == 0, "no heat, no change");
	r.heat = 106;
	r.base = 0;
	r.bonus = 0;
	Check(HeatDelta(r) == 0, "state 0x55 (no regeneration) drains nothing");
	return failures;
}

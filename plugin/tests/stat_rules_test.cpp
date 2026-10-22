// Tests of the stat rules (stat_rules_logic.h): Potion of Life and Lam Esen amounts, stamina recovery, mana base.
#include "../src/stat_rules_logic.h"

#include <cstdio>
#include <initializer_list>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: stat rules: %s\n", what);
	}
}
}  // namespace

int RunStatRulesTests() {
	using namespace statrules;

	// item 11
	Check(PotionOfLifeLife == 50 * 256, "Potion of Life gives 50 life in x256 units");
	Check(LamEsenStatPoints == 10, "Lam Esen's Tome gives 10 stat points");

	// item 15, stamina: mode percents
	Check(StaminaRatePercent(ModeNeutral) == 100 && StaminaRatePercent(ModeTownNeutral) == 100, "standing 100%");
	Check(StaminaRatePercent(ModeRun) == 0, "running 0%");
	Check(StaminaRatePercent(ModeWalk) == 75 && StaminaRatePercent(6) == 75 && StaminaRatePercent(4) == 75 && StaminaRatePercent(0) == 75,
	      "walking and every other mode 75%");

	// a 50 point bar (12800 raw): standing 50 raw a frame, walking 37, running none
	Stamina s;
	s.stamina = 1000;
	s.maxStamina = 12800;
	s.mode = ModeNeutral;
	Check(StaminaAfter(s) == 1050, "standing +50");
	s.mode = ModeWalk;
	Check(StaminaAfter(s) == 1037, "walking +37 (75% of 50)");
	s.mode = ModeRun;
	Check(StaminaAfter(s) == 1000, "running: no recovery");
	s.mode = ModeWalk;
	s.stamina = 0;
	Check(StaminaAfter(s) == 37, "walking from empty still recovers (no one-point rule)");
	s.stamina = 12790;
	Check(StaminaAfter(s) == 12800, "never above the maximum");
	s.stamina = 12800;
	Check(StaminaAfter(s) == 12800, "a full bar stays full");

	// recovery bonus: +100% doubles the step, then the mode's percent applies
	s.stamina = 0;
	s.mode = ModeNeutral;
	s.bonus = 100;
	Check(StaminaAfter(s) == 100, "bonus 100% standing");
	s.mode = ModeWalk;
	Check(StaminaAfter(s) == 75, "bonus 100% walking: 100 * 75%");
	s.mode = ModeRun;
	s.bonus = 1000;
	Check(StaminaAfter(s) == 0, "running recovers nothing even with D2R's 1000 bonus");

	// state 136 (stamina shrine): full at once, in every mode
	s.bonus = 0;
	s.infinite = true;
	s.stamina = 3;
	for (int32_t mode : {0, 1, 2, 3, 4, 5, 6}) {
		s.mode = mode;
		Check(StaminaAfter(s) == 12800, "stamina shrine fills the bar in every mode");
	}

	// item 15, mana: the base is not raised to 1
	Check(ManaBase(25600, 300) == 85 && ManaBase(299, 300) == 0 && ManaBase(25600, 0) == 0, "mana base without a floor");
	Check(ManaDelta(0, 299, 0, 0, 0) == 0, "tiny pool, no recovery stat: nothing");
	Check(ManaDelta(0, 25600, 85, 0, 0) == 85, "base alone");
	Check(ManaDelta(0, 25600, 85, 100, 10) == 180, "(100 + 100)% of the base, plus stat 26");
	Check(ManaDelta(25590, 25600, 85, 0, 0) == 10, "kept below the maximum");
	Check(ManaDelta(5, 25600, 0, 0, -50) == -5, "kept above empty");
	Check(ManaDelta(0, 25600, 85, -100, 0) == 0, "a bonus of -100% removes the base");
	return failures;
}

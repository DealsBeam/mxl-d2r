// Tests of the skill start decisions (skill_starts_logic.h).
#include "../src/skill_starts_logic.h"

#include <cstdio>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: skill starts: %s\n", what);
	}
}
}  // namespace

int RunSkillStartsTests() {
	using namespace skillstart;
	Input in{20, 182, 900, 1000, false, false};
	Decision d = Decide(in);
	Check(d.start && d.expiry == 1020, "Flash: the state until frame + length");
	in.present = true;
	d = Decide(in);
	Check(d.start && d.expiry == 1020, "slot 30 prolongs a state that is on");
	in.refuseIfPresent = true;
	Check(!Decide(in).start, "slot 66 refuses a state that is on");
	in.present = false;
	Check(Decide(in).start, "slot 66 starts without the state");
	in.state = 0;
	Check(!Decide(in).start, "no aurastate (Move Only): the cast does not start");
	in.state = -1;
	Check(!Decide(in).start, "state -1: the cast does not start");
	in.state = 900;
	Check(!Decide(in).start, "a state past the table is refused");
	in.state = 899;
	Check(Decide(in).start, "the last state row is allowed");
	in.duration = 0;
	Check(!Decide(in).start, "a length of 0 is refused");
	in.duration = -5;
	Check(!Decide(in).start, "a negative length is refused");
	Check(SlotTimedState == 71 && SlotTimedStateOnce == 72, "slots are the free ones after D2R's 70");
	return failures;
}

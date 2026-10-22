// Tests of the trophy challenge windows (trophy_timer_logic.h).
#include "../src/trophy_timer_logic.h"

#include <cstdio>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: trophy timer: %s\n", what);
	}
}
}  // namespace

int RunTrophyTimerTests() {
	using namespace trophy;
	using Event = Tracker::Event;
	Check(LimitFrames(167166) == 250, "Heart of Sin: 10 s");
	Check(LimitFrames(154154) == 625, "Vizjun: 25 s");
	Check(LimitFrames(182182) == 330 * 25, "Yshari: 5 minutes 30 seconds");
	Check(LimitFrames(214100) == 0 && LimitFrames(0) == 0, "an untimed code has no plugin limit");

	const uint32_t id = 7;
	Tracker t;
	Check(t.Open(id, 167166, 1000), "a player the tracker has not seen passes");
	Check(t.Observe(id, 0, 2, false, 1000) == Event::None, "no code, no window");
	Check(t.Observe(id, 167166, 2, false, 1000) == Event::Opened, "the code opens a window");
	Check(t.Open(id, 167166, 1000) && t.Open(id, 167166, 1250), "open for the whole limit (10 s = 250 frames)");
	Check(!t.Open(id, 167166, 1251), "too late: refused");
	Check(t.Open(id, 999, 5000), "another code is not this window's business");
	Check(t.Observe(id, 167166, 2, false, 1100) == Event::None, "the same code keeps its window");
	Check(!t.Open(id, 167166, 1251), "still refused");
	Check(t.Observe(id, 0, 2, false, 1300) == Event::Closed, "the code ending closes it");
	Check(t.Open(id, 167166, 1300), "closed windows do not refuse (the stat compare does)");

	// a zone change voids the window, even if the player comes back
	Check(t.Observe(id, 167166, 2, false, 2000) == Event::Opened, "reopen");
	Check(t.Observe(id, 167166, 3, false, 2005) == Event::VoidedZone, "another area voids it");
	Check(!t.Open(id, 167166, 2006), "voided: refused");
	Check(t.Observe(id, 167166, 2, false, 2010) == Event::None, "back in the area it stays voided");
	Check(!t.Open(id, 167166, 2011), "still refused");
	Check(t.Observe(id, 0, 2, false, 2100) == Event::Closed, "the code ends");
	Check(t.Observe(id, 167166, 2, false, 2200) == Event::Opened, "a new kill opens a new window");
	Check(t.Open(id, 167166, 2201), "and it is open");

	// death
	Check(t.Observe(id, 167166, 2, true, 2210) == Event::VoidedDeath, "dying voids it");
	Check(!t.Open(id, 167166, 2211), "dead: refused");
	Check(t.Observe(id, 167166, 2, true, 2220) == Event::None, "once");

	// a changed total starts over (Tran Athulua: the second boss adds its buff)
	Check(t.Observe(id, 146000, 4, false, 3000) == Event::Opened, "first buff");
	Check(t.Observe(id, 146146, 4, false, 3100) == Event::Opened, "the sum is a new window");
	Check(t.Open(id, 146146, 3100 + 250) && !t.Open(id, 146146, 3100 + 251), "counted from the sum");

	// a code that appears while the player is dead is void at once
	Tracker d;
	Check(d.Observe(9, 176000, 1, true, 10) == Event::VoidedDeath && !d.Open(9, 176000, 11), "born dead");

	// players do not share windows; a new game (frame counter back) forgets everything
	Tracker m;
	m.Touch(500);
	m.Observe(1, 172172, 5, false, 500);
	m.Observe(2, 172172, 5, false, 600);
	Check(!m.Open(1, 172172, 800) && m.Open(2, 172172, 800), "per player");
	m.Touch(50);
	Check(m.Open(1, 172172, 800), "a new game starts clean");

	// eight players at most
	Tracker f;
	for (uint32_t i = 1; i <= 8; ++i) {
		f.Observe(i, 1000 + static_cast<int32_t>(i), 1, false, 1);
	}
	Check(f.Observe(9, 1009, 1, false, 1) == Event::None, "a ninth player is not tracked");
	return failures;
}

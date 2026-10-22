// Tests of the dungeon lockout rules (dungeon_logic.h). Run through mxl_tests (requirements_test.cpp main).
#include "../src/dungeon_logic.h"

#include <cstdio>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL (dungeon): %s\n", what);
	}
}

}  // namespace

int RunDungeonTests() {
	using namespace dungeon;
	// the table is sorted (binary search) and keyed by D2R level id
	for (int i = 1; i < mxl_dungeons::EntryCount; ++i) {
		Check(mxl_dungeons::Entries[i - 1].level < mxl_dungeons::Entries[i].level, "entries sorted by level");
	}
	Check(Find(1) == nullptr && Find(1000) == nullptr, "levels without a row");
	Check(Find(157) != nullptr && Find(157)->lockSeconds == 20, "157 butcher_s_lair: 20 s");
	Check(Find(183) != nullptr && Find(183)->lockSeconds == 720, "183 yshari_sanctum: 720 s");

	// requirements: 144 library_of_fate = Hell only, level 115
	const Entry& lib = *Find(144);
	Check(!MeetsRequirements(lib, {120, 0, 0}), "Normal refused (minimum difficulty)");
	Check(!MeetsRequirements(lib, {120, 0, 1}), "Nightmare refused");
	Check(!MeetsRequirements(lib, {114, 0, 2}), "Hell level 114 refused");
	Check(MeetsRequirements(lib, {115, 0, 2}), "Hell level 115 allowed");
	Check(!MeetsRequirements(lib, {115, 0, 3}), "difficulty out of range refused");
	// 142 kurast_3000_ba: Nightmare from 80, Hell from 115
	const Entry& k = *Find(142);
	Check(!MeetsRequirements(k, {79, 0, 1}) && MeetsRequirements(k, {80, 0, 1}), "Nightmare level 80");
	Check(!MeetsRequirements(k, {0, 0, 0}), "Normal below the minimum difficulty");
	// 39 Moo Moo Farm: minimum Hell, Normal level 0 but still refused
	Check(!MeetsRequirements(*Find(39), {99, 0, 0}) && MeetsRequirements(*Find(39), {110, 0, 2}), "cow level Hell only");
	// class lock: 256 (class lock 6 = class 5, Druid): other classes refused
	const Entry& mt = *Find(256);
	Check(MeetsRequirements(mt, {120, 5, 2}), "mirror temple: class value 6 = class 5 allowed");
	Check(!MeetsRequirements(mt, {120, 0, 2}) && !MeetsRequirements(mt, {120, 6, 2}), "mirror temple: other classes refused");
	// rows with nothing set (247 whimsyshire) never refuse
	Check(MeetsRequirements(*Find(247), {1, 0, 0}), "no requirements");

	// clock: starts on entering, seals when it runs out, never reopens
	Lockout l;
	int g = 0;
	l.Touch(&g, 100);
	Check(!l.Sealed(157, 0) && !l.Running(157, 0), "fresh game: nothing running");
	Check(l.Start(157, 1000), "first entry starts the clock");
	Check(l.Running(157, 1000) && l.Running(157, 20999) && !l.Sealed(157, 20999), "running for 20 s");
	Check(l.Sealed(157, 21000) && !l.Running(157, 21000), "sealed after 20 s");
	Check(l.RemainingMs(157, 1000) == 20000 && l.RemainingMs(157, 12500) == 8500 && l.RemainingMs(157, 20999) == 1,
	      "time left counts down");
	Check(l.RemainingMs(157, 21000) == 0 && l.RemainingMs(5, 0) == 0, "no time left once sealed, none for a level without a clock");
	Check(!l.Start(157, 22000), "a sealed level does not restart");
	Check(!l.Start(157, 5000) && l.Sealed(157, 99999999), "stays sealed");
	Check(dungeon::Check(l, {120, 0, 2}, 157, 21000) == Verdict::Sealed, "entry refused once sealed");
	Check(dungeon::Check(l, {120, 0, 2}, 157, 20000) == Verdict::Allow, "entry allowed while running");
	Check(dungeon::Check(l, {90, 0, 2}, 157, 20000) == Verdict::Requirements, "requirements first");
	Check(!l.Sealed(159, 99999999), "other levels unaffected");
	// running clock is not restarted by a second entry
	Lockout m;
	m.Touch(&g, 1);
	Check(m.Start(159, 0) && !m.Start(159, 10000) && m.Sealed(159, 20000), "second entry keeps the first end time");
	// no lock time: no clock, never sealed
	Check(!m.Start(144, 0) && !m.Sealed(144, 99999999), "level without a lock time");
	Check(!m.Start(5, 0) && !m.Sealed(5, 0), "level without a row");

	// lock group 1: 268 (240 s) and 269 (10 s) lock together; the starter's time applies to both
	Lockout n;
	n.Touch(&g, 1);
	Check(n.Start(268, 0), "group start");
	Check(n.Running(269, 239999) && n.Sealed(269, 240000), "group member follows the starter's end time");
	Check(!n.Start(269, 1000), "group member does not restart");
	Check(dungeon::Check(n, {147, 0, 2}, 269, 250000) == Verdict::Sealed, "member sealed with the group");
	Lockout o;
	o.Touch(&g, 1);
	Check(o.Start(269, 0) && o.Sealed(268, 10000) && !o.Sealed(268, 9999), "short level starts the group: 10 s for both");

	// a new game (other pointer) or a frame counter that went back resets
	int g2 = 0;
	n.Touch(&g2, 5);
	Check(!n.Sealed(268, 99999999) && !n.Running(269, 0), "new game pointer resets");
	l.Touch(&g, 100);
	Check(l.Sealed(157, 99999999), "same game keeps the seal");
	l.Touch(&g, 200);
	Check(l.Sealed(157, 99999999), "frame moving on keeps the seal");
	l.Touch(&g, 50);
	Check(!l.Sealed(157, 99999999), "frame counter went back: new game");

	// the game-end hook: only the ended game's clocks go
	dungeon::Lockout f;
	f.Touch(&g, 10);
	Check(f.Start(157, 0) && f.Sealed(157, 99999999), "clock for the game-end check");
	f.Forget(&g2);
	Check(f.Sealed(157, 99999999), "another game's end keeps the clocks");
	f.Forget(nullptr);
	Check(f.Sealed(157, 99999999), "a null game is ignored");
	f.Forget(&g);
	Check(!f.Sealed(157, 99999999), "the game's end clears its clocks");
	f.Touch(&g, 500);
	Check(!f.Sealed(157, 99999999), "a reused pointer starts clean");
	return failures;
}

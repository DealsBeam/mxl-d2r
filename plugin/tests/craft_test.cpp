// Standalone test of craft flag storage, gold split and the effects table (not part of the plugin DLL).
#include "../src/craft_state.h"

#include <cstdio>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: %s\n", what);
	}
}
}  // namespace

int main() {
	craft::State s;
	int g1 = 0, g2 = 0;
	s.Touch(&g1);
	Check(!s.Test(false, 7, 1) && !s.Test(true, 7, 1), "fresh flags clear");
	s.Set(false, 7, 1, true);
	Check(s.Test(false, 7, 1) && !s.Test(false, 8, 1) && !s.Test(true, 7, 1), "player flag is per player");
	s.Set(true, 7, 50, true);
	Check(s.Test(true, 0, 50) && s.Test(true, 99, 50), "game flag is for everybody");
	s.Touch(&g1);
	Check(s.Test(false, 7, 1) && s.Test(true, 0, 50), "same game keeps flags");
	s.Set(false, 7, 1, false);
	Check(!s.Test(false, 7, 1), "clear works");
	s.Touch(&g2);
	Check(!s.Test(true, 0, 50), "new game resets the game flags");
	s.Set(false, 3, 255, true);
	s.Set(false, 3, 256, true);
	s.Set(false, 3, -1, true);
	Check(s.Test(false, 3, 255) && !s.Test(false, 3, 256) && !s.Test(false, 3, -1), "flag range 0-255");

	auto g = craft::Pay({1000, 500}, 300);
	Check(g.carried == 700 && g.bank == 500, "paid from carried");
	g = craft::Pay({100, 500}, 300);
	Check(g.carried == 0 && g.bank == 300, "rest from the bank");
	g = craft::Pay({0, 50}, 300);
	Check(g.carried == 0 && g.bank == 0, "never below zero");

	int n6 = 0, n7 = 0, n9 = 0, other = 0, prev = -1;
	bool sorted = true;
	for (const auto& e : mxl_cube::Effects) {
		sorted = sorted && e.recipe >= prev;
		prev = e.recipe;
		(e.kind == 6 ? n6 : e.kind == 7 ? n7 : e.kind == 9 ? n9 : other)++;
	}
	Check(sorted, "effects sorted by recipe");
	Check(n6 == 65 && n7 == 135 && n9 == 56 && other == 0, "effect counts 65 / 135 / 56");
	const auto r2 = craft::EffectsFor(2);
	Check(r2.last - r2.first == 1 && r2.first->kind == 9 && r2.first->param == 69, "row 2 sets game flag 69");
	const auto none = craft::EffectsFor(1);
	Check(none.first == none.last, "row 1 has no effect");

	std::printf(failures == 0 ? "craft: all tests passed\n" : "craft: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}

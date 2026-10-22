// Standalone test of craft flag storage, gold split and the effects table (not part of the plugin DLL).
#include "../src/craft_state.h"

#include <cstdio>
#include <string>

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
	Check(!s.Test(false, "p7", 1) && !s.Test(true, "p7", 1), "fresh flags clear");
	s.Set(false, "p7", 1, true);
	Check(s.Test(false, "p7", 1) && !s.Test(false, "p8", 1) && !s.Test(true, "p7", 1), "player flag is per player");
	s.Set(true, "p7", 50, true);
	Check(s.Test(true, "p0", 50) && s.Test(true, "p99", 50), "game flag is for everybody");
	s.Touch(&g1);
	Check(s.Test(false, "p7", 1) && s.Test(true, "p0", 50), "same game keeps flags");
	s.Set(false, "p7", 1, false);
	Check(!s.Test(false, "p7", 1), "clear works");
	s.Touch(&g2);
	Check(!s.Test(true, "p0", 50), "new game resets the game flags");
	s.Set(false, "p3", 255, true);
	s.Set(false, "p3", 256, true);
	s.Set(false, "p3", -1, true);
	Check(s.Test(false, "p3", 255) && !s.Test(false, "p3", 256) && !s.Test(false, "p3", -1), "flag range 0-255");

	// Flags are found by character name, not by unit id; a game's end clears that game's flags only.
	s.Touch(&g1);
	s.Set(false, "Alice", 5, true);
	Check(s.Test(false, "Alice", 5) && !s.Test(false, "alice2", 5), "player flag is per name");
	s.Forget(&g2);
	Check(s.Test(false, "Alice", 5), "forgetting another game keeps the flags");
	s.Set(true, "", 70, true);
	s.Forget(&g1);
	Check(!s.Test(false, "Alice", 5) && !s.Test(true, "", 70), "game end clears player and game flags");
	s.Forget(nullptr);
	s.Touch(&g1);
	Check(!s.Test(false, "Alice", 5), "a reused pointer starts clean");

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

	Check(craft::PortalLevel(2, 0) == 39 && craft::PortalLevel(3, 0) == 163 && craft::PortalLevel(85, 0) == 192 &&
	          craft::PortalLevel(4962, 1) == 242 && craft::PortalLevel(7453, 0) == 214,
	      "the five portal recipes name their Median level");
	Check(craft::PortalLevel(4962, 0) == 0 && craft::PortalLevel(1, 0) == 0, "no portal in other slots / recipes");

	// output slot pointer -> recipe and slot (record + 0x5a + 0x5b * i, stride 0x16c)
	int32_t recipe = -1, slot = -1;
	Check(craft::SlotOf(1000, 1000 + 4962 * 0x16c + 0x5a + 0x5b, 7500, &recipe, &slot) && recipe == 4962 && slot == 1,
	      "slot 1 of recipe 4962");
	Check(craft::SlotOf(1000, 1000 + 7 * 0x16c + 0x5a, 7500, &recipe, &slot) && recipe == 7 && slot == 0, "slot 0 of recipe 7");
	Check(craft::SlotOf(1000, 1000 + 7 * 0x16c + 0x5a + 2 * 0x5b, 7500, &recipe, &slot) && recipe == 7 && slot == 2, "slot 2");
	Check(!craft::SlotOf(1000, 1000 + 7 * 0x16c + 0x5a + 3 * 0x5b, 7500, &recipe, &slot), "no fourth slot");
	Check(!craft::SlotOf(1000, 1000 + 7 * 0x16c + 0x5b, 7500, &recipe, &slot), "not on a slot boundary");
	Check(!craft::SlotOf(1000, 999, 7500, &recipe, &slot) && !craft::SlotOf(1000, 1000 + 7500 * 0x16c + 0x5a, 7500, &recipe, &slot),
	      "outside the table");

	std::printf(failures == 0 ? "craft: all tests passed\n" : "craft: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}

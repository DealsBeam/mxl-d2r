// Tests of the town rules (plugin/src/town_rules_logic.h).
#include "../src/town_rules_logic.h"

#include <cstdio>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: town rules: %s\n", what);
	}
}

}  // namespace

int RunTownRulesTests() {
	Check(town::NeedsHealFlag(0) && !town::NeedsHealFlag(1), "heal flag is written only where clear");
	// Median's Tplock levels (levels.txt) include the Act 2 treasure tomb (61) and Duriel's lair (73); towns and unknown levels do not.
	Check(town::TownPortalLocked(61) && town::TownPortalLocked(73), "Tplock levels refuse");
	Check(!town::TownPortalLocked(1) && !town::TownPortalLocked(40) && !town::TownPortalLocked(0) && !town::TownPortalLocked(-5), "other levels allow");
	return failures;
}

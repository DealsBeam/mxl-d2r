// Tests of the item and mana rules (item_rules_logic.h). Run through mxl_tests (requirements_test.cpp main).
#include "../src/item_rules_logic.h"

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

int RunItemRulesTests() {
	using namespace itemrules;

	// IT3: magic, set, rare and unique items are born identified, the other qualities are not touched
	Check(!DropsIdentified(0) && !DropsIdentified(1) && !DropsIdentified(2) && !DropsIdentified(3), "low, normal, superior: as generated");
	Check(DropsIdentified(4) && DropsIdentified(5) && DropsIdentified(6) && DropsIdentified(7), "magic, set, rare, unique: identified");
	Check(!DropsIdentified(8) && !DropsIdentified(-1) && !DropsIdentified(9), "crafted and invalid: as generated");
	Check(ItemFlagIdentified == 0x10, "identified flag 0x10");

	// IT2: the stack is not used up; the call still reports a use for a non-empty stack
	Check(QuantityUseResult(60) == 60 && QuantityUseResult(1) == 1, "stack stays and reports a use");
	Check(QuantityUseResult(0) == 0 && QuantityUseResult(-1) == 0, "empty stack reports no use");

	// SK3: free in a town only
	Check(ManaCharge(500, true, true) == 0, "town: no mana");
	Check(ManaCharge(500, false, true) == 500, "field: normal cost");
	Check(ManaCharge(500, true, false) == 500, "town rule off: normal cost");
	Check(ManaCharge(0, true, true) == 0, "zero stays zero");
	return failures;
}

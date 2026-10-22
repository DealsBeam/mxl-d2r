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

	// IT3: identify only what is not identified yet
	Check(NeedsIdentify(0) && NeedsIdentify(0x4000000) && !NeedsIdentify(0x10) && !NeedsIdentify(0x4000010), "identified flag 0x10");
	Check(PickupSucceeded(1) && PickupSucceeded(0xffffff01) && !PickupSucceeded(0) && !PickupSucceeded(0x100), "pickup result is the low byte");

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

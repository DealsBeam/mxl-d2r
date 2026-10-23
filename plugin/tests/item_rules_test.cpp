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

	// IT1: the trade/repair line becomes trade, nothing else changes, a second pass finds nothing
	uint16_t charsi[3] = {0xd35, MenuLineTradeRepair, 0x102e};
	Check(RetitleRepairLines(charsi, 3) == 1 && charsi[0] == 0xd35 && charsi[1] == MenuLineTrade && charsi[2] == 0x102e, "trade/repair -> trade");
	Check(RetitleRepairLines(charsi, 3) == 0 && charsi[1] == MenuLineTrade, "second pass changes nothing");
	uint16_t gheed[4] = {0xd35, MenuLineTrade, 0xd46, 0x102e};
	Check(RetitleRepairLines(gheed, 4) == 0 && gheed[1] == MenuLineTrade && gheed[2] == 0xd46, "trade vendors untouched");
	uint16_t repairOnly[2] = {MenuLineTradeRepair, 0x102e};  // record 257: a repair-only vendor
	Check(RetitleRepairLines(repairOnly, 2) == 1 && repairOnly[0] == MenuLineTrade, "repair-only record");
	Check(RetitleRepairLines(charsi, 0) == 0, "empty list");
	Check(VendorModeTrade == 0 && VendorModeRepair == 1, "vendor panel modes");

	// SK3: free in a town only
	Check(ManaCharge(500, true, true) == 0, "town: no mana");
	Check(ManaCharge(500, false, true) == 500, "field: normal cost");
	Check(ManaCharge(500, true, false) == 500, "town rule off: normal cost");
	Check(ManaCharge(0, true, true) == 0, "zero stays zero");
	return failures;
}

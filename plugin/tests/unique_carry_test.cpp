// Tests of the per-unique carry1 number (unique_carry_logic.h). Run through mxl_tests (requirements_test.cpp main).
#include "../src/unique_carry_logic.h"

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

int RunUniqueCarryTests() {
	using unique_carry::CarryValue;
	Check(CarryValue(18, 0) == 0 && CarryValue(0, 0) == 0, "a unique without carry1 stays without");
	Check(CarryValue(18, 1) != CarryValue(19, 1) && CarryValue(18, 1) != CarryValue(57, 1), "different uniques get different numbers");
	Check(CarryValue(18, 1) == CarryValue(18, 1), "the same unique gets the same number");
	Check(CarryValue(18, CarryValue(18, 1)) == CarryValue(18, 1), "a second table load changes nothing");
	Check(CarryValue(0, 1) == 1 && CarryValue(1833, 1) == 1834, "the number is the row + 1 and never 0");
	bool distinct = true;
	for (int a = 0; a < 1834 && distinct; ++a) {
		distinct = CarryValue(a, 1) > 0 && (a == 0 || CarryValue(a, 1) != CarryValue(a - 1, 1));
	}
	Check(distinct, "numbers are positive and differ from row to row");
	Check(unique_carry::RecordSize == 0x15c && unique_carry::CarryOffset == 0x30, "the record layout read from D2R's loader");
	return failures;
}

// Tests of the vendor stock constants (vendor_stock_logic.h). Run through mxl_tests (requirements_test.cpp main).
#include "../src/vendor_stock_logic.h"

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

int RunVendorStockTests() {
	using namespace vendor_stock;
	const int before = failures;
	bool onlyImmediate = true;
	for (size_t i = 0; i < sizeof(ExtraCopiesOriginal); ++i) {
		onlyImmediate = onlyImmediate && ((ExtraCopiesOriginal[i] == ExtraCopiesPatched[i]) == (i != 3));
	}
	Check(onlyImmediate, "only byte 3 (the cmp immediate) differs");
	Check(ExtraCopiesOriginal[3] == NormalCopiesBelow && ExtraCopiesPatched[3] > 99, "the extra copies need an unreachable item level");
	Check(ExtraCopiesOriginal[0] == 0x41 && ExtraCopiesOriginal[1] == 0x83 && ExtraCopiesOriginal[2] == 0xfd, "cmp r13d, imm8");
	Check(NightmareLevel(0, 120) == 68 && NightmareLevel(0, 50) == 50 && NightmareLevel(4, 200) == 102, "Nightmare cap by act");
	Check(NightmareLevel(5, 200) == 200 && NightmareLevel(-1, 200) == 200, "no cap for an unknown act");
	Check(sizeof(CapEntry) == 15 && CapEntry[0] == 0x48, "cap entry: three whole instructions");
	return failures - before;
}

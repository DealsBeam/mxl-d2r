// Tests of the item generation rules (item_gen_logic.h). Run through mxl_tests (requirements_test.cpp main).
#include "../src/item_gen_logic.h"

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

int RunItemGenTests() {
	using namespace item_gen;

	// class lock: D2Sigma 0x1007e470, stat 463 (D2R 492) = class + 1
	Check(ClassLockAllows(0, 3) && ClassLockAllows(0, 0), "no lock: any class");
	Check(ClassLockAllows(1, 0) && !ClassLockAllows(1, 1), "locked to the Amazon (stat 1)");
	Check(ClassLockAllows(7, 6) && !ClassLockAllows(7, 5), "locked to the Assassin (stat 7)");
	Check(ClassLockAllows(8, 0) && ClassLockAllows(8, 6), "stat 8 (class 7, Median's none) locks nothing");
	Check(ClassLockAllows(-3, 2), "a negative stat is no lock");

	// ethereal: 0x1007e120 / 0x1007fdd0
	Check(EtherealChancePerMille(2) == 25 && EtherealChancePerMille(7) == 25 && EtherealChancePerMille(4) == 25, "25 per mille for normal, magic, unique");
	Check(EtherealChancePerMille(5) == 0 && EtherealChancePerMille(8) == 0 && EtherealChancePerMille(9) == 0 && EtherealChancePerMille(0) == 0, "none for set and crafted");
	Check(EtherealAcceptOutOf(25) == 26 && EtherealAcceptOutOf(0) == 1 && EtherealAcceptOutOf(49) == 50 && EtherealAcceptOutOf(400) == 50,
	      "acceptance out of D2R's 50 per mille gate, capped");
	Check(EtherealAcceptOutOf(-1) == 0, "negative chance accepts nothing");
	Check(EtherealStat(100) == 125 && EtherealStat(7) == 8 && EtherealStat(3) == 3 && EtherealStat(0) == 0, "+25 percent, truncated");
	Check(EtherealStat(1000000000) == 1250000000, "no 32-bit overflow in the product");
	Check(EtherealDurability(100) == 75 && EtherealDurability(1) == 1 && EtherealDurability(2) == 1 && EtherealDurability(3) == 2,
	      "75 percent of the maximum, at least 1");
	Check(EtherealDurability(0) == 0 && EtherealDurability(-5) == -5, "no durability stays as it is");

	// sockets: 0x1007e240, rand() % 7
	Check(SocketsFromRoll(0) == 0 && SocketsFromRoll(1) == 1 && SocketsFromRoll(6) == 6, "remainder 0 gives none, 1-6 as they are");
	Check(SocketsFromRoll(7) == 0 && SocketsFromRoll(-1) == 0, "out-of-range remainders give none");
	int given = 0;
	for (int r = 0; r < 7; ++r) {
		given += SocketsFromRoll(r) > 0 ? 1 : 0;
	}
	Check(given == 6, "six of seven remainders give sockets");

	// dye: 0x1007e6d0 / 0x1007e6c0
	Check(DyeIndex(0) == -1 && DyeIndex(1) == 0 && DyeIndex(21) == 20, "stat 1..21 is palette 0..20");
	Check(DyeIndex(22) == -1 && DyeIndex(40) == -1, "above 21 is no dye");
	Check(DyeIndex(-16) == 15 && DyeIndex(-11) == 20 && DyeIndex(-1) == -1 && DyeIndex(-31) == 0,
	      "five-bit signed values arrive as negatives: stats 16..21 come back as palette 15..20");
	Check(ComposeColour(1, 0) == 0x20 && ComposeColour(2, 5) == 0x45 && ComposeColour(7, 20) == 0xf4, "kind << 5 plus the colour");
	Check(ComposeColour(0, 3) == 0 && ComposeColour(9, 3) == 0 && ComposeColour(1, 21) == 0 && ComposeColour(1, -1) == 0, "no kind or an out-of-range colour gives 0");
	return failures;
}

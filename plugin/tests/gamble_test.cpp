// Tests of the gamble window rules (gamble_logic.h) and the small engine rules (engine_gaps_logic.h). Run through mxl_tests
// (requirements_test.cpp main).
#include "../src/engine_gaps_logic.h"
#include "../src/gamble_logic.h"

#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: %s\n", what);
	}
}
}  // namespace

int RunGambleTests() {
	using namespace gamble;

	// offer count: 14 in D2R, 32 in Median; the patch changes exactly the bound byte
	Check(VanillaOffers == 14 && MedianOffers == 32, "14 -> 32 offers");
	Check(OffersOk(32) && !OffersOk(14) && !OffersOk(0), "a window holds 32");
	bool onlyBound = true;
	for (size_t i = 0; i < sizeof(LoopBoundOriginal); ++i) {
		onlyBound = onlyBound && ((LoopBoundOriginal[i] == LoopBoundPatched[i]) == (i != 2));
	}
	Check(onlyBound, "only byte 2 (the cmp immediate) differs");
	Check(RingSlot == 0 && AmuletSlot == 1, "ring then amulet");

	// quality of one offer (0x541ab0): unique, then set, then rare, else magic; Median's 25000 / 1000 / 1000
	Check(QualityOfRoll(0, MedianRare, MedianSet, MedianUnique) == Unique, "roll 0 unique");
	Check(QualityOfRoll(999, MedianRare, MedianSet, MedianUnique) == Unique, "roll 999 unique");
	Check(QualityOfRoll(1000, MedianRare, MedianSet, MedianUnique) == Set, "roll 1000 set");
	Check(QualityOfRoll(1999, MedianRare, MedianSet, MedianUnique) == Set, "roll 1999 set");
	Check(QualityOfRoll(2000, MedianRare, MedianSet, MedianUnique) == Rare, "roll 2000 rare");
	Check(QualityOfRoll(26999, MedianRare, MedianSet, MedianUnique) == Rare, "roll 26999 rare");
	Check(QualityOfRoll(27000, MedianRare, MedianSet, MedianUnique) == Magic, "roll 27000 magic");
	Check(QualityOfRoll(99999, MedianRare, MedianSet, MedianUnique) == Magic, "roll 99999 magic");
	Check(QualityOfRoll(5, 0, 0, 0) == Magic, "no chances: always magic");
	Check(QualityOfRoll(0, 10000, 100, 50) == Unique && QualityOfRoll(60, 10000, 100, 50) == Set && QualityOfRoll(200, 10000, 100, 50) == Rare,
	      "D2R's own row (10000 / 100 / 50)");

	const Chances c = ChancesOf(MedianRare, MedianSet, MedianUnique);
	Check(c.rare == 25000 && c.set == 1000 && c.unique == 1000 && c.magic == 73000, "Median's chances");
	Check(ChancesOf(0, 0, 0).magic == RollRange, "no chances");
	Check(ChancesOf(90000, 20000, 0).magic == 0, "chances beyond the range leave no magic");

	// a roll counted over the whole range gives exactly the chances
	int counts[8] = {};
	for (int r = 0; r < RollRange; ++r) {
		++counts[QualityOfRoll(r, MedianRare, MedianSet, MedianUnique)];
	}
	Check(counts[Rare] == 25000 && counts[Set] == 1000 && counts[Unique] == 1000 && counts[Magic] == 73000, "roll range totals");

	Check(ShareInRange(80, 320, 250, 100) && !ShareInRange(10, 320, 250, 100) && !ShareInRange(0, 0, 250, 100), "share window");

	char line[200];
	FormatReport(line, sizeof(line), true);
	Check(std::strstr(line, "32") != nullptr && line[0] == 'o' && line[1] == 'n', "report on");
	FormatReport(line, sizeof(line), false);
	Check(std::strncmp(line, "off", 3) == 0, "report off");
	return failures;
}

int RunEngineGapsTests() {
	using namespace gaps;
	const int before = failures;

	Check(PlayerSlowFloor(0) == -12 && PlayerSlowFloor(1) == -25 && PlayerSlowFloor(2) == -50, "slow floor -12 / -25 / -50");
	Check(PlayerSlowFloor(7) == -50, "unknown difficulty: the hardest floor");

	Check(Hit20Value(5, Hit20FloorVanilla) == 5 && Hit20Value(0, Hit20FloorVanilla) == 1 && Hit20Value(-3, Hit20FloorVanilla) == 1, "D2R floor 1");
	Check(Hit20Value(5, Hit20FloorMedian) == 5 && Hit20Value(0, Hit20FloorMedian) == 0 && Hit20Value(-3, Hit20FloorMedian) == 0, "Median floor 0");

	// patch lengths match and the NOP block is all 0x90; the hit-20 patch is cmovg (0f 4f), not cmovl (0f 4c)
	Check(sizeof(Hit20FloorOriginal) == 7 && Hit20FloorOriginal[5] == 0x4c && Hit20FloorPatched[5] == 0x4f, "hit 20 patch swaps cmovl for cmovg");
	bool nops = true;
	for (const uint8_t b : RecastGuardPatched) {
		nops = nops && b == 0x90;
	}
	Check(nops && sizeof(RecastGuardPatched) == 13, "recast guard: 13 NOPs");
	Check(RecastGuardOriginal[0] == 0xe8 && RecastGuardOriginal[7] == 0x0f && RecastGuardOriginal[8] == 0x85, "recast guard is call / test / jne rel32");
	Check(sizeof(SlowFloorEntry) >= 5, "hook entry has at least 5 bytes");
	Check(SameBytes(Hit20FloorOriginal, Hit20FloorOriginal, sizeof(Hit20FloorOriginal)) && !SameBytes(Hit20FloorOriginal, Hit20FloorPatched, 7), "SameBytes");

	char line[256];
	FormatReport(line, sizeof(line), true, true, true, false, false, false);
	Check(std::strstr(line, "hit 20 floor 0x45edce on") != nullptr && std::strstr(line, "0x449f40 on") != nullptr, "report all on");
	FormatReport(line, sizeof(line), false, true, false, true, false, false);
	Check(std::strstr(line, "0x45edce off (word)") != nullptr && std::strstr(line, "0x449f40 FAILED") != nullptr, "report off / failed");
	return failures - before;
}

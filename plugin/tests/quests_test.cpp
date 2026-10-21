// Tests of the pure Act 1 quest rules (quests_logic.h). Run through mxl_tests (requirements_test.cpp main).
#include "../src/quests_logic.h"

#include <cstdio>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL (quests): %s\n", what);
	}
}

}  // namespace

int RunQuestsTests() {
	using namespace quests;
	// item codes are the engine's little-endian dwords (the hooks' constants in the dump: 0x205b6f33 = "3o[ ")
	static_assert(Code("3o[ ") == 0x205b6f33U);
	static_assert(Code("bkd ") == 0x20646b62U);
	static_assert(Code("bks ") == 0x20736b62U);
	Check(CodeCatalyst == 0x205b6f33U, "catalyst code");

	// Griswold's Heart: row and base per difficulty, none outside 0..2
	Check(HeartFor(0) != nullptr && HeartFor(0)->uniqueIndex == 69 && HeartFor(0)->baseCode == Code("600 "), "heart normal");
	Check(HeartFor(1) != nullptr && HeartFor(1)->uniqueIndex == 70 && HeartFor(1)->baseCode == Code("784 "), "heart nightmare");
	Check(HeartFor(2) != nullptr && HeartFor(2)->uniqueIndex == 71 && HeartFor(2)->baseCode == Code("@01 "), "heart hell");
	Check(HeartFor(3) == nullptr && HeartFor(-1) == nullptr, "heart out of range");

	// unique-spawned bitmap addressing (game + 0x3638, dword per 32 indexes)
	Check(UniqueSpawnedOffset(0) == 0x3638 && UniqueSpawnedMask(0) == 1U, "bitmap index 0");
	Check(UniqueSpawnedOffset(69) == 0x3638 + 8 && UniqueSpawnedMask(69) == (1U << 5), "bitmap index 69");
	Check(UniqueSpawnedOffset(71) == 0x3638 + 8 && UniqueSpawnedMask(71) == (1U << 7), "bitmap index 71");
	Check(UniqueSpawnedOffset(32) == 0x3638 + 4 && UniqueSpawnedMask(32) == 1U, "bitmap index 32");

	// Akara's reward: Akara, message 0x76, rescue bit set; every other combination is silent
	Check(ShouldGiveHeart(0x94, 0x76, true), "heart given");
	Check(!ShouldGiveHeart(0x94, 0x76, false), "heart needs the bit");
	Check(!ShouldGiveHeart(0x94, 0x70, true), "heart wrong message");
	Check(!ShouldGiveHeart(0x109, 0x76, true), "heart wrong npc (Cain)");
	Check(!ShouldGiveHeart(0x96, 0x76, true), "heart wrong npc (Kashya)");

	// Andariel: Normal only, class 156, once
	Check(ShouldGiveCatalystOnKill(0, 156, false), "andariel normal drops");
	Check(!ShouldGiveCatalystOnKill(1, 156, false), "andariel nightmare no drop");
	Check(!ShouldGiveCatalystOnKill(2, 156, false), "andariel hell no drop");
	Check(!ShouldGiveCatalystOnKill(0, 156, true), "andariel flag set: once");
	Check(!ShouldGiveCatalystOnKill(0, 155, false), "other monster no drop");

	// re-issue: flag set, Warriv, catalyst missing
	Check(ShouldReissueCatalyst(0x9b, true, false), "reissue");
	Check(!ShouldReissueCatalyst(0x9b, true, true), "reissue only when missing");
	Check(!ShouldReissueCatalyst(0x9b, false, false), "reissue needs the flag");
	Check(!ShouldReissueCatalyst(0x94, true, false), "reissue only at Warriv");

	// the plugin flag must stay inside the 96-byte quest record
	Check(FlagQuest * 16 + FlagCatalyst < 96 * 8, "flag inside the record");
	Check(FlagQuest > 41, "flag quest past Median's own ids");

	if (failures == 0) {
		std::printf("quests: all tests passed\n");
	}
	return failures;
}

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

	// ---- Acts 2-5 ----
	static_assert(Code("qum ") == 0x206d7571U);   // the dump's constant in D2Sigma 0x9ef90
	static_assert(Code("0o[ ") == 0x205b6f30U);   // 0x9ee40
	static_assert(Code("jcat") == 0x7461636aU);   // 0x9f530
	static_assert(Code("qrel") == 0x6c657271U);   // 0x9e580
	static_assert(Code("r08 ") == 0x20383072U);   // 0x9f7c0 after its byte swap
	Check(FlagLearning != FlagCatalyst && FlagJewelcraft != FlagCatalyst && FlagLearning != FlagJewelcraft, "flag bits differ");
	Check(FlagQuest * 16 + FlagJewelcraft < 96 * 8, "all flags inside the record");

	// Summoner / Izual: Nightmare only, right class, once
	Check(ShouldGiveLearningOnKill(1, 250, false), "summoner nightmare drops");
	Check(!ShouldGiveLearningOnKill(0, 250, false) && !ShouldGiveLearningOnKill(2, 250, false), "summoner other difficulties");
	Check(!ShouldGiveLearningOnKill(1, 250, true), "summoner once");
	Check(!ShouldGiveLearningOnKill(1, 256, false), "summoner class only");
	Check(ShouldGiveJewelcraftOnKill(1, 256, false), "izual nightmare drops");
	Check(!ShouldGiveJewelcraftOnKill(0, 256, false) && !ShouldGiveJewelcraftOnKill(2, 256, false), "izual other difficulties");
	Check(!ShouldGiveJewelcraftOnKill(1, 256, true), "izual once");
	Check(!ShouldGiveJewelcraftOnKill(1, 250, false), "izual class only");

	// re-issue
	Check(ShouldReissueLearning(true, false), "learning reissue");
	Check(!ShouldReissueLearning(true, true) && !ShouldReissueLearning(false, false), "learning reissue conditions");
	Check(ShouldReissueJewelcraft(367, true, false), "jewelcraft reissue at Tyrael");
	Check(!ShouldReissueJewelcraft(367, true, true) && !ShouldReissueJewelcraft(367, false, false), "jewelcraft reissue conditions");
	Check(!ShouldReissueJewelcraft(0x196, true, false), "jewelcraft reissue only at Tyrael (not izualghost 406)");

	// Jerhyn, Cain, Ormus
	Check(ShouldGiveCrystalJerhyn(201, 0x1ba, true), "jerhyn crystal");
	Check(!ShouldGiveCrystalJerhyn(201, 0x1ba, false) && !ShouldGiveCrystalJerhyn(201, 0x1ae, true) && !ShouldGiveCrystalJerhyn(210, 0x1ba, true),
	      "jerhyn crystal conditions");
	Check(ShouldGiveCrystalCain(245, 0x272, false, true), "cain crystal");
	Check(!ShouldGiveCrystalCain(245, 0x272, true, true) && !ShouldGiveCrystalCain(245, 0x272, false, false) &&
	          !ShouldGiveCrystalCain(244, 0x272, false, true) && !ShouldGiveCrystalCain(245, 0x252, false, true),
	      "cain crystal conditions");
	Check(ShouldGiveShrines(255, 0x251, true, false, false), "ormus shrines");
	Check(!ShouldGiveShrines(255, 0x251, false, false, false) && !ShouldGiveShrines(255, 0x251, true, true, false) &&
	          !ShouldGiveShrines(255, 0x251, true, false, true) && !ShouldGiveShrines(255, 0x24b, true, false, false),
	      "ormus shrines conditions");
	Check(ShrineCount(0) == 1 && ShrineCount(1) == 3 && ShrineCount(2) == 5 && ShrineCount(3) == 0 && ShrineCount(-1) == 0, "shrine counts");
	bool distinct = true;
	for (int i = 0; i < ShrineCodeCount; ++i) {
		for (int j = i + 1; j < ShrineCodeCount; ++j) {
			distinct = distinct && ShrineCodes[i] != ShrineCodes[j];
		}
	}
	Check(distinct && ShrineCodes[0] == Code("A0+ ") && ShrineCodes[15] == Code("R0+ "), "shrine codes");

	// Qual-Kehk
	Check(ShouldGiveRunes(515, 0x4e8e, false, true), "runes");
	Check(!ShouldGiveRunes(515, 0x4e8e, true, true) && !ShouldGiveRunes(515, 0x4e8e, false, false) &&
	          !ShouldGiveRunes(514, 0x4e8e, false, true) && !ShouldGiveRunes(515, 0x4e80, false, true),
	      "runes conditions");
	Check(RuneCount(false, false) == 3 && RuneCount(true, false) == 2 && RuneCount(false, true) == 2 && RuneCount(true, true) == 1,
	      "rune counts");
	Check(RescueRunes[0][0] == Code("r08 ") && RescueRunes[0][2] == Code("r10 ") && RescueRunes[1][0] == Code("r15 ") &&
	          RescueRunes[2][2] == Code("r24 "),
	      "rune table");

	// Baal and the relic orb
	Check(IsBaalHellKill(2, 544) && !IsBaalHellKill(1, 544) && !IsBaalHellKill(0, 544) && !IsBaalHellKill(2, 543), "baal hell kill");
	Check(ShouldGiveRelic(110, 132, false), "relic at 110 in the chamber");
	Check(!ShouldGiveRelic(109, 132, false), "relic needs level above 109");
	Check(!ShouldGiveRelic(110, 131, false) && !ShouldGiveRelic(110, -1, false), "relic needs the chamber");
	Check(!ShouldGiveRelic(110, 132, true), "relic once");

	// quest 41
	Check(RespecRearms(51) && !RespecRearms(50) && !RespecRearms(1), "respec re-arm above level 50");

	// bonus drops: once per player on the ground, rows of the NPCs that carry the classes
	Check(DropCount(0) == 1 && DropCount(1) == 1 && DropCount(3) == 3 && DropCount(8) == 8 && DropCount(40) == 8, "drops per player");
	Check(TcCatalyst.row == 155 && TcCatalyst.difficulty == 0 && TcLearning.row == 177 && TcLearning.difficulty == 1 &&
	          TcJewelcraft.row == NpcTyraelAct4 && TcJewelcraft.difficulty == 1,
	      "bonus class rows");

	if (failures == 0) {
		std::printf("quests: all tests passed\n");
	}
	return failures;
}

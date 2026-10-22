// Pure rules of the Act 1 quest changes (quests_act1.cpp). No engine access, so mxl_tests can run them.
// Source: D2Sigma's D2Quests.cpp hooks 0x9e730 (Akara), 0x9e9f0 (Andariel), 0x9eb40 (catalyst re-issue);
// docs/subsystems/quests-requirements.md section 2.2, docs/d2r-addresses.md section 2.
#pragma once

#include <cstdint>

namespace quests {

// 4-character item code as the engine passes it (little endian, space padded): "3o[ " -> 0x205b6f33.
constexpr auto Code(const char (&s)[5]) noexcept -> uint32_t {
	return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) | static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8 |
	       static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16 | static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24;
}

constexpr uint16_t NpcAkara = 0x94;       // a1q4 and D2Sigma 0xbf200: Akara
constexpr uint16_t NpcWarriv = 0x9b;      // D2Sigma 0xbff40(0): the NPC whose activation re-issues the catalyst
constexpr uint16_t MsgCainRescued = 0x76; // Akara's dialog message after the rescue (quest 4 bit 1)
constexpr int32_t MonsterAndariel = 0x9c; // D2Sigma 0xbf240
constexpr uint32_t CodeCatalyst = Code("3o[ ");  // catalyst of disenchantment (misc row catalyst_of_disenchantment)

constexpr int QuestSearchForCain = 4;
constexpr int BitCainRescued = 1;

// Plugin-owned flag "Andariel's catalyst was handed out". D2Sigma keeps it in the quest object (lost with the game);
// here it is a bit in the player's quest record of that difficulty (saved). Quest id 47 is past the highest id any
// code uses (Median's extra ids stop at 41), bit 0 of its 16.
constexpr int FlagQuest = 47;
constexpr int FlagCatalyst = 0;

// Griswold's Heart: one unique per difficulty (uniqueitems rows GriswoldsHeart, (N), (H) = D2R rows 69, 70, 71, i.e.
// line number - 2 in work/mod uniqueitems.txt), base armor codes from the same rows (600, 784, @01). D2Sigma picks the
// row from the game difficulty (0x9e810: string table at 0x1018fd54).
struct Heart {
	int32_t uniqueIndex;
	uint32_t baseCode;
};
constexpr Heart Hearts[3] = {{69, Code("600 ")}, {70, Code("784 ")}, {71, Code("@01 ")}};

constexpr auto HeartFor(int difficulty) noexcept -> const Heart* {
	return difficulty >= 0 && difficulty < 3 ? &Hearts[difficulty] : nullptr;
}

// D2R tracks "this unique already exists in the game" in a bitmap at game+0x3638 (dwords, bit = 1 << (index & 31));
// creation of an index that is set fails. Where the bit lives:
constexpr int UniqueSpawnedBase = 0x3638;
constexpr int UniqueSpawnedLimit = 0x1000;  // indexes above are not tracked (0x443910 tests "< 0x1001")
constexpr auto UniqueSpawnedOffset(int32_t index) noexcept -> int { return UniqueSpawnedBase + (index >> 5) * 4; }
constexpr auto UniqueSpawnedMask(int32_t index) noexcept -> uint32_t { return 1U << (index & 31); }

// Akara (quest 4 event 11): message 0x76 with the rescue bit set. The vanilla handler follows and clears the bit.
constexpr auto ShouldGiveHeart(uint16_t npcClass, uint16_t message, bool rescuedBit) noexcept -> bool {
	return npcClass == NpcAkara && message == MsgCainRescued && rescuedBit;
}

// Median's bonus treasure classes are data: three monstats rows of NPCs carry them (D2Quests.cpp 0x9ec20 / 0x9ef10 /
// 0x9f600 read word +0x86 / +0x8e / +0x8e of the row, i.e. TreasureClass1 or TreasureClass1(N)). The handlers roll that
// class once for every player in the game, on the ground at the victim, item level = the victim's level (0x9e9f0 /
// 0x9ed00 loop to the game's player count at game+0x8c), and set the "handed out" flag afterwards whatever dropped.
struct BonusTc {
	int32_t row;     // monstats row that holds the class name
	int difficulty;  // column set: 0 = TreasureClass1, 1 = TreasureClass1(N), 2 = TreasureClass1(H)
};
constexpr BonusTc TcCatalyst{155, 0};    // warriv1:  "Quest Cata Disenchant" (3o[ )
constexpr BonusTc TcLearning{177, 1};    // drognan:  "Quest Cata Learning" in TreasureClass1(N) (0o[ )
constexpr BonusTc TcJewelcraft{367, 1};  // tyrael2:  "Quest Cata Jewelcraft" in TreasureClass1(N) (jcat)
constexpr int MaxDropPlayers = 8;
// How many times the class is rolled: once per player in the game, at least once.
constexpr auto DropCount(int players) noexcept -> int {
	return players < 1 ? 1 : (players > MaxDropPlayers ? MaxDropPlayers : players);
}

// Quest 6 event 8 (a kill while the quest is active): Andariel in Normal, once (the flag is set afterwards).
constexpr auto ShouldGiveCatalystOnKill(int difficulty, int32_t monsterClass, bool flagSet) noexcept -> bool {
	return difficulty == 0 && monsterClass == MonsterAndariel && !flagSet;
}

// Quest 6 event 0 (the player activates an NPC): flag set, NPC is Warriv, catalyst missing.
constexpr auto ShouldReissueCatalyst(int32_t npcClass, bool flagSet, bool hasCatalyst) noexcept -> bool {
	return flagSet && npcClass == NpcWarriv && !hasCatalyst;
}

// Quest 5 (The Forgotten Tower) event 8, the kill of the Countess. D2Sigma 0x9e8b0 runs in front of the vanilla kill
// handler and skips it when the killer's record of the game's difficulty already holds quest 5 as done: bit 0 (reward
// granted), 0xe (completed in this game) or 0xf (completed in an earlier game), the three tests of 0xbec00. The vanilla
// handler would otherwise run its finish branch again (state 5, bit 0xd cleared, bit 0xd and 0 set for every party member
// without bit 0) for a killer who is long done.
constexpr int QuestForgottenTower = 5;
constexpr int MonsterCountess = 45;  // monstats row of the Countess (the harness counter counts her kills only)
constexpr int BitTowerGranted = 0;
constexpr int BitTowerCompletedNow = 0xe;
constexpr int BitTowerCompletedBefore = 0xf;

constexpr auto SkipTowerKill(bool granted, bool completedNow, bool completedBefore) noexcept -> bool {
	return granted || completedNow || completedBefore;
}

// ---------------------------------------------------------------------------------------------------------------------
// Acts 2-5 (quests_act2_5.cpp). Sources: D2Sigma D2Quests.cpp 0x9ed00 / 0x9ee40 (Summoner), 0x9ef90 (Jerhyn), 0x9f030 /
// 0x9f110 (Ormus), 0x9f340 (Cain), 0x9f3f0 / 0x9f530 (Izual), 0x9f7c0 (Qual-Kehk), 0x9fa80 / 0x9e580 (Baal), 0x9e600
// (quest 41). NPC and monster class ids are the same rows in Median's and D2R's monstats (checked for each in D2R's
// extracted monstats.txt).

constexpr uint16_t NpcJerhyn = 201;        // 0xc9
constexpr uint16_t NpcOrmus = 255;         // 0xff
constexpr uint16_t NpcCainAct3 = 245;      // 0xf5
constexpr uint16_t NpcQualKehk = 515;      // 0x203
constexpr int32_t NpcTyraelAct4 = 367;     // 0x16f (monstats tyrael2; D2Sigma 0xbfd80(3))
constexpr int32_t MonsterSummoner = 250;   // 0xfa
constexpr int32_t MonsterIzual = 256;      // 0x100
constexpr int32_t MonsterBaal = 544;       // 0x220 (baalcrab)

constexpr uint16_t MsgJerhynCrystal = 0x1ba;
constexpr uint16_t MsgOrmusShrines = 0x251;
constexpr uint16_t MsgCainCrystal = 0x272;
constexpr uint16_t MsgQualKehkRunes = 0x4e8e;

constexpr uint32_t CodeCatalystLearning = Code("0o[ ");
constexpr uint32_t CodeCatalystJewelcraft = Code("jcat");
constexpr uint32_t CodeArcaneCrystal = Code("qum ");
constexpr uint32_t CodeRelicOrb = Code("qrel");

constexpr int QuestDuriel = 14;       // a2q6; bit 3 = Duriel dead, talk to Jerhyn (the vanilla handler clears it)
constexpr int BitDurielTalk = 3;
constexpr int QuestBlade = 19;        // a3q3 (Blade of the Old Religion)
constexpr int BitBladeReturned = 6;   // handed over to Ormus
constexpr int BitBladeRewarded = 8;   // the vanilla handler sets it after the hand-over
constexpr int BitBladeDone = 0;
constexpr int QuestKhalimsWill = 21;  // a3q5
constexpr int BitKhalimDone = 0;
constexpr int BitKhalimTalk = 4;      // the vanilla handler accepts Cain's message 0x272 only with it set
constexpr int QuestRescue = 36;       // a5q2 (Rescue on Mount Arreat)
constexpr int BitRescueDone = 0;
constexpr int BitRescueReady = 1;     // set when the barbarians are free; the vanilla handler pays out and swaps the bits
constexpr int BitRescueTwo = 6;
constexpr int BitRescueOne = 7;
constexpr int QuestBaal = 40;         // a5q6
constexpr int BitBaalRelic = 0xb;     // Median's own once-per-player mark (unused by D2R)
constexpr int QuestRespec = 41;       // Median's level flag (respec token: bit 1; bit 0 = used)
constexpr int BitRespecUsed = 0;
constexpr int BitRespecReady = 1;
constexpr int BitRespecKeep = 0xf;
constexpr int QuestRespecObject = 0x1e;  // quest object that the bookkeeping switches off (0x518220 looks it up by id)

// Plugin-owned "bonus catalyst handed out" flags: bits of quest record 47 (bit 0 is Act 1's Andariel flag). Each
// difficulty has its own record, so a Nightmare flag lives in the Nightmare record only.
constexpr int FlagLearning = 1;    // Summoner, Nightmare
constexpr int FlagJewelcraft = 2;  // Izual, Nightmare

constexpr int RespecLevelAbove = 50;  // D2Sigma 0x9e600: player level > 0x32
constexpr int RelicLevelAbove = 109;  // 0x9e580: level > 0x6d
constexpr int AreaWorldstoneChamber = 132;
constexpr int RelicItemLevel = 125;   // 0x7d

// Quest 13 event 8: the Summoner dies in Nightmare, once.
constexpr auto ShouldGiveLearningOnKill(int difficulty, int32_t monsterClass, bool flagSet) noexcept -> bool {
	return difficulty == 1 && monsterClass == MonsterSummoner && !flagSet;
}

// Quest 25 event 8: Izual dies in Nightmare, once.
constexpr auto ShouldGiveJewelcraftOnKill(int difficulty, int32_t monsterClass, bool flagSet) noexcept -> bool {
	return difficulty == 1 && monsterClass == MonsterIzual && !flagSet;
}

// Quest 13 event 0 (any NPC of the act is activated; D2Sigma 0x9ee40 tests no class): flag set and catalyst missing.
constexpr auto ShouldReissueLearning(bool flagSet, bool hasCatalyst) noexcept -> bool {
	return flagSet && !hasCatalyst;
}

// Quest 25 event 0: flag set, the NPC is Tyrael (D2Sigma 0xbfd80(3)), catalyst missing.
constexpr auto ShouldReissueJewelcraft(int32_t npcClass, bool flagSet, bool hasCatalyst) noexcept -> bool {
	return flagSet && npcClass == NpcTyraelAct4 && !hasCatalyst;
}

// Quest 14 event 11: Jerhyn's message 0x1ba. D2Sigma gives the crystal without looking at the quest; the vanilla handler
// acts only when bit 3 is set (and clears it), so the reward follows that to stay one per quest.
constexpr auto ShouldGiveCrystalJerhyn(uint16_t npcClass, uint16_t message, bool talkBit) noexcept -> bool {
	return npcClass == NpcJerhyn && message == MsgJerhynCrystal && talkBit;
}

// Quest 19 event 11: Ormus, message 0x251, "returned" bit 6. The vanilla handler also needs the quest open (bit 0
// clear) and the reward bit 8 clear; D2Sigma tests only bit 6.
constexpr auto ShouldGiveShrines(uint16_t npcClass, uint16_t message, bool returnedBit, bool rewardedBit, bool doneBit) noexcept -> bool {
	return npcClass == NpcOrmus && message == MsgOrmusShrines && returnedBit && !rewardedBit && !doneBit;
}

// Number of shrine items by difficulty (1 / 3 / 5).
constexpr auto ShrineCount(int difficulty) noexcept -> int {
	return difficulty == 0 ? 1 : difficulty == 1 ? 3 : difficulty == 2 ? 5 : 0;
}

// The 16 shrine consumables (misc rows of item type 244 "flsh", the *_shrine_10 items), codes padded with a space.
constexpr int ShrineCodeCount = 16;
constexpr uint32_t ShrineCodes[ShrineCodeCount] = {Code("A0+ "), Code("B0+ "), Code("C0+ "), Code("D0+ "), Code("E0+ "), Code("F0+ "),
                                                   Code("G0+ "), Code("H0+ "), Code("I0+ "), Code("J0+ "), Code("K0+ "), Code("M0+ "),
                                                   Code("N0+ "), Code("O0+ "), Code("Q0+ "), Code("R0+ ")};

// Quest 21 event 11: Cain (act 3), message 0x272. D2Sigma: quest bit 0 clear; the vanilla handler also needs bit 4.
constexpr auto ShouldGiveCrystalCain(uint16_t npcClass, uint16_t message, bool doneBit, bool talkBit) noexcept -> bool {
	return npcClass == NpcCainAct3 && message == MsgCainCrystal && !doneBit && talkBit;
}

// Quest 36 event 11: Qual-Kehk, message 0x4e8e, bit 0 clear and bit 1 set.
constexpr auto ShouldGiveRunes(uint16_t npcClass, uint16_t message, bool doneBit, bool readyBit) noexcept -> bool {
	return npcClass == NpcQualKehk && message == MsgQualKehkRunes && !doneBit && readyBit;
}

// Runes of the difficulty: three each (r08 r09 r10 / r15 r16 r17 / r22 r23 r24).
constexpr uint32_t RescueRunes[3][3] = {{Code("r08 "), Code("r09 "), Code("r10 ")},
                                        {Code("r15 "), Code("r16 "), Code("r17 ")},
                                        {Code("r22 "), Code("r23 "), Code("r24 ")}};

// How many of the three: 3, 2 with bit 6, one less with bit 7 (D2Sigma 0x9f7c0).
constexpr auto RuneCount(bool bitTwo, bool bitOne) noexcept -> int {
	return (bitTwo ? 2 : 3) - (bitOne ? 1 : 0);
}

// Quest 40 event 8, after the vanilla handler: Baal dies in Hell.
constexpr auto IsBaalHellKill(int difficulty, int32_t monsterClass) noexcept -> bool {
	return difficulty == 2 && monsterClass == MonsterBaal;
}

// D2Sigma 0x9e580, for each player of the game: level above 109, in the Worldstone Chamber, mark not yet set.
constexpr auto ShouldGiveRelic(int level, int area, bool markSet) noexcept -> bool {
	return level > RelicLevelAbove && area == AreaWorldstoneChamber && !markSet;
}

// D2Sigma 0x9e600 (replaces the respec bookkeeping 0x5d9ae0): above level 50 the respec token is put back.
constexpr auto RespecRearms(int level) noexcept -> bool {
	return level > RespecLevelAbove;
}

}  // namespace quests

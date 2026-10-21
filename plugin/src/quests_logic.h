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

// Quest 6 event 8 (a kill while the quest is active): Andariel in Normal, once (the flag is set afterwards).
constexpr auto ShouldGiveCatalystOnKill(int difficulty, int32_t monsterClass, bool flagSet) noexcept -> bool {
	return difficulty == 0 && monsterClass == MonsterAndariel && !flagSet;
}

// Quest 6 event 0 (the player activates an NPC): flag set, NPC is Warriv, catalyst missing.
constexpr auto ShouldReissueCatalyst(int32_t npcClass, bool flagSet, bool hasCatalyst) noexcept -> bool {
	return flagSet && npcClass == NpcWarriv && !hasCatalyst;
}

}  // namespace quests

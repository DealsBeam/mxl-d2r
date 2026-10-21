// Engine access shared by the quest hooks of Median's Act 1 (quests_act1.cpp) and Acts 2-5 (quests_act2_5.cpp).
// Addresses and layouts are D2R build 93847 (docs/d2r-addresses.md section 2); nothing here may be used unless the image
// check of mxl_core.cpp passed.
#pragma once

#include "d2r.h"
#include "quests_logic.h"

#include <cstdint>

namespace questeng {

using d2r::Game;
using d2r::Unit;

inline constexpr uint64_t CreateQuestItemRva = 0x517530;  // (game, player, code, ilvl, quality, dropIfFull) -> item or null
inline constexpr uint64_t FindItemRva = 0x441980;         // (game, player, code) -> item or null
inline constexpr uint64_t QuestBitTestRva = 0x325c50;     // (record, quest, bit)
inline constexpr uint64_t QuestBitSetRva = 0x325c00;
inline constexpr uint64_t QuestBitClearRva = 0x325990;

inline constexpr uint64_t MonStatsRecordRva = 0x976e0;   // (dataFlag, class) -> monstats record or null
inline constexpr uint64_t TcDropRva = 0x441300;           // (game, victim, killer, tc index (u16), 0, ilvl, 0, 0, 0, 0)
inline constexpr uint64_t DefaultIlvlRva = 0x43e8a0;      // (unit, 0) -> max(1, the unit's level) for a monster

inline constexpr int GameDifficulty = 0x104;  // byte
inline constexpr int GameDataFlag = 0x106;    // byte, first argument of the table getters
inline constexpr int GameClients = 0x138;     // first client record; +0x628 next (goblins.cpp)
inline constexpr int ClientNext = 0x628;
inline constexpr int ClientState = 0x04;      // 4 = in the game
// monstats record: treasure class words, u16 each, 0x10 bytes per difficulty (0x447b80 reads record + 0xc6 + difficulty *
// 0x10 = TreasureClass4, the quest one): TreasureClass1..4 of normal are +0xc0..+0xc6, of nightmare +0xd0.., of hell +0xe0..
// (dump mon 155 / 177 / 367: 0x242 / 0x243 / 0x244 at +0xc0 / +0xd0 / +0xd0, game_checks 2026-10-22).
inline constexpr int MonStatsTc1 = 0xc0;
inline constexpr int MonStatsTcPerDifficulty = 0x10;
inline constexpr int UnitClass = 0x04;        // u32 (0x349860)
inline constexpr int UnitPlayerData = 0x10;   // player: data block (0x34b240); +0x40 + difficulty * 8 -> quest record
inline constexpr int EventUnit = 0x10;        // quest event block: the NPC or the victim
inline constexpr int EventPlayer = 0x18;
inline constexpr int EventNpcClass = 0x28;    // u16
inline constexpr int EventMessage = 0x30;     // u16

using MonStatsFn = const uint8_t* (*)(uint32_t dataFlag, int32_t monsterClass);
using TcDropFn = void (*)(Game* game, Unit* victim, Unit* killer, uint16_t tc, int32_t quality, int32_t itemLevel, int32_t arg7,
                          int64_t arg8, int64_t arg9, int32_t arg10);
using DefaultIlvlFn = int64_t (*)(Unit* unit, int32_t arg);
using CreateItemFn = Unit* (*)(Game* game, Unit* player, uint32_t code, int32_t itemLevel, int32_t quality, int32_t dropIfFull);
using FindItemFn = Unit* (*)(Game* game, Unit* player, uint32_t code);
using BitFn = int32_t (*)(void* record, int32_t quest, int32_t bit);

struct Api {
	CreateItemFn createItem = nullptr;
	FindItemFn findItem = nullptr;
	BitFn bitTest = nullptr;
	BitFn bitSet = nullptr;
	BitFn bitClear = nullptr;
	MonStatsFn monStats = nullptr;
	TcDropFn tcDrop = nullptr;
	DefaultIlvlFn defaultIlvl = nullptr;
};
inline Api api;

inline void Bind(uint64_t exeBase) noexcept {
	api.createItem = reinterpret_cast<CreateItemFn>(exeBase + CreateQuestItemRva);
	api.findItem = reinterpret_cast<FindItemFn>(exeBase + FindItemRva);
	api.bitTest = reinterpret_cast<BitFn>(exeBase + QuestBitTestRva);
	api.bitSet = reinterpret_cast<BitFn>(exeBase + QuestBitSetRva);
	api.bitClear = reinterpret_cast<BitFn>(exeBase + QuestBitClearRva);
	api.monStats = reinterpret_cast<MonStatsFn>(exeBase + MonStatsRecordRva);
	api.tcDrop = reinterpret_cast<TcDropFn>(exeBase + TcDropRva);
	api.defaultIlvl = reinterpret_cast<DefaultIlvlFn>(exeBase + DefaultIlvlRva);
}

inline auto Difficulty(Game* game) noexcept -> int {
	return d2r::At<uint8_t>(game, GameDifficulty);
}

// Quest record of the player for the game's difficulty (the argument of the quest bit functions), or null.
inline auto Record(Game* game, Unit* player) noexcept -> void* {
	const auto* data = player != nullptr ? d2r::At<const uint8_t*>(player, UnitPlayerData) : nullptr;
	return data != nullptr ? d2r::At<void*>(data, 0x40 + Difficulty(game) * 8) : nullptr;
}

// Players in the game: the client records in state 4.
inline auto PlayersInGame(Game* game) noexcept -> int {
	int n = 0, guard = 0;
	for (auto* client = d2r::At<uint8_t*>(game, GameClients); client != nullptr && guard < 64;
	     client = d2r::At<uint8_t*>(client, ClientNext), ++guard) {
		if (d2r::At<int32_t>(client, ClientState) == 4) {
			++n;
		}
	}
	return n;
}

// The treasure class index a BonusTc names (the monstats row's word), or 0 when the row or the word is missing.
inline auto BonusTcIndex(Game* game, const quests::BonusTc& tc) noexcept -> uint16_t {
	const uint8_t* record = api.monStats(d2r::At<uint8_t>(game, GameDataFlag), tc.row);
	return record != nullptr && tc.difficulty >= 0 && tc.difficulty < 3
	           ? d2r::At<uint16_t>(record, MonStatsTc1 + tc.difficulty * MonStatsTcPerDifficulty)
	           : 0;
}

// Median's bonus drop: the class is rolled once per player in the game, on the ground at the victim, item level = the
// victim's level. Returns the number of rolls, 0 when the class could not be resolved (the caller then hands the item out
// directly as before).
inline auto DropBonusPerPlayer(Game* game, Unit* victim, Unit* killer, const quests::BonusTc& tc) noexcept -> int {
	const uint16_t index = BonusTcIndex(game, tc);
	if (index == 0 || victim == nullptr) {
		return 0;
	}
	const int32_t ilvl = static_cast<int32_t>(api.defaultIlvl(victim, 0));
	const int rolls = quests::DropCount(PlayersInGame(game));
	for (int i = 0; i < rolls; ++i) {
		api.tcDrop(game, victim, killer, index, 0, ilvl, 0, 0, 0, 0);
	}
	return rolls;
}

inline auto Bit(void* record, int quest, int bit) noexcept -> bool {
	return record != nullptr && api.bitTest(record, quest, bit) != 0;
}

}  // namespace questeng

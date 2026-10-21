// Engine access shared by the quest hooks of Median's Act 1 (quests_act1.cpp) and Acts 2-5 (quests_act2_5.cpp).
// Addresses and layouts are D2R build 93847 (docs/d2r-addresses.md section 2); nothing here may be used unless the image
// check of mxl_core.cpp passed.
#pragma once

#include "d2r.h"

#include <cstdint>

namespace questeng {

using d2r::Game;
using d2r::Unit;

inline constexpr uint64_t CreateQuestItemRva = 0x517530;  // (game, player, code, ilvl, quality, dropIfFull) -> item or null
inline constexpr uint64_t FindItemRva = 0x441980;         // (game, player, code) -> item or null
inline constexpr uint64_t QuestBitTestRva = 0x325c50;     // (record, quest, bit)
inline constexpr uint64_t QuestBitSetRva = 0x325c00;
inline constexpr uint64_t QuestBitClearRva = 0x325990;

inline constexpr int GameDifficulty = 0x104;  // byte
inline constexpr int UnitClass = 0x04;        // u32 (0x349860)
inline constexpr int UnitPlayerData = 0x10;   // player: data block (0x34b240); +0x40 + difficulty * 8 -> quest record
inline constexpr int EventUnit = 0x10;        // quest event block: the NPC or the victim
inline constexpr int EventPlayer = 0x18;
inline constexpr int EventNpcClass = 0x28;    // u16
inline constexpr int EventMessage = 0x30;     // u16

using CreateItemFn = Unit* (*)(Game* game, Unit* player, uint32_t code, int32_t itemLevel, int32_t quality, int32_t dropIfFull);
using FindItemFn = Unit* (*)(Game* game, Unit* player, uint32_t code);
using BitFn = int32_t (*)(void* record, int32_t quest, int32_t bit);

struct Api {
	CreateItemFn createItem = nullptr;
	FindItemFn findItem = nullptr;
	BitFn bitTest = nullptr;
	BitFn bitSet = nullptr;
	BitFn bitClear = nullptr;
};
inline Api api;

inline void Bind(uint64_t exeBase) noexcept {
	api.createItem = reinterpret_cast<CreateItemFn>(exeBase + CreateQuestItemRva);
	api.findItem = reinterpret_cast<FindItemFn>(exeBase + FindItemRva);
	api.bitTest = reinterpret_cast<BitFn>(exeBase + QuestBitTestRva);
	api.bitSet = reinterpret_cast<BitFn>(exeBase + QuestBitSetRva);
	api.bitClear = reinterpret_cast<BitFn>(exeBase + QuestBitClearRva);
}

inline auto Difficulty(Game* game) noexcept -> int {
	return d2r::At<uint8_t>(game, GameDifficulty);
}

// Quest record of the player for the game's difficulty (the argument of the quest bit functions), or null.
inline auto Record(Game* game, Unit* player) noexcept -> void* {
	const auto* data = player != nullptr ? d2r::At<const uint8_t*>(player, UnitPlayerData) : nullptr;
	return data != nullptr ? d2r::At<void*>(data, 0x40 + Difficulty(game) * 8) : nullptr;
}

inline auto Bit(void* record, int quest, int bit) noexcept -> bool {
	return record != nullptr && api.bitTest(record, quest, bit) != 0;
}

}  // namespace questeng

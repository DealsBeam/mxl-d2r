// Median's dungeon lockout and level entry requirements: the pure part of dungeon_lockout.cpp (unit-tested in
// tests/dungeon_test.cpp). Source: D2Sigma D2DrlgWarp.cpp 0x5d810 (warp test), 0x5cee0 (requirements), D2Dungeon.cpp
// 0x5d9d0 / 0x5df20 (clock start), 0x5e180 (expiry); docs/subsystems/ubers-events.md section 3.
//
// Per game and per level a clock: it starts when a player enters a level that has a lock time (stairs, tile warps and
// portals alike) unless it already runs; when it runs out the level (and every level of its lock group) is sealed for
// the rest of the game: nobody may enter it again. Entry also needs the level's minimum difficulty, the player's
// level for the difficulty and the class lock. D2Sigma ticks with game events; here the end time is compared lazily.
#pragma once

#include "mxl_dungeons.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace dungeon {

using mxl_dungeons::Entry;

inline auto Find(int32_t level) noexcept -> const Entry* {
	const Entry* b = std::begin(mxl_dungeons::Entries);
	const Entry* e = std::end(mxl_dungeons::Entries);
	const Entry* it = std::lower_bound(b, e, level, [](const Entry& x, int32_t l) { return x.level < l; });
	return it != e && it->level == level ? it : nullptr;
}

struct Who {
	int32_t level;       // player level
	int32_t cls;         // player class, 0 Amazon .. 6 Assassin
	int32_t difficulty;  // game difficulty, 0 Normal .. 2 Hell
};

// D2Sigma 0x5cee0: refused when the player's level is below the level for this difficulty, the difficulty is below the
// level's minimum, or the level is locked to another class (class lock value = class + 1).
inline auto MeetsRequirements(const Entry& e, const Who& who) noexcept -> bool {
	if (who.difficulty < 0 || who.difficulty > 2) {
		return false;
	}
	if (who.level < e.levelReq[who.difficulty] || who.difficulty < e.minDifficulty) {
		return false;
	}
	return e.classLock == 0 || who.cls + 1 == e.classLock;
}

// D2Sigma 0x5d810, the quest-table condition: the destination's leveldefs row names a quest (QuestFlagEx, Entry::questLock,
// 0 = none) and the level is refused unless the player's quest record of the game's difficulty holds one of bits 1, 0, 0xe
// or 0xf of it (reward pending / granted, completed in this game / earlier). A player without a record is not refused.
struct QuestBits {
	bool haveRecord = false;
	bool pending = false;          // bit 1
	bool granted = false;          // bit 0
	bool completedNow = false;     // bit 0xe
	bool completedBefore = false;  // bit 0xf
};
inline constexpr int QuestBitCount = 4;
inline constexpr int QuestBitList[QuestBitCount] = {1, 0, 0xe, 0xf};

inline auto QuestAllows(const Entry& e, const QuestBits& q) noexcept -> bool {
	if (e.questLock == 0 || !q.haveRecord) {
		return true;
	}
	return q.pending || q.granted || q.completedNow || q.completedBefore;
}

enum class Verdict { Allow, Requirements, Quest, Sealed };

// One game's clocks. Times are milliseconds on any monotonic clock.
class Lockout {
public:
	// Call with the game of every warp / portal use. A different game pointer is a new game, and so is a frame counter
	// that went backwards (a destroyed game's memory reused), all clocks reset.
	void Touch(const void* game, uint32_t frame) noexcept {
		if (game != game_ || frame < frame_) {
			game_ = game;
			Reset();
		}
		frame_ = frame;
	}
	// The game is over (game-end hook): its clocks go, if they are that game's.
	void Forget(const void* game) noexcept {
		if (game != nullptr && game == game_) {
			game_ = nullptr;
			frame_ = 0;
			Reset();
		}
	}
	void Reset() noexcept {
		for (Slot& s : slots_) {
			s = Slot{};
		}
	}
	// The level's clock has run out (D2Sigma flag bit 2).
	auto Sealed(int32_t level, uint64_t now) const noexcept -> bool {
		const Entry* e = Find(level);
		if (e == nullptr) {
			return false;
		}
		const Slot& s = slots_[static_cast<size_t>(e - std::begin(mxl_dungeons::Entries))];
		return s.running && now >= s.end;
	}
	auto Running(int32_t level, uint64_t now) const noexcept -> bool {
		const Entry* e = Find(level);
		if (e == nullptr) {
			return false;
		}
		const Slot& s = slots_[static_cast<size_t>(e - std::begin(mxl_dungeons::Entries))];
		return s.running && now < s.end;
	}
	// D2Sigma 0x5d9d0 / 0x5df20: a player entered `level`. Nothing without a lock time or when the clock already ran
	// (running or sealed); else it starts on the level, or on every level of its non-zero lock group. True if started.
	auto Start(int32_t level, uint64_t now) noexcept -> bool {
		const Entry* e = Find(level);
		if (e == nullptr || e->lockSeconds == 0) {
			return false;
		}
		Slot& own = slots_[static_cast<size_t>(e - std::begin(mxl_dungeons::Entries))];
		if (own.running) {
			return false;
		}
		const uint64_t end = now + static_cast<uint64_t>(e->lockSeconds) * 1000;
		own = Slot{true, end};
		if (e->lockGroup != 0) {
			for (size_t i = 0; i < slots_.size(); ++i) {
				if (mxl_dungeons::Entries[i].lockGroup == e->lockGroup) {
					slots_[i] = Slot{true, end};
				}
			}
		}
		return true;
	}

private:
	struct Slot {
		bool running = false;
		uint64_t end = 0;
	};
	const void* game_ = nullptr;
	uint32_t frame_ = 0;
	std::array<Slot, mxl_dungeons::EntryCount> slots_{};
};

// D2Sigma 0x5d810 for a player entering `dst`: requirements first, then the quest condition, then the seal (only levels
// with a lock time). Every refusal is the same unit message in D2Sigma, the order only picks the text.
inline auto Check(const Lockout& lockout, const Who& who, int32_t dst, uint64_t now, const QuestBits& quest = {}) noexcept
    -> Verdict {
	const Entry* e = Find(dst);
	if (e == nullptr) {
		return Verdict::Allow;
	}
	if (!MeetsRequirements(*e, who)) {
		return Verdict::Requirements;
	}
	if (!QuestAllows(*e, quest)) {
		return Verdict::Quest;
	}
	if (e->lockSeconds != 0 && lockout.Sealed(dst, now)) {
		return Verdict::Sealed;
	}
	return Verdict::Allow;
}

}  // namespace dungeon

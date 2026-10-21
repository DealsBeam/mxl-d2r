// Pure rules of the treasure goblins (D2Sigma D2Goblins.cpp 0x66120 tick, 0x66070 chance, 0x65de0 pick,
// D2Achievements.cpp 0x49c40 last-kill stamp). No engine access, so mxl_tests can run them.
// docs/subsystems/ubers-events.md section 4.
#pragma once

#include "mxl_goblins.h"

#include <cstdint>

namespace goblins {

constexpr uint32_t TickFrames = 375;     // 15 s at 25 Hz: table entry (15 x 0x19) of D2GS.cpp 0x66230
constexpr uint32_t RecentFrames = 0x177; // a player counts as fighting when frame - lastKill < 375
constexpr uint32_t ChanceDie = 0x400;    // rolled against 1024
constexpr int32_t SpawnOffset = 8;       // the goblin appears 8 sub-tiles right and down of the player (0xbdda0)

constexpr auto IsTickFrame(uint32_t frame) noexcept -> bool {
	return frame != 0 && frame % TickFrames == 0;
}

// The player fought within the last 15 s: frame - lastKill < 0x177 (unsigned, as the original).
constexpr auto KilledRecently(uint32_t frame, uint32_t lastKill) noexcept -> bool {
	return frame - lastKill < RecentFrames;
}

// 0x66070: chance - chance * (10 * players - 10) / 100, at least 1. players is the game's player count (>= 1).
constexpr auto EffectiveChance(int32_t chance, int32_t players) noexcept -> int32_t {
	if (chance <= 0) {
		return 0;
	}
	const int32_t p = players < 1 ? 1 : players;
	const int32_t reduced = chance - static_cast<int32_t>(static_cast<int64_t>(chance) * (10 * p - 10) / 100);
	return reduced < 1 ? 1 : reduced;
}

// `roll < chance` for a roll in [0, 1024).
constexpr auto ChancePasses(int32_t effective, uint32_t random) noexcept -> bool {
	return static_cast<int32_t>(random % ChanceDie) < effective;
}

inline auto FindChance(int32_t level) noexcept -> const mxl_goblins::Chance* {
	for (int32_t i = 0; i < mxl_goblins::ChanceCount; ++i) {
		if (mxl_goblins::Chances[i].level == level) {
			return &mxl_goblins::Chances[i];
		}
	}
	return nullptr;
}

inline auto IsTown(int32_t level) noexcept -> bool {
	for (int32_t i = 0; i < mxl_goblins::TownCount; ++i) {
		if (mxl_goblins::Towns[i] == level) {
			return true;
		}
	}
	return false;
}

inline auto NoKillStamp(int32_t monster) noexcept -> bool {
	for (int32_t i = 0; i < mxl_goblins::NoKillCount; ++i) {
		if (mxl_goblins::NoKill[i] == monster) {
			return true;
		}
	}
	return false;
}

// 0x65de0: a row is a candidate when it has the goblin flag and not the rainbow flag, the area's monster level is
// within [min, max] (0 = no maximum) and the level restriction is -1 or this area.
constexpr auto IsCandidate(const mxl_goblins::Goblin& g, int32_t level, int32_t monLevel) noexcept -> bool {
	return (g.flags & mxl_goblins::FlagGoblin) != 0 && (g.flags & mxl_goblins::FlagRainbow) == 0 && g.rarity != 0 &&
	       monLevel >= g.minLevel && (g.maxLevel == 0 || monLevel <= g.maxLevel) &&
	       (g.levelRestrict == -1 || g.levelRestrict == level);
}

// Total draw weight of the candidates (each row is in the original's list `rarity` times).
inline auto TotalWeight(int32_t level, int32_t monLevel) noexcept -> uint32_t {
	uint32_t total = 0;
	for (int32_t i = 0; i < mxl_goblins::GoblinCount; ++i) {
		if (IsCandidate(mxl_goblins::Goblins[i], level, monLevel)) {
			total += mxl_goblins::Goblins[i].rarity;
		}
	}
	return total;
}

// The D2R monster id for a uniform random number, or -1 when there is no candidate.
inline auto Pick(int32_t level, int32_t monLevel, uint32_t random) noexcept -> int32_t {
	const uint32_t total = TotalWeight(level, monLevel);
	if (total == 0) {
		return -1;
	}
	uint32_t at = random % total;
	for (int32_t i = 0; i < mxl_goblins::GoblinCount; ++i) {
		const mxl_goblins::Goblin& g = mxl_goblins::Goblins[i];
		if (!IsCandidate(g, level, monLevel)) {
			continue;
		}
		if (at < g.rarity) {
			return g.monster;
		}
		at -= g.rarity;
	}
	return -1;
}

// Per-game state: the frame each player last killed something. Reset when the game pointer changes or the frame
// counter goes backwards (a new game that reuses the address).
class State {
public:
	static constexpr int Slots = 16;

	// Returns true when the state was reset.
	auto Touch(const void* game, uint32_t frame) noexcept -> bool {
		const bool fresh = game != game_ || frame < frame_;
		if (fresh) {
			game_ = game;
			count_ = 0;
			tickBucket_ = 0;
			next_ = 0;
		}
		frame_ = frame;
		return fresh;
	}

	void NoteKill(uint32_t playerId, uint32_t frame) noexcept {
		for (int i = 0; i < count_; ++i) {
			if (ids_[i] == playerId) {
				frames_[i] = frame;
				return;
			}
		}
		int slot;
		if (count_ < Slots) {
			slot = count_++;
		} else {
			slot = next_;
			next_ = (next_ + 1) % Slots;
		}
		ids_[slot] = playerId;
		frames_[slot] = frame;
	}

	auto LastKill(uint32_t playerId) const noexcept -> uint32_t {
		for (int i = 0; i < count_; ++i) {
			if (ids_[i] == playerId) {
				return frames_[i];
			}
		}
		return 0;
	}

	// Fallback clock (no frame hook): true once per 375-frame bucket.
	auto BucketDue(uint32_t frame) noexcept -> bool {
		const uint32_t bucket = frame / TickFrames;
		if (bucket == 0 || bucket == tickBucket_) {
			return false;
		}
		tickBucket_ = bucket;
		return true;
	}

	auto Game() const noexcept -> const void* { return game_; }

private:
	const void* game_ = nullptr;
	uint32_t frame_ = 0;
	int count_ = 0;
	int next_ = 0;
	uint32_t tickBucket_ = 0;
	uint32_t ids_[Slots] = {};
	uint32_t frames_[Slots] = {};
};

}  // namespace goblins

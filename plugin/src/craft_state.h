// Craft flag storage and gold split: the pure part of craft_flags.cpp (unit-tested in tests/craft_test.cpp).
// Median keeps 256 player flags per character and 256 game flags in game memory only (docs/plugin-spec.md,
// "Cube conditions"); nothing is saved. Median finds a player's flags by the character name; so do we. Flags belong to
// one game pointer: cleared when the game ends (Forget, from the game-end hook) and, as a fallback, when the pointer
// changes.
#pragma once

#include "mxl_cube_effects.h"

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace craft {

class State {
public:
	// Call with the game of every search / recipe execution: a different game pointer is a new game, flags reset.
	void Touch(const void* game) noexcept {
		if (game != game_) {
			game_ = game;
			game_flags_.reset();
			players_.clear();
		}
	}
	// The game is over: its flags go (only if they are that game's).
	void Forget(const void* game) noexcept {
		if (game != nullptr && game == game_) {
			game_ = nullptr;
			game_flags_.reset();
			players_.clear();
		}
	}
	auto Test(bool gameWide, const std::string& player, int flag) const noexcept -> bool {
		if (flag < 0 || flag > 255) {
			return false;
		}
		if (gameWide) {
			return game_flags_.test(static_cast<size_t>(flag));
		}
		const auto it = players_.find(player);
		return it != players_.end() && it->second.test(static_cast<size_t>(flag));
	}
	void Set(bool gameWide, const std::string& player, int flag, bool on) noexcept {
		if (flag < 0 || flag > 255) {
			return;
		}
		(gameWide ? game_flags_ : players_[player]).set(static_cast<size_t>(flag), on);
	}

private:
	const void* game_ = nullptr;
	std::bitset<256> game_flags_;
	std::unordered_map<std::string, std::bitset<256>> players_;
};

// The recipe's output effects in slot order (empty range for recipes without one).
struct Range {
	const mxl_cube::Effect* first;
	const mxl_cube::Effect* last;
};
inline auto EffectsFor(int32_t recipe) noexcept -> Range {
	const auto* b = std::begin(mxl_cube::Effects);
	const auto* e = std::end(mxl_cube::Effects);
	const auto* lo = std::lower_bound(b, e, recipe, [](const mxl_cube::Effect& x, int32_t r) { return x.recipe < r; });
	const auto* hi = lo;
	while (hi != e && hi->recipe == recipe) {
		++hi;
	}
	return {lo, hi};
}

// Median's gold payment (D2Craft.cpp 0x10059120): the cost comes from carried gold, and when that is not enough the
// carried gold goes to 0 and the rest comes from the bank. Nothing goes below 0.
struct Gold {
	int64_t carried;
	int64_t bank;
};
inline auto Pay(Gold have, int64_t cost) noexcept -> Gold {
	if (have.carried < cost) {
		return {0, std::max<int64_t>(0, have.bank - (cost - have.carried))};
	}
	return {have.carried - cost, have.bank};
}

}  // namespace craft

// Median's gamble window rules on D2R as pure functions (gamble.cpp patches the game; tests/gamble_test.cpp).
//
//   * Offer count. Median's D2Sigma patch game5-022 (D2Game 6fcde2a0, byte 0xbe615) raises the loop bound of the offer
//     generator from 14 to 32. D2R's generator is 0x541880 (called once, from the NPC gamble request 0x540850): offer 0 is
//     a ring, offer 1 an amulet, the rest are random picks from the NPC's per-level list, `cmp ebp, 0xe` at 0x541a7c.
//   * Quality. 0x541ab0 rolls 0..99999 against the difficulty row (difficultylevels GambleRare / GambleSet /
//     GambleUnique; Median's data: 25000 / 1000 / 1000): below Unique is unique (7), below Unique + Set is set (5), below
//     the sum of the three is rare (6), otherwise magic (4). Median's "rare 25000, unique 1000" prices (audit IT10) are
//     these chances, they are data (txt_migrate), not code.
//   * Offers are independent per player: the list hangs off the player's data (+0x108), D2R's own behaviour.
//   * The offers are drawn again on every gamble request: closing and reopening the window is the refresh.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace gamble {

inline constexpr int VanillaOffers = 14;  // D2R's loop bound (0x0e)
inline constexpr int MedianOffers = 32;   // Median's loop bound (0x20)
inline constexpr int RingSlot = 0, AmuletSlot = 1;

inline constexpr uint64_t LoopBoundRva = 0x541a7c;  // 83 fd 0e 0f 8c db fe ff ff: cmp ebp, 0xe / jl 0x54195f
inline constexpr uint8_t LoopBoundOriginal[] = {0x83, 0xfd, 0x0e, 0x0f, 0x8c, 0xdb, 0xfe, 0xff, 0xff};
inline constexpr uint8_t LoopBoundPatched[] = {0x83, 0xfd, 0x20, 0x0f, 0x8c, 0xdb, 0xfe, 0xff, 0xff};
static_assert(sizeof(LoopBoundOriginal) == sizeof(LoopBoundPatched));
static_assert(LoopBoundOriginal[2] == VanillaOffers && LoopBoundPatched[2] == MedianOffers);

inline constexpr int RollRange = 100000;

enum Quality : int { Magic = 4, Set = 5, Rare = 6, Unique = 7 };

// 0x541ab0: the quality of one offer for a roll in [0, 100000) and the three chances of the difficulty row.
constexpr auto QualityOfRoll(int roll, int rare, int set, int unique) noexcept -> int {
	const int total = rare + set + unique;
	if (total <= 0 || roll >= total) {
		return Magic;
	}
	if (roll < unique) {
		return Unique;
	}
	return roll >= unique + set ? Rare : Set;
}

// The chance of each quality out of RollRange for a row: what the harness check compares the sampled offers to.
struct Chances {
	int magic, rare, set, unique;  // out of RollRange
};

constexpr auto ChancesOf(int rare, int set, int unique) noexcept -> Chances {
	const int total = rare + set + unique;
	if (total <= 0) {
		return {RollRange, 0, 0, 0};
	}
	const int capped = total > RollRange ? RollRange : total;
	return {RollRange - capped, rare, set, unique};
}

// Median's rows (work/mod difficultylevels.txt, all three difficulties).
inline constexpr int MedianRare = 25000, MedianSet = 1000, MedianUnique = 1000;

// The harness check: a window holds Median's offer count.
constexpr auto OffersOk(int count) noexcept -> bool {
	return count == MedianOffers;
}

// Whether a sampled share (counts out of `offers`, in per mille) is within `slackPermille` of the expected per mille. One window
// of 32 is noisy, so the check samples many windows.
constexpr auto ShareInRange(int rares, int offers, int expectedPermille, int slackPermille) noexcept -> bool {
	if (offers <= 0) {
		return false;
	}
	const int share = rares * 1000 / offers;
	return share >= expectedPermille - slackPermille && share <= expectedPermille + slackPermille;
}

inline void FormatReport(char* out, size_t outSize, bool patched) noexcept {
	if (patched) {
		std::snprintf(out, outSize, "on: gamble window offers %d (D2R %d), ring + amulet first, rare/set/unique %d/%d/%d per 100000", MedianOffers,
		              VanillaOffers, MedianRare, MedianSet, MedianUnique);
	} else {
		std::snprintf(out, outSize, "off: gamble offer loop bound 0x541a7c not patched");
	}
}

}  // namespace gamble

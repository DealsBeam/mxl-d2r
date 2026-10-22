// Pure rules of the item/experience hooks (items_ext.cpp): Median's kill-experience penalty and the lookup of the
// runeword properties D2R's record has no room for. No engine access, so mxl_tests can run them.
// docs/subsystems/items-xp.md sections 2 (experience) and 3 (runewords).
#pragma once

#include "mxl_runewords.h"

#include <cstddef>
#include <cstdint>

namespace items_ext {

// --- Experience ---------------------------------------------------------------------------------------------------

// D2Sigma D2Experience.cpp 0x1005e7b0 (called from the thunk 0x1005e900 that replaces the 1.13c call at D2Game 0xdc076):
// kill experience `xp` for a player of level `plvl` and a monster of level `mlvl`. Symmetric: a level gap of 5 or less
// costs nothing, beyond that the share is (15 - gap) * 10 percent (players below level 100) or (10 - gap) * 20 percent
// (level 100 and up), never below 3 percent. 1.13c (and so D2R, 0x44fef0) uses two 11-entry tables of 1/256 steps, flat
// from a gap of 10, and pays players of level 25+ a bonus (experience * monster level / player level, 0x44a0b0) for
// stronger monsters.
constexpr auto XpSharePercent(int32_t plvl, int32_t mlvl) noexcept -> int32_t {
	if (plvl == mlvl) {
		return 100;
	}
	const int32_t gap = plvl > mlvl ? plvl - mlvl : mlvl - plvl;
	if (gap <= 5) {
		return 100;
	}
	int32_t pct = plvl < 100 ? (15 - gap) * 10 : (10 - gap) * 20;
	if (pct < 3) {
		pct = 3;
	}
	return pct;
}

// xp * share / 100 with a 64-bit product, truncated toward zero (D2Sigma 0x10088e50 is MulDiv with __alldiv).
// Experience of zero or less is returned as it is.
constexpr auto PenalizedXp(int32_t xp, int32_t plvl, int32_t mlvl) noexcept -> int32_t {
	if (xp <= 0) {
		return xp;
	}
	const int32_t pct = XpSharePercent(plvl, mlvl);
	if (pct == 100) {
		return xp;
	}
	return static_cast<int32_t>(static_cast<int64_t>(xp) * pct / 100);
}

// --- Runewords ----------------------------------------------------------------------------------------------------

// The extra properties of D2R's runeword table row `row`, or nullptr (Extras is sorted by row).
inline auto FindExtra(int32_t row) noexcept -> const mxl_runewords::Extra* {
	int32_t lo = 0;
	int32_t hi = mxl_runewords::ExtraCount - 1;
	while (lo <= hi) {
		const int32_t mid = lo + (hi - lo) / 2;
		const int32_t at = mxl_runewords::Extras[mid].row;
		if (at == row) {
			return &mxl_runewords::Extras[mid];
		}
		if (at < row) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return nullptr;
}

// Used slots of an extra row (a slot with prop < 0 ends the list, as in D2R's own record).
constexpr auto UsedSlots(const mxl_runewords::Extra& extra) noexcept -> int32_t {
	int32_t n = 0;
	while (n < mxl_runewords::ExtraSlots && extra.props[n].prop >= 0) {
		++n;
	}
	return n;
}

// Median's runeword matcher (D2Runeword.cpp 0x100a2b60) pads a runeword's rune list with Jewels (the first rune of the
// first row, Jewelword1's) at the *front* up to the number of sockets, then compares the lists in socket order. A
// 1.13c / D2R matcher needs a runeword with exactly as many runes as sockets. This is the comparison the padding
// adds: `socketed` holds the class ids of the n socketed items in socket order, `runes` the six rune class ids of the
// record (non-positive ends the list). True when the record has fewer runes than sockets and
// [jewel x (n - k), runes...] equals the socketed list. A record made only of Jewels is left to the exact match (the
// Jewelword rows count their Jewels; padded, Jewelword1 would shadow Jewelword2-6).
constexpr auto PaddedRunesMatch(const int32_t* socketed, int32_t n, const int32_t* runes, int32_t jewel) noexcept -> bool {
	if (socketed == nullptr || runes == nullptr || n < 1 || n > 6) {
		return false;
	}
	int32_t k = 0;
	bool onlyJewels = true;
	while (k < 6 && runes[k] > 0) {
		onlyJewels = onlyJewels && runes[k] == jewel;
		++k;
	}
	if (k == 0 || k >= n || onlyJewels) {
		return false;
	}
	const int32_t pad = n - k;
	for (int32_t i = 0; i < pad; ++i) {
		if (socketed[i] != jewel) {
			return false;
		}
	}
	for (int32_t j = 0; j < k; ++j) {
		if (socketed[pad + j] != runes[j]) {
			return false;
		}
	}
	return true;
}

// Row of a record inside D2R's runes table (`stride` bytes per record, `count` records), or -1 when the pointer is not
// the start of a record of that table.
constexpr auto RowOfRecord(const uint8_t* record, const uint8_t* table, uint64_t count, uint64_t stride) noexcept
	-> int64_t {
	if (record == nullptr || table == nullptr || record < table || stride == 0) {
		return -1;
	}
	const uint64_t offset = static_cast<uint64_t>(record - table);
	if (offset % stride != 0 || offset / stride >= count) {
		return -1;
	}
	return static_cast<int64_t>(offset / stride);
}

// Party experience. Median's D2Experience.cpp 0x5e830 gives every collected party member the monster's whole experience
// (each then goes through the per-member award: level gap penalty, ExpRatio, stat 0x55); 1.13c / D2R's share loop pools
// xp + (n-1) * xp * 0x59 / 256 and splits it by character level. Median also widens the collect range from 80 to about
// 179 units (squared distance 0x1900 -> 0x7d00, D2Game 0xdafcb). docs/subsystems/items-xp.md.
constexpr uint32_t VanillaPartyRangeSq = 0x1900;
constexpr uint32_t MedianPartyRangeSq = 0x7d00;
constexpr auto MedianPartyAward(int32_t xp) noexcept -> int32_t {
	return xp;
}
// The vanilla share of the member with `level` out of `n` members whose levels add up to `sum` (the code of 0x44a3b0).
constexpr auto VanillaPartyAward(int32_t xp, int32_t n, int32_t level, int32_t sum) noexcept -> int32_t {
	if (n <= 0 || sum <= 0) {
		return 0;
	}
	if (n == 1) {
		return xp;
	}
	const int64_t extra = static_cast<int64_t>(n - 1) * 0x59 * xp;
	const int64_t pool = xp + ((extra + (extra < 0 ? 0xff : 0)) >> 8);
	return static_cast<int32_t>(static_cast<double>(level) * (static_cast<double>(pool) / sum));
}

// --- Trophy challenges --------------------------------------------------------------------------------------------

// The right click on a trophy (misc2 InteractString) prints the challenge text (mxl_items.h Trophies) as chat lines: one per
// text line, spaces at both ends dropped, blank lines left out, each cut to what a chat record holds. `out(line, first)`
// gets a NUL-terminated line and whether it is the title. Returns the number of lines.
inline constexpr size_t TrophyLineMax = 250;

template <class Out>
auto ForEachTrophyLine(const char* text, Out out) noexcept -> int {
	int lines = 0;
	while (text != nullptr && *text != '\0') {
		const char* end = text;
		while (*end != '\0' && *end != '\n') {
			++end;
		}
		const char* from = text;
		const char* to = end;
		while (from < to && *from == ' ') {
			++from;
		}
		while (to > from && (to[-1] == ' ' || to[-1] == '\r')) {
			--to;
		}
		if (to > from) {
			char line[TrophyLineMax + 1];
			size_t n = static_cast<size_t>(to - from);
			if (n > TrophyLineMax) {
				n = TrophyLineMax;
			}
			for (size_t i = 0; i < n; ++i) {
				line[i] = from[i];
			}
			line[n] = '\0';
			out(static_cast<const char*>(line), lines == 0);
			++lines;
		}
		text = *end == '\n' ? end + 1 : end;
	}
	return lines;
}

}  // namespace items_ext

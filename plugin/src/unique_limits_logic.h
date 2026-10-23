// Median's unique carry limits (uniqueitems2: LimitGroup, LimitCount, PickDuplicates), as D2Sigma's D2Units checks
// 0x100bd060 / 0x100bd0b0 apply them. Pure rules, unit tested (plugin/tests/unique_limits_test.cpp); unique_limits.cpp
// feeds them from the game.
//
// What is counted is the player's backpack (inventory page 0, items stored there): equipped items, belt, stash, cube and
// the cursor do not count. A unique with a limit may enter the backpack only when
//   - fewer than LimitCount uniques of its limit group are there (group 0: fewer than LimitCount copies of that very
//     unique), and
//   - for a group other than 0 with PickDuplicates 0: no copy of that very unique is there.
// Median has no wearing rule: all 334 limited uniques are misc items (relics, keys, chimes, gems) that cannot be equipped.
#pragma once

#include "mxl_unique_limits.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace unique_limits {

inline constexpr int32_t QualityUnique = 7;
inline constexpr int32_t MaxCarried = 128;  // backpack cells: a bound for the lists the rule works on

// The limit of unique `index` (its row in uniqueitems.txt), or null when it has none.
inline auto Find(int32_t index) noexcept -> const mxl_unique_limits::Row* {
	int lo = 0;
	int hi = mxl_unique_limits::RowCount - 1;
	while (lo <= hi) {
		const int mid = (lo + hi) / 2;
		const auto& r = mxl_unique_limits::Rows[mid];
		if (r.row == index) {
			return &r;
		}
		if (r.row < index) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return nullptr;
}

// Limit group of unique `index`; 0 when it has no row.
inline auto GroupOf(int32_t index) noexcept -> int32_t {
	const auto* r = Find(index);
	return r != nullptr ? r->group : 0;
}

// True when the unique with limit `limit` (row `index`) may not enter a backpack that holds the uniques `carried[0..n)`
// (their unique indexes).
inline auto RefusesBy(const mxl_unique_limits::Row& limit_, int32_t index, const int32_t* carried, size_t n) noexcept -> bool {
	const auto* limit = &limit_;
	int32_t same = 0;   // uniques of the limit group (group 0: copies of this unique)
	bool copy = false;  // a copy of this very unique
	for (size_t i = 0; i < n; ++i) {
		if (carried[i] == index) {
			copy = true;
		}
		if (limit->group == 0 ? carried[i] == index : GroupOf(carried[i]) == limit->group) {
			++same;
		}
	}
	if (same >= limit->count) {
		return true;
	}
	return limit->group != 0 && limit->duplicates == 0 && copy;
}

// The same for unique `index` looked up in the table; a unique without a limit is never refused.
inline auto Refuses(int32_t index, const int32_t* carried, size_t n) noexcept -> bool {
	const auto* limit = Find(index);
	return limit != nullptr && RefusesBy(*limit, index, carried, n);
}

// Median's refusal text (string 12109, mxlstr::KeyUniqueLimit, shown for every limit); the English stands in when
// the string is not there.
inline constexpr const char* RefusalText = "\xC3\xBF" "c1You can only carry one Relic";

struct Counts {
	long checked;  // moves into the backpack looked at
	long limited;  // ... of a unique that has a limit
	long refused;
};

inline void FormatReport(char* out, size_t size, bool on, const Counts& c) noexcept {
	if (!on) {
		std::snprintf(out, size, "off");
		return;
	}
	std::snprintf(out, size, "on, %d limited uniques; moves into the backpack %ld (limited %ld, refused %ld)", mxl_unique_limits::RowCount,
	              c.checked, c.limited, c.refused);
}

}  // namespace unique_limits

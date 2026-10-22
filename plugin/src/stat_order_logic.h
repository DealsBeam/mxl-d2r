// Median's character sheet order on D2R's Advanced Stats panel (pure rules; stat_columns.cpp, docs/subsystems/stat-screen.md).
//
// Median's two extra sheet pages group their boxes under label rows ("General", "Weapon Damage" ...). The Advanced Stats panel
// lists the stats of its records in a vector (0x144-byte copies) and shows a row while the record's value is not 0 (advdisplay
// 2; an advdisplay 1 row is always shown). stat_columns.cpp rebuilds that vector in the order of mxl_statorder.h with one extra
// record per section label: a copy of a real record whose id is HeaderBase + the position in the list, which the row hooks
// answer with the label (shown while any stat of the section is). This file has that arithmetic.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace stat_order {

// Record ids from here up are section labels (D2R's itemstatcost has about 500 rows).
constexpr int32_t HeaderBase = 0x7000;

constexpr auto IsHeader(int32_t id) noexcept -> bool {
	return id >= HeaderBase;
}

constexpr auto HeaderId(int32_t entry) noexcept -> int32_t {
	return HeaderBase + entry;
}

constexpr auto EntryOf(int32_t headerId) noexcept -> int32_t {
	return headerId - HeaderBase;
}

// A stat row is listed when it is always listed or its displayed value is not 0.
constexpr auto Listed(bool always, int64_t value) noexcept -> bool {
	return always || value != 0;
}

// True when a section label (entry `label` of `order`, whose entries have .stat, -1 for a label) heads a listed stat:
// `listed(stat, always)` answers for one stat entry. Entries up to the next label (or the end) belong to the section.
template <typename Entry, typename Listed>
auto SectionListed(const Entry* order, int32_t count, int32_t label, Listed listed) -> bool {
	for (int32_t i = label + 1; i < count && order[i].stat >= 0; ++i) {
		if (listed(order[i].stat, order[i].always)) {
			return true;
		}
	}
	return false;
}

// The label's row text: the label between "== ", the panel's text widget prints colour codes as text, so it has none.
inline auto HeaderText(const char* header, char* out, size_t size) noexcept -> int {
	if (size == 0) {
		return 0;
	}
	return std::snprintf(out, size, "== %s ==", header);
}

}  // namespace stat_order

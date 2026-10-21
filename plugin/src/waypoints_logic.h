// Waypoint record facts and the waypoint panel's slots, as pure functions (waypoints.cpp reads the game; tests/waypoints_test.cpp).
//
// D2R 3.3 (build 93847): the waypoint record is 112 bits. Waypoint number n lives in 16-bit word 1 + n / 16 of the record under
// mask 1 << (n % 16) (table RVA 0x2389010, 4 bytes per number: word u16, mask u16; count dword at 0x23891d0 = 0x70). Word 0 is the
// record version (0x0102). The save's "WS" section keeps the first 16 bytes of each 24-byte per-difficulty record, so numbers
// up to 111 survive a save; Median's own D2Common has the same table, which is why its numbers 50..90 need no remap.
// The waypoint panel is what limits Median: it lists the levels named in actinfo.txt (waypoint1..waypoint9 of five rows).
#pragma once

#include "mxl_waypoints.h"

#include <cstdint>

namespace wp {

constexpr int RecordBits = 112;
constexpr int PanelTabs = 5;
constexpr int PanelSlots = 9;

constexpr int WordOf(int number) noexcept { return 1 + number / 16; }
constexpr uint16_t MaskOf(int number) noexcept { return static_cast<uint16_t>(1u << (number % 16)); }

// The engine's table: entries[2 * n] = word, entries[2 * n + 1] = mask. True when all `count` entries are the layout above.
inline auto TableMatches(const uint16_t* entries, int count) noexcept -> bool {
	if (count != RecordBits) {
		return false;
	}
	for (int n = 0; n < count; ++n) {
		if (entries[2 * n] != WordOf(n) || entries[2 * n + 1] != MaskOf(n)) {
			return false;
		}
	}
	return true;
}

struct Summary {
	int levels = 0;
	int highest = 0;
	int listed = 0;      // levels with a button in the panel
	int unlisted = 0;
	int duplicates = 0;  // numbers used by two levels
	int outOfRange = 0;  // numbers the record cannot hold
	int firstUnlisted[8] = {};  // level ids, up to 8
};

inline auto Summarise() noexcept -> Summary {
	Summary s;
	bool used[256] = {};
	for (const mxl_wp::Level& l : mxl_wp::Levels) {
		++s.levels;
		s.highest = l.number > s.highest ? l.number : s.highest;
		if (l.number >= RecordBits) {
			++s.outOfRange;
		}
		if (used[l.number]) {
			++s.duplicates;
		}
		used[l.number] = true;
		if (l.tab >= 1 && l.tab <= PanelTabs) {
			++s.listed;
		} else {
			if (s.unlisted < 8) {
				s.firstUnlisted[s.unlisted] = l.level;
			}
			++s.unlisted;
		}
	}
	return s;
}

}  // namespace wp

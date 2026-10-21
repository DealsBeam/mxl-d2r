// Waypoint record facts and the waypoint panel's slots, as pure functions (waypoints.cpp reads the game; tests/waypoints_test.cpp).
//
// D2R 3.3 (build 93847): the waypoint record is 112 bits. Waypoint number n lives in 16-bit word 1 + n / 16 of the record under
// mask 1 << (n % 16) (table RVA 0x2389010, 4 bytes per number: word u16, mask u16; count dword at 0x23891d0 = 0x70). Word 0 is the
// record version (0x0102). The save's "WS" section keeps the first 16 bytes of each 24-byte per-difficulty record, so numbers
// up to 111 survive a save; Median's own D2Common has the same table, which is why its numbers 50..90 need no remap.
// The waypoint panel is what limited Median: it listed the levels named in actinfo.txt (waypoint1..waypoint9 of five rows).
// waypoints.cpp now fills each tab from the generated list (mxl_waypoints.h, every level under its own act, up to MaxButtons).
#pragma once

#include "mxl_waypoints.h"

#include <cstdint>

namespace wp {

constexpr int RecordBits = 112;
constexpr int PanelTabs = 5;
constexpr int PanelSlots = 9;      // waypoint1..waypoint9 per actinfo row (the engine's per-row slot count)
constexpr int MaxRows = 3;         // synthetic rows the tab fill is run over (9 levels each)
constexpr int MaxButtons = MaxRows * PanelSlots;  // levels one tab can list (the plugin's button table holds these)

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

// The panel's 0x34-byte actinfo entry (vector at RVA 0x2a96a28: data pointer, size): a built flag, nine level ids and the nine
// waypoint numbers the tab fill computes from them (0xff = none yet). The plugin runs the engine's own tab fill over rows of
// this layout to get more than nine buttons for a tab.
struct ActRow {
	uint8_t built;
	uint8_t pad0[3];
	uint32_t level[PanelSlots];
	uint8_t number[PanelSlots];
	uint8_t pad1[3];
};
static_assert(sizeof(ActRow) == 0x34, "the panel's actinfo entry is 0x34 bytes");

// Levels listed under the 1-based tab `tab`, in ascending waypoint number (the order of the panel's buttons), at most `cap`.
// Returns how many fit; levels beyond `cap` are dropped (and counted by Summarise as overflow).
inline auto LevelsOfTab(int tab, int16_t* out, int cap) noexcept -> int {
	int n = 0;
	int last = -1;
	for (;;) {
		const mxl_wp::Level* next = nullptr;
		for (const mxl_wp::Level& l : mxl_wp::Levels) {
			if (l.tab == tab && l.number > last && (next == nullptr || l.number < next->number)) {
				next = &l;
			}
		}
		if (next == nullptr || n >= cap) {
			return n;
		}
		out[n++] = next->level;
		last = next->number;
	}
}

// The synthetic actinfo rows for a tab: nine levels per row, unused slots level 0 (the tab fill finds no number for it).
// Returns the number of rows used (0 when the tab lists nothing), at most `maxRows`.
inline auto FillRows(int tab, ActRow* rows, int maxRows) noexcept -> int {
	int16_t levels[MaxButtons] = {};
	const int n = LevelsOfTab(tab, levels, maxRows < MaxRows ? maxRows * PanelSlots : MaxButtons);
	const int used = (n + PanelSlots - 1) / PanelSlots;
	for (int r = 0; r < used; ++r) {
		ActRow& row = rows[r];
		row = ActRow{};
		for (int k = 0; k < PanelSlots; ++k) {
			const int i = r * PanelSlots + k;
			row.level[k] = i < n ? static_cast<uint32_t>(levels[i]) : 0u;
			row.number[k] = 0xff;
		}
	}
	return used;
}

// The button table entry the tab fill writes (8 bytes at RVA 0x2a969e0 + 8 * i): the level and 1 when it is not the level the
// player stands in. The plugin keeps its own, longer table of these.
struct ButtonEntry {
	uint32_t level;
	uint8_t other;
	uint8_t pad[3];
};
static_assert(sizeof(ButtonEntry) == 8, "button table entries are 8 bytes");

struct Summary {
	int levels = 0;
	int highest = 0;
	int listed = 0;      // levels with a button in the panel
	int unlisted = 0;    // no tab, or past MaxButtons on its tab
	int widest = 0;      // most levels on one tab
	int overflow = 0;    // levels past MaxButtons (counted in unlisted)
	int duplicates = 0;  // numbers used by two levels
	int outOfRange = 0;  // numbers the record cannot hold
	int firstUnlisted[8] = {};  // level ids, up to 8
};

inline auto Summarise() noexcept -> Summary {
	Summary s;
	bool used[256] = {};
	int perTab[PanelTabs + 1] = {};
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
		if (l.tab >= 1 && l.tab <= PanelTabs && ++perTab[l.tab] <= MaxButtons) {
			++s.listed;
			s.widest = perTab[l.tab] > s.widest ? perTab[l.tab] : s.widest;
		} else {
			s.overflow += l.tab >= 1 && l.tab <= PanelTabs ? 1 : 0;
			if (s.unlisted < 8) {
				s.firstUnlisted[s.unlisted] = l.level;
			}
			++s.unlisted;
		}
	}
	return s;
}

}  // namespace wp

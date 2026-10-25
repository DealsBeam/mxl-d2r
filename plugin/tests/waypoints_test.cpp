// Tests of the waypoint record layout and Median's waypoint data (waypoints_logic.h, generated mxl_waypoints.h). The bit table
// values are the ones read from the dump of build 93847 (RVA 0x2389010: 112 entries {word, mask}, word 1 + n / 16).
#include "../src/waypoints_logic.h"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL (waypoints): %s\n", what);
	}
}

}  // namespace

int RunWaypointsTests() {
	// The destination selector keeps towns and changes exactly the eight Median extras.
	for (int level = -1; level < 300; ++level) {
		bool expected = level == 1 || level == 40 || level == 75 || level == 103 || level == 109;
		for (int extra : {138, 139, 141, 147, 153, 155, 169, 242}) {
			expected = expected || level == extra;
		}
		Check(wp::NeedsObjectEntry(level) == expected, "waypoint object destination selection");
	}
	// record layout: number 0 = word 1 mask 1, 38 = word 3 mask 0x40, 90 = word 6 mask 0x400, 111 = word 7 mask 0x8000
	Check(wp::WordOf(0) == 1 && wp::MaskOf(0) == 0x1, "number 0");
	Check(wp::WordOf(15) == 1 && wp::MaskOf(15) == 0x8000, "number 15");
	Check(wp::WordOf(16) == 2 && wp::MaskOf(16) == 0x1, "number 16");
	Check(wp::WordOf(38) == 3 && wp::MaskOf(38) == 0x40, "number 38 (the last vanilla one)");
	Check(wp::WordOf(90) == 6 && wp::MaskOf(90) == 0x400, "number 90 (Median's highest)");
	Check(wp::WordOf(111) == 7 && wp::MaskOf(111) == 0x8000, "number 111 (the last the engine keeps)");
	// the 16-byte record copy holds header + 7 words = 112 bits
	Check(wp::WordOf(wp::RecordBits - 1) == 7 && 2 * (wp::WordOf(wp::RecordBits - 1) + 1) == 16, "record is 16 bytes");

	std::vector<uint16_t> t;
	for (int n = 0; n < wp::RecordBits; ++n) {
		t.push_back(static_cast<uint16_t>(wp::WordOf(n)));
		t.push_back(wp::MaskOf(n));
	}
	Check(wp::TableMatches(t.data(), wp::RecordBits), "engine table accepted");
	Check(!wp::TableMatches(t.data(), 39), "a 39-entry table is refused");
	t[2 * 90 + 1] = 0x800;
	Check(!wp::TableMatches(t.data(), wp::RecordBits), "a changed mask is refused");

	// Median's data: unique numbers below the record size, the eight extras past 38
	const wp::Summary s = wp::Summarise();
	Check(s.levels == 47, "47 waypoint levels (39 + Median's 8)");
	Check(s.duplicates == 0, "no two levels share a waypoint number");
	Check(s.outOfRange == 0, "every number fits the 112-bit record");
	Check(s.highest == 90, "highest number is 90");
	int above38 = 0;
	int perTab[wp::PanelTabs + 1] = {};
	for (const mxl_wp::Level& l : mxl_wp::Levels) {
		above38 += l.number > 38 ? 1 : 0;
		Check(l.tab <= wp::PanelTabs, "tab is 0-5");
		if (l.tab <= wp::PanelTabs) {
			++perTab[l.tab];
		}
	}
	Check(above38 == 8, "eight numbers past the old 39");
	// Median's acts hold 10, 11, 12, 4 and 10 waypoints: every level has a tab, none past the plugin's table
	Check(perTab[0] == 0 && perTab[1] == 10 && perTab[2] == 11 && perTab[3] == 12 && perTab[4] == 4 && perTab[5] == 10, "levels per act");
	Check(s.listed == s.levels && s.unlisted == 0 && s.overflow == 0, "every waypoint level has a button");
	Check(s.widest == 12, "the widest tab lists 12");
	for (int tab = 1; tab <= wp::PanelTabs; ++tab) {
		Check(perTab[tab] <= wp::MaxButtons, "no tab lists more than the button table holds");
	}

	// tab lists: ascending waypoint number, the extras after the vanilla ones; Act 3 has Caldeum, Torajan Jungles and Vizjun
	int16_t lv[wp::MaxButtons] = {};
	Check(wp::LevelsOfTab(1, lv, wp::MaxButtons) == 10 && lv[0] == 1 && lv[8] == 35 && lv[9] == 153, "Act 1: town ... Catacombs 2, Duncraig");
	Check(wp::LevelsOfTab(3, lv, wp::MaxButtons) == 12 && lv[0] == 75 && lv[8] == 101 && lv[9] == 138 && lv[10] == 141 && lv[11] == 155,
	      "Act 3: nine of Diablo II, then Caldeum, Torajan Jungles, Vizjun");
	Check(wp::LevelsOfTab(4, lv, wp::MaxButtons) == 4 && lv[3] == 139, "Act 4: three of Diablo II, Silver City");
	Check(wp::LevelsOfTab(5, lv, wp::MaxButtons) == 10 && lv[9] == 242, "Act 5: nine of Diablo II, t_r_d_lra");
	Check(wp::LevelsOfTab(2, lv, 4) == 4 && lv[3] == 57, "a short buffer takes the first ones");
	Check(wp::LevelsOfTab(0, lv, wp::MaxButtons) == 0 && wp::LevelsOfTab(6, lv, wp::MaxButtons) == 0, "tabs outside 1-5 list nothing");

	// the synthetic actinfo rows the plugin runs the engine's tab fill over
	wp::ActRow rows[wp::MaxRows];
	Check(wp::FillRows(3, rows, wp::MaxRows) == 2, "Act 3 needs two rows");
	Check(rows[0].built == 0 && rows[0].level[0] == 75 && rows[0].level[8] == 101 && rows[1].level[0] == 138 && rows[1].level[2] == 155,
	      "rows hold the levels nine to a row");
	Check(rows[1].level[3] == 0 && rows[1].number[0] == 0xff && rows[1].number[8] == 0xff, "unused slots are level 0, numbers unset");
	Check(wp::FillRows(4, rows, wp::MaxRows) == 1 && rows[0].level[3] == 139 && rows[0].level[4] == 0, "Act 4 fits one row");
	Check(wp::FillRows(6, rows, wp::MaxRows) == 0, "no row for a tab without levels");
	Check(wp::FillRows(3, rows, 1) == 1, "rows are capped by the buffer");
	return failures;
}

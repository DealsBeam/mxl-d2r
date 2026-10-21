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
	// the panel holds 9 slots per act: no tab is asked for more
	for (int tab = 1; tab <= wp::PanelTabs; ++tab) {
		Check(perTab[tab] <= wp::PanelSlots, "no tab lists more than 9 levels");
	}
	Check(s.listed + s.unlisted == s.levels, "listed + unlisted = all");
	Check(perTab[0] == s.unlisted, "tab 0 is the unlisted ones");
	return failures;
}

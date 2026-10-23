// Tests of the unique carry limits (unique_limits_logic.h, data mxl_unique_limits.h). Run through mxl_tests
// (requirements_test.cpp main).
#include "../src/unique_limits_logic.h"

#include <cstdio>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: %s\n", what);
	}
}
}  // namespace

int RunUniqueLimitsTests() {
	using namespace unique_limits;
	using mxl_unique_limits::Rows;
	using mxl_unique_limits::RowCount;

	// the data: sorted by row, inside the table, every row limited, 334 of them
	bool sorted = true, inside = true, limited = true;
	for (int i = 0; i < RowCount; ++i) {
		sorted = sorted && (i == 0 || Rows[i - 1].row < Rows[i].row);
		inside = inside && Rows[i].row >= 0 && Rows[i].row < mxl_unique_limits::TableRows;
		limited = limited && (Rows[i].count != 0 || Rows[i].group != 0 || Rows[i].duplicates != 0);
	}
	Check(RowCount == 334 && sorted && inside && limited, "334 limited rows, sorted");
	Check(Find(0) == nullptr && Find(-1) == nullptr && Find(mxl_unique_limits::TableRows) == nullptr, "an unlimited unique has no row");
	const int32_t single = Rows[0].row;  // the first limited unique: a single copy, no group
	Check(Find(single) == &Rows[0] && GroupOf(single) == Rows[0].group, "Find returns the row");
	bool allFound = true;
	for (int i = 0; i < RowCount; ++i) {
		allFound = allFound && Find(Rows[i].row) == &Rows[i];
	}
	Check(allFound, "every row is found");

	// group 0, count 1: one copy of that very unique, any other unique is unaffected
	int32_t soloA = -1, soloB = -1;
	for (int i = 0; i < RowCount; ++i) {
		if (Rows[i].group == 0 && Rows[i].count == 1) {
			(soloA < 0 ? soloA : soloB) = Rows[i].row;
			if (soloB >= 0) {
				break;
			}
		}
	}
	Check(soloA >= 0 && soloB >= 0, "two single-copy uniques in the data");
	{
		const int32_t held[] = {soloB, 0};
		Check(!Refuses(soloA, held, 0), "empty backpack: allowed");
		Check(!Refuses(soloA, held, 2), "another single-copy unique and an unlimited one do not count");
		const int32_t same[] = {0, soloA};
		Check(Refuses(soloA, same, 2), "the second copy of a single-copy unique is refused");
		Check(!Refuses(0, same, 2), "an unlimited unique is never refused");
	}

	// a group with a count above 1 (relics: group 1, count 3, no duplicates): three different members fit, the fourth does not
	int32_t g1[4] = {-1, -1, -1, -1};
	int found = 0;
	int32_t g1Count = 0;
	for (int i = 0; i < RowCount && found < 4; ++i) {
		if (Rows[i].group == 1) {
			g1[found++] = Rows[i].row;
			g1Count = Rows[i].count;
		}
	}
	Check(found == 4 && g1Count == 3, "group 1 has several members and a count of 3");
	{
		Check(!Refuses(g1[2], g1, 2), "two of the group carried: the third is allowed");
		Check(Refuses(g1[3], g1, 3), "three of the group carried: the fourth is refused");
		const int32_t dup[] = {g1[0]};
		Check(Refuses(g1[0], dup, 1), "no duplicates: a copy of the same member is refused although the group has room");
		const int32_t other[] = {g1[0], 0, soloA};
		Check(!Refuses(g1[1], other, 3), "members of other groups and unlimited uniques do not count");
	}

	// every group row is internally consistent: one count and one duplicates flag per group
	bool consistent = true;
	for (int i = 0; i < RowCount; ++i) {
		for (int k = 0; k < RowCount; ++k) {
			if (Rows[i].group != 0 && Rows[i].group == Rows[k].group) {
				consistent = consistent && Rows[i].count == Rows[k].count;
			}
		}
	}
	Check(consistent, "a group has one count");

	// PickDuplicates 1 (not in the data): a group member may be picked up while a copy is there, the count still applies
	{
		const mxl_unique_limits::Row allowDup{static_cast<int16_t>(g1[0]), 1, 2, 1};
		const int32_t copyOnly[] = {g1[0]};
		const int32_t full[] = {g1[0], g1[1]};
		Check(!RefusesBy(allowDup, g1[0], copyOnly, 1), "duplicates allowed: the second copy is fine");
		Check(RefusesBy(allowDup, g1[0], full, 2), "duplicates allowed: the count of 2 still stops the third");
	}

	char buf[160];
	FormatReport(buf, sizeof(buf), false, Counts{});
	Check(buf[0] == 'o' && buf[1] == 'f' && buf[2] == 'f' && buf[3] == 0, "off report");
	FormatReport(buf, sizeof(buf), true, Counts{5, 3, 1});
	Check(std::snprintf(nullptr, 0, "%s", buf) > 20, "on report");
	return failures;
}

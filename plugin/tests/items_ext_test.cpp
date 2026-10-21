// Tests of the item and experience rules (items_ext_logic.h, mxl_runewords.h). Run through mxl_tests (requirements_test.cpp main).
#include "../src/items_ext_logic.h"

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

int RunItemsExtTests() {
	using namespace items_ext;

	// kill experience: D2Sigma 0x1005e7b0
	Check(XpSharePercent(50, 50) == 100 && XpSharePercent(50, 55) == 100 && XpSharePercent(55, 50) == 100, "a gap of 5 costs nothing");
	Check(XpSharePercent(50, 56) == 90 && XpSharePercent(56, 50) == 90, "gap 6 keeps 90 percent, both directions");
	Check(XpSharePercent(60, 50) == 50 && XpSharePercent(60, 46) == 10, "gaps 10 and 14");
	Check(XpSharePercent(60, 45) == 3 && XpSharePercent(60, 1) == 3 && XpSharePercent(99, 10) == 3, "never below 3 percent");
	Check(XpSharePercent(100, 94) == 80 && XpSharePercent(100, 91) == 20 && XpSharePercent(100, 90) == 3, "level 100 and up: 20 percent steps");
	Check(XpSharePercent(99, 93) == 90, "level 99 still uses the 10 percent steps");
	Check(XpSharePercent(94, 100) == 90 && XpSharePercent(110, 100) == 3, "the 100 threshold is the player's level");
	Check(PenalizedXp(1000, 60, 60) == 1000 && PenalizedXp(1000, 60, 56) == 1000, "no penalty inside the gap");
	Check(PenalizedXp(1000, 60, 54) == 900 && PenalizedXp(1000, 60, 20) == 30, "scaled");
	Check(PenalizedXp(0, 60, 20) == 0 && PenalizedXp(-5, 60, 20) == -5, "zero and negative experience are left alone");
	Check(PenalizedXp(2000000000, 60, 54) == 1800000000, "64-bit product, no overflow");
	Check(PenalizedXp(7, 60, 54) == 6 && PenalizedXp(1, 60, 20) == 0, "truncation toward zero");

	// runeword extras: tools/items_data.py
	using namespace mxl_runewords;
	Check(ExtraSlots == 6 && RowCount == 237, "table shape");
	Check(ExtraCount == 197, "197 of 237 runewords have properties 8-13");
	int32_t previous = -1;
	int total = 0;
	bool contiguous = true;
	bool inRange = true;
	for (int32_t i = 0; i < ExtraCount; ++i) {
		const Extra& e = Extras[i];
		if (e.row <= previous) {
			Check(false, "rows are sorted and unique");
		}
		previous = e.row;
		inRange = inRange && e.row >= 0 && e.row < RowCount && e.rune1 != 0;
		const int32_t used = UsedSlots(e);
		contiguous = contiguous && used >= 1;
		for (int32_t k = used; k < ExtraSlots; ++k) {
			contiguous = contiguous && e.props[k].prop < 0;
		}
		total += used;
	}
	Check(inRange, "rows inside the table, every row has a first rune");
	Check(contiguous, "used slots are contiguous and non-empty");
	Check(total == 630, "630 properties in all");
	Check(FindExtra(0) == nullptr && FindExtra(RowCount) == nullptr && FindExtra(-1) == nullptr, "Jewelwords and out-of-range rows have none");
	const Extra* gehenna = FindExtra(7);
	Check(gehenna != nullptr && UsedSlots(*gehenna) == 2 && gehenna->rune1 == 0x20323372u, "Gehenna: two extra properties, first rune r32");
	Check(gehenna != nullptr && gehenna->props[0].min == 25 && gehenna->props[0].max == 25, "Gehenna: first extra is 25..25");
	for (int32_t i = 0; i < ExtraCount; ++i) {
		Check(FindExtra(Extras[i].row) == &Extras[i], "binary search finds every row");
	}

	// Jewel padding (D2Runeword.cpp 0x100a2b60): jewels first, then the runes, in socket order
	{
		const int32_t jew = 900, r1 = 11, r2 = 12;
		const int32_t shark[6] = {r1, 0, 0, 0, 0, 0};
		const int32_t two[6] = {r1, r2, 0, 0, 0, 0};
		const int32_t jewels[6] = {jew, 0, 0, 0, 0, 0};
		const int32_t a[] = {jew, jew, r1};
		Check(PaddedRunesMatch(a, 3, shark, jew), "one rune, two Jewels in front");
		const int32_t b[] = {r1, jew, jew};
		Check(!PaddedRunesMatch(b, 3, shark, jew), "Jewels behind the rune do not pad");
		const int32_t c[] = {jew, r1, r2};
		Check(PaddedRunesMatch(c, 3, two, jew) && !PaddedRunesMatch(c, 3, shark, jew), "two runes, one Jewel in front");
		const int32_t d[] = {r1};
		Check(!PaddedRunesMatch(d, 1, shark, jew), "equal length is the exact match's job");
		const int32_t e[] = {r1, r2};
		Check(!PaddedRunesMatch(e, 2, shark, jew), "a longer record never matches a shorter socket list");
		const int32_t f[] = {jew, jew, jew};
		Check(!PaddedRunesMatch(f, 3, jewels, jew), "a Jewel-only row is not padded (Jewelword1 would shadow 2-6)");
		const int32_t g[] = {jew, r2, r1};
		Check(!PaddedRunesMatch(g, 3, two, jew), "runes in the recipe's order");
		const int32_t h[] = {jew, jew, jew, jew, jew, r1};
		Check(PaddedRunesMatch(h, 6, shark, jew), "six sockets");
		const int32_t i[] = {jew, jew, jew, jew, jew, jew, r1};
		Check(!PaddedRunesMatch(i, 7, shark, jew) && !PaddedRunesMatch(a, 0, shark, jew) && !PaddedRunesMatch(nullptr, 3, shark, jew),
		      "bounds");
		const int32_t none[6] = {};
		Check(!PaddedRunesMatch(a, 3, none, jew), "a row without runes");
	}

	// record arithmetic
	alignas(8) static uint8_t table[0xe8 * 4];
	Check(RowOfRecord(table, table, 4, 0xe8) == 0 && RowOfRecord(table + 0xe8 * 3, table, 4, 0xe8) == 3, "record rows");
	Check(RowOfRecord(table + 0xe8 * 4, table, 4, 0xe8) == -1, "past the end");
	Check(RowOfRecord(table + 1, table, 4, 0xe8) == -1 && RowOfRecord(nullptr, table, 4, 0xe8) == -1, "unaligned and null");
	Check(RowOfRecord(table, table + 8, 4, 0xe8) == -1, "record before the table");

	return failures;
}

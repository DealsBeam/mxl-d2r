// Unique `carry1` as Median (and Diablo II) mean it: a second copy of the very same unique is refused, a different carry1 unique is
// not. Pure rule, unit tested (tests/unique_carry_test.cpp); unique_carry.cpp applies it to D2R's unique records.
//
// D2R 3.3 reads the carry1 column of uniqueitems.txt as a number, the record's dword at +0x30, and its placement test
// (0x473960, and the callers of the same record comparison) refuses a carry1 unique when a carried unique has the SAME non-zero
// number. Median's file has 1 in all 89 carry1 rows, so every carry1 unique counted as "the same" and only one of them fitted
// in the backpack (the Butcher's Tooth refused the Laser Focus Crystal). Giving each carry1 row its own number restores the
// meaning "this unique, once": the number is the row + 1, so two copies of one row still match.
#pragma once

#include <cstdint>

namespace unique_carry {

inline constexpr int RecordSize = 0x15c;  // bytes of a uniqueitems record in D2R's table
inline constexpr int CarryOffset = 0x30;  // the carry1 dword inside a record

// The carry number a record of unique `row` (its index in uniqueitems.txt) gets, given the number it has now: 0 stays 0
// (no carry limit), any other value becomes row + 1, which is different for every row and the same on every call.
inline auto CarryValue(int32_t row, int32_t now) noexcept -> int32_t {
	return now > 0 ? row + 1 : now;
}

}  // namespace unique_carry

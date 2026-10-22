// Tests of the stat sheet columns and the Strength / Dexterity damage bonus (plugin/src/stat_columns_logic.h).
#include "../src/stat_columns_logic.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: stat columns: %s\n", what);
	}
}

}  // namespace

int RunStatColumnsTests() {
	using namespace stat_cols;

	// Str / Dex damage bonus: stat * weapon bonus / 100, truncated
	Check(DamageBonus(100, 100) == 100 && DamageBonus(150, 100) == 150, "a bonus of 100 gives one percent per point");
	Check(DamageBonus(100, 75) == 75 && DamageBonus(33, 75) == 24 && DamageBonus(1, 75) == 0, "partial bonuses truncate");
	Check(DamageBonus(0, 100) == 0 && DamageBonus(250, 0) == 0, "zero operands give zero");
	Check(DamageBonus(2000000, 3000) == 60000000, "no 32-bit overflow in the product");
	Check(DamageBonus(-50, 100) == -50 && DamageBonus(-1, 75) == 0, "negative stats truncate toward zero");

	// life, mana and stamina are shown shifted by 8; the others by 0
	Check(Shifted(3060 * 256, 8) == 3060 && Shifted(3060 * 256 + 255, 8) == 3060 && Shifted(77, 0) == 77, "ValShift");
	Check(Shifted(-256, 8) == -1, "arithmetic shift");

	// the appended columns
	char buf[64];
	int n = ColumnsText(buf, sizeof(buf), true, 20, 190);
	Check(std::strcmp(buf, " (+20% bonus, 190 base)") == 0 && n == static_cast<int>(std::strlen(buf)), "columns text with a bonus");
	ColumnsText(buf, sizeof(buf), true, 0, 25);
	Check(std::strcmp(buf, " (+0% bonus, 25 base)") == 0, "a zero bonus is still shown (Median shows 0%)");
	ColumnsText(buf, sizeof(buf), true, -15, 25);
	Check(std::strcmp(buf, " (-15% bonus, 25 base)") == 0, "negative bonus");
	ColumnsText(buf, sizeof(buf), false, 0, 120);
	Check(std::strcmp(buf, " (120 base)") == 0, "no bonus column (stamina)");
	char small[8];
	ColumnsText(small, sizeof(small), true, 20, 190);
	Check(small[7] == '\0', "short buffer stays terminated");
	return failures;
}

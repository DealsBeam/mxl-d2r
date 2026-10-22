// Tests of the stat sheet columns and the Strength / Dexterity damage bonus (plugin/src/stat_columns_logic.h).
#include "../src/mxl_statorder.h"
#include "../src/stat_columns_logic.h"
#include "../src/stat_order_logic.h"

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
	// Median's order on the Advanced Stats panel
	{
		using namespace stat_order;
		Check(IsHeader(HeaderId(0)) && IsHeader(HeaderId(102)) && !IsHeader(512) && EntryOf(HeaderId(57)) == 57, "label record ids");
		Check(Listed(true, 0) && Listed(false, 5) && Listed(false, -3) && !Listed(false, 0), "always listed or value not 0");
		struct E {
			int32_t stat;
			const char* header;
			bool always;
		};
		const E order[] = {{-1, "A", false}, {10, nullptr, false}, {11, nullptr, true}, {-1, "B", false}, {12, nullptr, false}, {-1, "C", false}};
		auto none = [](int32_t, bool always) { return always; };
		auto only12 = [](int32_t stat, bool always) { return always || stat == 12; };
		Check(SectionListed(order, 6, 0, none) && !SectionListed(order, 6, 3, none), "a label is listed while an always stat or a value is");
		Check(SectionListed(order, 6, 3, only12) && !SectionListed(order, 6, 5, only12), "the section ends at the next label and at the end");
		char text[32];
		HeaderText("Speed", text, sizeof(text));
		Check(std::strcmp(text, "== Speed ==") == 0, "label text is the label between marks, without colour codes");
		HeaderText("Speed", text, 5);
		Check(text[4] == '\0', "short label buffer stays terminated");

		// the generated list (tools/stat_screen.py section_order)
		using mxl_statorder::Count;
		using mxl_statorder::Order;
		Check(Count > 80 && Order[0].stat == -1 && Order[0].header != nullptr, "the generated list starts with a section label");
		bool labelsOk = true, statsOk = true, seen[1024] = {};
		int always = 0;
		for (int32_t i = 0; i < Count; ++i) {
			if (Order[i].stat < 0) {
				labelsOk = labelsOk && Order[i].header != nullptr && Order[i].header[0] != '\0' && !Order[i].always &&
				           (i + 1 < Count && Order[i + 1].stat >= 0);  // every label heads at least one stat
			} else {
				statsOk = statsOk && Order[i].header == nullptr && Order[i].stat < HeaderBase && Order[i].stat < 1024 && !seen[Order[i].stat];
				seen[Order[i].stat] = true;
				always += Order[i].always ? 1 : 0;
			}
		}
		Check(labelsOk, "every label has text and heads a stat");
		Check(statsOk, "every stat appears once, below the label ids");
		Check(always == 15, "the fifteen boxes that are never empty are always listed");
	}

	return failures;
}

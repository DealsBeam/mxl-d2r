// Tests of the rules of Median's item property lines (plugin/src/item_text_logic.h, docs/item-tooltips.md).
#include "../src/item_text_logic.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: item text: %s\n", what);
	}
}

}  // namespace

int RunItemTextTests() {
	using namespace item_text;
	// class lock: class + 1, 8 and 0 mean no class
	Check(ClassLockClass(1) == 0 && ClassLockClass(7) == 6, "locks 1-7 are the seven classes");
	Check(ClassLockClass(0) == -1 && ClassLockClass(8) == -1 && ClassLockClass(-3) == -1, "0 and 8 lock nothing");
	// descfunc 32: tenths, never 0 for a non-zero value
	Check(TenthsValue(95) == 9 && TenthsValue(5) == 1 && TenthsValue(-5) == -1 && TenthsValue(0) == 0, "tenths round away from 0 only to 1");
	char text[64];
	SignedNumber(text, sizeof(text), 12);
	Check(std::strcmp(text, "+12") == 0, "positive numbers carry a plus");
	SignedNumber(text, sizeof(text), -4);
	Check(std::strcmp(text, "-4") == 0, "negative numbers keep their minus");
	SignedNumber(text, sizeof(text), 0);
	Check(std::strcmp(text, "0") == 0, "zero has no sign");
	ValueAndText(text, sizeof(text), 5, 1, "Life");
	Check(std::strcmp(text, "+5 Life") == 0, "descval 1: value first");
	ValueAndText(text, sizeof(text), 5, 2, "Frequency");
	Check(std::strcmp(text, "Frequency +5") == 0, "descval 2: text first");
	ValueAndText(text, sizeof(text), 5, 0, "Plain");
	Check(std::strcmp(text, "Plain") == 0, "descval 0: text alone");
	// descfunc 5: out of 128, a percent sign
	Check(PercentOf128(12) == 9 && PercentOf128(128) == 100 && PercentOf128(0) == 0, "value out of 128");
	PercentText(text, sizeof(text), 9, 2, "Flee");
	Check(std::strcmp(text, "Flee 9%") == 0, "descval 2: text then percent");
	PercentText(text, sizeof(text), 9, 1, "Flee");
	Check(std::strcmp(text, "9% Flee") == 0, "descval 1: percent then text");
	// descfunc 33: frames to seconds, sign picks the text
	Check(CooldownSeconds(50) == 2.0 && CooldownSeconds(-10) == 0.4 && CooldownReduced(10) && !CooldownReduced(-10), "cooldown frames");
	// descfunc 35: element, attribute, percent, damage
	Check(InnateElement(0x0302) == 2 && InnateElement(0x0200) == 0 && InnateElement(0x0309) == 0, "element is the low byte, out of range = fire");
	Check(InnateAttribute(0x0302) == 3 && InnateAttribute(0x0402) == 4 && InnateAttribute(0x0902) == 0, "attribute is the next byte, out of range = Strength");
	Check(InnatePercent(45, 20) == 54.0 && InnatePercent(45, 0) == 45.0, "percent adds the character's innate damage share");
	Check(InnateDamage(100, 54.0) == 54 && InnateDamage(99, 50.0) == 49, "damage is the percent of the attribute, truncated");
	Check(InnateDamage(199, 10.5) == 19, "classic tooltip truncates the effective percent before multiplying the attribute");
	return failures;
}

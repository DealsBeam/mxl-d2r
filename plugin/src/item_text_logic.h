// Median's item property lines that D2R's own formatter does not write (pure rules; item_text.cpp feeds them to the hook of
// 0x2d6520, docs/item-tooltips.md).
//
// D2Sigma D2ItemDesc.cpp keeps the lines of the descfuncs 31-38 at 0x10077720 ... 0x10078180; D2R's formatter ends the property
// without text for every descfunc above 31 and prints a Chronicle text for 31. The rules below follow those functions:
//   32 (stat 534)  the value / 10 (at least 1 away from 0), with a "+" when positive, before / after the string (descval 1 / 2)
//                  separated by a space
//   5  (stat 112)  the value * 100 / 128 with a percent sign, before / after the string (descval 1 / 2), when the string has no
//                  printf conversion of its own (D2R's descfunc 5 formats a string that has one; Median's does not)
//   33 (stat 309)  the cooldown of the skill named by the layer: "%s Cooldown Reduced by %.1g" with the value in seconds
//   35 (stat 444)  "Innate <element> Damage: N (P% of <attribute>)": the element is the low byte of the layer (a string each),
//                  the attribute the next byte: 0 Strength, 1 Energy, 2 Dexterity, 3 Vitality, 4 max(Strength, Dexterity)
//   36 (stat 289)  the string, only while the value is 2
//   37 (stat 478)  "%s Maximum Skill Level Increased by %d" with the skill of the layer
//   38 (stat 456)  like 32 without the division
// (D2Sigma shifts the value right by ValShift in these functions; D2R's caller 0x2d97e0 has done that before the formatter runs.)
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace item_text {

// Frames per second of Median's cooldown values (D2 runs at 25; the divisor Sigma uses is a constant of the DLL's data that
// could not be read, docs/item-tooltips.md).
inline constexpr double FramesPerSecond = 25.0;

// Item stat 492 (Median 463) holds class + 1; 8 (class 7) and 0 are "no class".
constexpr auto ClassLockClass(int32_t value) noexcept -> int {
	return value >= 1 && value <= 7 ? value - 1 : -1;
}

// Descfunc 32: value / 10 truncated toward 0 but never 0 (a positive value gives at least 1, a negative at least -1).
constexpr auto TenthsValue(int32_t raw) noexcept -> int32_t {
	int32_t n = raw / 10;
	if (raw > 0 && n < 1) {
		n = 1;
	} else if (raw < 0 && n > -1) {
		n = -1;
	}
	return n;
}

// "+12" / "-5" / "0": Sigma writes "%s%ld" with the plus sign for a positive value only.
inline auto SignedNumber(char* out, size_t size, int32_t n) noexcept -> int {
	return n < 1 ? std::snprintf(out, size, "%ld", static_cast<long>(n)) : std::snprintf(out, size, "+%ld", static_cast<long>(n));
}

// The value and the text of a line of the descfuncs 32 / 38: descval 1 = value first, 2 = text first, else the text alone.
inline auto ValueAndText(char* out, size_t size, int32_t n, int descval, const char* text) noexcept -> int {
	char number[24];
	SignedNumber(number, sizeof(number), n);
	if (descval == 1) {
		return std::snprintf(out, size, "%s %s", number, text);
	}
	if (descval == 2) {
		return std::snprintf(out, size, "%s %s", text, number);
	}
	return std::snprintf(out, size, "%s", text);
}

// Descfunc 5: the value is out of 128 ("Hit Causes Monster to Flee 9%").
constexpr auto PercentOf128(int32_t value) noexcept -> int32_t {
	return value * 100 / 128;
}
inline auto PercentText(char* out, size_t size, int32_t percent, int descval, const char* text) noexcept -> int {
	if (descval == 1) {
		return std::snprintf(out, size, "%d%% %s", static_cast<int>(percent), text);
	}
	if (descval == 2) {
		return std::snprintf(out, size, "%s %d%%", text, static_cast<int>(percent));
	}
	return std::snprintf(out, size, "%s", text);
}

// Descfunc 33: the cooldown change in seconds, always positive; `reduced` says which text to take (a negative value lengthens
// the cooldown: Median's "Increase" string).
constexpr auto CooldownSeconds(int32_t frames) noexcept -> double {
	return (frames < 0 ? -static_cast<double>(frames) : static_cast<double>(frames)) / FramesPerSecond;
}
constexpr auto CooldownReduced(int32_t frames) noexcept -> bool {
	return frames >= 0;
}

// Descfunc 35. The element (low byte of the layer) selects one of six strings, 0 = fire (ModitemEleInnateSeq, string id 11600),
// 1 lightning, 2 cold, 3 poison, 4 magic, 5 all three elements (ids 11601-11605); anything else reads as fire.
constexpr auto InnateElement(uint32_t layer) noexcept -> int {
	const uint32_t element = layer & 0xff;
	return element <= 5 ? static_cast<int>(element) : 0;
}
// The attribute (the byte above the element): 0-3 name a stat (Strength, Energy, Dexterity, Vitality: stats 0-3), 4 is Strength
// or Dexterity, whichever is larger (named by two stats), anything above reads as 0.
constexpr auto InnateAttribute(uint32_t layer) noexcept -> int {
	const uint32_t attribute = (layer >> 8) & 0xff;
	return attribute <= 4 ? static_cast<int>(attribute) : 0;
}
// The percent shown: the property's value plus the same share of the character's Innate Elemental Damage (stat 513).
constexpr auto InnatePercent(int32_t value, int32_t innateElementalDamage) noexcept -> double {
	return static_cast<double>(value) + static_cast<double>(value) * innateElementalDamage / 100.0;
}
// D2Sigma 0x10077e42 truncates the percentage first (cvttsd2si),
// then 0x10088e50 multiplies the attribute and divides by 100 in 64 bits.
// This display arithmetic differs from the hidden innate passive's formula.
constexpr auto InnateDamage(int32_t attribute, double percent) noexcept -> int32_t {
	return static_cast<int32_t>(static_cast<int64_t>(attribute) * static_cast<int32_t>(percent) / 100);
}

}  // namespace item_text

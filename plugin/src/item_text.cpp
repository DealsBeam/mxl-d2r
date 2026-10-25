// Median's item property lines in D2R's item tooltip. See item_text.h and docs/item-tooltips.md.
//
//   0x2d6520 (viewer, itemstatcost record, value, layer, flag, char out[256], mode) -> al: one property line, the descfunc
//            switch of D2Client\src\UI\mods.cpp. al = 1 when it wrote text.
//
// D2R's descfunc 31 means Chronicle text, while 32-38 produce no line. Sigma assigns these functions its own meanings.
// The hook implements those meanings, resolves layered Median string IDs, skips AltTooltip passive skill names, and
// supplies the class lock line. Other descriptions pass through to D2R.
#include "item_text.h"

#include "d2r.h"
#include "item_text_logic.h"
#include "mxl_special_text.h"
#include "strings.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {

constexpr uint64_t FormatRva = 0x2d6520;
constexpr uint64_t GetStatRva = 0x2f5020;       // (unit, stat, layer)
constexpr uint64_t LocalSlotRva = 0x8b2d0;      // () -> slot of the local player (skill_tree.cpp)
constexpr uint64_t SlotUnitRva = 0x9a480;       // (slot) -> the player unit of that slot or null
constexpr uint64_t UnitDataFlagRva = 0x34a0e0;  // (unit) -> byte at unit + 0x1bd: the data flag (client / server tables) the table lookups take
constexpr uint64_t SkillNameIdRva = 0x28bcf0;   // (data flag, skill, 0) -> string id of the skill's name, 0x1506 when there is none
constexpr uint64_t StringByIdRva = 0x3e2b730;   // (id) -> text

// 0x2d6520: push rbp / rbx / rsi / rdi / r12 / r13 / r14 / r15 / lea rbp,[rsp-0x988] (21 bytes)
constexpr uint8_t FormatEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                   0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0x78, 0xf6, 0xff, 0xff};

// itemstatcost record (0x144 bytes): the fields the lines need
constexpr int RecDescFunc = 0x32;
constexpr int RecDescVal = 0x33;
constexpr int RecDescStrPos = 0x34;
constexpr int RecDescStrNeg = 0x36;

constexpr int StatClassLock = 492;
constexpr int StatInnateElementalDamage = 513;  // Median 484, the multiplier of the innate damage line
constexpr uint16_t MissingString = 0x1506;
constexpr int LineSize = 256;                   // the formatter's output buffer

using FormatFn = char (*)(void* viewer, const int16_t* record, int32_t value, uint32_t layer, char flag, char* out, int32_t mode);
using GetStatFn = int32_t (*)(void* unit, int32_t stat, int32_t layer);
using LocalSlotFn = int32_t (*)();
using SlotUnitFn = void* (*)(int32_t slot);
using UnitDataFlagFn = uint8_t (*)(void* unit);
using SkillNameIdFn = uint16_t (*)(uint8_t dataFlag, int32_t skill, int32_t zero);
using StringByIdFn = const char* (*)(uint32_t id);

FormatFn g_formatOriginal = nullptr;
GetStatFn g_getStat = nullptr;
LocalSlotFn g_localSlot = nullptr;
SlotUnitFn g_slotUnit = nullptr;
UnitDataFlagFn g_dataFlag = nullptr;
SkillNameIdFn g_skillNameId = nullptr;
StringByIdFn g_stringById = nullptr;
FormatObserver g_observer = nullptr;
bool g_installed = false;
bool g_lines = false;
volatile LONG g_written = 0;

template <typename T>
auto Field(const int16_t* record, int offset) noexcept -> T {
	return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(record) + offset);
}

// The text D2R's string lookup gives for a record's string id; null when there is none.
auto IdText(uint16_t id) noexcept -> const char* {
	if (id == 0 || id == MissingString) {
		return nullptr;
	}
	const char* text = g_stringById(id);
	return text != nullptr && text[0] != '\0' ? text : nullptr;
}

// The record's text for the value's sign (descstrpos, descstrneg for a negative value; the other one when it is empty).
auto RecordText(const int16_t* record, int32_t value) noexcept -> const char* {
	const uint16_t pos = Field<uint16_t>(record, RecDescStrPos);
	const uint16_t neg = Field<uint16_t>(record, RecDescStrNeg);
	const char* text = IdText(value < 0 ? neg : pos);
	return text != nullptr ? text : IdText(value < 0 ? pos : neg);
}

// The unit the lines are about. The item text builder passes the ITEM as the first argument (read in game: unit type 4), but the
// stats the lines name (Strength, the class) are the local player's: tooltips are drawn for the player who looks at them.
auto Player(void* viewer) noexcept -> void* {
	void* unit = g_slotUnit != nullptr ? g_slotUnit(g_localSlot()) : nullptr;
	return unit != nullptr ? unit : viewer;
}

// The name of a skill as D2R's "+N to <skill>" lines write it; null when the id names no skill.
auto SkillName(void* viewer, int32_t skill) noexcept -> const char* {
	void* player = Player(viewer);
	if (player == nullptr || skill <= 0) {
		return nullptr;
	}
	return IdText(g_skillNameId(g_dataFlag(player), skill, 0));
}

// Writes the line of a descfunc / stat of Median's. False when the call is not one of these (the game's formatter answers);
// `ret` is the formatter's return value then.
auto OwnLine(void* viewer, const int16_t* record, int32_t value, uint32_t layer, char flag, char* out, char* ret) noexcept -> bool {
	if (flag != 0) {
		return false;  // the group line of a stat reads the dgrp fields: not Median's descfunc 31-38 lines
	}
	const int32_t stat = Field<int16_t>(record, 0);
	const int func = Field<uint8_t>(record, RecDescFunc);
	const int descval = Field<uint8_t>(record, RecDescVal);
	char text[LineSize];
	text[0] = '\0';

	if (func == 27 || func == 28) {
		// Sigma 0x100774c0 skips skills2 AltTooltip skills before looking up a name. Azure Wrath's hidden
		// passive (Median skill 1651) has no skilldesc: D2R would print "+2 to FLYING POLAR BUFFALO ERROR".
		bool alternate = false;
		for (int32_t skill : mxl_special_text::AltTooltipSkills) alternate |= skill == static_cast<int32_t>(layer);
		if (!alternate) return false;
	} else if (stat == StatClassLock && func != 0) {
		static const char* const Keys[] = {"d2r:strAmazonOnly",    "d2r:strSorceressOnly", "d2r:strNecromanerOnly", "d2r:strPaladinOnly",
		                                   "d2r:strBarbarianOnly", "d2r:strDruidOnly",     "d2r:strAssassinOnly"};
		static const char* const Fallbacks[] = {"(Amazon Only)",    "(Sorceress Only)", "(Necromancer Only)", "(Paladin Only)",
		                                        "(Barbarian Only)", "(Druid Only)",     "(Assassin Only)"};
		const int cls = item_text::ClassLockClass(value);
		if (cls >= 0 && cls < 7) {
			StringText(Keys[cls], Fallbacks[cls], text, sizeof(text));
		}
	} else if (func == 5 && RecordText(record, value) != nullptr && std::strchr(RecordText(record, value), '%') == nullptr) {
		// Median's string has no conversion for the value (Hit Causes Monster to Flee); D2R's own text has one and D2R's function formats it
		item_text::PercentText(text, sizeof(text), item_text::PercentOf128(value), descval, RecordText(record, value));
	} else if (func == 31 || func == 34) {
		// Sigma 0x10077720 / 0x10077cb0: the low layer word is a Median string ID. The value is a colour,
		// not a number to interpolate. Numeric D2R lookup would name an unrelated string after namespacing.
		const uint16_t id = static_cast<uint16_t>(layer);
		const auto* first = std::begin(mxl_special_text::Entries);
		const auto* last = std::end(mxl_special_text::Entries);
		while (first < last) {
			const auto* mid = first + (last - first) / 2;
			if (mid->id < id) first = mid + 1;
			else last = mid;
		}
		if (first != std::end(mxl_special_text::Entries) && first->id == id) {
			char label[1024];
			if (StringLookup(first->key, label, sizeof(label))) {
				const int colour = func == 34 ? 8 : (value >= 1 && value <= 13 ? value - 1 : 3);
				// D2R's UTF-8 text colour escape (the equivalent of Sigma's wide "ÿc" + ('0' + colour)).
				std::snprintf(text, sizeof(text), "\xee\x81\xbe%c%s", '0' + colour, label);
				// Do not leave an incomplete UTF-8 character when a long Median paragraph reaches D2R's 256-byte line limit.
				if (std::strlen(label) + 4 >= sizeof(text)) {
					size_t end = sizeof(text) - 1;
					while (end > 4 && (static_cast<unsigned char>(label[end - 4]) & 0xc0) == 0x80) --end;
					// A complete UTF-8 colour introducer without its following selector is still an incomplete escape.
					if (end >= 3 && (std::memcmp(text + end - 3, "\xee\x81\xbe", 3) == 0 ||
					                 std::memcmp(text + end - 3, "\xc3\xbf" "c", 3) == 0)) end -= 3;
					else if (end >= 2 && std::memcmp(text + end - 2, "\xc3\xbf", 2) == 0) end -= 2;
					text[end] = '\0';
				}
			}
		}
	} else if (func == 32 || func == 38) {
		const char* label = RecordText(record, value);
		if (label != nullptr) {
			// the value arrives shifted by ValShift already: the caller (0x2d97e0) does that, the formatter never reads the column
			const int32_t n = func == 32 ? item_text::TenthsValue(value) : value;
			item_text::ValueAndText(text, sizeof(text), n, descval, label);
		}
	} else if (func == 33) {
		const char* skill = SkillName(viewer, static_cast<int32_t>(layer & 0xffff));
		const char* format = RecordText(record, item_text::CooldownReduced(value) ? 1 : -1);
		if (skill != nullptr && format != nullptr) {
			std::snprintf(text, sizeof(text), format, skill, item_text::CooldownSeconds(value));
		}
	} else if (func == 35) {
		static const char* const ElementKeys[] = {"medianxl:ModitemEleInnateSeq", "medianxl:mxl_11601", "medianxl:mxl_11602",
		                                          "medianxl:mxl_11603",           "medianxl:mxl_11604", "medianxl:mxl_11605"};
		char format[LineSize];
		if (StringLookup(ElementKeys[item_text::InnateElement(layer)], format, sizeof(format))) {
			const int attribute = item_text::InnateAttribute(layer);
			char name[96];
			char second[48];
			static const char* const NameKeys[] = {"d2r:strchrstr", "d2r:strchreng", "d2r:strchrdex", "d2r:strchrvit", "d2r:strchrstr"};
			static const char* const NameFallbacks[] = {"Strength", "Energy", "Dexterity", "Vitality", "Strength"};
			StringText(NameKeys[attribute], NameFallbacks[attribute], name, sizeof(name));
			void* player = Player(viewer);
			int32_t points = g_getStat(player, attribute == 4 ? 0 : attribute, 0);
			if (attribute == 4) {
				points += g_getStat(player, 2, 0);
				StringText("d2r:strchrdex", "Dexterity", second, sizeof(second));
				std::snprintf(name + std::strlen(name), sizeof(name) - std::strlen(name), "/%s", second);
			}
			const double percent = item_text::InnatePercent(value, g_getStat(player, StatInnateElementalDamage, 0));
			std::snprintf(text, sizeof(text), format, item_text::InnateDamage(points, percent), percent, name);
		}
	} else if (func == 36) {
		if (value == 2) {
			const char* label = RecordText(record, value);
			if (label != nullptr) {
				std::snprintf(text, sizeof(text), "%s", label);
			}
		}
	} else if (func == 37) {
		const char* skill = SkillName(viewer, static_cast<int32_t>(layer & 0xffff));
		const char* format = RecordText(record, value);
		if (skill != nullptr && format != nullptr) {
			std::snprintf(text, sizeof(text), format, skill, value);
		}
	} else {
		return false;
	}
	std::snprintf(out, LineSize, "%s", text);
	*ret = text[0] != '\0' ? 1 : 0;
	if (*ret != 0) {
		InterlockedIncrement(&g_written);
	}
	return true;
}

char FormatHook(void* viewer, const int16_t* record, int32_t value, uint32_t layer, char flag, char* out, int32_t mode) {
	if (out != nullptr && g_observer != nullptr) {
		out[0] = '\0';  // a descfunc without text leaves the caller's buffer as it was; the readback must not show old text
	}
	char ret = 0;
	if (!(g_lines && out != nullptr && record != nullptr && viewer != nullptr && OwnLine(viewer, record, value, layer, flag, out, &ret))) {
		ret = g_formatOriginal(viewer, record, value, layer, flag, out, mode);
	}
	if (g_observer != nullptr) {
		g_observer(viewer, record, value, layer, flag, mode, ret, out);
	}
	return ret;
}

}  // namespace

auto InstallItemText(const D2RL::PluginContext* context, bool lines, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	if (std::memcmp(reinterpret_cast<const void*>(base + FormatRva), FormatEntry, sizeof(FormatEntry)) != 0) {
		std::snprintf(report, reportSize, "off: entry bytes of 0x2d6520 differ");
		return false;
	}
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	// the local player (skill_tree.cpp checks the same two functions); without them the lines use the first argument
	constexpr uint8_t LocalSlotEntry[] = {0x8b, 0x05, 0x2e, 0x84, 0x99, 0x02, 0xc3};
	constexpr uint8_t SlotUnitEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x83, 0xf9, 0x08};
	if (std::memcmp(reinterpret_cast<const void*>(base + LocalSlotRva), LocalSlotEntry, sizeof(LocalSlotEntry)) == 0 &&
	    std::memcmp(reinterpret_cast<const void*>(base + SlotUnitRva), SlotUnitEntry, sizeof(SlotUnitEntry)) == 0) {
		g_localSlot = reinterpret_cast<LocalSlotFn>(base + LocalSlotRva);
		g_slotUnit = reinterpret_cast<SlotUnitFn>(base + SlotUnitRva);
	}
	g_dataFlag = reinterpret_cast<UnitDataFlagFn>(base + UnitDataFlagRva);
	g_skillNameId = reinterpret_cast<SkillNameIdFn>(base + SkillNameIdRva);
	g_stringById = reinterpret_cast<StringByIdFn>(base + StringByIdRva);
	g_lines = lines;
	g_installed = context->InstallInlineHook(FormatRva, FormatEntry, sizeof(FormatEntry), reinterpret_cast<void*>(&FormatHook),
	                                         reinterpret_cast<void**>(&g_formatOriginal));
	if (!g_installed) {
		std::snprintf(report, reportSize, "off: the hook of 0x2d6520 was refused");
		return false;
	}
	std::snprintf(report, reportSize, "%s: item property lines (0x2d6520: descfunc 5 without a conversion, 31 / 34 layered text and colours, 32 / 33 / 35-38 lines, class lock stat 492)",
	              lines ? "on" : "hooked for the harness readback only");
	return true;
}

auto ItemTextFormat(void* viewer, const int16_t* record, int32_t value, uint32_t layer, char flag, char* out, int32_t mode) noexcept -> char {
	return g_installed ? FormatHook(viewer, record, value, layer, flag, out, mode) : 0;
}

auto ItemTextHooked() noexcept -> bool {
	return g_installed;
}

void SetFormatObserver(FormatObserver observer) noexcept {
	g_observer = observer;
}

auto ItemTextLinesWritten() noexcept -> uint32_t {
	return static_cast<uint32_t>(g_written);
}

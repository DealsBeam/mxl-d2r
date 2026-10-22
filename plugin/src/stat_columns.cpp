// Median's character sheet page 2 on D2R's Advanced Stats panel: the Strength / Dexterity damage bonus rows and the
// total / % bonus / base columns (docs/subsystems/stat-screen.md).
//
// Median's second sheet page (statscreen.bin) shows, besides plain stats, values its code computes: the damage bonus
// (type 0x17 rows 115 and 117: stat 299 / 300 are placeholders), and attribute rows with three columns - total (GetStat), a
// percent stat (to_strength ...), base (GetBaseStat). D2R's Advanced Stats panel lists itemstatcost rows by data
// (`advdisplay`, tools/txt_migrate.py advanced_stats) with one value each, so two functions of the panel are hooked:
//
//   0x14eb1b0 (panel, record) -> the displayed value (stat >> ValShift) a row is listed by (advdisplay 2: only when not 0)
//   0x14eb250 (panel, string*, record): builds the row text from the stat's own description (reads the stat with 0x2f5020)
//
// Both read the unit at panel + 0x168. The value hook answers the damage bonus rows with the computed value; the text hook
// lets D2R write its line, then for 299 / 300 re-formats the stat's description (0x2d97e0) with the computed value, and
// for a stat of the columns appends " (+20% bonus, 190 base)" (the string class is {char* data, size, capacity, 16 inline
// bytes}; 0x783d0 appends printf-style, as D2R does for its own suffixes). The computed bonus is Median's: the unit's total
// Strength / Dexterity times the StrBonus / DexBonus of the wielded weapon (D2R's getters 0x3736e0 / 0x373490; the item in the
// right hand body location 4 by 0x3886d0 of the inventory 0x34a360), 0 without a weapon. stat_columns_logic.h has the arithmetic; the stats of the
// columns and the two bonus rows are generated into mxl_statcols.h from statscreen.bin.
//
// Entry bytes checked against the dump of build 93847; whole instructions, no rip-relative.
#include "stat_columns.h"

#include "d2r.h"
#include "mxl_statcols.h"
#include "stat_columns_logic.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Unit;

constexpr uint64_t PanelValueRva = 0x14eb1b0;    // (panel, record) -> displayed value
constexpr uint64_t PanelTextRva = 0x14eb250;     // (panel, string*, record)
constexpr uint64_t AppendRva = 0x783d0;          // (string*, format, ...): appends printf-style
constexpr uint64_t FormatStatRva = 0x2d6520;     // (unit, record, value, 0, 0, char[256], 4): the stat's line, as 0x2d97e0 calls it
// (0x2d97e0 first scales the value by the character level for the by-level ops 2-5, which these two rows must not get)
constexpr uint64_t GetStatRva = 0x2f5020;        // (unit, stat, layer)
constexpr uint64_t BaseStatRva = 0x2f48c0;       // (unit, stat, layer): base value
constexpr uint64_t UnitInventoryRva = 0x34a360;  // (unit) -> inventory
constexpr uint64_t BodyLocItemRva = 0x3886d0;    // (inventory, body location) -> the item there or null
constexpr uint32_t RightHand = 4;                // body location of the wielded weapon (a weapon swap moves items between 4 / 5 and 11 / 12)
constexpr uint64_t StrBonusRva = 0x3736e0;       // (item) -> the base's StrBonus (record +0x116; 100 for 0 / 1 / no item)
constexpr uint64_t DexBonusRva = 0x373490;       // (item) -> the base's DexBonus (record +0x118)
constexpr int PanelUnit = 0x168;                 // panel: the unit whose stats it lists
constexpr int RecordShift = 0x14;                // itemstatcost record: ValShift (byte)
constexpr int StatStrength = 0;
constexpr int StatDexterity = 2;

// 0x14eb1b0: mov [rsp+0x10],rbx / mov [rsp+0x18],rsi / push rdi / sub rsp,0x30 (15 bytes)
constexpr uint8_t PanelValueEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x30};
// 0x14eb250: push rbp / push rbx / push rdi / push r15 / lea rbp,[rsp-0x478] (14 bytes)
constexpr uint8_t PanelTextEntry[] = {0x40, 0x55, 0x53, 0x57, 0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0x88, 0xfb, 0xff, 0xff};

// D2R's string class (the panel's row text): data pointer, length, capacity (top bit: inline), 16 inline bytes.
struct GameString {
	char* data;
	uint64_t size;
	uint64_t capacity;
	char inlineText[16];
};

using PanelValueFn = uint64_t (*)(void* panel, const int16_t* record);
using PanelTextFn = GameString* (*)(void* panel, GameString* out, const int16_t* record);  // returns `out`
using AppendFn = void (*)(GameString* out, const char* format, ...);
using FormatStatFn = void (*)(Unit* unit, const int16_t* record, int32_t value, uint32_t flags, char zero, char* buffer, int32_t mode);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using UnitInventoryFn = void* (*)(Unit* unit, const char* file, int line);  // file / line: the assert's call site
using BodyLocItemFn = void* (*)(void* inventory, uint32_t bodyLoc);
using ItemBonusFn = uint64_t (*)(void* item);

PanelValueFn g_valueOriginal = nullptr;
PanelTextFn g_textOriginal = nullptr;
AppendFn g_append = nullptr;
FormatStatFn g_formatStat = nullptr;
GetStatFn g_getStat = nullptr;
GetStatFn g_baseStat = nullptr;
UnitInventoryFn g_unitInventory = nullptr;
BodyLocItemFn g_bodyLocItem = nullptr;
ItemBonusFn g_strBonus = nullptr;
ItemBonusFn g_dexBonus = nullptr;
bool g_hooked = false;
uint64_t g_base = 0;

auto IsDamageStat(int32_t stat) noexcept -> bool {
	return stat == mxl_statcols::StrengthDamageStat || stat == mxl_statcols::DexterityDamageStat;
}

auto GroupOf(int32_t stat) noexcept -> const mxl_statcols::Group* {
	for (const mxl_statcols::Group& g : mxl_statcols::Groups) {
		if (g.stat == stat) {
			return &g;
		}
	}
	return nullptr;
}

auto WeaponOf(Unit* unit) noexcept -> void* {
	void* inventory = unit != nullptr ? g_unitInventory(unit, "mxl-core stat_columns.cpp", __LINE__) : nullptr;
	return inventory != nullptr ? g_bodyLocItem(inventory, RightHand) : nullptr;
}

// Median's Str. / Dex. Damage Bonus of a unit (percent), 0 without a wielded weapon.
auto DamageBonusOf(Unit* unit, int32_t damageStat) noexcept -> int32_t {
	void* weapon = WeaponOf(unit);
	if (weapon == nullptr) {
		return 0;
	}
	const bool strength = damageStat == mxl_statcols::StrengthDamageStat;
	const int32_t bonus = static_cast<int16_t>(strength ? g_strBonus(weapon) : g_dexBonus(weapon));
	return stat_cols::DamageBonus(g_getStat(unit, strength ? StatStrength : StatDexterity, 0), bonus);
}

auto ShiftOf(const int16_t* record) noexcept -> uint32_t {
	return d2r::At<uint8_t>(record, RecordShift);
}

auto PanelValueHook(void* panel, const int16_t* record) -> uint64_t {
	if (record != nullptr && panel != nullptr && IsDamageStat(*record)) {
		auto* unit = d2r::At<Unit*>(panel, PanelUnit);
		return static_cast<uint32_t>(stat_cols::Shifted(DamageBonusOf(unit, *record), ShiftOf(record)));
	}
	return g_valueOriginal(panel, record);
}

auto PanelTextHook(void* panel, GameString* out, const int16_t* record) -> GameString* {
	GameString* result = g_textOriginal(panel, out, record);
	if (record == nullptr || panel == nullptr || out == nullptr || out->data == nullptr) {
		return result;
	}
	auto* unit = d2r::At<Unit*>(panel, PanelUnit);
	if (unit == nullptr) {
		return result;
	}
	const int32_t stat = *record;
	if (IsDamageStat(stat)) {
		char line[256] = "";
		g_formatStat(unit, record, stat_cols::Shifted(DamageBonusOf(unit, stat), ShiftOf(record)), 0, 0, line, 4);
		if (line[0] != '\0') {
			out->size = 0;
			out->data[0] = '\0';
			g_append(out, "%s", line);
		}
		return result;
	}
	if (const mxl_statcols::Group* group = GroupOf(stat); group != nullptr && out->size != 0) {
		const uint32_t shift = ShiftOf(record);
		const bool hasBonus = group->bonusStat >= 0;
		char suffix[64];
		stat_cols::ColumnsText(suffix, sizeof(suffix), hasBonus, hasBonus ? g_getStat(unit, group->bonusStat, 0) : 0,
		                       stat_cols::Shifted(g_baseStat(unit, stat, 0), shift));
		g_append(out, "%s", suffix);
	}
	return result;
}

auto Matches(uint64_t base, uint64_t rva, const uint8_t* bytes, size_t size) noexcept -> bool {
	return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes, size) == 0;
}

// The itemstatcost record of a stat: D2R's data manager (0x300a90) keeps them in an array at +0x1258, 0x144 bytes each.
auto StatRecord(uint64_t base, int32_t stat) noexcept -> const int16_t* {
	for (int idx = 0; idx < 4; ++idx) {  // 0x300a90(idx) returns the pointer at exe + 0x2a9a580 + 16 * idx
		const auto* manager = *reinterpret_cast<const uint8_t* const*>(base + 0x2a9a580 + idx * 16);
		const auto* array = manager != nullptr ? d2r::At<const uint8_t*>(manager, 0x1258) : nullptr;
		if (array != nullptr && stat >= 0 && stat < d2r::At<int64_t>(manager, 0x1260)) {
			return reinterpret_cast<const int16_t*>(array + static_cast<size_t>(stat) * 0x144);
		}
	}
	return nullptr;
}

}  // namespace

auto InstallStatColumns(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	g_base = base;
	if (!Matches(base, PanelValueRva, PanelValueEntry, sizeof(PanelValueEntry)) ||
	    !Matches(base, PanelTextRva, PanelTextEntry, sizeof(PanelTextEntry))) {
		std::snprintf(report, reportSize, "off: entry bytes of 0x14eb1b0 / 0x14eb250 differ");
		return false;
	}
	g_append = reinterpret_cast<AppendFn>(base + AppendRva);
	g_formatStat = reinterpret_cast<FormatStatFn>(base + FormatStatRva);
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	g_baseStat = reinterpret_cast<GetStatFn>(base + BaseStatRva);
	g_unitInventory = reinterpret_cast<UnitInventoryFn>(base + UnitInventoryRva);
	g_bodyLocItem = reinterpret_cast<BodyLocItemFn>(base + BodyLocItemRva);
	g_strBonus = reinterpret_cast<ItemBonusFn>(base + StrBonusRva);
	g_dexBonus = reinterpret_cast<ItemBonusFn>(base + DexBonusRva);
	const bool value = context->InstallInlineHook(PanelValueRva, PanelValueEntry, sizeof(PanelValueEntry),
	                                              reinterpret_cast<void*>(&PanelValueHook),
	                                              reinterpret_cast<void**>(&g_valueOriginal));
	const bool text = context->InstallInlineHook(PanelTextRva, PanelTextEntry, sizeof(PanelTextEntry),
	                                             reinterpret_cast<void*>(&PanelTextHook),
	                                             reinterpret_cast<void**>(&g_textOriginal));
	g_hooked = value && text;
	std::snprintf(report, reportSize,
	              "%s: Advanced Stats panel rows - Str / Dex damage bonus (stats %d / %d) and %d total / %% bonus / base rows; "
	              "hooks 0x14eb1b0 %s, 0x14eb250 %s",
	              g_hooked ? "on" : "partial", mxl_statcols::StrengthDamageStat, mxl_statcols::DexterityDamageStat,
	              static_cast<int>(sizeof(mxl_statcols::Groups) / sizeof(mxl_statcols::Groups[0])), value ? "ok" : "NOT hooked",
	              text ? "ok" : "NOT hooked");
	return g_hooked;
}

auto StatColumnsProbe(Unit* unit, int32_t stat, int64_t* value, char* text, size_t textSize) noexcept -> bool {
	if (!g_hooked || unit == nullptr || value == nullptr || text == nullptr || textSize == 0) {
		return false;
	}
	const int16_t* record = StatRecord(g_base, stat);
	if (record == nullptr || *record != stat) {
		return false;
	}
	alignas(16) uint8_t panel[0x200] = {};
	std::memcpy(panel + PanelUnit, &unit, sizeof(unit));
	*value = static_cast<int32_t>(reinterpret_cast<PanelValueFn>(g_base + PanelValueRva)(panel, record));
	alignas(16) GameString row = {};
	reinterpret_cast<PanelTextFn>(g_base + PanelTextRva)(panel, &row, record);
	std::snprintf(text, textSize, "%s", row.data != nullptr ? row.data : "");
	return true;
}

auto StatColumnsWeapon(Unit* unit, int32_t* strBonus, int32_t* dexBonus) noexcept -> bool {
	void* weapon = g_hooked ? WeaponOf(unit) : nullptr;
	if (weapon == nullptr) {
		return false;
	}
	*strBonus = static_cast<int16_t>(g_strBonus(weapon));
	*dexBonus = static_cast<int16_t>(g_dexBonus(weapon));
	return true;
}

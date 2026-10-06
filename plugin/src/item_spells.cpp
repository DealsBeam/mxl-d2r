// D2Sigma's item spells: what using (right-clicking) an item does (docs/plugin-spec.md, "Item spells").
//
// D2Sigma replaced misc.txt pSpell 12-22 with its own handlers (table 0x101d95b0, dispatcher 0x1007f140).
// tools/txt_migrate.py moves all of them (except the timed potion 22, which is D2R's 3) to pSpell
// mxl_items::PluginSpell, an empty slot of D2R's item spell table 0x236fe30, and lists the items by code in
// mxl_items.h. D2R checks and runs an item spell through two dispatchers that get both the used item and, for items
// whose spellicon is not -1, the item the player clicked with the spell cursor:
//   0x5817f0 (game, player, item, target) -> bool   may the item be used (on that target)
//   0x581680 (game, player, item, target)           use it; the caller then removes the used item
// Both are hooked: an item with pSpell PluginSpell is handled here, everything else goes to D2R.
//
// Implemented:
//   20 mystic orbs (D2Sigma 0x1007f4b0): the orb's mysticorbs row (calc1 - 1) is applied to the target item if
//      the target's item type and quality fit the row (0x1008f840: excluded types first, then included types), the
//      item holds fewer than the row's maximum of that orb (stat 290, layer = row) and the item's level requirement
//      plus the orb's stays within the player's level. Applying adds the requirement (item_levelreq) and the row's
//      six properties (times stat 289, at least once) to the item's magic stat list (flags 0x40), and counts the
//      orb in stat 290.
//   14 dyes (0x1007eff0): sets the dye colour stat (Median 183) to calc1 + 1 (1-21) on a weapon or armour.
// Not yet (refused, so the item is kept): 12, 13, 15-19, 21 (arcane shards, keys, cube items, ...).
#include "item_spells.h"

#include "d2r.h"
#include "mxl_items.h"

#include <cstdint>
#include <cstdio>
#include <iterator>

namespace {

constexpr uint64_t UseDispatchRva = 0x581680;
// MOV [RSP+0x18],RBX / MOV [RSP+0x20],RBP / PUSH RSI / PUSH RDI / PUSH R15
constexpr uint8_t UseDispatchEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x6c, 0x24, 0x20, 0x56, 0x57, 0x41, 0x57};
constexpr uint64_t CheckDispatchRva = 0x5817f0;
// MOV [RSP+0x8],RBX / MOV [RSP+0x10],RBP / MOV [RSP+0x18],RSI
constexpr uint8_t CheckDispatchEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
                                          0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};

constexpr uint64_t UnitClassRva = 0x349860;      // (unit, file, line) -> class id (items: items.txt row)
constexpr uint64_t ItemRecordRva = 0x314110;     // (dataFlag, class id) -> items record
constexpr uint64_t ItemIsTypeRva = 0x373890;     // (item, itemtype) -> non-zero if the item is of that type
constexpr uint64_t ItemQualityRva = 0x36cf60;    // (item) -> quality (item data +0)
constexpr uint64_t ItemLevelReqRva = 0x376de0;   // (item, player) -> level requirement, -1 none
constexpr uint64_t GetUnitStatRva = 0x2f5020;    // (unit, stat, layer) -> total value
constexpr uint64_t ItemStatListRva = 0x3d5230;   // (owner, item, state, flags) -> stat list, created if missing
constexpr uint64_t StatListAddRva = 0x2f3330;    // (dataFlag, list, stat, value, layer)
constexpr uint64_t ApplyPropertyRva = 0x3d4620;  // (type, unit, item, mods, index, set, &property, state, flags, 0)
constexpr uint64_t SendItemRva = 0x535f60;       // (game, player, item, 1, 0, unit id, byte, byte, position):
                                                 // the item update D2R sends after identifying (0x46e8c0)
constexpr uint64_t UnitIdRva = 0x34ab60;         // (unit) -> unit id
constexpr uint64_t ItemByte55Rva = 0x36cfe0;     // (item) -> item data +0x55
constexpr uint64_t ItemByte54Rva = 0x36ca10;     // (item) -> item data +0x54
constexpr uint64_t UnitPositionRva = 0x34a110;   // (unit) -> packed position
constexpr uint64_t UnitStatsChangedRva = 0x34dbe0;  // (unit, 1)

constexpr int ItPSpell = 0x94;  // items record: pSpell (int32), as 1.13c
constexpr int ItCode = 0x80;    // items record: code (4 chars), as 1.13c
constexpr int32_t StatLevel = 12;
constexpr int32_t MagicStatList = 0x40;

// D2R's item property (1.13c's D2PropertyStrc; D2R reads the low half of `prop` as the id, the high half as a kind).
struct Property {
	int32_t prop;
	int32_t param;
	int32_t min;
	int32_t max;
};

using DispatchFn = uint64_t (*)(d2r::Game* game, d2r::Unit* player, d2r::Unit* item, d2r::Unit* target) noexcept;
using UnitClassFn = int32_t (*)(d2r::Unit* unit, const char* file, int32_t line);
using ItemRecordFn = const uint8_t* (*)(uint32_t dataFlag, int32_t classId);
using ItemIsTypeFn = int32_t (*)(d2r::Unit* item, int32_t itemType);
using ItemQualityFn = int32_t (*)(d2r::Unit* item);
using ItemLevelReqFn = int32_t (*)(d2r::Unit* item, d2r::Unit* player);
using GetUnitStatFn = int32_t (*)(d2r::Unit* unit, int32_t stat, int32_t layer);
using ItemStatListFn = void* (*)(d2r::Unit* owner, d2r::Unit* item, int32_t state, int32_t flags);
using StatListAddFn = void (*)(uint32_t dataFlag, void* list, int32_t stat, int32_t value, int32_t layer);
using ApplyPropertyFn = void (*)(int32_t type, d2r::Unit* unit, d2r::Unit* item, void* mods, int32_t index,
                                 int32_t set, const Property* property, int32_t state, int32_t flags, void* extra);
using SendItemFn = void (*)(d2r::Game* game, d2r::Unit* player, d2r::Unit* item, int32_t mode, int32_t zero,
                            int32_t unitId, uint8_t byte55, uint8_t byte54, uint32_t position);
using UnitIdFn = int32_t (*)(d2r::Unit* unit);
using ItemByteFn = uint8_t (*)(d2r::Unit* item);
using UnitPositionFn = uint32_t (*)(d2r::Unit* unit);
using UnitStatsChangedFn = void (*)(d2r::Unit* unit, int32_t on);

DispatchFn g_useOriginal = nullptr;
DispatchFn g_checkOriginal = nullptr;
UnitClassFn g_unitClass = nullptr;
ItemRecordFn g_itemRecord = nullptr;
ItemIsTypeFn g_itemIsType = nullptr;
ItemQualityFn g_itemQuality = nullptr;
ItemLevelReqFn g_itemLevelReq = nullptr;
GetUnitStatFn g_getUnitStat = nullptr;
ItemStatListFn g_itemStatList = nullptr;
StatListAddFn g_statListAdd = nullptr;
ApplyPropertyFn g_applyProperty = nullptr;
SendItemFn g_sendItem = nullptr;
UnitIdFn g_unitId = nullptr;
ItemByteFn g_itemByte55 = nullptr;
ItemByteFn g_itemByte54 = nullptr;
UnitPositionFn g_unitPosition = nullptr;
UnitStatsChangedFn g_unitStatsChanged = nullptr;

// The D2Sigma item behind a PluginSpell item, or null for every other item.
auto SigmaItemOf(d2r::Game* game, d2r::Unit* item) noexcept -> const mxl_items::SigmaItem* {
	if (item == nullptr || d2r::At<int32_t>(item, d2r::UnitType) != 4) {
		return nullptr;
	}
	const uint8_t* record = g_itemRecord(d2r::DataFlag(game), g_unitClass(item, "mxl-core item_spells", 0));
	if (record == nullptr || d2r::At<int32_t>(record, ItPSpell) != mxl_items::PluginSpell) {
		return nullptr;
	}
	const uint32_t code = d2r::At<uint32_t>(record, ItCode);
	for (const auto& entry : mxl_items::Items) {
		if (entry.code == code) {
			return &entry;
		}
	}
	return nullptr;
}

// D2Sigma 0x1008f840: may this orb row go on the item (excluded item types first, then included ones; each list
// can be limited to one quality, compared as D2Sigma's quality + 1).
auto OrbFitsItem(const mxl_items::Orb& orb, d2r::Unit* item) noexcept -> bool {
	const int32_t quality = g_itemQuality(item) + 1;
	for (const int32_t type : orb.exclude) {
		if (type >= 0 && g_itemIsType(item, type) != 0) {
			return orb.excludeQuality != 0 && orb.excludeQuality != quality;
		}
	}
	for (const int32_t type : orb.include) {
		if (type >= 0 && g_itemIsType(item, type) != 0) {
			return orb.includeQuality == 0 || orb.includeQuality == quality;
		}
	}
	return false;
}

auto OrbRow(const mxl_items::SigmaItem& entry) noexcept -> const mxl_items::Orb* {
	return entry.arg >= 0 && entry.arg < static_cast<int32_t>(std::size(mxl_items::Orbs)) ? &mxl_items::Orbs[entry.arg]
	                                                                                       : nullptr;
}

auto CanApplyOrb(const mxl_items::SigmaItem& entry, d2r::Unit* player, d2r::Unit* target) noexcept -> bool {
	const mxl_items::Orb* orb = OrbRow(entry);
	if (orb == nullptr || !OrbFitsItem(*orb, target)) {
		return false;
	}
	if (g_getUnitStat(target, mxl_items::StatOrbCount, entry.arg) + 1 > orb->maxCount) {
		return false;
	}
	const int32_t required = g_itemLevelReq(target, player);
	return (required < 0 ? 0 : required) + orb->reqLevel <= g_getUnitStat(player, StatLevel, 0);
}

auto CanDye(d2r::Unit* target) noexcept -> bool {
	return g_itemIsType(target, mxl_items::ItemTypeArmor) != 0 || g_itemIsType(target, mxl_items::ItemTypeWeapon) != 0;
}

// Resend the changed item, as D2R does after identifying one.
void SendItem(d2r::Game* game, d2r::Unit* player, d2r::Unit* item) noexcept {
	g_sendItem(game, player, item, 1, 0, g_unitId(item), g_itemByte55(item), g_itemByte54(item), g_unitPosition(item));
	g_unitStatsChanged(player, 1);
}

void ApplyOrb(const mxl_items::SigmaItem& entry, d2r::Game* game, d2r::Unit* player, d2r::Unit* target) noexcept {
	const mxl_items::Orb* orb = OrbRow(entry);
	void* list = g_itemStatList(nullptr, target, 0, MagicStatList);
	if (orb == nullptr || list == nullptr) {
		return;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	g_statListAdd(dataFlag, list, mxl_items::StatItemLevelReq, orb->reqLevel, 0);
	int32_t times = g_getUnitStat(target, mxl_items::StatOrbMultiplier, 0);
	if (times == 0) {
		times = 1;
	}
	for (; times > 0; --times) {
		for (int32_t k = 0; k < 6; ++k) {
			const mxl_items::OrbProp& p = orb->props[k];
			if (p.prop < 0) {
				continue;
			}
			const Property property {p.prop, p.param, p.min, p.max};
			g_applyProperty(0, nullptr, target, nullptr, k + 1, 0, &property, 0, MagicStatList, nullptr);
		}
	}
	g_statListAdd(dataFlag, list, mxl_items::StatOrbCount, 1, entry.arg);
	SendItem(game, player, target);
}

void ApplyDye(const mxl_items::SigmaItem& entry, d2r::Game* game, d2r::Unit* player, d2r::Unit* target) noexcept {
	void* list = g_itemStatList(nullptr, target, 0, MagicStatList);
	if (list == nullptr) {
		return;
	}
	int32_t colour = entry.arg + 1;
	if (colour < 1 || colour > 21) {
		colour = 1;
	}
	// D2Sigma sets the stat (D2Common 10261); adding the difference does the same on the list holding it
	const int32_t current = g_getUnitStat(target, mxl_items::StatDyeColour, 0);
	g_statListAdd(d2r::DataFlag(game), list, mxl_items::StatDyeColour, colour - current, 0);
	SendItem(game, player, target);
}

auto CheckDispatch(d2r::Game* game, d2r::Unit* player, d2r::Unit* item, d2r::Unit* target) noexcept -> uint64_t {
	const mxl_items::SigmaItem* entry = SigmaItemOf(game, item);
	if (entry == nullptr) {
		return g_checkOriginal(game, player, item, target);
	}
	if (target == nullptr || target == item || d2r::At<int32_t>(target, d2r::UnitType) != 4) {
		return 0;
	}
	switch (entry->spell) {
	case 20:
		return CanApplyOrb(*entry, player, target) ? 1 : 0;
	case 14:
		return CanDye(target) ? 1 : 0;
	default:
		return 0;  // not implemented: refuse, so the item is not used up
	}
}

auto UseDispatch(d2r::Game* game, d2r::Unit* player, d2r::Unit* item, d2r::Unit* target) noexcept -> uint64_t {
	const mxl_items::SigmaItem* entry = SigmaItemOf(game, item);
	if (entry == nullptr) {
		return g_useOriginal(game, player, item, target);
	}
	if (target == nullptr || target == item || d2r::At<int32_t>(target, d2r::UnitType) != 4) {
		return 0;
	}
	switch (entry->spell) {
	case 20:
		if (CanApplyOrb(*entry, player, target)) {
			ApplyOrb(*entry, game, player, target);
		}
		break;
	case 14:
		if (CanDye(target)) {
			ApplyDye(*entry, game, player, target);
		}
		break;
	default:
		break;
	}
	return 0;
}

}  // namespace

auto InstallItemSpells(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	g_unitClass = reinterpret_cast<UnitClassFn>(base + UnitClassRva);
	g_itemRecord = reinterpret_cast<ItemRecordFn>(base + ItemRecordRva);
	g_itemIsType = reinterpret_cast<ItemIsTypeFn>(base + ItemIsTypeRva);
	g_itemQuality = reinterpret_cast<ItemQualityFn>(base + ItemQualityRva);
	g_itemLevelReq = reinterpret_cast<ItemLevelReqFn>(base + ItemLevelReqRva);
	g_getUnitStat = reinterpret_cast<GetUnitStatFn>(base + GetUnitStatRva);
	g_itemStatList = reinterpret_cast<ItemStatListFn>(base + ItemStatListRva);
	g_statListAdd = reinterpret_cast<StatListAddFn>(base + StatListAddRva);
	g_applyProperty = reinterpret_cast<ApplyPropertyFn>(base + ApplyPropertyRva);
	g_sendItem = reinterpret_cast<SendItemFn>(base + SendItemRva);
	g_unitId = reinterpret_cast<UnitIdFn>(base + UnitIdRva);
	g_itemByte55 = reinterpret_cast<ItemByteFn>(base + ItemByte55Rva);
	g_itemByte54 = reinterpret_cast<ItemByteFn>(base + ItemByte54Rva);
	g_unitPosition = reinterpret_cast<UnitPositionFn>(base + UnitPositionRva);
	g_unitStatsChanged = reinterpret_cast<UnitStatsChangedFn>(base + UnitStatsChangedRva);
	const bool ok =
		context->InstallInlineHook(CheckDispatchRva, CheckDispatchEntry, sizeof(CheckDispatchEntry), CheckDispatch,
	                               &g_checkOriginal) &&
		context->InstallInlineHook(UseDispatchRva, UseDispatchEntry, sizeof(UseDispatchEntry), UseDispatch, &g_useOriginal);
	std::snprintf(report, reportSize,
	              ok ? "on: %zu items on pSpell %d; mystic orbs (%zu rows), dyes; others refused for now"
	                 : "off: item spell hooks failed (%zu items on pSpell %d, %zu orb rows)",
	              std::size(mxl_items::Items), mxl_items::PluginSpell, std::size(mxl_items::Orbs));
	return ok;
}

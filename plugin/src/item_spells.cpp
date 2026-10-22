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
//   16 keys / summoning scrolls / riftstones (0x1007f720): a portal next to the player to level calc1 (a Median
//      level number, translated by LevelIds), once per level and game. D2Sigma: calc2 0 -> red portal (object 60,
//      0x10097870), else Median's one-way portal (0x100976d0); misc2 +0x26 limits the item to one difficulty;
//      calc3 non-zero keeps the item (pSpell PluginSpellKeep, below). Opened with
//      D2R's portal routine 0x432ce0 the way D2R's cube "Red Portal" output does (0x5da800), which also places the
//      return portal; D2R only links levels of the same act. Destinations above 255 get their high byte written here.
//   18 containers (gem/rune clusters, 0x1006f240): right-click takes every item the container holds (its misc.bin
//      ultracode) from the inventory and cube, and from the stash in town, adding `amount` to its count stat each,
//      up to its maximum. Items are removed the way D2R removes a used-up tome (0x4712a0).
//   13, 21 (arcane shards, catalysts, oils): D2Sigma only shows the item's help text; it is in the tooltip now
//      (tools/txt_migrate.py help_tooltips), right-click is refused. The 20 trophies (misc2 InteractString) keep the text
//      back: their tooltip says "Right click to reveal trophy challenge" (the hover string of D2Sigma's interact mode)
//      and the right click prints the challenge as chat lines (mxl_items.h Trophies, items_ext_logic.h).
// Items D2Sigma keeps (13, 18, 21, keys with calc3) use pSpell PluginSpellKeep (17), whose table flag is set here:
// D2R's client marks an item spell as "used up" from that flag (0x308e80) and the server checks the two agree.
// Not yet (refused, so the item is kept): 12, 15, 17, 19 (home_cherry, Perfect Ruby, askari_call, cherry, toa).
#include "item_spells.h"

#include "client_messages.h"
#include "client_messages_logic.h"
#include "d2r.h"
#include "items_ext_logic.h"
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
constexpr uint64_t SendItemRva = 0x535f60;       // (game, player, item, 1, 0, unit mode, byte, byte, position):
                                                 // the item update D2R sends after identifying (0x46e8c0)
constexpr uint64_t UnitModeRva = 0x34ab60;       // (unit) -> unit +0xc, the mode
constexpr uint64_t ItemByte55Rva = 0x36cfe0;     // (item) -> item data +0x55
constexpr uint64_t ItemByte54Rva = 0x36ca10;     // (item) -> item data +0x54
constexpr uint64_t UnitPositionRva = 0x34a110;   // (unit) -> packed position
constexpr uint64_t UnitStatsChangedRva = 0x34dbe0;  // (unit, 1)
constexpr uint64_t EvalItemCalcRva = 0x3b4880;   // (dataFlag, player, item, calc) -> value of an items.txt formula
constexpr uint64_t UnitRoomRva = 0x34b440;       // (unit) -> room
constexpr uint64_t RoomLevelIdRva = 0x2efc10;    // (room) -> level id
constexpr uint64_t LevelActRva = 0x3e2b5c8;      // (dataFlag, level id) -> act
constexpr uint64_t UnitXYRva = 0x34a040;         // (unit, int32 xy[2])
constexpr uint64_t FreeSpotRva = 0x517f90;       // (game, room, xy[2], 3, 0x400, &room, 5, 100): free spot near xy
constexpr uint64_t CreatePortalRva = 0x432ce0;   // (game, unit, room, x, y, destLevel, &object, objectId, 0, 1)
constexpr uint64_t SetPortalLevelRva = 0x34e9d0; // (object, level byte); clears the high byte
constexpr int UnitObjectData = 0x10;             // object unit -> object data
constexpr int ObjPortalLevelHigh = 0x78;         // portal destination, high byte (low byte at +0x08)
constexpr uint64_t GameCounterRva = 0x5d88b0;    // (game + 0x1aac8, &key) -> int* (per-game counters of the cube's
                                                 // Red Portal, keyed by recipe)
constexpr uint64_t UnitMessageRva = 0x491960;    // (player, message, player): 0x15 = "can't do that"
constexpr int GameDifficulty = 0x104;            // byte: 0 normal, 1 nightmare, 2 hell
constexpr int GameCounters = 0x1aac8;
constexpr int ItCalc1 = 0xa4;                    // items record: calc1-calc3 (compiled formulas), as 1.13c
constexpr int32_t ObjectRedPortal = 60;
constexpr int32_t CounterKeyBase = 0x6d780000;   // per-game counter key for "portal to level n opened": base + n

constexpr uint64_t ItemSpellTable = 0x236fe30;  // 20 x {check, use, flag}; flag 1 = the item is not used up
constexpr uint64_t UnitInventoryRva = 0x34a360;  // (unit, file, line) -> inventory
constexpr uint64_t InventoryFirstRva = 0x388c10; // (inventory) -> first item
constexpr uint64_t InventoryNextRva = 0x38aba0;  // (item) -> next item in the owner's inventory
constexpr uint64_t RoomIsTownRva = 0x2f0750;     // (room) -> non-zero in town
constexpr uint64_t ItemLocationRva = 0x382d20;   // (int32 location[4], item): unit id, page, packed x/y, ...
constexpr uint64_t UnitIdRva = 0x34a330;         // (unit, file, line) -> unit +8, the unit id
constexpr uint64_t InventoryChangeRva = 0x472590;  // (player, adds, removes, addIds, removeIds, nAdds, nRemoves, 0)
                                                   // -> bool: validated inventory change (as in 0x4712a0)
constexpr uint64_t SetItemByte5cRva = 0x36dea0;  // (item, byte): item data +0x5c
constexpr uint64_t FreeItemRva = 0x43ec10;       // (game, item)
constexpr int32_t PageInventory = 0;
constexpr int32_t PageCube = 3;
constexpr int32_t PageStash = 4;

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
                            int32_t unitMode, uint8_t byte55, uint8_t byte54, uint32_t position);
using UnitModeFn = int32_t (*)(d2r::Unit* unit);
using ItemByteFn = uint8_t (*)(d2r::Unit* item);
using UnitPositionFn = uint32_t (*)(d2r::Unit* unit);
using UnitStatsChangedFn = void (*)(d2r::Unit* unit, int32_t on);
using EvalItemCalcFn = int32_t (*)(uint32_t dataFlag, d2r::Unit* player, d2r::Unit* item, int32_t calc);
using UnitRoomFn = void* (*)(d2r::Unit* unit);
using RoomLevelIdFn = int32_t (*)(void* room);
using LevelActFn = uint8_t (*)(uint32_t dataFlag, int32_t level);
using UnitXYFn = void (*)(d2r::Unit* unit, int32_t* xy);
using FreeSpotFn = void (*)(d2r::Game* game, void* room, int32_t* xy, int32_t size, int32_t flags, void** outRoom,
                            int64_t mode, int32_t range);
using CreatePortalFn = int32_t (*)(d2r::Game* game, d2r::Unit* unit, void* room, int32_t x, int32_t y, int32_t level,
                                   d2r::Unit** object, int32_t objectId, int32_t findSpot, int32_t anywhere);
using SetPortalLevelFn = void (*)(d2r::Unit* object, uint32_t level);
using GameCounterFn = int32_t* (*)(void* counters, const int32_t* key);
using UnitMessageFn = void (*)(d2r::Unit* player, int32_t message, d2r::Unit* subject);
using UnitInventoryFn = void* (*)(d2r::Unit* unit, const char* file, int32_t line);
using InventoryFirstFn = d2r::Unit* (*)(void* inventory);
using InventoryNextFn = d2r::Unit* (*)(d2r::Unit* item);
using RoomIsTownFn = int64_t (*)(void* room);
using ItemLocationFn = int32_t* (*)(int32_t* location, d2r::Unit* item);
using UnitIdFn = uint32_t (*)(d2r::Unit* unit, const char* file, int32_t line);
using InventoryChangeFn = uint8_t (*)(d2r::Unit* player, int32_t* adds, int32_t* removes, uint32_t* addIds,
                                      uint32_t* removeIds, uint64_t addCount, uint64_t removeCount, uint8_t zero);
using SetItemByteFn = void (*)(d2r::Unit* item, uint8_t value);
using FreeItemFn = void (*)(d2r::Game* game, d2r::Unit* item);

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
UnitModeFn g_unitMode = nullptr;
ItemByteFn g_itemByte55 = nullptr;
ItemByteFn g_itemByte54 = nullptr;
UnitPositionFn g_unitPosition = nullptr;
UnitStatsChangedFn g_unitStatsChanged = nullptr;
EvalItemCalcFn g_evalItemCalc = nullptr;
UnitRoomFn g_unitRoom = nullptr;
RoomLevelIdFn g_roomLevelId = nullptr;
LevelActFn g_levelAct = nullptr;
UnitXYFn g_unitXY = nullptr;
FreeSpotFn g_freeSpot = nullptr;
CreatePortalFn g_createPortal = nullptr;
SetPortalLevelFn g_setPortalLevel = nullptr;
GameCounterFn g_gameCounter = nullptr;
UnitMessageFn g_unitMessage = nullptr;
UnitInventoryFn g_unitInventory = nullptr;
InventoryFirstFn g_inventoryFirst = nullptr;
InventoryNextFn g_inventoryNext = nullptr;
RoomIsTownFn g_roomIsTown = nullptr;
ItemLocationFn g_itemLocation = nullptr;
UnitIdFn g_unitId = nullptr;
InventoryChangeFn g_inventoryChange = nullptr;
SetItemByteFn g_setItemByte5c = nullptr;
FreeItemFn g_freeItem = nullptr;

auto ItemRecordOf(d2r::Game* game, d2r::Unit* item) noexcept -> const uint8_t* {
	return g_itemRecord(d2r::DataFlag(game), g_unitClass(item, "mxl-core item_spells", 0));
}

// The D2Sigma item behind a PluginSpell item, or null for every other item.
auto SigmaItemOf(d2r::Game* game, d2r::Unit* item) noexcept -> const mxl_items::SigmaItem* {
	if (item == nullptr || d2r::At<int32_t>(item, d2r::UnitType) != 4) {
		return nullptr;
	}
	const uint8_t* record = ItemRecordOf(game, item);
	if (record == nullptr || (d2r::At<int32_t>(record, ItPSpell) != mxl_items::PluginSpell &&
	                          d2r::At<int32_t>(record, ItPSpell) != mxl_items::PluginSpellKeep)) {
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
	g_sendItem(game, player, item, 1, 0, g_unitMode(item), g_itemByte55(item), g_itemByte54(item), g_unitPosition(item));
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

// A Median level number as D2R's level id, or 0 when D2R cannot link it from where the player stands: no such level,
// or another act (D2R's portal routine refuses those).
auto LinkableLevel(d2r::Game* game, d2r::Unit* player, int32_t median) noexcept -> int32_t {
	if (median <= 0 || median >= static_cast<int32_t>(std::size(mxl_items::LevelIds))) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const int32_t level = mxl_items::LevelIds[median];
	void* room = g_unitRoom(player);
	if (level <= 0 || level > 0xffff || room == nullptr ||
	    g_levelAct(dataFlag, level) != g_levelAct(dataFlag, g_roomLevelId(room))) {
		return 0;
	}
	return level;
}

// D2Sigma 0x1007f720: the destination of a key (its calc1, a Median level number) or 0 when it may not open one
// here: wrong difficulty, a level D2R cannot link or already opened.
auto KeyLevel(const mxl_items::SigmaItem& entry, d2r::Game* game, d2r::Unit* player, d2r::Unit* item) noexcept
	-> int32_t {
	if (entry.difficulty != 0 && entry.difficulty - 1 != d2r::At<uint8_t>(game, GameDifficulty)) {
		return 0;
	}
	const uint8_t* record = ItemRecordOf(game, item);
	const int32_t level =
		LinkableLevel(game, player, g_evalItemCalc(d2r::DataFlag(game), player, item, d2r::At<int32_t>(record, ItCalc1)));
	if (level == 0) {
		return 0;
	}
	const int32_t key = CounterKeyBase + level;
	if (*g_gameCounter(reinterpret_cast<uint8_t*>(game) + GameCounters, &key) > 0) {
		return 0;
	}
	return level;
}

// A portal next to `at` (the player, or a missile), as D2R's cube "Red Portal" output (0x5da800) opens one.
auto OpenPortalAt(d2r::Game* game, d2r::Unit* player, d2r::Unit* at, int32_t level, int32_t object) noexcept -> bool {
	void* room = g_unitRoom(at);
	int32_t xy[2] = {};
	g_unitXY(at, xy);
	void* spot = nullptr;
	g_freeSpot(game, room, xy, 3, 0x400, &spot, 5, 100);
	d2r::Unit* portal = nullptr;
	if (room == nullptr || spot == nullptr ||
	    g_createPortal(game, player, spot, xy[0], xy[1], level, &portal, object, 0, 1) == 0) {
		return false;
	}
	if (portal != nullptr) {
		g_setPortalLevel(portal, static_cast<uint32_t>(level) & 0xff);
		// D2R keeps a portal's destination in 16 bits, low byte at object data +0x08, high byte at +0x78 (getter
		// 0x34ad40, the object packet 0x51 sends both); the byte setter clears the high one, so it is written here.
		if (uint8_t* data = d2r::At<uint8_t*>(portal, UnitObjectData)) {
			data[ObjPortalLevelHigh] = static_cast<uint8_t>(level >> 8);
		}
	}
	return true;
}

auto OpenPortal(d2r::Game* game, d2r::Unit* player, int32_t level, int32_t object) noexcept -> bool {
	return OpenPortalAt(game, player, player, level, object);
}

auto OpenKeyPortal(d2r::Game* game, d2r::Unit* player, d2r::Unit* item, int32_t level) noexcept -> bool {
	const uint8_t* record = ItemRecordOf(game, item);
	const int32_t kind = g_evalItemCalc(d2r::DataFlag(game), player, item, d2r::At<int32_t>(record, ItCalc1 + 4));
	if (!OpenPortal(game, player, level, kind != 0 ? mxl_items::ObjectOneWayPortal : ObjectRedPortal)) {
		return false;
	}
	const int32_t key = CounterKeyBase + level;
	*g_gameCounter(reinterpret_cast<uint8_t*>(game) + GameCounters, &key) += 1;
	return true;
}

auto ContainerOf(const mxl_items::SigmaItem& entry) noexcept -> const mxl_items::Container* {
	for (const auto& c : mxl_items::Containers) {
		if (c.code == entry.code) {
			return &c;
		}
	}
	return nullptr;
}

// Take an item out of the player's inventory and free it, as D2R does with a used-up tome (0x4712a0): a validated
// inventory change removing it, the "removed" item update to the client, then the unit is freed.
auto RemoveItem(d2r::Game* game, d2r::Unit* player, d2r::Unit* item) noexcept -> bool {
	int32_t location[8] = {};
	g_itemLocation(location, item);
	uint32_t unitId = g_unitId(item, "mxl-core item_spells", 0);
	if (g_inventoryChange(player, nullptr, location, nullptr, &unitId, 0, 1, 0) == 0) {
		return false;
	}
	g_setItemByte5c(item, 0);
	g_sendItem(game, player, item, 0x20, 4, 0, 0, 0, static_cast<uint32_t>(location[2]));
	g_setItemByte5c(item, 0);
	g_freeItem(game, item);
	return true;
}

// D2Sigma 0x1006f240: the container takes every item it holds from the inventory and the cube (and the stash in
// town), one `amount` each, until its count reaches `max`.
void FillContainer(const mxl_items::Container& c, d2r::Game* game, d2r::Unit* player, d2r::Unit* container) noexcept {
	void* list = g_itemStatList(nullptr, container, 0, MagicStatList);
	void* inventory = g_unitInventory(player, "mxl-core item_spells", 0);
	if (list == nullptr || inventory == nullptr) {
		return;
	}
	void* room = g_unitRoom(player);
	const bool town = room != nullptr && g_roomIsTown(room) != 0;
	const uint32_t dataFlag = d2r::DataFlag(game);
	int32_t taken = 0;
	for (d2r::Unit* it = g_inventoryFirst(inventory); it != nullptr;) {
		d2r::Unit* next = g_inventoryNext(it);
		if (it != container && d2r::At<int32_t>(it, d2r::UnitType) == 4) {
			const uint8_t* record = ItemRecordOf(game, it);
			const int32_t page = g_itemByte55(it);
			if (record != nullptr && d2r::At<uint32_t>(record, ItCode) == c.held &&
			    (page == PageInventory || page == PageCube || (page == PageStash && town))) {
				if (g_getUnitStat(container, c.stat, 0) + c.amount > c.max) {
					break;
				}
				if (RemoveItem(game, player, it)) {
					g_statListAdd(dataFlag, list, c.stat, c.amount, 0);
					++taken;
				}
			}
		}
		it = next;
	}
	if (taken > 0) {
		SendItem(game, player, container);
	}
}

auto IsOtherItem(d2r::Unit* target, d2r::Unit* item) noexcept -> bool {
	return target != nullptr && target != item && d2r::At<int32_t>(target, d2r::UnitType) == 4;
}

auto TrophyOf(const mxl_items::SigmaItem& entry) noexcept -> const mxl_items::Trophy* {
	for (const auto& t : mxl_items::Trophies) {
		if (t.code == entry.code) {
			return &t;
		}
	}
	return nullptr;
}

// The right click on a trophy: its challenge text in the chat log, the title in gold.
void RevealTrophy(d2r::Unit* player, const mxl_items::Trophy& trophy) noexcept {
	items_ext::ForEachTrophyLine(trophy.text, [player](const char* line, bool title) {
		SendClientMessage(player, line, title ? clientmsg::Tan : clientmsg::White);
	});
}

auto CheckDispatch(d2r::Game* game, d2r::Unit* player, d2r::Unit* item, d2r::Unit* target) noexcept -> uint64_t {
	const mxl_items::SigmaItem* entry = SigmaItemOf(game, item);
	if (entry == nullptr) {
		return g_checkOriginal(game, player, item, target);
	}
	switch (entry->spell) {
	case 20:
		return IsOtherItem(target, item) && CanApplyOrb(*entry, player, target) ? 1 : 0;
	case 14:
		return IsOtherItem(target, item) && CanDye(target) ? 1 : 0;
	case 16:
		if (KeyLevel(*entry, game, player, item) != 0) {
			return 1;
		}
		g_unitMessage(player, 0x15, player);
		return 0;
	case 18:
		return ContainerOf(*entry) != nullptr ? 1 : 0;
	case 13:
	case 21:
		return TrophyOf(*entry) != nullptr ? 1 : 0;
	default:
		return 0;  // not implemented: refuse, so the item is not used up
	}
}

auto UseDispatch(d2r::Game* game, d2r::Unit* player, d2r::Unit* item, d2r::Unit* target) noexcept -> uint64_t {
	const mxl_items::SigmaItem* entry = SigmaItemOf(game, item);
	if (entry == nullptr) {
		return g_useOriginal(game, player, item, target);
	}
	switch (entry->spell) {
	case 20:
		if (IsOtherItem(target, item) && CanApplyOrb(*entry, player, target)) {
			ApplyOrb(*entry, game, player, target);
		}
		break;
	case 14:
		if (IsOtherItem(target, item) && CanDye(target)) {
			ApplyDye(*entry, game, player, target);
		}
		break;
	case 16:
		if (const int32_t level = KeyLevel(*entry, game, player, item);
		    level == 0 || !OpenKeyPortal(game, player, item, level)) {
			g_unitMessage(player, 0x15, player);
		}
		break;
	case 18:
		if (const mxl_items::Container* c = ContainerOf(*entry)) {
			FillContainer(*c, game, player, item);
		}
		break;
	case 13:
	case 21:
		if (const mxl_items::Trophy* t = TrophyOf(*entry)) {
			RevealTrophy(player, *t);
		}
		break;
	default:
		break;
	}
	return 0;
}

}  // namespace

// D2Sigma's srvdofunc 154 (0x100aafa0) -> slot 173: a red portal next to the caster to the Median level its calc1
// gives (0x10097870, the keys' routine). Used by mysterious_companion_skill(_2) and judgementwander_2_skill.
auto SrvDoLevelPortal(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr || level < 1 || g_createPortal == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t target = LinkableLevel(game, unit, d2r::EvalCalc(game, unit, skill, d2r::SkCalc1, skillId, level));
	return target != 0 && OpenPortal(game, unit, target, ObjectRedPortal) ? 1 : 0;
}

// D2Sigma's cube output byte 1 (handler 0x58d20): a red portal next to the player to Median level `median`. D2R's own
// handler for that byte only knows the Cow Level (craft_flags.cpp swaps it for this one for the other levels).
auto CubeLevelPortal(d2r::Game* game, d2r::Unit* player, int32_t median) noexcept -> bool {
	if (game == nullptr || player == nullptr || g_createPortal == nullptr) {
		return false;
	}
	const int32_t level = LinkableLevel(game, player, median);
	return level != 0 && OpenPortal(game, player, level, ObjectRedPortal);
}

// D2Sigma missile hit 30 (0x1008d220 -> D2Objects.cpp 0x10097870 / 0x100976d0): a portal where the missile is, to
// Median level sHitPar1. With sHitPar2 set, D2Sigma's one-way portal (Median object 586) and only once per level and
// game (D2Sigma's level flag 4, shared with the keys: the same game counter here); without, a red portal (D2Sigma
// takes the level's own portal object, else 60). ancient_gate, judgementwander (8)
auto HitLevelPortal(d2r::Game* game, d2r::Unit* owner, d2r::Unit* missile, int32_t median, bool once) noexcept -> bool {
	if (game == nullptr || owner == nullptr || missile == nullptr) {
		return false;
	}
	const int32_t level = LinkableLevel(game, missile, median);
	if (level == 0) {
		return false;
	}
	const int32_t key = CounterKeyBase + level;
	if (once && *g_gameCounter(reinterpret_cast<uint8_t*>(game) + GameCounters, &key) > 0) {
		return false;
	}
	if (!OpenPortalAt(game, owner, missile, level, once ? mxl_items::ObjectOneWayPortal : ObjectRedPortal)) {
		return false;
	}
	if (once) {
		*g_gameCounter(reinterpret_cast<uint8_t*>(game) + GameCounters, &key) += 1;
	}
	return true;
}

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
	g_unitMode = reinterpret_cast<UnitModeFn>(base + UnitModeRva);
	g_itemByte55 = reinterpret_cast<ItemByteFn>(base + ItemByte55Rva);
	g_itemByte54 = reinterpret_cast<ItemByteFn>(base + ItemByte54Rva);
	g_unitPosition = reinterpret_cast<UnitPositionFn>(base + UnitPositionRva);
	g_unitStatsChanged = reinterpret_cast<UnitStatsChangedFn>(base + UnitStatsChangedRva);
	g_evalItemCalc = reinterpret_cast<EvalItemCalcFn>(base + EvalItemCalcRva);
	g_unitRoom = reinterpret_cast<UnitRoomFn>(base + UnitRoomRva);
	g_roomLevelId = reinterpret_cast<RoomLevelIdFn>(base + RoomLevelIdRva);
	g_levelAct = reinterpret_cast<LevelActFn>(base + LevelActRva);
	g_unitXY = reinterpret_cast<UnitXYFn>(base + UnitXYRva);
	g_freeSpot = reinterpret_cast<FreeSpotFn>(base + FreeSpotRva);
	g_createPortal = reinterpret_cast<CreatePortalFn>(base + CreatePortalRva);
	g_setPortalLevel = reinterpret_cast<SetPortalLevelFn>(base + SetPortalLevelRva);
	g_gameCounter = reinterpret_cast<GameCounterFn>(base + GameCounterRva);
	g_unitMessage = reinterpret_cast<UnitMessageFn>(base + UnitMessageRva);
	g_unitInventory = reinterpret_cast<UnitInventoryFn>(base + UnitInventoryRva);
	g_inventoryFirst = reinterpret_cast<InventoryFirstFn>(base + InventoryFirstRva);
	g_inventoryNext = reinterpret_cast<InventoryNextFn>(base + InventoryNextRva);
	g_roomIsTown = reinterpret_cast<RoomIsTownFn>(base + RoomIsTownRva);
	g_itemLocation = reinterpret_cast<ItemLocationFn>(base + ItemLocationRva);
	g_unitId = reinterpret_cast<UnitIdFn>(base + UnitIdRva);
	g_inventoryChange = reinterpret_cast<InventoryChangeFn>(base + InventoryChangeRva);
	g_setItemByte5c = reinterpret_cast<SetItemByteFn>(base + SetItemByte5cRva);
	g_freeItem = reinterpret_cast<FreeItemFn>(base + FreeItemRva);
	// D2R's slot PluginSpellKeep is empty; its flag 1 makes the client report the item as "not used up" (0x308e80)
	static constexpr uint8_t Zero[4] = {};
	const bool keep = context->PatchWriteU32(ItemSpellTable + static_cast<uint64_t>(mxl_items::PluginSpellKeep) * 12 + 8,
	                                         Zero, sizeof(Zero), 1);
	const bool ok = keep &&
		context->InstallInlineHook(CheckDispatchRva, CheckDispatchEntry, sizeof(CheckDispatchEntry), CheckDispatch,
	                               &g_checkOriginal) &&
		context->InstallInlineHook(UseDispatchRva, UseDispatchEntry, sizeof(UseDispatchEntry), UseDispatch, &g_useOriginal);
	std::snprintf(report, reportSize,
	              ok ? "on: %zu items on pSpell %d/17; mystic orbs (%zu rows), dyes, keys, containers; help items in tooltips"
	                 : "off: item spell patch/hooks failed (%zu items on pSpell %d/17, %zu orb rows)",
	              std::size(mxl_items::Items), mxl_items::PluginSpell, std::size(mxl_items::Orbs));
	return ok;
}

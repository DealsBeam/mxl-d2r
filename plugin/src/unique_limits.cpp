// Median's unique carry limits (D2Sigma's uniqueitems2 checks D2Units 0x100bd060 / 0x100bd0b0) on D2R.
//
// Sigma refuses a limited unique when it would enter the backpack: ground pickup into the inventory and its quick-move
// packet (cube, stash and belt to the inventory). D2R has two functions for that:
//
//   0x471950 pickup(Unit* player, uint32 itemId, uint8 toInventory, uint32 distance, uint8, uint8) -> bool
//            ground item to the inventory (packet 0x16 with its to-inventory flag set) or to the cursor (flag 0)
//   0x471e90 move(Unit* player, const Loc* oldLocs, const Loc* newLocs, const int32* itemIds, uint64 count, uint8 flag) -> bool
//            the item move every container packet ends in (0x18 cursor to container, 0x19 container to cursor, ...)
//
// A location is four int32: {mode, page, x | y << 16, body location}; mode 0 page 0 is the backpack. The hooks look at the
// items that arrive in the backpack and are not in it yet, apply the rule (unique_limits_logic.h) to what the backpack
// holds, and on a refusal send Median's line (string 12109) and return 0 without calling the original: the callers answer
// a failed move the way they do for a move without room, the item stays where it was (on the ground or the cursor, in the
// cube or stash). A pickup to the cursor is not stopped: the item is not in the backpack yet, the drop into it is checked.
//
// Entry bytes of build 93847 (timestamp 0x6ab3782c), whole instructions, no rip-relative operand:
//   0x471e90: push rbp / push rbx / push rsi / push rdi / push r12 / push r14 / lea rbp,[rsp-0xa9a8]   (17 bytes)
//   0x471950: mov [rsp+0x18],rbx / push rbp,rsi,rdi,r12,r13,r14,r15 / lea rbp,[rsp-0x17]                (21 bytes)
#include "unique_limits.h"

#include "client_messages.h"
#include "d2r.h"
#include "strings.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t MoveRva = 0x471e90;
constexpr uint64_t PickupRva = 0x471950;
constexpr uint64_t ItemDataRva = 0x34a500;       // (item) -> item data: +0 quality, +0x34 unique index
constexpr uint64_t ItemPageRva = 0x36cfe0;       // (item) -> inventory page (0 backpack, 3 cube, 4 stash)
constexpr uint64_t UnitInventoryRva = 0x34a360;  // (unit, file, line) -> inventory
constexpr uint64_t InventoryFirstRva = 0x388c10; // (inventory) -> first item
constexpr uint64_t InventoryNextRva = 0x38aba0;  // (item) -> next item
constexpr uint64_t FindUnitRva = 0x48fe80;       // (game, type, id) -> unit

constexpr int UnitTypeItem = 4;
constexpr int UnitModeOffset = 0x0c;
constexpr int GameOfUnit = 0xd8;
constexpr int ItemQuality = 0;
constexpr int ItemUniqueIndex = 0x34;
constexpr int32_t ModeStored = 0;
constexpr uint64_t MaxMoves = 16;

constexpr uint8_t MoveEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x8d, 0xac, 0x24, 0x58, 0x56, 0xff, 0xff};
constexpr uint8_t PickupEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
                                   0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xe9};

struct Loc {
	int32_t mode, page, xy, body;
};
static_assert(sizeof(Loc) == 16);

using MoveFn = uint8_t (*)(Unit* player, const Loc* oldLocs, const Loc* newLocs, const int32_t* ids, uint64_t count, uint8_t flag);
using PickupFn = uint8_t (*)(Unit* player, uint32_t itemId, uint8_t toInventory, uint32_t distance, uint8_t a5, uint8_t a6);
using ItemDataFn = uint8_t* (*)(Unit* item);
using ItemPageFn = uint8_t (*)(Unit* item);
using InventoryFn = void* (*)(Unit* unit, const char* file, int32_t line);
using InventoryFirstFn = Unit* (*)(void* inventory);
using InventoryNextFn = Unit* (*)(Unit* item);
using FindUnitFn = Unit* (*)(Game* game, int32_t type, uint32_t id);

MoveFn g_original = nullptr;
PickupFn g_pickupOriginal = nullptr;
ItemDataFn g_itemData = nullptr;
ItemPageFn g_itemPage = nullptr;
InventoryFn g_inventory = nullptr;
InventoryFirstFn g_first = nullptr;
InventoryNextFn g_next = nullptr;
FindUnitFn g_findUnit = nullptr;
bool g_installed = false;
unique_limits::Counts g_counts = {};

auto IsBackpack(const Loc& l) noexcept -> bool {
	return l.mode == ModeStored && l.page == 0;
}

// Unique index of a unique-quality item, -1 for anything else.
auto UniqueIndexOf(Unit* item) noexcept -> int32_t {
	const uint8_t* data = g_itemData(item);
	if (data == nullptr || d2r::At<int32_t>(data, ItemQuality) != unique_limits::QualityUnique) {
		return -1;
	}
	return d2r::At<int32_t>(data, ItemUniqueIndex);
}

auto InBackpack(Unit* item) noexcept -> bool {
	return d2r::At<int32_t>(item, UnitModeOffset) == ModeStored && g_itemPage(item) == 0;
}

// True when putting the items that `arriving` marks into the backpack takes a limited unique over its limit.
auto Blocked(Unit* player, Unit* const* items, const bool* arriving, uint64_t count) noexcept -> bool {
	int32_t carried[unique_limits::MaxCarried];
	size_t carriedCount = 0;
	bool built = false;
	for (uint64_t i = 0; i < count; ++i) {
		if (!arriving[i] || items[i] == nullptr || InBackpack(items[i])) {
			continue;
		}
		++g_counts.checked;
		const int32_t index = UniqueIndexOf(items[i]);
		if (index < 0 || unique_limits::Find(index) == nullptr) {
			continue;
		}
		++g_counts.limited;
		if (!built) {  // the uniques in the backpack now, less the ones this same move takes out of it
			built = true;
			void* inv = g_inventory(player, "uniquelimits", 0);
			for (Unit* it = inv != nullptr ? g_first(inv) : nullptr; it != nullptr && carriedCount < unique_limits::MaxCarried; it = g_next(it)) {
				if (!InBackpack(it)) {
					continue;
				}
				const int32_t u = UniqueIndexOf(it);
				bool leaving = false;
				for (uint64_t k = 0; k < count; ++k) {
					leaving = leaving || (items[k] == it && !arriving[k]);
				}
				if (u >= 0 && !leaving) {
					carried[carriedCount++] = u;
				}
			}
		}
		if (unique_limits::Refuses(index, carried, carriedCount)) {
			return true;
		}
		if (carriedCount < unique_limits::MaxCarried) {
			carried[carriedCount++] = index;  // an earlier arrival of the same call counts for the next
		}
	}
	return false;
}

void Refuse(Unit* player) noexcept {
	++g_counts.refused;
	char text[160];
	SendClientMessage(player, StringText(mxlstr::KeyUniqueLimit, unique_limits::RefusalText, text, sizeof(text)));
}

// The move 0x471e90 puts the items `ids` at `newLocs`.
auto MoveRefused(Unit* player, const Loc* newLocs, const int32_t* ids, uint64_t count) noexcept -> bool {
	if (d2r::At<int32_t>(player, d2r::UnitType) != 0) {
		return false;
	}
	bool arriving[MaxMoves] = {};
	bool any = false;
	for (uint64_t i = 0; i < count; ++i) {
		arriving[i] = IsBackpack(newLocs[i]);
		any = any || arriving[i];
	}
	if (!any) {
		return false;
	}
	Game* game = d2r::At<Game*>(player, GameOfUnit);
	Unit* items[MaxMoves] = {};
	for (uint64_t i = 0; i < count; ++i) {
		items[i] = game != nullptr ? g_findUnit(game, UnitTypeItem, static_cast<uint32_t>(ids[i])) : nullptr;
	}
	return Blocked(player, items, arriving, count);
}

uint8_t MoveHook(Unit* player, const Loc* oldLocs, const Loc* newLocs, const int32_t* ids, uint64_t count, uint8_t flag) {
	if (player != nullptr && newLocs != nullptr && ids != nullptr && count != 0 && count <= MaxMoves && MoveRefused(player, newLocs, ids, count)) {
		Refuse(player);
		return 0;
	}
	return g_original(player, oldLocs, newLocs, ids, count, flag);
}

uint8_t PickupHook(Unit* player, uint32_t itemId, uint8_t toInventory, uint32_t distance, uint8_t a5, uint8_t a6) {
	if (player != nullptr && toInventory != 0 && d2r::At<int32_t>(player, d2r::UnitType) == 0) {
		Game* game = d2r::At<Game*>(player, GameOfUnit);
		Unit* item = game != nullptr ? g_findUnit(game, UnitTypeItem, itemId) : nullptr;
		const bool arriving = true;
		if (item != nullptr && Blocked(player, &item, &arriving, 1)) {
			Refuse(player);
			return 0;
		}
	}
	return g_pickupOriginal(player, itemId, toInventory, distance, a5, a6);
}

}  // namespace

auto UniqueLimitCounts(unique_limits::Counts* out) noexcept -> bool {
	*out = g_counts;
	return g_installed;
}

auto InstallUniqueLimits(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	g_itemData = reinterpret_cast<ItemDataFn>(base + ItemDataRva);
	g_itemPage = reinterpret_cast<ItemPageFn>(base + ItemPageRva);
	g_inventory = reinterpret_cast<InventoryFn>(base + UnitInventoryRva);
	g_first = reinterpret_cast<InventoryFirstFn>(base + InventoryFirstRva);
	g_next = reinterpret_cast<InventoryNextFn>(base + InventoryNextRva);
	g_findUnit = reinterpret_cast<FindUnitFn>(base + FindUnitRva);
	const bool move = context->InstallInlineHook(MoveRva, MoveEntry, sizeof(MoveEntry), reinterpret_cast<void*>(&MoveHook),
	                                             reinterpret_cast<void**>(&g_original));
	const bool pickup = context->InstallInlineHook(PickupRva, PickupEntry, sizeof(PickupEntry), reinterpret_cast<void*>(&PickupHook),
	                                               reinterpret_cast<void**>(&g_pickupOriginal));
	g_installed = move && pickup;
	if (g_installed) {
		unique_limits::FormatReport(report, reportSize, true, g_counts);
	} else {
		std::snprintf(report, reportSize, "off: hook failed (move %s, pickup %s)", move ? "ok" : "no", pickup ? "ok" : "no");
	}
	return g_installed;
}

// D2Sigma's teleport (srvdofunc 27, D2SkillDo.cpp 0x100ad9d0) -> slot 179, for Median's teleports with an
// aurarangecalc (47 of 53: Teleport, harbinger, blink, pounce, ...).
//
// D2Sigma's one change to vanilla: the skill's aurarangecalc. -1 refuses the teleport (Median's formulas give -1 in
// the areas that forbid it, e.g. 25-26*skill('skills_2901'.clc2)); for a player a destination farther than |range|
// moves onto the line towards it, |range| away (0x100aeca0: 0x10088ca0 distance, 0x10088b70 angle, 0x10088d30 point;
// range 0 is no limit). Monsters keep the clicked spot. The rest is D2R's own teleport (srvdofunc 27, 0x554c40):
// the level's Teleport column (0 no, 2 only with a clear line, 0x350a20 mask 0x804) and the move (0x491ca0).
//
// Gold pickup (audit GS2): every Sigma teleport (srvdofunc 27, 0x100ada40 ends with the call) calls the auto pickup
// D2AutoPickup.cpp 0x1004c5e0 for a player, which walks the units around the arrival point and, for each gold pile
// ('gld ') while the carried gold is below the limit and the no-pickup flag (hotkey X) is off, picks it up through the
// game's own pickup. Reach: 36 sub-tiles, the same as the waypoint auto-activation (travel.cpp). The 47 teleports on slot
// 179 call it after the move; the 6 left on D2R's slot 27 (0x554c40) get a wrapper of the table entry with the same call.
#include "teleport.h"

#include "d2r.h"
#include "travel.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr uint64_t UnitRoomRva = 0x34b440;
constexpr uint64_t RoomLevelRva = 0x2efc10;
constexpr uint64_t LevelRecordRva = 0x32c4a0;  // (dataFlag, level) -> levels.txt record
constexpr uint64_t PathBlockedRva = 0x350a20;  // (unit, x, y, mask) -> non-zero when blocked
constexpr uint64_t MoveUnitRva = 0x491ca0;     // (game, unit, 0, x, y, 0, 0)
constexpr uint64_t VanillaTeleportRva = 0x554c40;  // D2R's srvdofunc 27
constexpr int LvTeleport = 0xe;                // uint8: 0 no, 1 yes, 2 only with a clear line
constexpr uint64_t PickupRva = 0x471950;       // (player, item id, toInventory, distance, pathCheck, a6) -> 1 when taken
constexpr uint64_t ItemKindRva = 0x372c90;     // (item) -> the items.txt type class of the base item, 4 = gold
constexpr uint64_t DistanceRva = 0x325140;     // (unit, unit) -> distance in sub-tiles
constexpr uint64_t GoldLimitRva = 0x34b320;    // (player) -> the most gold he can carry
constexpr uint64_t GetStatRva = 0x2f5020;      // (unit, stat, layer)
constexpr int HashItems = 0x5c6;               // game + (base + (id & 0x7f)) * 8
constexpr int UnitNextInHash = 0x158;
constexpr int UnitId = 0x08;
constexpr int UnitMode = 0x0c;
constexpr int ItemOnGround = 3;
constexpr int ItemKindGold = 4;
constexpr int32_t StatGold = 14;
constexpr uint32_t GoldReach = 36;
constexpr int MaxPiles = 32;

using UnitRoomFn = void* (*)(d2r::Unit* unit);
using RoomLevelFn = int32_t (*)(void* room);
using LevelRecordFn = const uint8_t* (*)(uint32_t dataFlag, int32_t level);
using PathBlockedFn = int32_t (*)(d2r::Unit* unit, int32_t x, int32_t y, int32_t mask);
using MoveUnitFn = int64_t (*)(d2r::Game* game, d2r::Unit* unit, int32_t zero, int32_t x, int32_t y, int32_t a,
                               int32_t b);
using PickupFn = int64_t (*)(d2r::Unit* player, uint32_t itemId, uint8_t toInventory, uint32_t distance, uint8_t pathCheck,
                             uint8_t a6);
using ItemKindFn = int64_t (*)(d2r::Unit* item);
using DistanceFn = uint32_t (*)(d2r::Unit* a, d2r::Unit* b);
using GoldLimitFn = int32_t (*)(d2r::Unit* player);
using GetStatFn = int32_t (*)(d2r::Unit* unit, int32_t stat, int32_t layer);
using SrvDoFn = int64_t (*)(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level);

UnitRoomFn g_unitRoom = nullptr;
RoomLevelFn g_roomLevel = nullptr;
LevelRecordFn g_levelRecord = nullptr;
PathBlockedFn g_pathBlocked = nullptr;
MoveUnitFn g_moveUnit = nullptr;
PickupFn g_pickup = nullptr;
ItemKindFn g_itemKind = nullptr;
DistanceFn g_distance = nullptr;
GoldLimitFn g_goldLimit = nullptr;
GetStatFn g_getStat = nullptr;
SrvDoFn g_vanillaTeleport = nullptr;  // D2R's srvdofunc 27, the wrapped table entry
std::atomic<bool> g_goldOn{false};
std::atomic<uint32_t> g_goldTeleports{0}, g_goldPiles{0}, g_goldSeen{0};

void ClampToRange(d2r::Unit* unit, int32_t range, int32_t* x, int32_t* y) noexcept {
	int32_t ux = 0;
	int32_t uy = 0;
	if (range == 0 || !d2r::UnitXY(unit, &ux, &uy)) {
		return;
	}
	const double dx = *x - ux;
	const double dy = *y - uy;
	const double distance = std::sqrt(dx * dx + dy * dy);
	const int32_t limit = std::abs(range);
	if (distance < limit || distance <= 0.0) {
		return;
	}
	*x = ux + static_cast<int32_t>(dx * limit / distance);
	*y = uy + static_cast<int32_t>(dy * limit / distance);
}

// D2Sigma's auto pickup for the player who just arrived: gold piles in reach, one pickup each. The ids are gathered first
// because a pickup takes the pile out of the hash chain being walked.
void PickupGoldAround(d2r::Game* game, d2r::Unit* player) noexcept {
	if (!g_goldOn.load(std::memory_order_relaxed) || !TravelAutoPickupOn() || game == nullptr || player == nullptr ||
	    d2r::At<int32_t>(player, d2r::UnitType) != 0) {
		return;
	}
	int32_t px = 0;
	int32_t py = 0;
	if (!d2r::UnitXY(player, &px, &py)) {
		return;
	}
	uint32_t ids[MaxPiles];
	int count = 0;
	for (int bucket = 0; bucket < 0x80 && count < MaxPiles; ++bucket) {
		int guard = 0;
		for (auto* u = d2r::At<d2r::Unit*>(game, (HashItems + bucket) * 8); u != nullptr && guard < 20000 && count < MaxPiles;
		     u = d2r::At<d2r::Unit*>(u, UnitNextInHash), ++guard) {
			if (d2r::At<int32_t>(u, d2r::UnitType) != 4 || d2r::At<int32_t>(u, UnitMode) != ItemOnGround ||
			    g_itemKind(u) != ItemKindGold) {
				continue;
			}
			int32_t ix = 0;
			int32_t iy = 0;
			if (!d2r::UnitXY(u, &ix, &iy)) {
				continue;
			}
			const int64_t dx = static_cast<int64_t>(ix) - px;
			const int64_t dy = static_cast<int64_t>(iy) - py;
			if (dx * dx + dy * dy > static_cast<int64_t>(GoldReach) * GoldReach) {
				continue;  // Euclid in sub-tiles: 0x325140 measures in a larger scale
			}
			ids[count++] = d2r::At<uint32_t>(u, UnitId);
		}
	}
	g_goldTeleports.fetch_add(1);
	g_goldSeen.fetch_add(static_cast<uint32_t>(count));
	for (int i = 0; i < count; ++i) {
		if (g_getStat(player, StatGold, 0) >= g_goldLimit(player)) {
			break;  // D2Sigma's 0x1004befb0: nothing to pick up while the purse is full
		}
		if (g_pickup(player, ids[i], 0, 255, 0, 0) != 0) {
			g_goldPiles.fetch_add(1);
		}
	}
}

// Slot 27 as D2R has it plus the pickup, for the teleports without an aurarangecalc (txt_migrate leaves 6 on it).
auto SrvDoTeleportWithGold(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	const int64_t result = g_vanillaTeleport(game, unit, skillId, level);
	PickupGoldAround(game, unit);
	return result;
}

}  // namespace

auto SrvDoRangedTeleport(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr || g_moveUnit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t range = d2r::EvalCalc(game, unit, skill, d2r::SkAuraRangeCalc, skillId, level);
	if (range == -1) {
		return 0;
	}
	int32_t x = 0;
	int32_t y = 0;
	d2r::api.skillTargetXY(game, unit, &x, &y);
	if (d2r::At<int32_t>(unit, d2r::UnitType) == 0) {
		ClampToRange(unit, range, &x, &y);
	}
	void* room = g_unitRoom(unit);
	if (room == nullptr) {
		return 0;
	}
	const uint8_t* levelRecord = g_levelRecord(dataFlag, g_roomLevel(room));
	if (levelRecord == nullptr) {
		return 0;
	}
	const uint8_t teleport = d2r::At<uint8_t>(levelRecord, LvTeleport);
	if (teleport == 0 || (teleport == 2 && g_pathBlocked(unit, x, y, 0x804) != 0)) {
		return 0;
	}
	const int64_t moved = g_moveUnit(game, unit, 0, x, y, 0, 0);
	PickupGoldAround(game, unit);
	return moved;
}

void InstallTeleport(uint64_t exeBase) noexcept {
	g_unitRoom = reinterpret_cast<UnitRoomFn>(exeBase + UnitRoomRva);
	g_roomLevel = reinterpret_cast<RoomLevelFn>(exeBase + RoomLevelRva);
	g_levelRecord = reinterpret_cast<LevelRecordFn>(exeBase + LevelRecordRva);
	g_pathBlocked = reinterpret_cast<PathBlockedFn>(exeBase + PathBlockedRva);
	g_moveUnit = reinterpret_cast<MoveUnitFn>(exeBase + MoveUnitRva);
	g_pickup = reinterpret_cast<PickupFn>(exeBase + PickupRva);
	g_itemKind = reinterpret_cast<ItemKindFn>(exeBase + ItemKindRva);
	g_distance = reinterpret_cast<DistanceFn>(exeBase + DistanceRva);
	g_goldLimit = reinterpret_cast<GoldLimitFn>(exeBase + GoldLimitRva);
	g_getStat = reinterpret_cast<GetStatFn>(exeBase + GetStatRva);
}

auto InstallTeleportGold(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	InstallTeleport(context->exeBase);  // the function pointers, in case the skill part is left out
	const uint64_t slot27 = d2r::SrvDoTable + 27 * 8;
	g_vanillaTeleport = reinterpret_cast<SrvDoFn>(context->exeBase + VanillaTeleportRva);
	const uint64_t expected = context->exeBase + VanillaTeleportRva;
	uint8_t expectedBytes[8];
	std::memcpy(expectedBytes, &expected, sizeof(expected));
	const bool wrapped = context->PatchWriteU64(slot27, expectedBytes, sizeof(expectedBytes), reinterpret_cast<uint64_t>(SrvDoTeleportWithGold));
	g_goldOn.store(true);
	std::snprintf(report, reportSize, wrapped ? "on: gold within %u sub-tiles after a player's teleport (slot 179 and slot 27, no-pickup flag X honoured)"
	                                          : "partly on: slot 179 only (the slot 27 table entry did not match D2R's 0x554c40)",
	              GoldReach);
	return true;
}

auto TeleportGoldStats(unsigned* teleports, unsigned* seen, unsigned* piles) noexcept -> void {
	*teleports = g_goldTeleports.load();
	*seen = g_goldSeen.load();
	*piles = g_goldPiles.load();
}

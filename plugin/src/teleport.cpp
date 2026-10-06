// D2Sigma's teleport (srvdofunc 27, D2SkillDo.cpp 0x100ad9d0) -> slot 173, for Median's teleports with an
// aurarangecalc (47 of 53: Teleport, harbinger, blink, pounce, ...).
//
// D2Sigma's one change to vanilla: the skill's aurarangecalc. -1 refuses the teleport (Median's formulas give -1 in
// the areas that forbid it, e.g. 25-26*skill('skills_2901'.clc2)); for a player a destination farther than |range|
// moves onto the line towards it, |range| away (0x100aeca0: 0x10088ca0 distance, 0x10088b70 angle, 0x10088d30 point;
// range 0 is no limit). Monsters keep the clicked spot. The rest is D2R's own teleport (srvdofunc 27, 0x554c40):
// the level's Teleport column (0 no, 2 only with a clear line, 0x350a20 mask 0x804) and the move (0x491ca0).
// Not ported: D2Sigma's gold auto-pickup after a player's teleport (D2AutoPickup.cpp 0x1004c5e0).
#include "teleport.h"

#include "d2r.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace {

constexpr uint64_t UnitRoomRva = 0x34b440;
constexpr uint64_t RoomLevelRva = 0x2efc10;
constexpr uint64_t LevelRecordRva = 0x32c4a0;  // (dataFlag, level) -> levels.txt record
constexpr uint64_t PathBlockedRva = 0x350a20;  // (unit, x, y, mask) -> non-zero when blocked
constexpr uint64_t MoveUnitRva = 0x491ca0;     // (game, unit, 0, x, y, 0, 0)
constexpr int LvTeleport = 0xe;                // uint8: 0 no, 1 yes, 2 only with a clear line

using UnitRoomFn = void* (*)(d2r::Unit* unit);
using RoomLevelFn = int32_t (*)(void* room);
using LevelRecordFn = const uint8_t* (*)(uint32_t dataFlag, int32_t level);
using PathBlockedFn = int32_t (*)(d2r::Unit* unit, int32_t x, int32_t y, int32_t mask);
using MoveUnitFn = int64_t (*)(d2r::Game* game, d2r::Unit* unit, int32_t zero, int32_t x, int32_t y, int32_t a,
                               int32_t b);

UnitRoomFn g_unitRoom = nullptr;
RoomLevelFn g_roomLevel = nullptr;
LevelRecordFn g_levelRecord = nullptr;
PathBlockedFn g_pathBlocked = nullptr;
MoveUnitFn g_moveUnit = nullptr;

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
	return g_moveUnit(game, unit, 0, x, y, 0, 0);
}

void InstallTeleport(uint64_t exeBase) noexcept {
	g_unitRoom = reinterpret_cast<UnitRoomFn>(exeBase + UnitRoomRva);
	g_roomLevel = reinterpret_cast<RoomLevelFn>(exeBase + RoomLevelRva);
	g_levelRecord = reinterpret_cast<LevelRecordFn>(exeBase + LevelRecordRva);
	g_pathBlocked = reinterpret_cast<PathBlockedFn>(exeBase + PathBlockedRva);
	g_moveUnit = reinterpret_cast<MoveUnitFn>(exeBase + MoveUnitRva);
}

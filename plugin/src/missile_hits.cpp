// D2Sigma's own missile hit functions (D2MslHitFuncs.cpp; server table 0x101dc7c8). Its added ones (61-77) and the
// three whose numbers D2R 3.x reused (34, 41, 42) are moved by txt_migrate (SRV_HIT_MOVED) into D2R's empty server
// hit slots 61-70 (table 0x2391030, bound 71 at 0x2391404; dispatcher 0x464ac0 calls (game, missile, target unit
// or null), and the plugin fills the ones ported here. Median's 69 runs D2R's own hit 1 and its 40 D2R's 40 (both
// vanilla's, data only). Not yet ported: 42 (one missile, parasite_2: bounces to a unit nearby) and 73 (one,
// habeas_corpus); their slots stay empty, so those missiles hit without the extra effect.
#include "missile_hits.h"

#include "d2r.h"
#include "item_spells.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr uint64_t SrvHitTable = 0x2391030;
constexpr uint64_t CltHitTable = 0x2358710;       // client hit functions (record +0x2a), bound 81 at 0x2358a64
constexpr uint64_t RvaClientOwner = 0x9a7b0;      // (missile) -> its owner on the client
constexpr uint64_t RvaClientTarget = 0x9a570;     // (unit) -> its skill target on the client
constexpr uint64_t RvaClientMissile = 0x1b7760;   // (&params, 0): client missile
constexpr uint64_t RvaUnitDataFlag = 0x34a0e0;    // (unit) -> the data-table flag
constexpr uint64_t RvaMissileOwner = 0x490300;    // (game, missile) -> owner unit
constexpr uint64_t RvaUnitClass = 0x349860;       // (unit, file, line) -> class id (missile row)
constexpr uint64_t RvaMissileRecord = 0x166040;   // (dataFlag, missile) -> missiles.txt record
constexpr uint64_t RvaMissileSkill = 0x3bc450;    // (missile) -> skill
constexpr uint64_t RvaMissileLevel = 0x3bb470;    // (missile) -> skill level
constexpr uint64_t RvaEventSkill = 0x589930;      // (game, caster, skill, level, target, x, y, 4 x out, 0)
constexpr uint64_t RvaMissileCalc = 0x3b4a90;     // (dataFlag, missile, owner, calc, class (-1: own), level (-1))
constexpr uint64_t RvaStateList = 0x2f5940;       // (unit, state) -> the unit's stat list for that state
constexpr uint64_t RvaStatListSet = 0x2f7c00;     // (dataFlag, list, stat, value, layer)
constexpr uint64_t RvaCreateStateList = 0x433d20; // (&StateListParams) -> list (D2R's self buffs, srvdofunc 18)
constexpr uint64_t RvaBuffExpired = 0x5551f0;     // the self buffs' expiry callback
constexpr uint64_t RvaSetState = 0x3355a0;        // (unit, state, on)
constexpr uint64_t RvaClearState = 0x3354c0;      // (unit, state, 0)
constexpr uint64_t RvaDetachList = 0x2f7920;      // (unit, list)
constexpr uint64_t RvaFreeList = 0x2f4180;        // (dataFlag, list)
constexpr uint64_t RvaUnitRoom = 0x34b440;        // (unit) -> room
constexpr uint64_t RvaRoomAt = 0x38dfb0;          // (room, x, y) -> the room holding x/y (near the given one)
constexpr uint64_t RvaHostile = 0x492790;         // (game, a, b, players by hostility flag) -> non-zero: enemies
constexpr uint64_t RvaGetStat = 0x2f5020;         // (unit, stat, layer)
constexpr uint64_t RvaSetStat = 0x2f7d10;         // (unit, stat, value, layer)
constexpr uint64_t RvaSpawnMonster = 0x492cf0;    // (game, room, x, y, monster, mode, -1 / 4 (search), flags)

constexpr int MsHitPar1 = 0x70;
constexpr int MsHitPar2 = 0x74;
constexpr int MsSkill = 0x1b6;                    // int16
constexpr int MsHitSubMissile1 = 0x46;            // int16
constexpr int MsCltHitSubMissile1 = 0x4e;         // int16 (D2R's client hit 2, 0x1ac8f0)
constexpr int MsCHitPar1 = 0x90;
constexpr int MsCHitPar2 = 0x94;
constexpr int SkCalc1 = 0x190;                    // skills calc1-4 at +0x190
constexpr int SkCalc3 = 0x198;
constexpr int StatLife = 6;
constexpr int StatMaxLife = 7;
constexpr int SkSummode = 0x113;                  // byte, -1 none
constexpr int SkParam1 = 0x1b8;
// D2R's missile record is 1.13c's moved on by 0x24 from +0x24 (D2Sigma reads Param4 at +0x44, SrvCalc1 at +0x80, ...)
constexpr int MsParam4 = 0x68;
constexpr int MsParam5 = 0x6c;
constexpr int MsCltParam5 = 0x8c;
constexpr int MsSrvCalc1 = 0xa4;
constexpr int MsCltCalc1 = 0xa8;
constexpr int MsSHitCalc1 = 0xac;
constexpr int MsCHitCalc1 = 0xb0;

// D2R's state stat list request (built on the stack by srvdofunc 18, 0x553e60)
struct StateListParams {
	Unit* caster;            // 0x00
	Unit* target;            // 0x08
	int32_t skill;           // 0x10
	int32_t level;           // 0x14
	int32_t duration;        // 0x18 frames
	int32_t minus1;          // 0x1c
	int32_t zero;            // 0x20
	int32_t state;           // 0x24
	void* expired;           // 0x28
};
static_assert(sizeof(StateListParams) == 0x30);

using MissileOwnerFn = Unit* (*)(Game* game, Unit* missile);
using UnitClassFn = int32_t (*)(Unit* unit, const char* file, int32_t line);
using MissileRecordFn = const uint8_t* (*)(uint32_t dataFlag, int32_t missile);
using MissileIntFn = int32_t (*)(Unit* missile);
using EventSkillFn = uint64_t (*)(Game* game, Unit* caster, int32_t skill, int32_t level, Unit* target, int32_t x,
                                  int32_t y, int32_t* outType, int32_t* outId, int32_t* outX, int32_t* outY,
                                  int32_t flag);
using SrvDoFn = int64_t (*)(Game* game, Unit* unit, int32_t skill, int32_t level);
using MissileCalcFn = int32_t (*)(uint32_t dataFlag, Unit* missile, Unit* owner, uint32_t calc, uint32_t missileClass,
                                  int32_t level);
using StateListFn = void* (*)(Unit* unit, int32_t state);
using StatListSetFn = void (*)(uint32_t dataFlag, void* list, int32_t stat, int32_t value, int32_t layer);
using CreateStateListFn = void* (*)(StateListParams* params);
using SetStateFn = void (*)(Unit* unit, int32_t state, int32_t on);
using DetachListFn = void (*)(Unit* unit, void* list);
using FreeListFn = void (*)(uint32_t dataFlag, void* list);
using HostileFn = uint64_t (*)(Game* game, Unit* a, Unit* b, char playerFlag);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using SetStatFn = void (*)(Unit* unit, int32_t stat, int32_t value, int32_t layer);
using UnitRoomFn = void* (*)(Unit* unit);
using RoomAtFn = void* (*)(void* room, int32_t x, int32_t y);
using SpawnMonsterFn = Unit* (*)(Game* game, void* room, int32_t x, int32_t y, int32_t monster, int32_t mode,
                                 int32_t search, int32_t flags);

MissileOwnerFn g_owner = nullptr;
UnitClassFn g_unitClass = nullptr;
MissileRecordFn g_missileRecord = nullptr;
MissileIntFn g_missileSkill = nullptr;
MissileIntFn g_missileLevel = nullptr;
EventSkillFn g_eventSkill = nullptr;
MissileCalcFn g_missileCalc = nullptr;
StateListFn g_stateList = nullptr;
StatListSetFn g_statListSet = nullptr;
CreateStateListFn g_createStateList = nullptr;
SetStateFn g_setState = nullptr;
SetStateFn g_clearState = nullptr;
DetachListFn g_detachList = nullptr;
FreeListFn g_freeList = nullptr;
HostileFn g_hostile = nullptr;
GetStatFn g_getStat = nullptr;
SetStatFn g_setStat = nullptr;
UnitRoomFn g_unitRoom = nullptr;
RoomAtFn g_roomAt = nullptr;
SpawnMonsterFn g_spawnMonster = nullptr;
uint64_t g_exeBase = 0;

auto RecordOf(Game* game, Unit* missile) noexcept -> const uint8_t* {
	return g_missileRecord(d2r::DataFlag(game), g_unitClass(missile, "mxl-core missile_hits.cpp", __LINE__));
}

// D2Sigma 72 (0x1008d400): the missile's owner casts skill sHitPar1 (translated by txt_migrate) at level sHitPar2
// (else the missile's level) on the unit hit, or on the missile's spot, through the item-event skill path
// (1.13c D2Game 0x6fd117e0 / 0x6fd11730 -> 0x6fd114f0; D2R 0x589930). egg_trap, wintertide, phoenix_wave, ... (55)
auto HitCastSkill(Game* game, Unit* missile, Unit* target) noexcept -> int64_t {
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	Unit* owner = g_owner(game, missile);
	const uint8_t* record = RecordOf(game, missile);
	if (owner == nullptr || record == nullptr) {
		return 1;
	}
	const int32_t skill = At<int32_t>(record, MsHitPar1);
	int32_t level = At<int32_t>(record, MsHitPar2);
	if (level < 1) {
		level = g_missileLevel(missile);
	}
	if (skill <= 0 || level < 1) {
		return 1;
	}
	int32_t x = 0, y = 0;
	if (target == nullptr && !d2r::UnitXY(missile, &x, &y)) {
		return 1;
	}
	g_eventSkill(game, owner, skill, level, target, x, y, nullptr, nullptr, nullptr, nullptr, 0);
	return 1;
}

// D2Sigma srvdofunc numbers -> D2R's, as txt_migrate moves the skills (skill_rules).
auto SrvDoSlot(int32_t sigma) noexcept -> int32_t {
	switch (sigma) {
	case 155: return 109;
	case 160: return 124;
	case 161: return 121;
	case 167: return 58;
	default: return sigma >= 153 && sigma <= 171 ? sigma + 19 : sigma;
	}
}

// D2Sigma 61 (0x1008d2d0): the missile's owner runs server skill function sHitPar1 for the missile's skill (the
// record's Skill column when set, else the skill that made the missile) at the missile's level. (3)
auto HitRunSkillFunction(Game* game, Unit* missile, Unit* /*target*/) noexcept -> int64_t {
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	Unit* owner = g_owner(game, missile);
	const uint8_t* record = RecordOf(game, missile);
	if (owner == nullptr || record == nullptr) {
		return 1;
	}
	const int32_t slot = SrvDoSlot(At<int32_t>(record, MsHitPar1));
	int32_t skill = At<int16_t>(record, MsSkill);
	if (skill <= 0) {
		skill = g_missileSkill(missile);
	}
	const int32_t level = g_missileLevel(missile);
	if (slot <= 0 || slot > 190 || skill <= 0 || level < 1) {
		return 1;
	}
	const auto function = *reinterpret_cast<SrvDoFn*>(g_exeBase + d2r::SrvDoTable + static_cast<uint64_t>(slot) * 8);
	if (function != nullptr) {
		function(game, owner, skill, level);
	}
	return 1;
}

// D2Sigma's 64 directions (D2Sigma 0x1018b140..0x1018b23f, two tables): a circle of radius 30, counter-clockwise
// from +x.
constexpr int8_t NovaDX[64] = {30, 29, 29, 28, 27, 26, 24, 23, 21, 19, 16, 14, 11, 8, 5, 2, 0, -2, -5, -8, -11, -14,
                               -16, -19, -21, -23, -24, -26, -27, -28, -29, -29, -30, -29, -29, -28, -27, -26, -24,
                               -23, -21, -19, -16, -14, -11, -8, -5, -2, 0, 2, 5, 8, 11, 14, 16, 19, 21, 23, 24, 26,
                               27, 28, 29, 29};
constexpr auto NovaDY(int i) noexcept -> int8_t {
	return NovaDX[(i + 48) % 64];  // the second table is the first a quarter turn on
}

// D2Sigma 77 (0x1008d960) -> slot 66: when the missile's range has run out, HitSubMissile1 flies out from it in
// every sHitPar1-th of the 64 directions (64: one, straight along +x), for sHitPar2 frames (else the skill's calc3,
// at least 1), with the missile's skill and level, then the missile ends (returns 3). With range left it does
// nothing (2), like vanilla's spawn-on-hit (1.13c 0x6fc5c850, D2Common 10737). frigid_nova, frigid_domain,
// coronation, hurricane_2, hammertime, ... (14). Not ported: D2Common 10637/10019 after each create (the direction
// stored on the new missile again); the sub-missiles here are straight-line or Frozen Orb style ones that take it
// from the target point.
auto HitNova(Game* game, Unit* missile, Unit* /*target*/) noexcept -> int64_t {
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	Unit* owner = g_owner(game, missile);
	const uint8_t* record = RecordOf(game, missile);
	if (owner == nullptr || record == nullptr) {
		return 1;
	}
	if (const auto* data = At<const uint8_t*>(missile, 0x10); data != nullptr && At<int16_t>(data, 0x10) != 0) {
		return 2;  // range left (missile data +0x10, set by D2R 0x3bdbc0)
	}
	const int32_t sub = At<int16_t>(record, MsHitSubMissile1);
	const int32_t skill = g_missileSkill(missile);
	const int32_t level = g_missileLevel(missile);
	if (sub <= 0) {
		return 3;
	}
	int32_t range = At<int32_t>(record, MsHitPar2);
	if (range < 1) {
		const uint8_t* skillRecord = skill > 0 ? d2r::api.getSkillRecord(d2r::DataFlag(game), skill) : nullptr;
		range = skillRecord != nullptr ? d2r::EvalCalc(game, owner, skillRecord, SkCalc3, skill, level) : 1;
		if (range < 1) {
			range = 1;
		}
	}
	int32_t step = At<int32_t>(record, MsHitPar1);
	if (step < 1) {
		step = 1;  // D2Sigma would loop forever on 0
	}
	for (int i = 0; i < 64; i += step) {
		d2r::MissileParams params{};
		params.flags = 0x8002;  // 2: target = start + offset; 0x8000: range from the params
		params.owner = owner;
		params.origin = missile;
		params.missile = sub;
		params.targetX = NovaDX[i];
		params.targetY = NovaDY(i);
		params.skill = skill;
		params.skillLevel = level;
		params.range = range;
		d2r::api.createMissile(game, &params);
	}
	return 3;
}

// D2Sigma 71 (0x1008e6f0) -> slot 63: a state on the missile's OWNER. SrvCalc1 > 0 puts state Param4 on (for
// CltCalc1 frames, or for good when < 1), < 0 takes it off; with the state on (new or already there and SrvCalc1 not
// negative) stat sHitPar1 (param Param5) is set to SHitCalc1 and stat sHitPar2 (param CltParam5) to CHitCalc1, all
// missile formulas. Ids are translated by txt_migrate. D2Sigma's own expiry is a game timer event (0xc); here
// D2R's state stat list with its self-buff expiry. Not ported: D2Sigma's stat-update call after the state (1.13c
// D2Common 10158) and the client state message D2R's buffs send (0x438470): the stats and state apply on the
// server; the state's look may not show. (12)
auto HitOwnerState(Game* game, Unit* missile, Unit* /*target*/) noexcept -> int64_t {
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	Unit* owner = g_owner(game, missile);
	const uint8_t* record = RecordOf(game, missile);
	if (owner == nullptr || record == nullptr) {
		return 1;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const int32_t state = At<int32_t>(record, MsParam4);
	if (state <= 0) {
		return 1;
	}
	const int32_t level = g_missileLevel(missile);
	const auto calc = [&](int field) noexcept -> int32_t {
		const uint32_t code = At<uint32_t>(record, field);
		return code == 0xffffffffu ? 0 : g_missileCalc(dataFlag, missile, owner, code, 0xffffffffu, -1);
	};
	const int32_t on = calc(MsSrvCalc1);
	void* list = g_stateList(owner, state);
	if (list != nullptr && on < 0) {
		g_clearState(owner, state, 0);
		g_detachList(owner, list);
		g_freeList(dataFlag, list);
		return 1;
	}
	if (list == nullptr) {
		if (on <= 0) {
			return 1;
		}
		const int32_t length = calc(MsCltCalc1);
		StateListParams params{owner, owner, g_missileSkill(missile), level,
		                       length >= 1 ? length : 25 * 60 * 60 * 24,  // "for good": a day of frames
		                       -1, 0, state, reinterpret_cast<void*>(g_exeBase + RvaBuffExpired)};
		list = g_createStateList(&params);
		if (list == nullptr) {
			return 1;
		}
		g_setState(owner, state, 1);
	}
	if (const int32_t stat = At<int32_t>(record, MsHitPar1); stat > 0) {
		g_statListSet(dataFlag, list, stat, calc(MsSHitCalc1), At<int32_t>(record, MsParam5));
	}
	if (const int32_t stat = At<int32_t>(record, MsHitPar2); stat > 0) {
		g_statListSet(dataFlag, list, stat, calc(MsCHitCalc1), At<int32_t>(record, MsCltParam5));
	}
	return 1;
}

// D2Sigma 41 (0x1008f210) -> slot 68: a monster appears where the missile hit: monstats row = the missile skill's
// calc1 (txt_migrate turns Median's row numbers in it into D2R's), in the skill's summode (else NU), as an
// ordinary monster (not a pet) through vanilla's spawn (1.13c D2Game 0x6fc66f70 -> D2R 0x492cf0, retried with a
// search distance of 4). Monster skills: corrupted_necromancer, councillor_ormac, sinew_shaper, gestating_growth,
// ... (14). Not ported: the skill-Param1 path (a superunique, D2Game 0x6fc451f0; no Median skill sets it) and
// sumumod (one skill).
auto HitSummon(Game* game, Unit* missile, Unit* /*target*/) noexcept -> int64_t {
	if (game == nullptr || missile == nullptr) {
		return 0;
	}
	Unit* owner = g_owner(game, missile);
	const int32_t skill = g_missileSkill(missile);
	const int32_t level = g_missileLevel(missile);
	const uint8_t* skillRecord = skill > 0 ? d2r::api.getSkillRecord(d2r::DataFlag(game), skill) : nullptr;
	if (owner == nullptr || skillRecord == nullptr || level < 1 || At<int32_t>(skillRecord, SkParam1) != 0) {
		return 0;
	}
	const int32_t monster = d2r::EvalCalc(game, owner, skillRecord, SkCalc1, skill, level);
	const int8_t summode = At<int8_t>(skillRecord, SkSummode);
	const int32_t mode = summode > 0 ? summode : 1;
	int32_t x = 0, y = 0;
	void* room = g_unitRoom(missile);
	if (monster <= 0 || room == nullptr || !d2r::UnitXY(missile, &x, &y)) {
		return 0;
	}
	room = g_roomAt(room, x, y);
	if (room == nullptr) {
		return 0;
	}
	if (g_spawnMonster(game, room, x, y, monster, mode, -1, 0) == nullptr) {
		g_spawnMonster(game, room, x, y, monster, mode, 4, 0);
	}
	return 1;
}

// D2Sigma 34 (0x1008df40) -> slot 67: the unit hit, when it is the owner's ally (1.13c D2Game 0x6fd00570, D2R
// 0x492790 the other way round), gets calc1 % of its maximum life back (the missile skill's calc1 at its level;
// 100 or more: full life). Life is stat 6 and maximum life 7, both in 256ths. (6)
auto HitHealAlly(Game* game, Unit* missile, Unit* target) noexcept -> int64_t {
	if (game == nullptr || missile == nullptr || target == nullptr) {
		return 0;
	}
	Unit* owner = g_owner(game, missile);
	const int32_t skill = g_missileSkill(missile);
	const int32_t level = g_missileLevel(missile);
	const uint8_t* skillRecord = skill > 0 ? d2r::api.getSkillRecord(d2r::DataFlag(game), skill) : nullptr;
	if (owner == nullptr || skillRecord == nullptr || level < 1 || g_hostile(game, owner, target, 0) != 0) {
		return 0;
	}
	const int32_t percent = d2r::EvalCalc(game, owner, skillRecord, SkCalc1, skill, level);
	const int32_t life = g_getStat(target, StatLife, 0);
	const int32_t maxLife = g_getStat(target, StatMaxLife, 0);
	if (percent > 99) {
		g_setStat(target, StatLife, maxLife, 0);
	} else if (percent > 0 && life < maxLife) {
		const int64_t heal = static_cast<int64_t>(maxLife) * percent / 100;
		g_setStat(target, StatLife, static_cast<int32_t>(std::min<int64_t>(life + heal, maxLife)), 0);
	}
	return 1;
}

// D2Sigma 30 (0x1008d220) -> slot 69: a portal where the missile is, to Median level sHitPar1; sHitPar2 set: the
// one-way kind, once per level and game (item_spells.cpp HitLevelPortal). ancient_gate, judgementwander (8)
auto HitPortal(Game* game, Unit* missile, Unit* /*target*/) noexcept -> int64_t {
	if (game == nullptr || missile == nullptr) {
		return 0;
	}
	const uint8_t* record = RecordOf(game, missile);
	if (record == nullptr) {
		return 0;
	}
	return HitLevelPortal(game, g_owner(game, missile), missile, At<int32_t>(record, MsHitPar1),
	                      At<int32_t>(record, MsHitPar2) != 0)
	           ? 1
	           : 0;
}

// D2Sigma 62 (0x1008dd70) -> slot 62: HitSubMissile1 starts where the missile is and goes for the owner's current
// skill target (1.13c path target copied over, D2Common 10042 -> 10706; D2R: the params' target unit), with the
// missile's skill and level, range sHitPar1 (else the skill's calc1, at least 1). (2)
auto HitSpawnAtTarget(Game* game, Unit* missile, Unit* /*target*/) noexcept -> int64_t {
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	Unit* owner = g_owner(game, missile);
	const uint8_t* record = RecordOf(game, missile);
	if (owner == nullptr || record == nullptr) {
		return 1;
	}
	const int32_t sub = At<int16_t>(record, MsHitSubMissile1);
	const int32_t skill = g_missileSkill(missile);
	const int32_t level = g_missileLevel(missile);
	int32_t x = 0, y = 0;
	if (sub <= 0 || !d2r::UnitXY(missile, &x, &y)) {
		return 1;
	}
	int32_t range = At<int32_t>(record, MsHitPar1);
	if (range < 1) {
		const uint8_t* skillRecord = skill > 0 ? d2r::api.getSkillRecord(d2r::DataFlag(game), skill) : nullptr;
		range = skillRecord != nullptr ? d2r::EvalCalc(game, owner, skillRecord, SkCalc1, skill, level) : 1;
		range = range < 1 ? 1 : range;
	}
	d2r::MissileParams params{};
	params.flags = 0x8001;  // 1: start at x/y; 0x8000: range from the params
	params.owner = owner;
	params.target = d2r::api.skillTargetUnit(game, owner);
	params.missile = sub;
	params.x = x;
	params.y = y;
	params.skill = skill;
	params.skillLevel = level;
	params.range = range;
	d2r::api.createMissile(game, &params);
	return 1;
}

// Client side: D2R's client hit functions get (missile, unit hit or null).
using ClientUnitFn = Unit* (*)(Unit* unit);
using ClientMissileFn = Unit* (*)(d2r::MissileParams* params, int32_t zero);
using UnitDataFlagFn = uint32_t (*)(Unit* unit);
ClientUnitFn g_clientOwner = nullptr;
ClientUnitFn g_clientTarget = nullptr;
ClientMissileFn g_clientMissile = nullptr;
UnitDataFlagFn g_unitDataFlag = nullptr;

// The client half of a hit: the missile's record, owner, skill and level.
struct ClientHit {
	const uint8_t* record;
	Unit* owner;
	int32_t skill;
	int32_t level;
	uint32_t dataFlag;
};
auto ClientHitOf(Unit* missile, ClientHit* hit) noexcept -> bool {
	if (missile == nullptr) {
		return false;
	}
	hit->dataFlag = g_unitDataFlag(missile);
	hit->record = g_missileRecord(hit->dataFlag, g_unitClass(missile, "mxl-core missile_hits.cpp", __LINE__));
	hit->owner = g_clientOwner(missile);
	hit->skill = g_missileSkill(missile);
	hit->level = g_missileLevel(missile);
	return hit->record != nullptr && hit->owner != nullptr;
}

// A calc of the missile skill on the client (D2Sigma's fallbacks for an empty cHitPar)
auto ClientSkillCalc(const ClientHit& hit, int field) noexcept -> int32_t {
	const uint8_t* skill = hit.skill > 0 ? d2r::api.getSkillRecord(hit.dataFlag, hit.skill) : nullptr;
	const int32_t v = skill != nullptr
	                      ? d2r::api.evalSkillCalc(hit.dataFlag, hit.owner, At<int32_t>(skill, field), hit.skill, hit.level)
	                      : 1;
	return v < 1 ? 1 : v;
}

// D2Sigma client 7 (0x1008c770) -> client slot 7 (empty in D2R): the client half of hit 77's nova,
// CltHitSubMissile1 in every cHitPar1-th of the 64 directions, range cHitPar2 (else calc3). (14)
auto CltHitNova(Unit* missile, Unit* /*target*/) noexcept -> int64_t {
	ClientHit hit{};
	if (!ClientHitOf(missile, &hit)) {
		return 1;
	}
	const int32_t sub = At<int16_t>(hit.record, MsCltHitSubMissile1);
	if (sub <= 0) {
		return 1;
	}
	int32_t range = At<int32_t>(hit.record, MsCHitPar2);
	if (range < 1) {
		range = ClientSkillCalc(hit, SkCalc3);
	}
	int32_t step = At<int32_t>(hit.record, MsCHitPar1);
	step = step < 1 ? 1 : step;
	for (int i = 0; i < 64; i += step) {
		d2r::MissileParams params{};
		params.flags = 0x8002;
		params.owner = hit.owner;
		params.origin = missile;
		params.missile = sub;
		params.targetX = NovaDX[i];
		params.targetY = NovaDY(i);
		params.skill = hit.skill;
		params.skillLevel = hit.level;
		params.range = range;
		g_clientMissile(&params, 0);
	}
	return 1;
}

// D2Sigma client 6 (0x1008c970) -> client slot 6 (empty in D2R): the client half of hit 62, CltHitSubMissile1 from
// the missile towards the owner's target, range cHitPar1 (else calc1). (2)
auto CltHitAtTarget(Unit* missile, Unit* /*target*/) noexcept -> int64_t {
	ClientHit hit{};
	if (!ClientHitOf(missile, &hit)) {
		return 1;
	}
	const int32_t sub = At<int16_t>(hit.record, MsCltHitSubMissile1);
	int32_t x = 0, y = 0;
	if (sub <= 0 || !d2r::UnitXY(missile, &x, &y)) {
		return 1;
	}
	int32_t range = At<int32_t>(hit.record, MsCHitPar1);
	if (range < 1) {
		range = ClientSkillCalc(hit, SkCalc1);
	}
	d2r::MissileParams params{};
	params.flags = 0x8001;
	params.owner = hit.owner;
	params.target = g_clientTarget(hit.owner);
	params.missile = sub;
	params.x = x;
	params.y = y;
	params.skill = hit.skill;
	params.skillLevel = hit.level;
	params.range = range;
	g_clientMissile(&params, 0);
	return 1;
}

using HitFn = int64_t (*)(Game* game, Unit* missile, Unit* target);
using CltHitFn = int64_t (*)(Unit* missile, Unit* target);
struct HitSlot {
	int slot;
	HitFn function;
	const char* name;
};
struct CltHitSlot {
	int slot;
	CltHitFn function;
	const char* name;
};
constexpr CltHitSlot CltHits[] = {
	{6, CltHitAtTarget, "missile at owner's target (D2Sigma client 6)"},
	{7, CltHitNova, "nova (D2Sigma client 7)"},
};
constexpr HitSlot Hits[] = {
	{61, HitRunSkillFunction, "run skill function (D2Sigma hit 61)"},
	{62, HitSpawnAtTarget, "missile at owner's target (D2Sigma hit 62)"},
	{63, HitOwnerState, "owner state (D2Sigma hit 71)"},
	{64, HitCastSkill, "cast skill (D2Sigma hit 72)"},
	{66, HitNova, "nova (D2Sigma hit 77)"},
	{67, HitHealAlly, "heal ally (D2Sigma hit 34)"},
	{68, HitSummon, "summon (D2Sigma hit 41)"},
	{69, HitPortal, "level portal (D2Sigma hit 30)"},
};

}  // namespace

auto InstallMissileHits(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	g_exeBase = context->exeBase;
	g_owner = reinterpret_cast<MissileOwnerFn>(g_exeBase + RvaMissileOwner);
	g_unitClass = reinterpret_cast<UnitClassFn>(g_exeBase + RvaUnitClass);
	g_missileRecord = reinterpret_cast<MissileRecordFn>(g_exeBase + RvaMissileRecord);
	g_missileSkill = reinterpret_cast<MissileIntFn>(g_exeBase + RvaMissileSkill);
	g_missileLevel = reinterpret_cast<MissileIntFn>(g_exeBase + RvaMissileLevel);
	g_eventSkill = reinterpret_cast<EventSkillFn>(g_exeBase + RvaEventSkill);
	g_missileCalc = reinterpret_cast<MissileCalcFn>(g_exeBase + RvaMissileCalc);
	g_stateList = reinterpret_cast<StateListFn>(g_exeBase + RvaStateList);
	g_statListSet = reinterpret_cast<StatListSetFn>(g_exeBase + RvaStatListSet);
	g_createStateList = reinterpret_cast<CreateStateListFn>(g_exeBase + RvaCreateStateList);
	g_setState = reinterpret_cast<SetStateFn>(g_exeBase + RvaSetState);
	g_clearState = reinterpret_cast<SetStateFn>(g_exeBase + RvaClearState);
	g_detachList = reinterpret_cast<DetachListFn>(g_exeBase + RvaDetachList);
	g_freeList = reinterpret_cast<FreeListFn>(g_exeBase + RvaFreeList);
	g_hostile = reinterpret_cast<HostileFn>(g_exeBase + RvaHostile);
	g_getStat = reinterpret_cast<GetStatFn>(g_exeBase + RvaGetStat);
	g_setStat = reinterpret_cast<SetStatFn>(g_exeBase + RvaSetStat);
	g_unitRoom = reinterpret_cast<UnitRoomFn>(g_exeBase + RvaUnitRoom);
	g_roomAt = reinterpret_cast<RoomAtFn>(g_exeBase + RvaRoomAt);
	g_spawnMonster = reinterpret_cast<SpawnMonsterFn>(g_exeBase + RvaSpawnMonster);
	g_clientOwner = reinterpret_cast<ClientUnitFn>(g_exeBase + RvaClientOwner);
	g_clientTarget = reinterpret_cast<ClientUnitFn>(g_exeBase + RvaClientTarget);
	g_clientMissile = reinterpret_cast<ClientMissileFn>(g_exeBase + RvaClientMissile);
	g_unitDataFlag = reinterpret_cast<UnitDataFlagFn>(g_exeBase + RvaUnitDataFlag);
	static constexpr uint8_t Empty[8] = {};
	int installed = 0;
	size_t used = 0;
	for (const HitSlot& h : Hits) {
		const bool ok = context->PatchWriteU64(SrvHitTable + static_cast<uint64_t>(h.slot) * 8, Empty, sizeof(Empty),
		                                       reinterpret_cast<uint64_t>(h.function));
		installed += ok ? 1 : 0;
		if (used < reportSize) {
			const int n = std::snprintf(report + used, reportSize - used, "%s srvhit %d %s%s", used ? ";" : "", h.slot,
			                            h.name, ok ? "" : " FAILED");
			used += n > 0 ? static_cast<size_t>(n) : 0;
		}
	}
	for (const CltHitSlot& h : CltHits) {
		const bool ok = context->PatchWriteU64(CltHitTable + static_cast<uint64_t>(h.slot) * 8, Empty, sizeof(Empty),
		                                       reinterpret_cast<uint64_t>(h.function));
		installed += ok ? 1 : 0;
		if (used < reportSize) {
			const int n = std::snprintf(report + used, reportSize - used, "%s clthit %d %s%s", used ? ";" : "", h.slot,
			                            h.name, ok ? "" : " FAILED");
			used += n > 0 ? static_cast<size_t>(n) : 0;
		}
	}
	return installed;
}

auto MissileHitCount() noexcept -> int {
	return static_cast<int>(sizeof(Hits) / sizeof(Hits[0]) + sizeof(CltHits) / sizeof(CltHits[0]));
}

// D2Sigma's added skill server functions (skills.txt srvdofunc), rebuilt on D2R's game functions.
//
// D2Sigma numbers them 153-171; D2R already uses those, so tools/txt_migrate.py moves Median's skills to 172-190
// (+19) and this file fills those empty slots of D2R's srvdofunc table. Each function follows the disassembly of the
// D2Sigma original named in its comment (docs/plugin-spec.md); 1.10 record offsets are translated to D2R's.
#include "sigma_skills.h"

#include "d2r.h"
#include "pet_scaling.h"
#include "area_damage.h"
#include "item_spells.h"
#include "teleport.h"
#include "stat_list.h"
#include "auras.h"

#include <cstdio>
#include <cmath>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// D2R calls srvdofunc entries as (game, unit, skillId, skillLevel) and treats non-zero as success.
using SrvDoFn = int64_t (*)(Game* game, Unit* unit, int32_t skillId, int32_t level);

auto ValidMissile(Game* game, int32_t missile) noexcept -> bool {
	return missile >= 0 && missile < d2r::api.missileCount(d2r::DataFlag(game));
}

// D2Sigma 158 (0x100acc50) -> 176+1 = 177: a row of missiles. calc1 missiles fly from the caster towards points on a
// line through the target, perpendicular to the aim; calc3 (clamped to 1-3) picks srvmissilea/b/c and calc2 fills the
// missile's attack bonus. DeathMaul, MegademonInferno, CountessFirewall, egg_trap and 70 more.
auto SrvDoMissileRow(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	int32_t tx = 0, ty = 0;
	if (d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0) {
		return 1;
	}
	const int32_t count = d2r::EvalCalc(game, unit, skill, d2r::SkCalc1, skillId, level);
	int32_t ux = 0, uy = 0;
	d2r::UnitXY(unit, &ux, &uy);
	int32_t dx = tx - ux, dy = ty - uy;
	int32_t d2 = dx * dx + dy * dy;
	if (d2 < 4) {  // as D2Sigma: scale short aims up (its second term is 2*dx, not dx*dx)
		dx <<= 2;
		dy <<= 2;
		d2 = dy * dy + dx * 2;
	}
	int32_t stepX = d2 >= 16 ? dy : dy * 2;   // perpendicular to (dx, dy)
	int32_t stepY = -(d2 >= 16 ? dx : dx * 2);
	while (stepX * stepX + stepY * stepY > 3) {
		stepX /= 2;
		stepY /= 2;
	}
	int32_t pick = d2r::EvalCalc(game, unit, skill, d2r::SkCalc3, skillId, level);
	pick = pick < 1 ? 1 : (pick > 3 ? 3 : pick);
	const int32_t missile = At<int16_t>(skill, d2r::SkSrvMissile + pick * 2);
	if (!ValidMissile(game, missile)) {
		return 0;
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x820;
	params.owner = unit;
	params.origin = unit;
	params.missile = missile;
	params.skill = skillId;
	params.skillLevel = level;
	params.attBonus = d2r::EvalCalc(game, unit, skill, d2r::SkCalc2, skillId, level);
	int32_t x = tx - stepX * count / 2;
	int32_t y = ty - stepY * count / 2;
	for (int32_t i = 0; i < count; ++i) {
		params.targetX = x;
		params.targetY = y;
		d2r::api.createMissile(game, &params);
		x += stepX;
		y += stepY;
	}
	return 1;
}

// D2Sigma 157 (0x100aca50) -> 176: a barrage. prgcalc1 missiles of srvmissilea, each aimed at a random point within
// aurarangecalc of the target (points closer than 2 to the caster are skipped); with one missile or a radius
// below 2, a single missile at the target. catapult_shot, overkill, hail_of_stones and 22 more.
auto SrvDoMissileBarrage(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr || level < 1) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, d2r::SkSrvMissileA);
	const int32_t count = d2r::EvalCalc(game, unit, skill, d2r::SkPrgCalc1, skillId, level);
	const int32_t radius = d2r::EvalCalc(game, unit, skill, d2r::SkAuraRangeCalc, skillId, level);
	if (missile == -1 || count <= 0 || !ValidMissile(game, missile)) {
		return 0;
	}
	int32_t tx = 0, ty = 0;
	if (d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0) {
		return 0;
	}
	uint32_t* seed = d2r::api.unitSeed(unit);  // D2Sigma reseeds the caster: lo = next roll, hi = 666
	d2r::Roll(seed);
	seed[1] = 0x29a;
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x420;
	params.owner = unit;
	params.origin = unit;
	params.missile = missile;
	params.skill = skillId;
	params.skillLevel = level;
	if (count <= 1 || radius < 2) {
		params.targetX = tx;
		params.targetY = ty;
		return d2r::api.createMissile(game, &params) != nullptr ? 1 : 0;
	}
	for (int32_t i = 0; i < count; ++i) {
		params.targetX = d2r::RandomBelow(seed, radius * 2) - radius + tx;
		params.targetY = d2r::RandomBelow(seed, radius * 2) - radius + ty;
		int32_t ux = 0, uy = 0;
		if (d2r::UnitXY(unit, &ux, &uy)) {
			const int32_t ex = params.targetX - ux, ey = params.targetY - uy;
			if (ex * ex + ey * ey >= 4) {
				d2r::api.createMissile(game, &params);
			}
		}
	}
	return 1;
}

// D2Sigma 153 (0x100ad720) -> 172: several pets per cast. min(calc2, petmax) pets of the skill's summon/summode/
// pettype; at the skill target when it lies within aurarangecalc (a squared distance) of the caster, else at the
// caster. blood_skeleton, ice_elementals, fire_elementals, the Barbarian spirits, the totems and 35 more.
// The pets are set up as D2R's Raise Skeleton does it (level, the skill's passive/aura stats, party). Not ported
// yet: D2Sigma's TargetCorpse branch (no skill on this function uses it) and its achievement counter. Each pet gets
// D2Sigma's minion scaling (0x100683d0, pet_scaling.cpp) after its level is set.
auto SrvDoMultiSummon(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr || level < 1) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	int32_t x = 0, y = 0;
	const int32_t radius = d2r::EvalCalc(game, unit, skill, d2r::SkAuraRangeCalc, skillId, level);
	if (radius != 0) {
		int32_t ux = 0, uy = 0, tx = 0, ty = 0;
		if (d2r::UnitXY(unit, &ux, &uy) && d2r::api.skillTargetXY(game, unit, &tx, &ty) != 0 &&
		    (tx - ux) * (tx - ux) + (ty - uy) * (ty - uy) <= radius) {
			x = tx;
			y = ty;
		}
	}
	const int32_t petMax = d2r::EvalCalc(game, unit, skill, d2r::SkPetMaxCalc, skillId, level);
	int32_t count = d2r::EvalCalc(game, unit, skill, d2r::SkCalc2, skillId, level);
	count = count > petMax ? petMax : count;
	const uint8_t mode = At<uint8_t>(skill, d2r::SkSumMode);
	d2r::SummonParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = (x != 0 && y != 0) ? 1 : 0;
	params.owner = unit;
	params.monster = At<uint16_t>(skill, d2r::SkSummon);
	params.mode = mode == 0xff ? 1 : mode;
	params.x = x;
	params.y = y;
	params.petType = At<uint8_t>(skill, d2r::SkPetType);
	params.petMax = petMax;
	if (At<int16_t>(skill, d2r::SkSummon) < 0) {
		return 0;
	}
	for (int32_t i = 0; i < count; ++i) {
		Unit* pet = d2r::api.summonPet(game, &params);
		if (pet == nullptr) {
			continue;
		}
		d2r::api.setPetLevel(game, unit, pet, 0, level);
		ScalePet(game, unit, pet, skillId, level);
		d2r::api.petSkillStats(game, unit, pet, skillId, level, At<int32_t>(unit, d2r::UnitArg12c), 0);
		d2r::api.addPetToParty(game, pet, 0, At<int32_t>(unit, d2r::UnitArg130));
	}
	return 1;
}

// D2Sigma 159 (0x100aa7e0) -> 178: a strike that bursts into missiles, built like D2R's Charged Strike (0x5562b0).
// The skill's target (if any) takes the melee hit; then calc1 missiles (srvmissilea/b/c by calc2, clamped 1-3) start
// at the skill target and fly on past it, each initialised as a charged-strike bolt with its index (flags 0x21).
// spiral_dance, heaven_s_fury, crucify_2, barrier_strike and 11 more. Not ported: D2Sigma's call 0x10041f30
// (game, unit) through D2Game (unidentified).
auto SrvDoStrikeBurst(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr) {
		return 0;
	}
	int32_t ux = 0, uy = 0;
	d2r::UnitXY(unit, &ux, &uy);
	if (Unit* target = d2r::api.skillTargetUnit(game, unit)) {
		d2r::api.applyMeleeHit(game, unit, target, 0, 1);
	}
	int32_t tx = 0, ty = 0;
	if (d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0) {
		return 0;
	}
	const int32_t count = d2r::EvalCalc(game, unit, skill, d2r::SkCalc1, skillId, level);
	int32_t pick = d2r::EvalCalc(game, unit, skill, d2r::SkCalc2, skillId, level);
	pick = pick < 1 ? 1 : (pick > 3 ? 3 : pick);
	const int32_t missile = At<int16_t>(skill, d2r::SkSrvMissile + pick * 2);
	if (!ValidMissile(game, missile)) {
		return 0;
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x21;
	params.owner = unit;
	params.missile = missile;
	params.x = tx;
	params.y = ty;
	params.targetX = tx * 2 - ux;
	params.targetY = ty * 2 - uy;
	params.skill = skillId;
	params.skillLevel = level;
	params.initKind = 1;
	for (int32_t i = 0; i < count; ++i) {
		params.initArg = i;
		d2r::api.createMissile(game, &params);
	}
	return 1;
}

// D2Sigma 166 (0x100abff0) -> 185: one missile placed on the target point (flags 1, start = the skill target),
// srvmissilea/b/c by calc3 clamped 1-3; refused beyond D2's approximate distance 100. flamestrike, flamestrike_2,
// Echoing-Strike-style skills (7). Not ported: D2Sigma's collision test of the target spot for the missile's size
// (D2Common 10303, mask 5); D2R's missile creation handles a blocked spot itself.
auto SrvDoMissileAtTarget(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr) {
		return 0;
	}
	int32_t pick = d2r::EvalCalc(game, unit, skill, d2r::SkCalc3, skillId, level);
	pick = pick < 1 ? 1 : (pick > 3 ? 3 : pick);
	const int32_t missile = At<int16_t>(skill, d2r::SkSrvMissile + pick * 2);
	if (!ValidMissile(game, missile)) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	int32_t tx = 0, ty = 0, ux = 0, uy = 0;
	if (d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0 && tx == 0 && ty == 0) {
		return 0;
	}
	if (d2r::UnitXY(unit, &ux, &uy)) {  // D2Common 10080: max(|dx|,|dy|) + min(|dx|,|dy|)/2
		const int32_t ax = tx > ux ? tx - ux : ux - tx, ay = ty > uy ? ty - uy : uy - ty;
		if ((ay < ax ? (ay + ax * 2) / 2 : (ax + ay * 2) / 2) > 100) {
			return 0;
		}
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 1;
	params.owner = unit;
	params.missile = missile;
	params.x = tx;
	params.y = ty;
	params.skill = skillId;
	params.skillLevel = level;
	return d2r::api.createMissile(game, &params) != nullptr ? 1 : 0;
}

struct SlotFunction {
	int slot;
	SrvDoFn function;
	const char* name;
};

constexpr SlotFunction Functions[] = {
	{172, SrvDoMultiSummon, "multi-summon (D2Sigma 153)"},
	{173, SrvDoLevelPortal, "portal to a level (D2Sigma 154)"},
	{174, SrvDoLayeredBuff, "self buff with layered stats (D2Sigma 18)"},
	{176, SrvDoMissileBarrage, "missile barrage (D2Sigma 157)"},
	{177, SrvDoMissileRow, "missile row (D2Sigma 158)"},
	{178, SrvDoStrikeBurst, "strike burst (D2Sigma 159)"},
	{179, SrvDoRangedTeleport, "teleport with a range (D2Sigma 27)"},
	{180, SrvDoLayeredStorm, "storm with layered stats (D2Sigma 124)"},
	{184, SrvDoAura, "aura with layered stats (D2Sigma 165)"},
	{185, SrvDoMissileAtTarget, "missile at target (D2Sigma 166)"},
	{190, SrvDoAreaDamage, "area damage (D2Sigma 171)"},
};


// --- Client skill functions (cltdofunc) -------------------------------------------------------------------------
// D2Sigma's client table (0x101df428) adds 96-99, but D2R 3.x gave 96-109 to its own skills (97 Volcano, 98 Sigil
// Death Explosion, 99 Ring of Fire), so Median's rows ran those. D2R's table (0x235f7d0, 130 slots, bound at
// 0x235f7c4) is free from 110 on: txt_migrate moves Median's 97/98/99 to 110/111/112. Client functions are
// (unit, skill, level) -> non-zero when they ran (dispatcher 0x216d20).
constexpr uint64_t CltDoTable = 0x235f7d0;
constexpr uint64_t RvaUnitDataFlag = 0x34a0e0;     // (unit) -> data flag byte, as client functions read it
constexpr uint64_t RvaClientTarget = 0x9a570;      // (unit) -> the unit's skill target (client)
constexpr uint64_t RvaClientMissile = 0x1b7760;    // (&params, 0): client missile, D2R's client Charged Strike
constexpr uint64_t RvaClientTargetXY = 0x214c80;   // (unit, &x, &y) -> non-zero: the skill's target point (client)
constexpr int SkCltMissile = 0x13e;                // int16 cltmissile, then cltmissilea/b/c at +0x140/+0x142/+0x144

using UnitDataFlagFn = uint8_t (*)(Unit* unit);
using ClientTargetFn = Unit* (*)(Unit* unit);
using ClientMissileFn = Unit* (*)(d2r::MissileParams* params, int32_t zero);
using ClientTargetXYFn = int32_t (*)(Unit* unit, int32_t* x, int32_t* y);
ClientTargetXYFn g_clientTargetXY = nullptr;
UnitDataFlagFn g_unitDataFlag = nullptr;
ClientTargetFn g_clientTarget = nullptr;
ClientMissileFn g_clientMissile = nullptr;

// D2Sigma client 98 (0x100a84b0), the client half of the strike burst (159): cltmissilea/b/c by calc3 (clamped 1-3)
// placed on the target, as the server places its missile (here also aimed past it, like the server port). Skipped
// beyond D2Sigma's distance limit (D2Common distance > 10000). whirlpool, torment, spiral_dance, crucify_2, ...
auto CltDoStrikeBurst(Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (unit == nullptr || g_clientMissile == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = g_unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	Unit* target = g_clientTarget(unit);
	if (skill == nullptr || target == nullptr) {
		return 0;
	}
	int32_t pick = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::SkCalc3), skillId, level);
	pick = pick < 1 ? 1 : (pick > 3 ? 3 : pick);
	const int32_t missile = At<int16_t>(skill, SkCltMissile + pick * 2);
	if (missile < 0 || missile >= d2r::api.missileCount(dataFlag)) {
		return 0;
	}
	int32_t ux = 0, uy = 0, tx = 0, ty = 0;
	if (!d2r::UnitXY(unit, &ux, &uy) || !d2r::UnitXY(target, &tx, &ty)) {
		return 0;
	}
	const int64_t dx = tx - ux, dy = ty - uy;
	if (dx * dx + dy * dy > 10000) {
		return 0;
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x21;
	params.owner = unit;
	params.missile = missile;
	params.x = tx;
	params.y = ty;
	params.targetX = tx * 2 - ux;
	params.targetY = ty * 2 - uy;
	params.skill = skillId;
	params.skillLevel = level;
	params.initKind = 1;
	g_clientMissile(&params, 0);
	return 1;
}

// D2Sigma client 97 (0x100a7690), paired with the missile row (158): evaluates aurarangecalc and three calcs, reads
// the target and returns 0, creating nothing (the row's missiles come from the server). Kept as a function so the
// dispatcher takes the same "returned 0" path rather than the empty-slot one.
auto CltDoMissileRow(Unit* /*unit*/, int32_t /*skillId*/, int32_t /*level*/) noexcept -> int64_t {
	return 0;
}

// D2Sigma client 99 (0x100a7cb0), paired with the missile at target (166): calc1 client missiles, cltmissilea/b/c by
// calc2 (clamped 1-3), each placed on the skill's target point and aimed past it (2 * target - caster), flags 0x21,
// the index as init argument with D2Client's charged-strike style init (D2R initKind 1). flamestrike, dragon_jaws, ...
auto CltDoMissileAtTarget(Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (unit == nullptr || g_clientMissile == nullptr || g_clientTargetXY == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = g_unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	int32_t pick = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::SkCalc2), skillId, level);
	pick = pick < 1 ? 1 : (pick > 3 ? 3 : pick);
	const int32_t missile = At<int16_t>(skill, SkCltMissile + pick * 2);
	if (missile < 0 || missile >= d2r::api.missileCount(dataFlag)) {
		return 0;
	}
	int32_t ux = 0, uy = 0, tx = 0, ty = 0;
	if (!d2r::UnitXY(unit, &ux, &uy) || g_clientTargetXY(unit, &tx, &ty) == 0 || (tx == 0 && ty == 0)) {
		return 0;
	}
	const int32_t count = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::SkCalc1), skillId, level);
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x21;
	params.owner = unit;
	params.missile = missile;
	params.x = tx;
	params.y = ty;
	params.targetX = tx * 2 - ux;
	params.targetY = ty * 2 - uy;
	params.skill = skillId;
	params.skillLevel = level;
	params.initKind = 1;
	for (int32_t i = 0; i < count; ++i) {
		params.initArg = i;
		g_clientMissile(&params, 0);
	}
	return 1;
}

// D2Sigma client 22 (0x100a7fa0; D2R's 22 is now an Amazon chain-lightning client): two cltmissilea placed on the
// skill's target point, moved within aurarangecalc of the caster (as the teleport clamps), flying out perpendicular
// to the cast direction, like the server's fire wall. parasite_2, parasite_3, rock_shock. Not ported: D2Sigma's
// collision test of the spot for the missile's size (D2Common 10303, mask 5) and its town check.
auto CltDoWall(Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (unit == nullptr || g_clientMissile == nullptr || g_clientTargetXY == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = g_unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, SkCltMissile + 2);
	if (missile < 0 || missile >= d2r::api.missileCount(dataFlag)) {
		return 0;
	}
	int32_t ux = 0, uy = 0, tx = 0, ty = 0;
	if (!d2r::UnitXY(unit, &ux, &uy) || g_clientTargetXY(unit, &tx, &ty) == 0 || (tx == 0 && ty == 0)) {
		return 0;
	}
	const int32_t range =
		d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::SkAuraRangeCalc), skillId, level);
	if (range != 0) {
		const double dx = tx - ux, dy = ty - uy;
		const double distance = std::sqrt(dx * dx + dy * dy);
		const double limit = range < 0 ? -range : range;
		if (distance >= limit && distance > 0.0) {
			tx = ux + static_cast<int32_t>(dx * limit / distance);
			ty = uy + static_cast<int32_t>(dy * limit / distance);
		}
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x21;
	params.owner = unit;
	params.missile = missile;
	params.x = tx;
	params.y = ty;
	params.skill = skillId;
	params.skillLevel = level;
	params.targetX = tx + (ty - uy);
	params.targetY = ty - (tx - ux);
	g_clientMissile(&params, 0);
	params.targetX = tx - (ty - uy);
	params.targetY = ty + (tx - ux);
	g_clientMissile(&params, 0);
	return 1;
}

// D2Sigma's own versions of two vanilla server functions differ from D2R's in one step each (docs/plugin-spec.md); the
// rest of both is the same code, so D2R's function runs in place and the step is added around it.
//   41 (D2Sigma 0x100a9300; batstrike_2, vampiric_strike, deathgaze, skills_2759): the skill's target unit, if any, takes
//      the queued melee hit first (1.13c pointer 0x101f1b04, D2R 0x44b2b0 as in SrvDoStrikeBurst), then the missiles
//      fly in a random scatter around it. D2R's 0x56dd60 only throws the missiles.
//   73 (D2Sigma 0x100aa190; hammer_of_zerae, power_shift): the missile is srvmissilea/b/c by calc2 clamped 1-3
//      (calc2 blank = 1 = srvmissilea). D2R's 0x561f80 always takes srvmissilea (its progressive-skill getter 0x571700):
//      for the duration of the call srvmissilea holds the one calc2 picks.
constexpr int SrvDoFanSlot = 41;
constexpr int SrvDoHammerSlot = 73;
constexpr uint64_t RvaSrvDoFan = 0x56dd60;
constexpr uint64_t RvaSrvDoHammer = 0x561f80;
SrvDoFn g_srvDoFan = nullptr;
SrvDoFn g_srvDoHammer = nullptr;
long g_fanCasts = 0, g_fanMeleeHits = 0, g_hammerCasts = 0, g_hammerSwaps = 0;

auto SrvDoFan(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr || g_srvDoFan == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	++g_fanCasts;
	if (Unit* target = d2r::api.skillTargetUnit(game, unit)) {
		d2r::api.applyMeleeHit(game, unit, target, 0, 1);
		++g_fanMeleeHits;
	}
	g_srvDoFan(game, unit, skillId, level);
	return 1;
}

auto SrvDoHammer(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr || g_srvDoHammer == nullptr) {
		return 0;
	}
	uint8_t* skill = const_cast<uint8_t*>(d2r::api.getSkillRecord(d2r::DataFlag(game), skillId));
	int16_t* first = skill != nullptr ? reinterpret_cast<int16_t*>(skill + d2r::SkSrvMissileA) : nullptr;
	int16_t kept = 0;
	++g_hammerCasts;
	if (first != nullptr) {
		int32_t pick = d2r::EvalCalc(game, unit, skill, d2r::SkCalc2, skillId, level);
		pick = pick < 1 ? 1 : (pick > 3 ? 3 : pick);
		const int16_t chosen = At<int16_t>(skill, d2r::SkSrvMissile + pick * 2);
		if (pick > 1 && ValidMissile(game, chosen)) {
			kept = *first;
			*first = chosen;
			++g_hammerSwaps;
		} else {
			first = nullptr;
		}
	}
	const int64_t done = g_srvDoHammer(game, unit, skillId, level);
	if (first != nullptr) {
		*first = kept;
	}
	return done;
}

// D2R's client curse function (slot 30, 0x2b05d0) reads cltmissilea and asserts ("nMissile >= 0 && nMissile <
// NumMissileStats", then returns without drawing) when the skill has none. Median's curses are mostly rows with no
// cltmissilea / cltmissilec (52 of 75 cltdofunc 30 rows): their look is the state overlay, and D2Sigma's function
// (D2Client's curse, filled in at start-up) skips the missile quietly. Wrapped in place: a row without a client
// missile draws nothing, as in Median (the cast sound and the rest of the dispatcher still run, the function
// returns 1), a row with one runs D2R's function unchanged. No curse visual is added to the rows.
constexpr int CltDoCurseSlot = 30;
constexpr uint64_t RvaCltDoCurse = 0x2b05d0;
using CltDoFn = int64_t (*)(Unit* unit, int32_t skill, int32_t level);
CltDoFn g_cltDoCurse = nullptr;
auto CltDoCurse(Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (unit == nullptr || g_cltDoCurse == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = g_unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, SkCltMissile + 2);
	if (missile < 0 || missile >= d2r::api.missileCount(dataFlag)) {
		return 1;
	}
	g_cltDoCurse(unit, skillId, level);
	return 1;
}

// D2R's area applier of the server aura / curse functions (0x4327d0 -> 0x4398b0) asserts "nRadius > 0" and applies nothing when the
// skill's radius calc (aurarangecalc, skill +0x84) gives 0 or less. Median's self-only auras and monster property auras (property
// dmg% = aura, e.g. the treasure goblin's skills_2511, srvdofunc 65) leave aurarangecalc blank: D2Sigma applies the state to the
// caster and then to nobody. The applier itself returns before doing anything for a radius below 1, so skipping the call here keeps
// the effect and drops the assert that fired every aura tick (about every 400 ms per monster).
constexpr uint64_t RvaAreaAura = 0x4327d0;
// MOV [RSP+0x10],RBX / MOV [RSP+0x18],RBP / MOV [RSP+0x20],RSI / PUSH RDI / SUB RSP,0x60
constexpr uint8_t AreaAuraEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18,
                                     0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x60};
using AreaAuraFn = void (*)(void*, void*, uint32_t, uint32_t, int32_t radius, uint32_t, void*, void*, uint32_t, void*, uint32_t);
AreaAuraFn g_areaAura = nullptr;
void AreaAuraHook(void* game, void* unit, uint32_t a, uint32_t b, int32_t radius, uint32_t flags, void* callback, void* context,
                  uint32_t mode, void* file, uint32_t line) noexcept {
	if (radius < 1) {
		return;
	}
	g_areaAura(game, unit, a, b, radius, flags, callback, context, mode, file, line);
}

struct ClientSlot {
	int slot;
	CltDoFn function;
	const char* name;
};
constexpr ClientSlot ClientFunctions[] = {
	{110, CltDoMissileRow, "client missile row (D2Sigma client 97)"},
	{111, CltDoStrikeBurst, "client strike burst (D2Sigma client 98)"},
	{112, CltDoMissileAtTarget, "client missile at target (D2Sigma client 99)"},
	{113, CltDoWall, "client wall (D2Sigma client 22)"},
};

}  // namespace

auto InstallSigmaSkills(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	d2r::Bind(context->exeBase);
	InstallPetScaling(context->exeBase);
	InstallAuras(context->exeBase);
	InstallAreaDamage(context->exeBase);
	InstallTeleport(context->exeBase);
	InstallStatList(context->exeBase);
	static constexpr uint8_t Empty[8] = {};
	int installed = 0;
	size_t used = 0;
	for (const SlotFunction& f : Functions) {
		const uint64_t rva = d2r::SrvDoTable + static_cast<uint64_t>(f.slot) * 8;
		const bool ok = context->PatchWriteU64(rva, Empty, sizeof(Empty), reinterpret_cast<uint64_t>(f.function));
		installed += ok ? 1 : 0;
		if (used < reportSize) {
			const int n = std::snprintf(report + used, reportSize - used, "%s srvdofunc %d %s%s", used ? ";" : "",
			                            f.slot, f.name, ok ? "" : " FAILED");
			used += n > 0 ? static_cast<size_t>(n) : 0;
		}
	}
	g_unitDataFlag = reinterpret_cast<UnitDataFlagFn>(context->exeBase + RvaUnitDataFlag);
	g_clientTarget = reinterpret_cast<ClientTargetFn>(context->exeBase + RvaClientTarget);
	g_clientMissile = reinterpret_cast<ClientMissileFn>(context->exeBase + RvaClientMissile);
	g_clientTargetXY = reinterpret_cast<ClientTargetXYFn>(context->exeBase + RvaClientTargetXY);
	for (const ClientSlot& f : ClientFunctions) {
		const uint64_t rva = CltDoTable + static_cast<uint64_t>(f.slot) * 8;
		const bool ok = context->PatchWriteU64(rva, Empty, sizeof(Empty), reinterpret_cast<uint64_t>(f.function));
		installed += ok ? 1 : 0;
		if (used < reportSize) {
			const int n = std::snprintf(report + used, reportSize - used, "%s cltdofunc %d %s%s", used ? ";" : "",
			                            f.slot, f.name, ok ? "" : " FAILED");
			used += n > 0 ? static_cast<size_t>(n) : 0;
		}
	}
	// Curse client function: wrapped in place (the slot holds D2R's own function).
	const uint64_t curseRva = CltDoTable + static_cast<uint64_t>(CltDoCurseSlot) * 8;
	const uint64_t curse = *reinterpret_cast<const volatile uint64_t*>(context->exeBase + curseRva);
	g_cltDoCurse = reinterpret_cast<CltDoFn>(context->exeBase + RvaCltDoCurse);
	{
		struct Wrap { int slot; uint64_t rva; SrvDoFn* orig; SrvDoFn replacement; const char* name; };
		const Wrap wraps[] = {{SrvDoFanSlot, RvaSrvDoFan, &g_srvDoFan, &SrvDoFan, "41 with the melee hit"},
		                      {SrvDoHammerSlot, RvaSrvDoHammer, &g_srvDoHammer, &SrvDoHammer, "73 missile by calc2"}};
		for (const Wrap& w : wraps) {
			const uint64_t at = d2r::SrvDoTable + static_cast<uint64_t>(w.slot) * 8;
			const uint64_t have = *reinterpret_cast<const volatile uint64_t*>(context->exeBase + at);
			*w.orig = reinterpret_cast<SrvDoFn>(context->exeBase + w.rva);
			const bool ok = have == context->exeBase + w.rva &&
			                context->PatchWriteU64(at, &have, sizeof(have), reinterpret_cast<uint64_t>(w.replacement));
			installed += ok ? 1 : 0;
			if (used < reportSize) {
				const int n = std::snprintf(report + used, reportSize - used, "%s srvdofunc %s%s", used ? ";" : "", w.name,
				                            ok ? "" : " FAILED");
				used += n > 0 ? static_cast<size_t>(n) : 0;
			}
		}
	}
	const bool curseOk = curse == context->exeBase + RvaCltDoCurse &&
	                     context->PatchWriteU64(curseRva, &curse, sizeof(curse), reinterpret_cast<uint64_t>(&CltDoCurse));
	installed += curseOk ? 1 : 0;
	if (used < reportSize) {
		const int n = std::snprintf(report + used, reportSize - used, "%s cltdofunc %d curse without client missile%s",
		                            used ? ";" : "", CltDoCurseSlot, curseOk ? "" : " FAILED");
		used += n > 0 ? static_cast<size_t>(n) : 0;
	}
	const bool areaOk = context->InstallInlineHook(RvaAreaAura, AreaAuraEntry, sizeof(AreaAuraEntry),
	                                               reinterpret_cast<void*>(&AreaAuraHook), reinterpret_cast<void**>(&g_areaAura));
	installed += areaOk ? 1 : 0;
	if (used < reportSize) {
		const int n = std::snprintf(report + used, reportSize - used, "%s area aura without radius%s", used ? ";" : "",
		                            areaOk ? "" : " FAILED");
		used += n > 0 ? static_cast<size_t>(n) : 0;
	}
	return installed;
}

auto SigmaSkillCounters(long* fanCasts, long* fanMeleeHits, long* hammerCasts, long* hammerSwaps) noexcept -> void {
	*fanCasts = g_fanCasts;
	*fanMeleeHits = g_fanMeleeHits;
	*hammerCasts = g_hammerCasts;
	*hammerSwaps = g_hammerSwaps;
}

auto SigmaSkillCount() noexcept -> int {
	return static_cast<int>(sizeof(Functions) / sizeof(Functions[0]) + sizeof(ClientFunctions) / sizeof(ClientFunctions[0]) + 4);
}

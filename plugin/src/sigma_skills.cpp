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
	return installed;
}

auto SigmaSkillCount() noexcept -> int {
	return static_cast<int>(sizeof(Functions) / sizeof(Functions[0]));
}

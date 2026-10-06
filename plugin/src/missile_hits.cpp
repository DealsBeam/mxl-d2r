// D2Sigma's own missile hit functions (D2MslHitFuncs.cpp; server table 0x101dc7c8). Its added ones (61-77) and the
// three whose numbers D2R 3.x reused (34, 41, 42) are moved by txt_migrate (SRV_HIT_MOVED) into D2R's empty server
// hit slots 61-70 (table 0x2391030, bound 71 at 0x2391404; dispatcher 0x464ac0 calls (game, missile, target unit
// or null), and the plugin fills the ones ported here. 69 runs D2R's own hit 1 (data only). Not yet ported: 34, 41,
// 42, 62, 71, 73 (their slots stay empty: the missile hits without the extra effect).
#include "missile_hits.h"

#include "d2r.h"

#include <cstdint>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr uint64_t SrvHitTable = 0x2391030;
constexpr uint64_t RvaMissileOwner = 0x490300;    // (game, missile) -> owner unit
constexpr uint64_t RvaUnitClass = 0x349860;       // (unit, file, line) -> class id (missile row)
constexpr uint64_t RvaMissileRecord = 0x166040;   // (dataFlag, missile) -> missiles.txt record
constexpr uint64_t RvaMissileSkill = 0x3bc450;    // (missile) -> skill
constexpr uint64_t RvaMissileLevel = 0x3bb470;    // (missile) -> skill level
constexpr uint64_t RvaEventSkill = 0x589930;      // (game, caster, skill, level, target, x, y, 4 x out, 0)

constexpr int MsHitPar1 = 0x70;
constexpr int MsHitPar2 = 0x74;
constexpr int MsSkill = 0x1b6;                    // int16
constexpr int MsHitSubMissile1 = 0x46;            // int16
constexpr int SkCalc3 = 0x198;                    // skills calc1-4 at +0x190

using MissileOwnerFn = Unit* (*)(Game* game, Unit* missile);
using UnitClassFn = int32_t (*)(Unit* unit, const char* file, int32_t line);
using MissileRecordFn = const uint8_t* (*)(uint32_t dataFlag, int32_t missile);
using MissileIntFn = int32_t (*)(Unit* missile);
using EventSkillFn = uint64_t (*)(Game* game, Unit* caster, int32_t skill, int32_t level, Unit* target, int32_t x,
                                  int32_t y, int32_t* outType, int32_t* outId, int32_t* outX, int32_t* outY,
                                  int32_t flag);
using SrvDoFn = int64_t (*)(Game* game, Unit* unit, int32_t skill, int32_t level);

MissileOwnerFn g_owner = nullptr;
UnitClassFn g_unitClass = nullptr;
MissileRecordFn g_missileRecord = nullptr;
MissileIntFn g_missileSkill = nullptr;
MissileIntFn g_missileLevel = nullptr;
EventSkillFn g_eventSkill = nullptr;
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

using HitFn = int64_t (*)(Game* game, Unit* missile, Unit* target);
struct HitSlot {
	int slot;
	HitFn function;
	const char* name;
};
constexpr HitSlot Hits[] = {
	{61, HitRunSkillFunction, "run skill function (D2Sigma hit 61)"},
	{64, HitCastSkill, "cast skill (D2Sigma hit 72)"},
	{66, HitNova, "nova (D2Sigma hit 77)"},
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
	return installed;
}

auto MissileHitCount() noexcept -> int {
	return static_cast<int>(sizeof(Hits) / sizeof(Hits[0]));
}

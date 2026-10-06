// D2Sigma's minion scaling (D2Hirelings.cpp 0x100683d0), run on every pet of srvdofunc 153 (multi-summon, slot 172)
// after D2R's pet level setup (0x55fb50: level = the owner's level, monlvl armour and attack rating) and before the
// skill's passive/aura stats (0x55ef80, D2Sigma 0x10067940). Steps, in D2Sigma's order (docs/plugin-spec.md):
//   attack rating (0x10068120, "mnar" 0x10067bd0): set to (level - 1) * Param5, plus that times the owner's
//      "to summon attack rating" % (Median stat 500)
//   life (0x10068190, "mnhp" 0x10067c60): max life and life set to
//      (Param3 + (level - 1) * Param4) * (pet level / 2 + 100) * (calc1 + 100) / 3333, in D2's 1/256 units
//   resistances (0x10068250): + the owner's "to summon elemental resist" (Median 487) to fire/cold/lightning (at most
//      105) and poison (at most 80); + its Median stat 229 to damage resist (at most 50)
//   pierce (0x10067db0): the owner's four pierce stats, when not 0
//   attributes (0x10068020, pettypeext byte 0): the owner's energy, strength, Median 485, 278 and 86 in full, its four
//      elemental masteries and magic mastery halved
// Not ported: the stat list recording the summoning skill/level (0x10067b30, stats 350/351 under state 307), the
// owner's skill-on-event stats (0x10067e20, pettypeext byte 2), the summon unique mod (0x100681e0: sumumod is empty on
// every 153 skill).
#include "pet_scaling.h"

#include "d2r.h"
#include "mxl_pets.h"

#include <cstdint>
#include <iterator>

namespace {

constexpr uint64_t GetUnitStatRva = 0x2f5020;  // (unit, stat, layer) -> total
constexpr uint64_t SetUnitStatRva = 0x2f7d10;  // (unit, stat, value, layer): as D2R's pet level setup sets level
constexpr uint64_t AddUnitStatRva = 0x2f34f0;  // (unit, stat, value, layer): as D2R's potions add theirs

constexpr int SkParam3 = 0x1c0;
constexpr int SkParam4 = 0x1c4;
constexpr int SkParam5 = 0x1c8;
constexpr int32_t StatHitpoints = 6;
constexpr int32_t StatMaxHp = 7;
constexpr int32_t StatLevel = 12;
constexpr int32_t StatToHit = 19;
constexpr int32_t StatDamageResist = 36;
constexpr int32_t StatFireResist = 39;
constexpr int32_t StatLightResist = 41;
constexpr int32_t StatColdResist = 43;
constexpr int32_t StatPoisonResist = 45;

using GetUnitStatFn = int32_t (*)(d2r::Unit* unit, int32_t stat, int32_t layer);
using ChangeUnitStatFn = void (*)(d2r::Unit* unit, int32_t stat, int32_t value, int32_t layer);

GetUnitStatFn g_getStat = nullptr;
ChangeUnitStatFn g_setStat = nullptr;
ChangeUnitStatFn g_addStat = nullptr;

auto Stat(d2r::Unit* unit, int32_t stat) noexcept -> int32_t {
	return g_getStat(unit, stat, 0);
}

void AddIfSet(d2r::Unit* pet, int32_t stat, int32_t value) noexcept {
	if (value != 0) {
		g_addStat(pet, stat, value, 0);
	}
}

// D2Sigma 0x10067bd0 (calc code mnar)
auto MinionAttackRating(d2r::Unit* owner, const uint8_t* skill, int32_t level) noexcept -> int32_t {
	int32_t value = (level - 1) * d2r::At<int32_t>(skill, SkParam5);
	if (value == 0) {
		return 0;
	}
	if (const int32_t bonus = Stat(owner, mxl_pets::StatSummonAttackRating); bonus != 0) {
		value += static_cast<int32_t>(static_cast<int64_t>(value) * bonus / 100);
	}
	return value;
}

// D2Sigma 0x10067c60 (calc code mnhp): unsigned 64-bit arithmetic, clamped to INT32_MAX after the << 8
auto MinionLife(uint32_t dataFlag, d2r::Unit* owner, int32_t petLevel, const uint8_t* skill, int32_t skillId,
                int32_t level) noexcept -> int32_t {
	const int64_t base = d2r::At<int32_t>(skill, SkParam3) +
	                     static_cast<int64_t>(level - 1) * d2r::At<int32_t>(skill, SkParam4);
	const int64_t calc1 = d2r::api.evalSkillCalc(dataFlag, owner, d2r::At<int32_t>(skill, d2r::SkCalc1), skillId, level);
	const uint64_t product = ((static_cast<uint64_t>(static_cast<int64_t>(petLevel)) >> 1) + 100) * static_cast<uint64_t>(calc1 + 100) * static_cast<uint64_t>(base);
	const uint64_t life = product / 3333;
	return life > (0x7fffffffULL >> 8) ? 0x7fffffff : static_cast<int32_t>(life << 8);
}

}  // namespace

void ScalePet(d2r::Game* game, d2r::Unit* owner, d2r::Unit* pet, int32_t skillId, int32_t level) noexcept {
	if (g_getStat == nullptr || owner == nullptr || pet == nullptr || level < 1) {
		return;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr) {
		return;
	}
	g_setStat(pet, StatToHit, MinionAttackRating(owner, skill, level), 0);
	const int32_t life = MinionLife(d2r::DataFlag(game), owner, Stat(pet, StatLevel), skill, skillId, level);
	g_setStat(pet, StatMaxHp, life, 0);
	g_setStat(pet, StatHitpoints, life, 0);

	const int32_t resist = Stat(owner, mxl_pets::StatSummonElementalResist);
	const int32_t elemental = resist > 105 ? 105 : resist;
	g_addStat(pet, StatFireResist, elemental, 0);
	g_addStat(pet, StatColdResist, elemental, 0);
	g_addStat(pet, StatLightResist, elemental, 0);
	g_addStat(pet, StatPoisonResist, resist > 80 ? 80 : resist, 0);
	const int32_t damageResist = Stat(owner, mxl_pets::StatSummonDamageResist);
	g_addStat(pet, StatDamageResist, damageResist > 50 ? 50 : damageResist, 0);

	for (const int32_t stat : mxl_pets::PierceStats) {
		AddIfSet(pet, stat, Stat(owner, stat));
	}
	const uint8_t petType = d2r::At<uint8_t>(skill, d2r::SkPetType);
	if (petType < std::size(mxl_pets::PetTypes) && mxl_pets::PetTypes[petType].copyAttributes != 0) {
		for (const int32_t stat : mxl_pets::AttributeStats) {
			AddIfSet(pet, stat, Stat(owner, stat));
		}
		for (const int32_t stat : mxl_pets::HalfStats) {
			AddIfSet(pet, stat, Stat(owner, stat) / 2);
		}
	}
}

// Calc codes mnhp / mnar (D2Sigma 0x100a6340 calls the same functions without a pet: the owner's level).
auto MinionLifeCode(uint32_t dataFlag, d2r::Unit* owner, int32_t skillId, int32_t level) noexcept -> int32_t {
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (g_getStat == nullptr || owner == nullptr || skill == nullptr) {
		return 0;
	}
	return MinionLife(dataFlag, owner, Stat(owner, StatLevel), skill, skillId, level);
}

auto MinionAttackRatingCode(uint32_t dataFlag, d2r::Unit* owner, int32_t skillId, int32_t level) noexcept -> int32_t {
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (g_getStat == nullptr || owner == nullptr || skill == nullptr) {
		return 0;
	}
	return MinionAttackRating(owner, skill, level);
}

void InstallPetScaling(uint64_t exeBase) noexcept {
	g_getStat = reinterpret_cast<GetUnitStatFn>(exeBase + GetUnitStatRva);
	g_setStat = reinterpret_cast<ChangeUnitStatFn>(exeBase + SetUnitStatRva);
	g_addStat = reinterpret_cast<ChangeUnitStatFn>(exeBase + AddUnitStatRva);
}

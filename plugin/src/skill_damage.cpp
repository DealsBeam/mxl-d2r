// Median XL's skill damage scaling (docs/plugin-spec.md, "Skill damage scaling"; arithmetic in skill_damage_logic.h).
//
// D2Sigma replaces the four D2Common skill damage functions. D2R's four equivalents (dataFlag, unit, skill,
// level, mastery flag) are replaced the same way, reading the record fields they read (skills.txt record offsets =
// the 1.13c ones + 0xa8 behind the damage columns):
//   0x3387d0 elemental min   base +0x288, steps +0x290
//   0x338340 elemental max   base +0x28c, steps +0x2a4
//   0x3385b0 physical min    base +0x250, steps +0x258
//   0x338160 physical max    base +0x254, steps +0x26c
//   shift +0x24c, hit shift +0x24d, damage type +0x284, synergy percent calc +0x280 (physical) / +0x2b8 (elemental)
// Differences from D2R's versions: the BaseSynergy percent, no mastery for StaticDamage skills, physical damage
// takes stat 357 mastery, the synergy calc is always evaluated (D2R skipped it for small damage without steps), and
// the percent products are 64-bit (D2R split them by value range).
// D2R's own code answers the cases D2Sigma leaves alone: no unit, no record, and skills flagged passive.
#include "skill_damage.h"

#include "mxl_skill_flags.h"
#include "skill_damage_logic.h"

#include <cstdint>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Unit;

constexpr uint64_t EleMinRva = 0x3387d0;
constexpr uint64_t EleMaxRva = 0x338340;
constexpr uint64_t PhysMinRva = 0x3385b0;
constexpr uint64_t PhysMaxRva = 0x338160;
// PUSH RBP / PUSH RSI / PUSH RDI / PUSH R14 / PUSH R15 / SUB RSP,0x30
constexpr uint8_t EleMinEntry[] = {0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x30};
// MOV [RSP+0x18],RBP / MOV [RSP+0x20],RSI / PUSH RDI / PUSH R14 / PUSH R15 / SUB RSP,0x30
constexpr uint8_t EleMaxEntry[] = {0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20,
                                   0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x30};
// MOV [RSP+0x10],RBP / MOV [RSP+0x18],RSI / PUSH RDI / PUSH R14 / PUSH R15 / SUB RSP,0x30
constexpr uint8_t PhysEntry[] = {0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
                                 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x30};

constexpr int SkShift = 0x24c;     // uint8
constexpr int SkHitShift = 0x24d;  // uint8: percent of the weapon's damage, /128
constexpr int SkPassive = 0x25;    // uint8: flags tested against the byte at PassiveMaskRva
constexpr int SkEType = 0x284;     // uint8: elemental damage type
constexpr int SkEleCalc = 0x2b8;   // int32 calc row, -1 none
constexpr int SkPhysCalc = 0x280;  // int32 calc row, -1 none
constexpr uint64_t PassiveMaskRva = 0x1d996d4;
constexpr uint64_t SkillsFileRva = 0x1cff7c0;  // "D2Common\src\Skills\Skills.cpp", the assert text the weapon query takes

constexpr uint64_t GetStatRva = 0x2f5020;      // (unit, stat, layer)
constexpr uint64_t MasteryRva = 0x337f40;      // (unit, damage type) -> the mastery stat of the type (0x149 .. 0x165)
constexpr uint64_t WeaponRva = 0x34b000;       // (unit) -> the weapon in the hand, or null
constexpr uint64_t WeaponInfoRva = 0x34a360;   // (unit, file, line) -> argument of the hand query
constexpr uint64_t HandRva = 0x388910;         // (unit, info, 0) -> 2 when the weapon's secondary damage applies
constexpr uint64_t ItemStatMaxRva = 0x2f5320;  // (item, 0x18)
constexpr uint64_t ItemStatMinRva = 0x2f54e0;  // (item, 1)

constexpr int StatEnergy = 1;
constexpr int StatLevel = 0xc;
constexpr int StatMagicMastery = 0x165;

using DamageFn = int32_t (*)(uint32_t dataFlag, Unit* unit, int32_t skill, int32_t level, int32_t mastery);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using MasteryFn = int32_t (*)(Unit* unit, int32_t type);
using WeaponFn = void* (*)(Unit* unit);
using WeaponInfoFn = void* (*)(Unit* unit, const char* file, int32_t line);
using HandFn = int32_t (*)(Unit* unit, void* info, int32_t zero);
using ItemStatFn = int32_t (*)(void* item, int32_t index);

DamageFn g_eleMin = nullptr;
DamageFn g_eleMax = nullptr;
DamageFn g_physMin = nullptr;
DamageFn g_physMax = nullptr;
GetStatFn g_getStat = nullptr;
MasteryFn g_mastery = nullptr;
WeaponFn g_weapon = nullptr;
WeaponInfoFn g_weaponInfo = nullptr;
HandFn g_hand = nullptr;
ItemStatFn g_itemStatMax = nullptr;
ItemStatFn g_itemStatMin = nullptr;
const uint8_t* g_passiveMask = nullptr;
const char* g_skillsFile = nullptr;

auto FlagsOf(int32_t skill) noexcept -> int32_t {
	int lo = 0;
	int hi = mxl_skill_flags::FlagCount - 1;
	while (lo <= hi) {
		const int mid = (lo + hi) / 2;
		const int32_t id = mxl_skill_flags::Flags[mid].skill;
		if (id == skill) {
			return mxl_skill_flags::Flags[mid].flags;
		}
		if (id < skill) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return 0;
}

auto CalcPercent(uint32_t dataFlag, Unit* unit, int32_t skill, int32_t level, int32_t calc) noexcept -> int32_t {
	return calc != -1 ? d2r::api.evalSkillCalc(dataFlag & 0xff, unit, calc, skill, level) : 0;
}

auto BaseSynergy(Unit* unit, int32_t flags) noexcept -> int32_t {
	if (!(flags & mxl_skill_flags::BaseSynergy)) {
		return 0;
	}
	return skill_damage::BaseSynergyPercent(g_getStat(unit, StatLevel, 0), g_getStat(unit, StatEnergy, 0),
	                                        g_getStat(unit, mxl_skill_flags::StatBaseSynergy, 0));
}

auto Elemental(DamageFn original, bool isMax, uint32_t dataFlag, Unit* unit, int32_t skill, int32_t level,
               int32_t mastery) noexcept -> int32_t {
	const uint8_t* rec = unit != nullptr && level > 0 ? d2r::api.getSkillRecord(dataFlag & 0xff, skill) : nullptr;
	if (rec == nullptr) {
		return original(dataFlag, unit, skill, level, mastery);
	}
	const int32_t flags = FlagsOf(skill);
	const int32_t percent = CalcPercent(dataFlag, unit, skill, level, At<int32_t>(rec, SkEleCalc)) + BaseSynergy(unit, flags);
	const int32_t masteryStat =
		mastery != 0 && !(flags & mxl_skill_flags::StaticDamage) ? g_mastery(unit, At<uint8_t>(rec, SkEType)) : 0;
	return skill_damage::Elemental(At<int32_t>(rec, isMax ? 0x28c : 0x288),
	                               reinterpret_cast<const int32_t*>(rec + (isMax ? 0x2a4 : 0x290)), level,
	                               At<uint8_t>(rec, SkShift), percent, masteryStat);
}

// The damage of the weapon in the hand, as D2R reads it (secondary damage when the hand query answers 2).
auto WeaponDamage(Unit* unit, bool isMax) noexcept -> int32_t {
	void* item = g_weapon(unit);
	if (item != nullptr) {
		void* info = g_weaponInfo(unit, g_skillsFile, isMax ? 0xcbc : 0xc82);
		if (g_hand(unit, info, 0) == 2) {
			return isMax ? g_itemStatMax(item, 0x18) : g_itemStatMin(item, 1);
		}
	}
	return g_getStat(unit, isMax ? 0x16 : 0x15, 0);
}

auto Physical(DamageFn original, bool isMax, uint32_t dataFlag, Unit* unit, int32_t skill, int32_t level,
              int32_t mastery) noexcept -> int32_t {
	const uint8_t* rec = unit != nullptr && level > 0 ? d2r::api.getSkillRecord(dataFlag & 0xff, skill) : nullptr;
	if (rec == nullptr || (At<uint8_t>(rec, SkPassive) & *g_passiveMask) != 0) {
		return original(dataFlag, unit, skill, level, mastery);
	}
	const int32_t flags = FlagsOf(skill);
	int32_t weapon = 0;
	if (At<uint8_t>(rec, SkHitShift) != 0 && mastery != 0) {
		weapon = skill_damage::WeaponPart(At<uint8_t>(rec, SkHitShift), WeaponDamage(unit, isMax));
	}
	const int32_t percent = CalcPercent(dataFlag, unit, skill, level, At<int32_t>(rec, SkPhysCalc)) + BaseSynergy(unit, flags);
	const int32_t masteryStat = !(flags & mxl_skill_flags::StaticDamage) ? g_mastery(unit, 3) : 0;
	return skill_damage::Physical(weapon, At<int32_t>(rec, isMax ? 0x254 : 0x250),
	                              reinterpret_cast<const int32_t*>(rec + (isMax ? 0x26c : 0x258)), level,
	                              At<uint8_t>(rec, SkShift), percent, masteryStat);
}

auto EleMinHook(uint32_t dataFlag, Unit* unit, int32_t skill, int32_t level, int32_t mastery) noexcept -> int32_t {
	return Elemental(g_eleMin, false, dataFlag, unit, skill, level, mastery);
}
auto EleMaxHook(uint32_t dataFlag, Unit* unit, int32_t skill, int32_t level, int32_t mastery) noexcept -> int32_t {
	return Elemental(g_eleMax, true, dataFlag, unit, skill, level, mastery);
}
auto PhysMinHook(uint32_t dataFlag, Unit* unit, int32_t skill, int32_t level, int32_t mastery) noexcept -> int32_t {
	return Physical(g_physMin, false, dataFlag, unit, skill, level, mastery);
}
auto PhysMaxHook(uint32_t dataFlag, Unit* unit, int32_t skill, int32_t level, int32_t mastery) noexcept -> int32_t {
	return Physical(g_physMax, true, dataFlag, unit, skill, level, mastery);
}

}  // namespace

auto InstallSkillDamage(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	g_mastery = reinterpret_cast<MasteryFn>(base + MasteryRva);
	g_weapon = reinterpret_cast<WeaponFn>(base + WeaponRva);
	g_weaponInfo = reinterpret_cast<WeaponInfoFn>(base + WeaponInfoRva);
	g_hand = reinterpret_cast<HandFn>(base + HandRva);
	g_itemStatMax = reinterpret_cast<ItemStatFn>(base + ItemStatMaxRva);
	g_itemStatMin = reinterpret_cast<ItemStatFn>(base + ItemStatMinRva);
	g_passiveMask = reinterpret_cast<const uint8_t*>(base + PassiveMaskRva);
	g_skillsFile = reinterpret_cast<const char*>(base + SkillsFileRva);
	int installed = 0;
	const auto hook = [&](uint64_t rva, const uint8_t* entry, uint32_t size, DamageFn replacement, DamageFn* original) {
		if (context->InstallInlineHook(rva, entry, size, reinterpret_cast<void*>(replacement),
		                               reinterpret_cast<void**>(original))) {
			++installed;
		}
	};
	hook(EleMinRva, EleMinEntry, sizeof(EleMinEntry), EleMinHook, &g_eleMin);
	hook(EleMaxRva, EleMaxEntry, sizeof(EleMaxEntry), EleMaxHook, &g_eleMax);
	hook(PhysMinRva, PhysEntry, sizeof(PhysEntry), PhysMinHook, &g_physMin);
	hook(PhysMaxRva, PhysEntry, sizeof(PhysEntry), PhysMaxHook, &g_physMax);
	int baseSynergy = 0;
	int staticDamage = 0;
	for (const auto& f : mxl_skill_flags::Flags) {
		baseSynergy += (f.flags & mxl_skill_flags::BaseSynergy) ? 1 : 0;
		staticDamage += (f.flags & mxl_skill_flags::StaticDamage) ? 1 : 0;
	}
	if (installed == 4) {
		std::snprintf(report, reportSize, "on: elemental + physical min/max, %d BaseSynergy and %d StaticDamage skills",
		              baseSynergy, staticDamage);
	} else {
		std::snprintf(report, reportSize, "off: %d of 4 damage hooks failed", 4 - installed);
	}
	return installed == 4;
}

auto SampleSkillDamage(Unit* unit, uint32_t dataFlag, int32_t skill, int32_t level, int32_t withMastery,
                       SkillDamageSample* out) noexcept -> bool {
	const uint8_t* rec = unit != nullptr ? d2r::api.getSkillRecord(dataFlag & 0xff, skill) : nullptr;
	if (rec == nullptr || g_eleMin == nullptr || g_physMax == nullptr) {
		return false;
	}
	*out = {};
	out->flags = FlagsOf(skill);
	out->calcElem = CalcPercent(dataFlag, unit, skill, level, At<int32_t>(rec, SkEleCalc));
	out->calcPhys = CalcPercent(dataFlag, unit, skill, level, At<int32_t>(rec, SkPhysCalc));
	out->baseSynergy = BaseSynergy(unit, out->flags);
	out->masteryElem = g_mastery(unit, At<uint8_t>(rec, SkEType));
	out->masteryMagic = g_getStat(unit, StatMagicMastery, 0);
	out->eleMin = EleMinHook(dataFlag, unit, skill, level, withMastery);
	out->eleMax = EleMaxHook(dataFlag, unit, skill, level, withMastery);
	out->physMin = PhysMinHook(dataFlag, unit, skill, level, withMastery);
	out->physMax = PhysMaxHook(dataFlag, unit, skill, level, withMastery);
	out->origEleMin = g_eleMin(dataFlag, unit, skill, level, withMastery);
	out->origEleMax = g_eleMax(dataFlag, unit, skill, level, withMastery);
	out->origPhysMin = g_physMin(dataFlag, unit, skill, level, withMastery);
	out->origPhysMax = g_physMax(dataFlag, unit, skill, level, withMastery);
	return true;
}

// D2Sigma's area damage (srvdofunc 171, D2SkillDo.cpp 0x100acee0) -> slot 190. Median uses it for queen_of_blades: a
// periodic skill (srvstfunc 28 puts the buff state on; the periodic event calls this every perdelay frames) that hits
// every unit in aurarangecalc chosen by aurafilter.
//
// D2Sigma builds one damage block (hit flags 2 | HitFlags, HitClass, the skill's elemental damage) and, per unit
// found (callback 0x100bb740), adds the caster's weapon damage scaled by SrcDam (D2Game 0x6fcfd450) and applies it.
// Ported with D2R's own pieces, in the way its Holy Fire/Holy Shock aura (srvdofunc 66, 0x5630e0) and its callback
// (0x561280) do it:
//   0x437d00 (unit, &damage, skill, level)   the skill's elemental damage (EType/EMin/EMax, synergies)
//   0x4327d0 (game, unit, 0, 0, range, filter, callback, context, 0, file, line)   unit finder
//   0x4494b0 (&copy, &damage)                copy of the damage block for one target
//   0x44c030 (game, attacker, target, &damage, 0, srcDam)   weapon damage (srcDam / 128; 0 counts as 128)
//   0x44ce80 (game, attacker, target, 1, &damage), 0x44a9b0 (game, attacker, target, &damage)   apply the hit
// Not ported: D2Sigma's hit roll when ResultFlags bit 0 is clear (0x10057690; every unit found is hit, as with D2R's
// damage auras), its physical damage helpers (0x100a6020/0x100a5ec0: Median's MinDam/MaxDam, not set on
// queen_of_blades) and 0x10056d20.
#include "area_damage.h"

#include "d2r.h"
#include "mxl_damage.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr uint64_t SkillDamageRva = 0x437d00;
constexpr uint64_t FindUnitsRva = 0x4327d0;
constexpr uint64_t CopyDamageRva = 0x4494b0;
constexpr uint64_t WeaponDamageRva = 0x44c030;
constexpr uint64_t HitUnitRva = 0x44ce80;
constexpr uint64_t ApplyDamageRva = 0x44a9b0;
constexpr uint64_t AllocatorRva = 0x12063c0;  // -> allocator object; vtable +0x20 frees

constexpr int SkAuraFilter = 0x70;
constexpr int SkAuraRangeCalc = 0x84;
constexpr int SkHitFlags = 0x188;
constexpr int SkHitClass = 0x18c;

// D2R's damage block (0x180 bytes; layout from srvdofunc 66's setup and the copy 0x4494b0)
constexpr size_t DamageSize = 0x180;
constexpr int DmgHitFlags = 0x00;      // uint32
constexpr int DmgResultFlags = 0x04;   // uint16
constexpr int DmgListData = 0x40;      // small list: data pointer, count, capacity (bit 63 = inline storage)
constexpr int DmgListCount = 0x48;
constexpr int DmgListCapacity = 0x50;
constexpr int DmgListInline = 0x58;    // 16 x 12 bytes
constexpr int DmgHitClass = 0x144;
constexpr int DmgCallback = 0x158;     // tagged pointer, 1 = empty

using SkillDamageFn = uint64_t (*)(d2r::Unit* unit, void* damage, int32_t skill, int32_t level);
using FinderCallback = int32_t (*)(void** finder, d2r::Unit* target);
using FindUnitsFn = void (*)(d2r::Game* game, d2r::Unit* unit, int32_t x, int32_t y, int32_t range, int32_t filter,
                             FinderCallback callback, void* context, int32_t flag, const char* file, int32_t line);
using CopyDamageFn = void* (*)(void* to, const void* from);
using WeaponDamageFn = void (*)(d2r::Game* game, d2r::Unit* attacker, d2r::Unit* target, void* damage, int32_t zero,
                                uint8_t srcDam);
using HitUnitFn = void (*)(d2r::Game* game, d2r::Unit* attacker, d2r::Unit* target, int32_t one, void* damage);
using ApplyDamageFn = void (*)(d2r::Game* game, d2r::Unit* attacker, d2r::Unit* target, void* damage);
using AllocatorFn = void** (*)();
using FreeFn = void (*)(void* allocator, void* block);
using DestroyFn = void (*)(void* object, int32_t flags);

SkillDamageFn g_skillDamage = nullptr;
FindUnitsFn g_findUnits = nullptr;
CopyDamageFn g_copyDamage = nullptr;
WeaponDamageFn g_weaponDamage = nullptr;
HitUnitFn g_hitUnit = nullptr;
ApplyDamageFn g_applyDamage = nullptr;
AllocatorFn g_allocator = nullptr;

struct alignas(16) Damage {
	uint8_t bytes[DamageSize];
};

template <typename T>
auto Field(Damage& damage, int offset) noexcept -> T& {
	return *reinterpret_cast<T*>(damage.bytes + offset);
}

struct Context {
	Damage* damage;
	uint8_t srcDam;
};

void InitDamage(Damage& damage) noexcept {
	std::memset(damage.bytes, 0, sizeof(damage.bytes));
	Field<uint8_t*>(damage, DmgListData) = damage.bytes + DmgListInline;
	Field<uint64_t>(damage, DmgListCapacity) = 0x8000000000000010ULL;
	Field<uint64_t>(damage, DmgCallback) = 1;
}

void FreeBlock(void* block) noexcept {
	void** allocator = g_allocator();
	reinterpret_cast<FreeFn>(reinterpret_cast<void**>(*allocator)[4])(allocator, block);
}

// what D2R inlines at the end of every function holding a damage block (e.g. 0x5630e0)
void DestroyDamage(Damage& damage) noexcept {
	uint64_t& callback = Field<uint64_t>(damage, DmgCallback);
	void* object = (callback & 1) == 0 ? static_cast<void*>(&callback) : reinterpret_cast<void*>(callback & ~1ULL);
	if (object != nullptr) {
		reinterpret_cast<DestroyFn>((*static_cast<void***>(object))[0])(object, 0);
		if ((callback & 1) != 0) {
			FreeBlock((callback & 1) == 0 ? static_cast<void*>(&callback) : reinterpret_cast<void*>(callback & ~1ULL));
		}
		callback = 1;
	}
	if (static_cast<int64_t>(Field<uint64_t>(damage, DmgListCapacity)) >= 0) {
		FreeBlock(Field<void*>(damage, DmgListData));
	}
}

// finder context: game, caster, count, our context (D2R 0x4398b0)
auto HitTarget(void** finder, d2r::Unit* target) noexcept -> int32_t {
	auto* game = static_cast<d2r::Game*>(finder[0]);
	auto* caster = static_cast<d2r::Unit*>(finder[1]);
	const auto* context = static_cast<const Context*>(finder[3]);
	if (game == nullptr || caster == nullptr || target == nullptr || context == nullptr) {
		return 0;
	}
	Damage copy;
	g_copyDamage(copy.bytes, context->damage->bytes);
	if (context->srcDam != 0) {
		g_weaponDamage(game, caster, target, copy.bytes, 0, context->srcDam);
	}
	g_hitUnit(game, caster, target, 1, copy.bytes);
	g_applyDamage(game, caster, target, copy.bytes);
	DestroyDamage(copy);
	return 1;
}

auto SrcDamOf(int32_t skillId) noexcept -> int32_t {
	for (const auto& entry : mxl_damage::Skills) {
		if (entry.skill == skillId) {
			return entry.srcDam;
		}
	}
	return 0;
}

}  // namespace

auto SrvDoAreaDamage(d2r::Game* game, d2r::Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr || g_findUnits == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t range = d2r::api.evalSkillCalc(dataFlag, unit, d2r::At<int32_t>(skill, SkAuraRangeCalc), skillId, level);
	Damage damage;
	InitDamage(damage);
	g_skillDamage(unit, damage.bytes, skillId, level);
	Field<uint32_t>(damage, DmgHitFlags) |= 2 | d2r::At<uint32_t>(skill, SkHitFlags);
	Field<uint16_t>(damage, DmgResultFlags) |= 1;
	if (const int32_t hitClass = d2r::At<int32_t>(skill, SkHitClass); hitClass != 0) {
		Field<int32_t>(damage, DmgHitClass) = hitClass;
	}
	const int32_t srcDam = SrcDamOf(skillId);
	Context context{&damage, static_cast<uint8_t>(srcDam > 255 ? 255 : srcDam)};
	if (range > 0) {
		g_findUnits(game, unit, 0, 0, range, d2r::At<int32_t>(skill, SkAuraFilter), HitTarget, &context, 0,
		            "mxl-core area_damage.cpp", __LINE__);
	}
	DestroyDamage(damage);
	return 1;
}

void InstallAreaDamage(uint64_t exeBase) noexcept {
	g_skillDamage = reinterpret_cast<SkillDamageFn>(exeBase + SkillDamageRva);
	g_findUnits = reinterpret_cast<FindUnitsFn>(exeBase + FindUnitsRva);
	g_copyDamage = reinterpret_cast<CopyDamageFn>(exeBase + CopyDamageRva);
	g_weaponDamage = reinterpret_cast<WeaponDamageFn>(exeBase + WeaponDamageRva);
	g_hitUnit = reinterpret_cast<HitUnitFn>(exeBase + HitUnitRva);
	g_applyDamage = reinterpret_cast<ApplyDamageFn>(exeBase + ApplyDamageRva);
	g_allocator = reinterpret_cast<AllocatorFn>(exeBase + AllocatorRva);
}

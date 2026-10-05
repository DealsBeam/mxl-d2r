// D2R game image interface for mxl-core: RVAs, record offsets and the few game functions the plugin calls.
// All values are for one image (D2R 3.3.0 build 93847 under D2RLoader 1.3.1, PE timestamp 0x6ab3782c); they come
// from the Ghidra analysis of the dumped image (docs/plugin-spec.md, "D2R side"). Nothing here may be used unless
// mxl_core.cpp's image check passed.
#pragma once

#include <cstddef>
#include <cstdint>

namespace d2r {

struct Game;
struct Unit;

// --- Addresses (RVAs) ---------------------------------------------------------------------------------------------
inline constexpr uint64_t SrvDoTable = 0x238ea00;  // skill server "do" functions, 191 slots, indexed by srvdofunc
inline constexpr int SrvDoSlots = 191;

inline constexpr uint64_t RvaGetSkillRecord = 0x97790;    // (dataFlag, skillId) -> skills record
inline constexpr uint64_t RvaEvalSkillCalc = 0x3b5160;    // (dataFlag, unit, calc, skillId, level) -> value
inline constexpr uint64_t RvaSkillTargetXY = 0x432b70;    // (game, unit, &x, &y) -> both non-zero
inline constexpr uint64_t RvaCreateMissile = 0x5371a0;    // (game, &params) -> missile unit
inline constexpr uint64_t RvaMissileCount = 0x1b89a0;     // (dataFlag) -> rows in missiles.txt
inline constexpr uint64_t RvaUnitSeed = 0x34a1e0;         // (unit) -> &seed {lo, hi}
inline constexpr uint64_t RvaSetUnitSkillFlag = 0x34e790; // (unit, on): unit flags (+0x124) bit 0x40

// --- Offsets ------------------------------------------------------------------------------------------------------
inline constexpr int GameDataFlag = 0x106;  // byte passed to the data-table getters

inline constexpr int UnitType = 0x00;  // 0 player, 1 monster, 2 object, 3 missile, 4 item, 5 tile
inline constexpr int UnitPath = 0x38;  // dynamic path (x at +0x2, y at +0x6) or static path (x +0x10, y +0x14)

// skills.txt record (748 bytes; tools: research/bin_layout.py skills)
inline constexpr int SkSrvMissile = 0x66;  // int16 srvmissile, then srvmissilea/b/c at +0x68/+0x6a/+0x6c
inline constexpr int SkSrvMissileA = 0x68;
inline constexpr int SkPrgCalc1 = 0x58;
inline constexpr int SkAuraRangeCalc = 0x84;
inline constexpr int SkCalc1 = 0x190;
inline constexpr int SkCalc2 = 0x194;
inline constexpr int SkCalc3 = 0x198;

// Missile creation parameters. Same fields as 1.10's 0x5c-byte block, 0x10 further on after the three pointers.
struct MissileParams {
	uint32_t flags;          // 0x00
	uint32_t pad04;
	Unit* owner;             // 0x08
	Unit* origin;            // 0x10
	Unit* target;            // 0x18
	int32_t missile;         // 0x20
	int32_t x, y;            // 0x24 (0: the origin's position)
	int32_t targetX, targetY;// 0x2c
	int32_t gfxArg;          // 0x34
	int32_t velocity;        // 0x38
	int32_t skill;           // 0x3c
	int32_t skillLevel;      // 0x40
	int32_t loops;           // 0x44
	int32_t unk48;           // 0x48
	int32_t frame;           // 0x4c
	int32_t activateFrame;   // 0x50
	int32_t attBonus;        // 0x54 (1.10 +0x44)
	uint8_t rest[0xa0 - 0x58];
};
static_assert(sizeof(MissileParams) == 0xa0);
static_assert(offsetof(MissileParams, missile) == 0x20);
static_assert(offsetof(MissileParams, targetX) == 0x2c);
static_assert(offsetof(MissileParams, skill) == 0x3c);
static_assert(offsetof(MissileParams, attBonus) == 0x54);

// --- Calls --------------------------------------------------------------------------------------------------------
using GetSkillRecordFn = uint8_t* (*)(uint32_t dataFlag, int32_t skillId);
using EvalSkillCalcFn = int32_t (*)(uint32_t dataFlag, Unit* unit, int32_t calc, int32_t skillId, int32_t level);
using SkillTargetXYFn = int32_t (*)(Game* game, Unit* unit, int32_t* x, int32_t* y);
using CreateMissileFn = Unit* (*)(Game* game, MissileParams* params);
using MissileCountFn = int32_t (*)(uint32_t dataFlag);
using UnitSeedFn = uint32_t* (*)(Unit* unit);
using SetUnitSkillFlagFn = void (*)(Unit* unit, int32_t on);

// Bound by Bind() from the image base.
struct Api {
	GetSkillRecordFn getSkillRecord;
	EvalSkillCalcFn evalSkillCalc;
	SkillTargetXYFn skillTargetXY;
	CreateMissileFn createMissile;
	MissileCountFn missileCount;
	UnitSeedFn unitSeed;
	SetUnitSkillFlagFn setUnitSkillFlag;
};
extern Api api;
void Bind(uint64_t exeBase) noexcept;

// --- Helpers ------------------------------------------------------------------------------------------------------
template <typename T>
inline auto At(const void* p, int offset) noexcept -> T {
	return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(p) + offset);
}

inline auto DataFlag(Game* game) noexcept -> uint32_t {
	return At<uint8_t>(game, GameDataFlag);
}

// Position as 0x432b70 reads it.
inline auto UnitXY(Unit* unit, int32_t* x, int32_t* y) noexcept -> bool {
	const int32_t type = At<int32_t>(unit, UnitType);
	const uint8_t* path = At<const uint8_t*>(unit, UnitPath);
	if (path == nullptr) {
		return false;
	}
	if (type == 2 || type == 4 || type == 5) {
		*x = At<int32_t>(path, 0x10);
		*y = At<int32_t>(path, 0x14);
	} else {
		*x = At<uint16_t>(path, 0x2);
		*y = At<uint16_t>(path, 0x6);
	}
	return true;
}

inline auto EvalCalc(Game* game, Unit* unit, const uint8_t* skill, int field, int32_t skillId, int32_t level) noexcept
	-> int32_t {
	return api.evalSkillCalc(DataFlag(game), unit, At<int32_t>(skill, field), skillId, level);
}

// D2's seed step (the game inlines it): lo' = lo * 0x6ac690c5 + hi, hi' = the carry.
inline auto Roll(uint32_t* seed) noexcept -> uint32_t {
	const uint64_t s = static_cast<uint64_t>(seed[0]) * 0x6ac690c5ULL + seed[1];
	seed[0] = static_cast<uint32_t>(s);
	seed[1] = static_cast<uint32_t>(s >> 32);
	return seed[0];
}

inline auto RandomBelow(uint32_t* seed, int32_t n) noexcept -> int32_t {
	if (n <= 0) {
		return 0;
	}
	const uint32_t v = Roll(seed);
	return static_cast<int32_t>((n & (n - 1)) == 0 ? v & static_cast<uint32_t>(n - 1) : v % static_cast<uint32_t>(n));
}

}  // namespace d2r

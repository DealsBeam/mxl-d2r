// Median's avoidance and block limits (pure rules; combat.cpp applies them to D2R).
//
// D2Sigma 0x10056e20 (D2Combat.cpp, the avoid check) and 0x100bd8d0 (the block chance) cap what the passive
// dodge/avoid/evade stats and the block chance can reach; D2R reads the raw stats. docs/subsystems/combat-stats.md.
//
//   dodge, avoid, evade (stats 338-340, players only here): chance <= min(75, 60 + "maximum avoid chance" stat)
//   block chance: <= min(80, 50 + "maximum block chance" stat); and <= 50 when the unit has the Indomitable skill
#pragma once

#include <algorithm>
#include <cstdint>

namespace mxl_combat {

// Migrated ids (tools/mxl_names.py idmap.json): Median's stats 205 and 213 were moved out of D2R's slots 183-213.
inline constexpr int32_t StatDodge = 338;
inline constexpr int32_t StatAvoid = 339;
inline constexpr int32_t StatEvade = 340;
inline constexpr int32_t StatMaxAvoid = 380;  // Median 205
inline constexpr int32_t StatMaxBlock = 387;  // Median 213
inline constexpr int32_t SkillIndomitable = 1135;  // Median 1063

// The result bits of D2R's avoid check 0x44ef70 (the same as D2Sigma's 0x10056e20).
inline constexpr int32_t ResultAvoid = 2;
inline constexpr int32_t ResultDodge = 4;
inline constexpr int32_t ResultEvade = 8;
inline constexpr int32_t ResultBlock = 0x10;

inline constexpr int32_t AvoidBase = 60;
inline constexpr int32_t AvoidMax = 75;
inline constexpr int32_t BlockBase = 50;
inline constexpr int32_t BlockMax = 80;
inline constexpr int32_t IndomitableBlock = 50;

constexpr auto AvoidCap(int32_t maxAvoidStat) noexcept -> int32_t {
	return std::clamp(AvoidBase + maxAvoidStat, 0, AvoidMax);
}

constexpr auto BlockCap(int32_t maxBlockStat, bool indomitable) noexcept -> int32_t {
	const int32_t cap = std::clamp(BlockBase + maxBlockStat, 0, BlockMax);
	return indomitable ? std::min(cap, IndomitableBlock) : cap;
}

// Ethereal items: D2Sigma 0x1007fdd0 (D2ItemStats.cpp) adds 25% to the weapon damage stats (0x15-0x18, 0x9f, 0xa0) or
// the defense (0x1f); D2R's 0x3d3510 multiplies them by 3/2.
inline constexpr int32_t EtherealPercent = 25;

constexpr auto EtherealBonus(int32_t value) noexcept -> int32_t {
	return static_cast<int32_t>(value + static_cast<int64_t>(value) * EtherealPercent / 100);
}

// The stat behind an avoid-check result bit; 0 when the result is not one of the three.
constexpr auto StatOfResult(int32_t result) noexcept -> int32_t {
	switch (result) {
	case ResultAvoid: return StatAvoid;
	case ResultDodge: return StatDodge;
	case ResultEvade: return StatEvade;
	default: return 0;
	}
}

// D2R rolled `value` percent and the roll succeeded; Median would have rolled min(value, cap). Keeping the success
// with probability cap / value gives exactly that: P = value/100 * cap/value. `roll` is uniform in [0, value).
constexpr auto KeepsSuccess(int32_t value, int32_t cap, int32_t roll) noexcept -> bool {
	return value <= cap || roll < cap;
}

}  // namespace mxl_combat

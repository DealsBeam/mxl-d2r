// Median's avoidance and block limits (pure rules; combat.cpp applies them to D2R).
//
// D2Sigma 0x10056e20 (D2Combat.cpp, the avoid check) and 0x100bd8d0 (the block chance) cap what the passive
// dodge/avoid/evade stats and the block chance can reach; D2R reads the raw stats. docs/subsystems/combat-stats.md.
//
//   dodge, avoid, evade (stats 338-340, players only here): chance <= min(75, 60 + "maximum avoid chance" stat)
//   block chance: <= min(80, 50 + "maximum block chance" stat); and <= 50 when the unit has the Indomitable skill
//   elemental resistances (players and mercenaries): <= min(90, 75 + the element's maximum-resistance stat)
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

// Resistance ceiling. D2R's damage code (0x4523e0) and the two client displays (character sheet 0x14e6e70, hireling
// panel 0x14f3400) all clamp 75 + max-resistance to 95; D2Sigma clamps to 90. The immediate in each clamp is one byte
// of a `mov r32, 0x5f`.
inline constexpr int32_t ResistBase = 75;
inline constexpr int32_t ResistMaxD2R = 95;
inline constexpr int32_t ResistMaxMedian = 90;

constexpr auto ResistCap(int32_t maxResistStat, int32_t hardCap) noexcept -> int32_t {
	return std::min(ResistBase + maxResistStat, hardCap);
}

constexpr auto AvoidCap(int32_t maxAvoidStat) noexcept -> int32_t {
	return std::clamp(AvoidBase + maxAvoidStat, 0, AvoidMax);
}

constexpr auto BlockCap(int32_t maxBlockStat, bool indomitable) noexcept -> int32_t {
	const int32_t cap = std::clamp(BlockBase + maxBlockStat, 0, BlockMax);
	return indomitable ? std::min(cap, IndomitableBlock) : cap;
}

// The block cap for a block chance of `value`. The Indomitable lookup (`hasIndomitable`, a skill level read) is only made
// when the chance is above the Indomitable limit, because below it the limit cannot lower the result.
template <typename HasIndomitable>
constexpr auto BlockCapFor(int32_t maxBlockStat, int32_t value, HasIndomitable&& hasIndomitable) -> int32_t {
	return BlockCap(maxBlockStat, value > IndomitableBlock && hasIndomitable());
}

// Absorb (D2Sigma 0x10057a20, D2R 0x450670). A hit's elemental damage is first cut by the defender's absorb-percent stat
// (at most 40%) and then by the flat absorb stat; the cut amount heals the defender (D2R adds it to the damage block's
// absorbed total at +0x130, which 0x451820 turns into life). D2Sigma does the same cuts but runs each healed amount
// through three defender stats before crediting it: Median 189 (divisor), 184 (percent off) and 417 (percent off, per
// damage type). They are monster stats (monprop 'time' / 'stam' rows and a few skills); no item rolls them. D2R ids:
inline constexpr int32_t StatAbsorbDivisor = 373;   // Median 189 (also the open wounds / crushing blow immunity flag)
inline constexpr int32_t StatAbsorbPercent = 369;   // Median 184
inline constexpr int32_t StatAbsorbTypePercent = 446;  // Median 417, layer = damage type below
inline constexpr int32_t AbsorbPercentCap = 40;
inline constexpr int32_t AbsorbFloor = 0x100;  // 1 point of life in D2's 8.8 fixed point

// Median's damage type (the layer of stat 417), from the resistance stat of D2R's table entry; 0 for the others.
constexpr auto AbsorbLayer(int32_t resistStat) noexcept -> int32_t {
	switch (resistStat) {
	case 0x27: return 1;  // fire
	case 0x29: return 2;  // lightning
	case 0x25: return 3;  // magic
	case 0x2b: return 4;  // cold
	case 0x2d: return 5;  // poison
	default: return 0;
	}
}

// D2Sigma 0x10057d00: amount / divisor, never below 1 point; unchanged when either is 0.
constexpr auto AbsorbDivide(int32_t amount, int32_t divisor) noexcept -> int32_t {
	if (amount == 0 || divisor == 0) {
		return amount;
	}
	return std::max(amount / divisor, AbsorbFloor);
}

// D2Sigma 0x10057ee0: amount minus `percent` of it, never below 1 point; unchanged when either is 0.
constexpr auto AbsorbReduce(int32_t amount, int32_t percent) noexcept -> int32_t {
	if (amount == 0 || percent == 0) {
		return amount;
	}
	const auto cut = static_cast<int32_t>(static_cast<int64_t>(amount) * percent / 100);
	return std::max(amount - cut, AbsorbFloor);
}

// What the defender is healed by one absorbed amount (D2Sigma applies the three stats in this order).
constexpr auto AbsorbCredit(int32_t amount, int32_t divisor, int32_t percent, int32_t typePercent) noexcept -> int32_t {
	return AbsorbReduce(AbsorbReduce(AbsorbDivide(amount, divisor), percent), typePercent);
}

// The absorb-percent step: the stat is limited to 40, the cut is that percent of the damage.
constexpr auto AbsorbPercentCut(int32_t damage, int32_t stat) noexcept -> int32_t {
	const int32_t pct = std::min(stat, AbsorbPercentCap);
	return pct > 0 ? static_cast<int32_t>(static_cast<int64_t>(damage) * pct / 100) : 0;
}

// Damage reduction on the incoming damage itself (D2Sigma 0x10057c10, reached from D2Game rva 0xdc4ea through the stub
// 0x1009b7e0, which replaces the call of the per-type step 6fcfbd00 in the loop of SUnitDmg 6fcfc0b0). For each of the twelve
// type entries Median first runs the vanilla step (D2R 0x4523e0: flat reduction, resistance, absorb), and then, when the
// entry's field is not 0, cuts what is left with the same defender stats as the absorb credit, in this order:
//   189 (D2R 373) divisor    a damage field becomes field / divisor (no floor, truncating); a duration (cold, freeze and
//                            poison length) becomes half of it, at least 5 frames
//   184 (D2R 369) percent    damage fields only: field - field * percent / 100
//   417 (D2R 446) percent    damage fields only, layer = the type below (0 for physical): the same cut
// A step needs its stat to be non-zero and the field above 0. There is no floor of one life point (the absorb credit has one),
// and the stat is not limited. The entries are told apart by D2R's name string at entry + 0x30; the layer is Sigma's 0x10057d70.
enum class FieldKind { None, Damage, Duration };

struct DamageType {
	FieldKind kind;
	int32_t layer;  // the layer of stat 417
};

inline constexpr int32_t DurationFloor = 5;

constexpr auto NameWord(char a, char b, char c, char d) noexcept -> uint32_t {
	return static_cast<uint32_t>(static_cast<uint8_t>(a)) | static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8 |
	       static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16 | static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24;
}

// The type of a table entry from the first four characters of its name ("Phys", "Fire", "Ligt", "Cold", "Magc", "CLen",
// "FLen", "PLen", "Pois", "Life", "Mana", "Stam").
constexpr auto DamageTypeOfName(uint32_t name) noexcept -> DamageType {
	switch (name) {
	case NameWord('P', 'h', 'y', 's'): return {FieldKind::Damage, 0};
	case NameWord('F', 'i', 'r', 'e'): return {FieldKind::Damage, 1};
	case NameWord('L', 'i', 'g', 't'): return {FieldKind::Damage, 2};
	case NameWord('M', 'a', 'g', 'c'): return {FieldKind::Damage, 3};
	case NameWord('C', 'o', 'l', 'd'): return {FieldKind::Damage, 4};
	case NameWord('P', 'o', 'i', 's'): return {FieldKind::Damage, 5};
	case NameWord('L', 'i', 'f', 'e'): return {FieldKind::Damage, 6};
	case NameWord('M', 'a', 'n', 'a'): return {FieldKind::Damage, 7};
	case NameWord('S', 't', 'a', 'm'): return {FieldKind::Damage, 8};
	case NameWord('C', 'L', 'e', 'n'):
	case NameWord('F', 'L', 'e', 'n'):
	case NameWord('P', 'L', 'e', 'n'): return {FieldKind::Duration, 0};
	default: return {FieldKind::None, 0};
	}
}

// D2Sigma 0x10057d20: field / divisor for a damage field, half (at least 5) for a duration.
constexpr auto FieldDivide(int32_t field, FieldKind kind, int32_t divisor) noexcept -> int32_t {
	if (field <= 0 || divisor == 0) {
		return field;
	}
	if (kind == FieldKind::Damage) {
		return std::max(field / divisor, 0);
	}
	if (kind == FieldKind::Duration) {
		return std::max(field >> 1, DurationFloor);
	}
	return field;
}

// D2Sigma 0x10057f10: a damage field loses `percent` of itself. Not applied to durations. (Median's stats are far below 100;
// the result is held at 0 so that a stray value cannot turn damage into healing.)
constexpr auto FieldReduce(int32_t field, FieldKind kind, int32_t percent) noexcept -> int32_t {
	if (field <= 0 || percent == 0 || kind != FieldKind::Damage) {
		return field;
	}
	const auto cut = static_cast<int32_t>(static_cast<int64_t>(field) * percent / 100);
	return std::max(field - cut, 0);
}

// What is left of a field after D2R's step, once Median's three stats have been applied (the field is read after the step).
constexpr auto ReduceField(int32_t field, DamageType type, int32_t divisor, int32_t percent, int32_t typePercent) noexcept -> int32_t {
	if (field == 0 || type.kind == FieldKind::None) {
		return field;
	}
	return FieldReduce(FieldReduce(FieldDivide(field, type.kind, divisor), type.kind, percent), type.kind, typePercent);
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

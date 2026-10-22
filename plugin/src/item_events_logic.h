// Pure rules of the item event port (item_events.cpp; docs/subsystems/items-xp.md section 5b), kept free of game headers so
// plugin/tests can run them.
#pragma once

#include <cstdint>

namespace mxl_itemevents {

// Item event function ids Median's itemstatcost rows use (itemeventfunc1 / 2) that need work on D2R.
inline constexpr int FuncOpenWounds = 15;
inline constexpr int FuncCrushingBlow = 16;
inline constexpr int FuncHealOnKill = 33;  // stat 265: Median's flat life on kill, a copy of the body of D2R function 28
inline constexpr int FuncCastOnKill = 34;  // stat 376: chance to cast a skill level on kill, the same layout as D2R function 20
inline constexpr int D2rHealFunc = 28;
inline constexpr int D2rCastFunc = 20;
inline constexpr int ImmunityStat = 373;   // D2R's id of Median's stat 189: set on the monsters that open wounds / crushing blow skip
inline constexpr int UnitMonster = 1;

// D2RCore.dll's handler table (8-byte pointers, slot = function id, slot 0 empty).
inline constexpr uint64_t CoreHandlerTableRva = 0x630260;  // in D2RCore.dll, slot = function id

// D2Sigma's wrappers 0x7de40 (open wounds) and 0x7db40 (crushing blow): the handler is skipped when the unit it is given is a
// monster with the stat set. Median's registered unit is the attacker (its own handlers read the item stat from it), so
// the wrapper as read would only skip a monster attacker; the intended monster immunity (docs: the stat is "immune to open
// wounds and crushing blow") is the unit that is hit. Both are checked.
constexpr bool UnitBlocks(int unitType, int immunityStat) noexcept {
	return unitType == UnitMonster && immunityStat != 0;
}
constexpr bool BlocksWoundsOrCrush(int attackerType, int attackerStat, int victimType, int victimStat) noexcept {
	return UnitBlocks(attackerType, attackerStat) || UnitBlocks(victimType, victimStat);
}

// D2R's handlers take the stat code as (stat << 16) | layer in 32 bits, D2RCore's table calls its handlers with
// (stat << 32) | layer.
constexpr uint32_t ToD2rCode(uint64_t core) noexcept {
	return core >> 32 != 0 ? static_cast<uint32_t>(((core >> 32) & 0xffff) << 16) | static_cast<uint32_t>(core & 0xffff)
	                       : static_cast<uint32_t>(core);
}

// Layer of stat 376 (and D2R's 195-199 / 539 cast stats): (skill << 6) | level in 16 bits. D2R reads the shift and mask from its data
// tables (0x14d0 / 0x14d4); they are 6 and 0x3f in this build (layer 2309 = Fire Bolt, 36 << 6 | 5). A skill id above 1023 does not fit.
inline constexpr int CastLayerShift = 6;
inline constexpr uint32_t CastLayerLevelMask = 0x3f;
constexpr int CastSkill(uint32_t layer) noexcept { return static_cast<int>((layer & 0xffff) >> CastLayerShift); }
constexpr int CastLevel(uint32_t layer) noexcept { return static_cast<int>(layer & CastLayerLevelMask); }
constexpr bool CastLayerFits(int skill, int level) noexcept {
	return skill >= 0 && skill < (1 << (16 - CastLayerShift)) && level >= 0 && level <= static_cast<int>(CastLayerLevelMask);
}
constexpr uint32_t CastLayer(int skill, int level) noexcept { return (static_cast<uint32_t>(skill) << CastLayerShift) | static_cast<uint32_t>(level); }

// Median's function 34 (D2Sigma `0x7de90` -> `0x7da00`) ends with a post-handler: when the cast skill has flag 0x40 in the second
// flag dword of its record (+8; the skills.txt column `warp`, 51 Median skills: Teleport, pounce, blink, ...) it calls the skill's start
// function (table `0x101df948[srvstfunc]`) after the cast. D2R's own cast used by function 20 (`0x589930`) already does this for warp
// skills, before the do function and as a condition for it (`0x43b3e0` runs `0x238e710[srvstfunc]`), so the port needs no code of its own;
// the flag is bit 6 of the byte at +0x28 of D2R's record (+0x20 against Median's layout). Used for the harness' reporting.
inline constexpr int RecordWarpOffset = 0x28;
inline constexpr uint8_t RecordWarpBit = 0x40;
constexpr bool IsWarpSkill(uint8_t flagByte) noexcept { return (flagByte & RecordWarpBit) != 0; }

}  // namespace mxl_itemevents

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

}  // namespace mxl_itemevents

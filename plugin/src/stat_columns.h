// Median's character sheet page 2 on D2R's Advanced Stats panel (stat_columns.cpp; docs/subsystems/stat-screen.md).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace d2r {
struct Unit;
}

// Hooks the panel's two row functions (0x14eb1b0: the value a row is listed by, 0x14eb250: the row text). True when hooked.
auto InstallStatColumns(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// For the test harness (UI thread): the panel's own functions run on a stand-in panel of `unit` (the client's copy of the
// player). `stat` is a D2R stat id; fills the displayed value (stat >> ValShift, what decides whether an advdisplay 2 row is
// listed) and the row text. False when the stat has no record or the functions are not hooked.
auto StatColumnsProbe(d2r::Unit* unit, int32_t stat, int64_t* value, char* text, size_t textSize) noexcept -> bool;
// The Strength / Dexterity bonus of the unit's wielded weapon (the records' StrBonus / DexBonus as D2R's damage code reads
// them); false without a weapon.
auto StatColumnsWeapon(d2r::Unit* unit, int32_t* strBonus, int32_t* dexBonus) noexcept -> bool;

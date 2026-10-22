// Median's mercenary class bonuses and hire gate (hirelings.cpp; docs/subsystems/npc-hirelings.md section 2).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Hooks D2R's mercenary stat setup 0x544f60 (run on hire, load and level-up) to set D2Sigma's class bonuses afterwards,
// and clears Kashya's level-8 hire alternative so that, as in Median, only the quest opens her hire. True when hooked.
auto InstallHirelings(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Median's mercenary revive price (hirelings.cpp, hire::RevivePrice): hooks the price function 0x3c1ad0 that the server's
// charge and the client's dialog both call. True when hooked.
auto InstallMercRevive(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Prices the hook has returned since start (the `mxl` line and the `mercrevive` game check read it).
auto MercRevivePrices() noexcept -> int;

// The last price the hook returned and the mercenary level it came from (0 before the first). Test harness.
auto MercReviveLast(int32_t* level) noexcept -> int32_t;

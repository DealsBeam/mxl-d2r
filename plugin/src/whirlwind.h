// Whirlwind's 1.13c hit rate and its chance to cast on strike / attack (whirlwind.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallWhirlwind(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Lets item-event casts (chance to cast on striking / on attack) through while the caster spins; counts them.
auto InstallWhirlwindProcs(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;
void WhirlwindProcStats(long* cast, long* refused) noexcept;

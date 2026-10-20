// D2Sigma's Horadric Cube recipe conditions (cube_conditions.cpp; docs/plugin-spec.md, "Cube conditions").
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallCubeConditions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Craft flags (cubemain ops 38-41): 256 bits per player and 256 for the game. D2Sigma sets them from recipe output
// effects (D2Craft.cpp 0x1005ad90 callers); that part is not ported, so nothing calls this yet and all flags are clear.
void SetCraftFlag(const void* player, int flag, bool game, bool on) noexcept;

// D2Sigma's Horadric Cube recipe conditions (cube_conditions.cpp; docs/plugin-spec.md, "Cube conditions").
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallCubeConditions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Craft flags (cubemain ops 38-41) and the recipes' output effects: craft_flags.h.

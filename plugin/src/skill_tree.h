// Eight skill pages in the skill tree panel (skill_tree.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Patches the game image (all or nothing, every site checked byte for byte first).
auto InstallSkillTree(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Eight skill pages in the skill tree panel (skill_tree.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Patches the game image (all or nothing, every site checked byte for byte first).
auto InstallSkillTree(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Harness verb `skilltab`: the selected tab (global 0x3bbd8bc), a tab set through the hooked setter with the last panel
// it was called with (false before the setter has run once: click any tab first), and how often the setter ran.
auto SkillTreeCurrentTab() noexcept -> int32_t;
auto SkillTreeSetTab(int32_t tab) noexcept -> bool;
auto SkillTreeSetterCalls() noexcept -> uint32_t;

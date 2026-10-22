// Median's NPC menu actions (npc_actions.cpp; docs/subsystems/npc-hirelings.md section 1.5).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Client: hooks the NPC menu builder 0x1147a0 and adds Median's lines to the menu table. Server: hooks the menu
// packet handler 0x4b0470 and runs custom kinds (>= 0x10) through the requirement evaluator. True when both installed.
auto InstallNpcActions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Test harness (client): true while the Yes / No confirmation of a respec-style line is open; the answer does what a click on
// its Yes / No line does (false when none is open).
auto NpcConfirmOpen() noexcept -> bool;
auto NpcConfirmAnswer(bool yes) noexcept -> bool;

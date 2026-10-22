// D2R's own NPC services against Median's menu slots (npc_services.cpp; docs/subsystems/npc-hirelings.md section 1.6).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Moves the gate byte (+0x10) of a D2R-gated travel line to the quest Median's slot requires (Tyrael: quest 26 -> 28).
// Returns the number of gates written, or -1 when the NPC table is not recognised.
auto FixTravelGates(const D2RL::PluginContext* context) noexcept -> int;

// Reads the game's NPC table and client menu fix table, compares them with the Median slots that D2R runs itself (imbue,
// sockets, personalize, travel packages) and writes a one-line summary. Returns the number of slots that differ, or -1
// when the tables are not recognised. Writes nothing to the game.
auto VerifyNativeServices(uint64_t exeBase, char* report, size_t reportSize) noexcept -> int;

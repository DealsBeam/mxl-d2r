// D2R's own NPC services against Median's menu slots (npc_services.cpp; docs/subsystems/npc-hirelings.md section 1.6).
#pragma once

#include <cstddef>
#include <cstdint>

// Reads the game's NPC table and client menu fix table, compares them with the Median slots that D2R runs itself (imbue,
// sockets, personalize, travel packages) and writes a one-line summary. Returns the number of slots that differ, or -1
// when the tables are not recognised. Writes nothing to the game.
auto VerifyNativeServices(uint64_t exeBase, char* report, size_t reportSize) noexcept -> int;

// Median's requirement rows where D2R enforces them beyond the NPC menu lines (requirement_gates.cpp;
// docs/subsystems/quests-requirements.md section 1, "Who asks").
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// D2Sigma's evaluator has one family of callers, the NPC services (shop / gamble / hire); D2R keeps those gates in a
// static NPC table (0x237ea60) read by both the client menu and the server packet handler 0x4b0470. This compares
// Median's npcext flags and requirement rows with that table and clears the service bytes Median denies. True when the
// table was recognised (expected class ids at both ends) and every write that was needed succeeded.
auto InstallRequirementGates(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

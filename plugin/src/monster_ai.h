// Median's monster AI on D2R: the `andariel_ai` boss think as a native AI id 155 (beyond D2R's 155-entry table), the
// Summoner first-branch rule and the MosquitoNest init. docs/subsystems/monsters-ai.md; monster_ai.cpp.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks the AI table getter (0x4a36c0) so that AI id 155 resolves to an entry of the plugin's. `native` false (the "medianai"
// word of mxl-core-off.txt) keeps id 155 alive with D2R's own Andariel think and the old parameter mapping, and leaves
// Summoner and the MosquitoNest alone. Returns false when the getter could not be hooked.
auto InstallMonsterAi(const D2RL::PluginContext* context, bool native, char* report, size_t reportSize) noexcept -> bool;

// "<n> thinks, <attack/skill/walk...> counts" for the mxl console command.
void MonsterAiStatus(char* out, size_t outSize) noexcept;

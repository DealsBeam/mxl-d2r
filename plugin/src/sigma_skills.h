// D2Sigma's added skill server functions (sigma_skills.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Writes the functions into their empty srvdofunc slots. Call only on the expected game image. Returns how many
// were installed; report gets one "srvdofunc <slot> <name>[ FAILED]" entry per function.
auto InstallSigmaSkills(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto SigmaSkillCount() noexcept -> int;
// What the wrapped slots 41 / 73 did since the game started (harness verb `skillstats`): melee hits thrown before the
// scatter, hammer casts that took srvmissileb / c.
auto SigmaSkillCounters(long* fanCasts, long* fanMeleeHits, long* hammerCasts, long* hammerSwaps) noexcept -> void;
// The multi-summon (slot 172) marks pets of a skill with Param7 as ranged pets, as D2Sigma does (NecroPet's ranged
// branch, bug #59). Off word: rangedpets. RangedPetCounts returns whether it is on and how many pets it marked.
auto SetRangedPets(bool on) noexcept -> void;
auto RangedPetCounts(long* set) noexcept -> bool;

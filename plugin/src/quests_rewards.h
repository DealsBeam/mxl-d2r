// Median's Act 5 quest experience and the vanilla quest rewards D2Sigma removes or replaces (quests_rewards.cpp;
// docs/sigma-patch-audit.md sections 9 and 10).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Rite of Passage (a5q5) experience: D2R's 1.4M / 20M / 40M by difficulty becomes a flat 50,000 (none above level 129),
// hooked on 0x550c20. True when hooked.
auto InstallQuestXp(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// The vanilla rewards Median removes or replaces: Qual-Kehk's runes, Akara's ring, Andariel's chipped gems (byte
// patches), the Prison of Ice item (an amulet), the Hellforge and the Countess (treasure classes). True when all six
// pieces installed.
auto InstallQuestRewards(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Counters for the `mxl` line and the game checks: quest experience grants (and the last amount), Hellforge classes
// dropped, Countess classes dropped, Prison of Ice items rewritten.
struct QuestRewardCounts {
	int xpGrants;
	int lastXp;
	int hellforge;
	int countess;
	int prisonItems;
};
auto QuestRewardsCounts() noexcept -> QuestRewardCounts;

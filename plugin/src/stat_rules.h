// Median's stat rules in the original game code: Potion of Life and Lam Esen's Tome amounts, stamina recovery
// (stat_rules.cpp; docs/sigma-patch-audit.md items 11 and 15). The mana half is in heat.cpp.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

struct StatRulesParts {
	bool potionAmounts = true;  // off word "potionamounts": Potion of Life +50 life, Lam Esen's Tome 10 stat points
	bool stamina = true;        // off word "stamina": D2Sigma's stamina recovery step
	bool shiftClick = true;     // off word "statclick": Shift-click on a stat "+" adds 25 points (audit XP6)
};

// Installs the selected parts; the report names each one. Returns true when every selected part is in.
auto InstallStatRules(const D2RL::PluginContext* context, const StatRulesParts& parts, char* report, size_t reportSize) noexcept -> bool;

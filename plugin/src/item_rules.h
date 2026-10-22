// No durability loss, no quantity loss (item_rules.cpp; audit IT1, IT2).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

struct ItemRulesParts {
	bool durability = true;
	bool quantity = true;
};

// Installs the selected parts; the report names each one ("durability on, quantity off: ...").
// Returns true when every selected part is in.
auto InstallItemRules(const D2RL::PluginContext* context, const ItemRulesParts& parts, char* report, size_t reportSize) noexcept -> bool;

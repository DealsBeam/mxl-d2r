// No durability (wear, tooltip line, repair UI), no quantity loss (item_rules.cpp; audit IT1, IT2).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

struct ItemRulesParts {
	bool durability = true;  // no wear (0x441b10, 0x5590c0)
	bool quantity = true;    // no stack use (0x439130)
	bool tooltip = true;     // no "Durability: x of y" line in the item description (0x2c1640)
	bool repairUi = true;    // no repair mode in the vendor panel, "trade/repair" menu lines read "trade"
};

// The NPC menu table (0x2a48830) is a heap array that D2R fills at start-up: the line retitle runs at install (when the
// table is there already) and again before each menu build (npc_actions.cpp BuilderHook calls this). Returns the number of
// lines changed; 0 when the part is off or nothing is left to change.
auto ItemRulesRetitleMenu() noexcept -> int;

// Installs the selected parts; the report names each one ("durability on, quantity off: ...").
// Returns true when every selected part is in.
auto InstallItemRules(const D2RL::PluginContext* context, const ItemRulesParts& parts, char* report, size_t reportSize) noexcept -> bool;

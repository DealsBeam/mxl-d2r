// Mana costs with D2Sigma's formulas and mana cost % (mana_costs.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// townFree: the two charge sites cost nothing in a town (audit SK3).
auto InstallManaCosts(const D2RL::PluginContext* context, bool townFree, char* report, size_t reportSize) noexcept -> bool;

// What the last charge-site call decided (the harness `manacharge` verb): calls so far, the unit's room, the town test,
// the cost before and after the town rule, and the return address' site (1 server, 2 client).
struct ManaChargeLog {
	unsigned calls = 0;
	void* room = nullptr;
	int roomIsTown = 0;
	int cost = 0;
	int charged = 0;
	int site = 0;
	unsigned otherCalls = 0;       // calls from anywhere else (tooltips, AI): D2R's value
	uint64_t otherFrom = 0;        // the last such return address as an RVA
	int otherSkill = 0;
};
auto LastManaCharge() noexcept -> ManaChargeLog;

// Skill affordability (audit common2-016 (a)): the usability test's afford step with D2Sigma's cost (the 17 formula-cost
// skills, stat 228). `SkillAffordLog`: tests answered by the hook (player, record with a formula or a mana cost %) and
// how many of them differ from D2R's own answer. `SigmaSkillCost` (harness `afford`): the cost the hook compares with the
// mana stat, in 1/256 points, for the unit's skill entry; -1 when it cannot be computed.
namespace d2r {
struct Unit;
}
struct AffordLog {
	unsigned calls = 0;
	unsigned changed = 0;
};
auto InstallSkillAfford(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;
auto SkillAffordLog() noexcept -> AffordLog;
auto SigmaSkillCost(d2r::Unit* unit, int64_t* entry) noexcept -> int32_t;

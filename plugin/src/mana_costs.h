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

// Items without durability or quantity loss (docs/median-rules-audit.md IT1, IT2). The third item rule of the audit, IT3
// (magic, set, rare and unique items are born identified), is in item_gen.cpp: it needs the generator hook 0x442d60 that
// file already owns.
//
// Median keeps the durability and quantity columns in its tables and removes the behaviour from D2Game.dll with binary
// patches (D2Sigma's patch table, docs/unported.md "Item rules"): the call to the wear dispatcher at D2Game+0xdee28 is
// NOPed (durability) and the `dec ebp` of the quantity use (SKILLS_Skills 0x6fcc16a0 / 0x6fcc1790) is NOPed. D2R's
// counterparts are
//   0x441b10  wear of a weapon / armour piece after a hit (game, unit, item): replaced by a no-op,
//   0x5590c0  the skill-use wear / stack chance (game, unit, item, chance, amount): replaced by a no-op,
//   0x439130  quantity use (game, unit, item, flag): replaced by one that leaves the stack alone.
// The wear dispatcher is the only writer of stat 0x48 on a hit, and every throw or skill use of a stack goes through
// 0x439130 (callers 0x439320 from 0x432f90, 0x4333f0, 0x43acb0, 0x55906e, 0x5590c0).
#include "item_rules.h"

#include "d2r.h"
#include "item_rules_logic.h"

#include <cstdint>
#include <cstdio>

namespace {

using d2r::Unit;

constexpr uint64_t WearRva = 0x441b10;
constexpr uint64_t SkillWearRva = 0x5590c0;
constexpr uint64_t QuantityUseRva = 0x439130;
constexpr uint64_t GetStatRva = 0x2f5020;  // (unit, stat, layer)

constexpr int32_t StatQuantity = 0x46;

// Entry bytes of the dump of build 93847 (timestamp 0x6ab3782c); each has whole instructions without rip-relative
// operands in its first 5 bytes.
// 0x441b10: mov [rsp+0x10],rbp / push rsi / push rdi / push r12 / push r14 / push r15 / sub rsp,0x30 / mov r14,rdx ...
constexpr uint8_t WearEntry[] = {0x48, 0x89, 0x6c, 0x24, 0x10, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57,
                                 0x48, 0x83, 0xec, 0x30, 0x4c, 0x8b, 0xf2, 0x48, 0x8b, 0xe9, 0xba};
// 0x5590c0: mov [rsp+0x10],rbx / mov [rsp+0x18],rbp / mov [rsp+0x20],rsi / push rdi / sub rsp,0x40 / mov rdi,rdx ...
constexpr uint8_t SkillWearEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89,
                                      0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0xfa, 0x48};
// 0x439130: mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x18],rsi / mov [rsp+0x20],rdi / push r12 / push r14 ...
constexpr uint8_t QuantityUseEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89,
                                        0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56};

using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
GetStatFn g_getStat = nullptr;

void WearNone(void* game, void* unit, void* item) noexcept {
	(void)game;
	(void)unit;
	(void)item;
}

void SkillWearNone(void* game, void* unit, void* item, int32_t chance, int32_t amount) noexcept {
	(void)game;
	(void)unit;
	(void)item;
	(void)chance;
	(void)amount;
}

auto QuantityUseNone(void* game, void* unit, Unit* item, char flag) noexcept -> int32_t {
	(void)game;
	(void)unit;
	(void)flag;
	return item == nullptr ? 0 : itemrules::QuantityUseResult(g_getStat(item, StatQuantity, 0));
}

}  // namespace

auto InstallItemRules(const D2RL::PluginContext* context, const ItemRulesParts& parts, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);

	bool all = true;
	const char* durability = "off";
	const char* quantity = "off";
	if (parts.durability) {
		const bool ok = context->InstallInlineHook(WearRva, WearEntry, sizeof(WearEntry), reinterpret_cast<void*>(&WearNone)) &&
		                context->InstallInlineHook(SkillWearRva, SkillWearEntry, sizeof(SkillWearEntry), reinterpret_cast<void*>(&SkillWearNone));
		durability = ok ? "on" : "FAILED";
		all = all && ok;
	}
	if (parts.quantity) {
		const bool ok = context->InstallInlineHook(QuantityUseRva, QuantityUseEntry, sizeof(QuantityUseEntry),
		                                           reinterpret_cast<void*>(&QuantityUseNone));
		quantity = ok ? "on" : "FAILED";
		all = all && ok;
	}
	std::snprintf(report, reportSize, "%s: durability %s (0x441b10, 0x5590c0), quantity %s (0x439130)",
	              all ? "ok" : "partly failed", durability, quantity);
	return all;
}

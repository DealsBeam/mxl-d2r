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
//
// The wear is only half of what Median removes. Items keep the durability stats (itemstatcost 72 / 73 have no description,
// stat 152 has no descfunc), but Median's own code never shows or sells any of it: D2Sigma's item description builder
// (D2ItemDesc.cpp) has no durability line, its NPC menu and merchant panel (ui/NpcMenu.cpp, ui/Merchant.cpp) have no
// repair, and the audit of patches to the 1.13c DLLs could not list that (docs/sigma-patch-audit.md, limits of the audit).
// D2R draws all of it itself, so the port takes it out:
//   0x2c1640  the "Durability: x of y" line of the item description (item, text buffer): replaced by a no-op (the two
//             description builders 0x2bd480 and 0x2be560 call it and ignore the result; the line is its only output),
//   0x2412de  vendor panel set-up (0x2411e0): `mov [rbx+0x168],1` (repair vendor mode for the NPC classes 0x9a Charsi,
//             0xb2, 0xfd, 0x101, 0x1ff Larzuk) writes 0, so the panel is the plain trade panel (repair buttons and
//             repair background off),
//   0x2a48830 the NPC menu records: string id 0xd06 "trade/repair" -> 0xd44 "trade" (Charsi, Larzuk, record 257).
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
constexpr uint64_t TooltipRva = 0x2c1640;
constexpr uint64_t VendorModeRva = 0x2412de;
constexpr uint64_t MenuTableRva = 0x2a48830;  // pointer to the records (heap)
constexpr uint64_t MenuCountRva = 0x2a48838;  // record count (qword)
constexpr int MenuRecordSize = 0x48;
constexpr int MenuIdsOffset = 0x08;  // u16[8]
constexpr int MenuIdsCount = 8;
constexpr uint64_t MenuMaxRecords = 256;

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

// 0x2c1640: mov [rsp+0x20],rbx / push rsi / sub rsp,0x440 / ... (whole instructions, none rip-relative, in the first 13 bytes).
constexpr uint8_t TooltipEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x56, 0x48, 0x81, 0xec, 0x40, 0x04, 0x00, 0x00};
// 0x2412de: mov dword ptr [rbx+0x168],1 -> 0 (the repair vendor mode of the panel).
constexpr uint8_t VendorModeOriginal[] = {0xc7, 0x83, 0x68, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
constexpr uint8_t VendorModePatched[] = {0xc7, 0x83, 0x68, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

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

void TooltipNone(void* item, void* text) noexcept {
	(void)item;
	(void)text;
}

uint64_t g_base = 0;
bool g_retitle = false;

auto QuantityUseNone(void* game, void* unit, Unit* item, char flag) noexcept -> int32_t {
	(void)game;
	(void)unit;
	(void)flag;
	return item == nullptr ? 0 : itemrules::QuantityUseResult(g_getStat(item, StatQuantity, 0));
}

}  // namespace

auto ItemRulesRetitleMenu() noexcept -> int {
	if (!g_retitle) {
		return 0;
	}
	auto* table = *reinterpret_cast<uint8_t**>(g_base + MenuTableRva);
	const uint64_t count = *reinterpret_cast<const uint64_t*>(g_base + MenuCountRva);
	if (table == nullptr || count == 0 || count > MenuMaxRecords) {
		return 0;
	}
	int changed = 0;
	for (uint64_t r = 0; r < count; ++r) {
		changed += itemrules::RetitleRepairLines(reinterpret_cast<uint16_t*>(table + r * MenuRecordSize + MenuIdsOffset), MenuIdsCount);
	}
	return changed;
}

auto InstallItemRules(const D2RL::PluginContext* context, const ItemRulesParts& parts, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	g_base = base;

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
	const char* tooltip = "off";
	if (parts.tooltip) {
		const bool ok = context->InstallInlineHook(TooltipRva, TooltipEntry, sizeof(TooltipEntry), reinterpret_cast<void*>(&TooltipNone));
		tooltip = ok ? "on" : "FAILED";
		all = all && ok;
	}
	char repair[128] = "repair ui off";
	if (parts.repairUi) {
		const bool mode = context->PatchBytes(VendorModeRva, VendorModeOriginal, sizeof(VendorModeOriginal), VendorModePatched,
		                                      sizeof(VendorModePatched));
		g_retitle = true;
		const int lines = ItemRulesRetitleMenu();  // the table may not exist yet: the menu builder hook runs it again
		if (lines > 0) {
			std::snprintf(repair, sizeof(repair), "repair ui %s (0x2412de), %d menu lines now read trade (0x2a48830)", mode ? "on" : "FAILED", lines);
		} else {
			std::snprintf(repair, sizeof(repair), "repair ui %s (0x2412de), menu lines read trade from the first menu build on (0x2a48830)",
			              mode ? "on" : "FAILED");
		}
		all = all && mode;
	}
	std::snprintf(report, reportSize, "%s: durability %s (0x441b10, 0x5590c0), tooltip line %s (0x2c1640), %s, quantity %s (0x439130)",
	              all ? "ok" : "partly failed", durability, tooltip, repair, quantity);
	return all;
}

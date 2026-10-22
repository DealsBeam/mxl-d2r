// Median's stat rules in the original game code (docs/sigma-patch-audit.md items 11 and 15).
//
// Potion of Life and Lam Esen's Tome. D2Sigma changes the amounts in the two vanilla grants: the Potion of Life's
// `push 0x1400` becomes 0x3200 (+50 life instead of +20, D2Game 0xd9eea) and the byte of the Tome's stat point grant
// becomes 0xa (10 points instead of 5, D2Game 0xe6496). D2R's twins are two immediates, patched in place:
//   0x5806b5  mov r8d,0x1400     in the potion's use step 0x580680 (game, unit): AddStat(unit, 7, r8d, 0)   -> 0x3200
//   0x5ed710  lea r8d,[rax+4]    in the Tome's reward step 0x5ed6d0 (game, unit), rax = 1 after the quest bit test:
//                                 AddStat(unit, 4, r8d, 0)  -> lea r8d,[rax+9] = 10 (the displacement byte at 0x5ed713)
//
// Stamina. The per-frame regeneration of a player calls the stamina step and then the mana step (D2R 0x42e850 and
// 0x42e350 from 0x42e73c; 1.13c 0x6fc97a50 / 0x6fc97950, redirected by D2Sigma to 0x100a1f90 / 0x100a1f60). Median's step
// (stat_rules_logic.h): state 136 (shrine_stamina, the Stamina Shrine, shrines.txt code 14) fills the bar at once; otherwise
// standing in the field or a town gives max/256 raw per frame, running nothing and every other mode 75% of it. D2R's step
// gives 50% walking or attacking, nothing in any other mode without a recovery bonus of 1000, and does not know the state.
// The town rule of Median's infinite running is not in this step: the drain of a run (D2Game 0x6fc97bb0 in 1.13c, a room
// in a town: 1 = no drain) is D2R's own code and works the same in both games (game check `manarules`).
//
// Entry bytes of the dump of build 93847 (timestamp 0x6ab3782c); each has whole instructions without rip-relative operands
// in its first 5 bytes. The hooked function is replaced, the original is not called.
#include "stat_rules.h"

#include "d2r.h"
#include "stat_rules_logic.h"

#include <cstdint>
#include <cstdio>

namespace {

using d2r::Unit;

constexpr uint64_t StaminaRva = 0x42e850;       // (game, unit)
constexpr uint64_t GetStatRva = 0x2f5020;       // (unit, stat, layer)
constexpr uint64_t SetStatRva = 0x2f7d10;       // (unit, stat, value, layer)
constexpr uint64_t HasStateRva = 0x3351b0;      // (unit, state) -> non-zero when set
constexpr uint64_t UnitModeRva = 0x34ab60;      // (unit) -> mode
constexpr uint64_t PotionImmediateRva = 0x5806b5;
constexpr uint64_t TomeRva = 0x5ed70d;

constexpr int32_t StatStamina = 10;
constexpr int32_t StatMaxStamina = 11;
constexpr int32_t StatStaminaRecovery = 28;
constexpr int32_t StateStaminaShrine = 136;

// 0x42e850: mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x18],rsi / push rdi / sub rsp,0x20 / mov rbp,rdx
constexpr uint8_t StaminaEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74,
                                    0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xea};
// 0x5806b5: mov r8d,0x1400 ; patched to 0x3200
constexpr uint8_t PotionOriginal[] = {0x41, 0xb8, 0x00, 0x14, 0x00, 0x00};
constexpr uint8_t PotionPatched[] = {0x41, 0xb8, 0x00, 0x32, 0x00, 0x00};
// 0x5ed70d: lea edx,[rax+3] / lea r8d,[rax+4] ; the second patched to [rax+9] (rax is 1 there)
constexpr uint8_t TomeOriginal[] = {0x8d, 0x50, 0x03, 0x44, 0x8d, 0x40, 0x04};
constexpr uint8_t TomePatched[] = {0x8d, 0x50, 0x03, 0x44, 0x8d, 0x40, 0x09};

using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using SetStatFn = void (*)(Unit* unit, int32_t stat, int32_t value, int32_t layer);
using HasStateFn = uint64_t (*)(Unit* unit, int32_t state);
using UnitModeFn = int32_t (*)(Unit* unit);

GetStatFn g_getStat = nullptr;
SetStatFn g_setStat = nullptr;
HasStateFn g_hasState = nullptr;
UnitModeFn g_unitMode = nullptr;

void StaminaStep(void* game, Unit* unit) noexcept {
	(void)game;
	if (unit == nullptr) {
		return;
	}
	statrules::Stamina s;
	s.stamina = g_getStat(unit, StatStamina, 0);
	s.maxStamina = g_getStat(unit, StatMaxStamina, 0);
	s.bonus = g_getStat(unit, StatStaminaRecovery, 0);
	s.mode = g_unitMode(unit);
	s.infinite = g_hasState(unit, StateStaminaShrine) != 0;
	const int32_t next = statrules::StaminaAfter(s);
	if (s.infinite || next != s.stamina) {
		g_setStat(unit, StatStamina, next, 0);
	}
}

}  // namespace

auto InstallStatRules(const D2RL::PluginContext* context, const StatRulesParts& parts, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	g_setStat = reinterpret_cast<SetStatFn>(base + SetStatRva);
	g_hasState = reinterpret_cast<HasStateFn>(base + HasStateRva);
	g_unitMode = reinterpret_cast<UnitModeFn>(base + UnitModeRva);

	bool all = true;
	const char* potions = "off";
	const char* stamina = "off";
	if (parts.potionAmounts) {
		const bool life = context->PatchBytes(PotionImmediateRva, PotionOriginal, sizeof(PotionOriginal), PotionPatched, sizeof(PotionPatched));
		const bool tome = context->PatchBytes(TomeRva, TomeOriginal, sizeof(TomeOriginal), TomePatched, sizeof(TomePatched));
		potions = life && tome ? "on" : (life ? "FAILED (Lam Esen)" : (tome ? "FAILED (Potion of Life)" : "FAILED"));
		all = all && life && tome;
	}
	if (parts.stamina) {
		const bool ok = context->InstallInlineHook(StaminaRva, StaminaEntry, sizeof(StaminaEntry), reinterpret_cast<void*>(&StaminaStep));
		stamina = ok ? "on" : "FAILED";
		all = all && ok;
	}
	std::snprintf(report, reportSize, "%s: potion amounts %s (Potion of Life +%d life, Lam Esen %d points), stamina %s (0x42e850)",
	              all ? "ok" : "partly failed", potions, statrules::PotionOfLifeLife >> 8, statrules::LamEsenStatPoints, stamina);
	return all;
}

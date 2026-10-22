// Median's Act 5 quest experience and the vanilla rewards D2Sigma takes out (docs/sigma-patch-audit.md sections 9 and 10).
//
// D2Sigma removes vanilla rewards on top of which the quest hooks of quests_act1.cpp / quests_act2_5.cpp already hand out
// Median's own, so without the patches below a player is paid twice:
//
//   experience   Rite of Passage (a5q5.cpp) pays 1.4M / 20M / 40M by difficulty in D2R (0x550c20); D2Sigma 0x9fa20 pays a
//                flat 50,000 and nothing above level 129. The function is replaced: the client update 0x518620 as before,
//                then 0x44f2e0 (AddExperience) with the flat amount.
//   Qual-Kehk    the vanilla rune loop calls the item creation 0x517530 at 0x54c9ac; D2Sigma nops the 29 bytes of its
//                counterpart. The call becomes `mov eax,1`: the loop still counts a created item (the result is tested for
//                the "could not create" flag) but nothing is created. QualKehkHook gives Median's runes.
//   Akara        the ring task 0x5d9a30 creates a "rin " through 0x517530 at 0x5d9a6d; the call is nopped (D2Sigma nops 50
//                bytes). Griswold's Heart replaces the ring.
//   Andariel     the kill handler 0x5e1f00 drops two chipped gems in a loop at 0x5e1fc7..0x5e201d (r14 = 1, edi = 2), then
//                one standard gem at 0x5e201f. D2Sigma nops the loop's counterpart; the loop is jumped over (edi = 0, the
//                value the standard gem's roll expects after the loop). Median's catalyst comes from the quest hook.
//   Prison of Ice the reward task 0x546cc0 reads the item code from the task node (+0x18) and creates it, rare, for the
//                player; D2Sigma 0x1009b7d0 rewrites the request to an amulet, so the code in the node becomes "amu ".
//   Hellforge    0x5e4540 (quest 27, called from the object/NPC dispatcher 0x516370 with the quest and the dying Hephasto):
//                four waves of random runes, then one more by difficulty. D2Sigma replaces it with one roll of "Quest
//                Hellforge" (the class of that difficulty, halbu's monstats row 257) at the monster, victim and killer the
//                same unit. The hook rolls the class and clears the quest's active byte (+0x23), which is what the vanilla
//                function does when it is done; the original then only runs its tail.
//   Countess     0x5e3010 (called by the quest 5 kill handler 0x5e10c0 and, when the reward is still pending, by the retry
//                task 0x5d98b0) puts a rune chest at every listed player. D2Sigma 0x1009e9a0 drops the class "Quest
//                Countess" (navi's row 266) once per player in the game. The hook keeps the vanilla gate (data +0x118 set,
//                +0x119 clear), rolls the class at the Countess for the killer and sets +0x119 itself. Without a kill in
//                progress it lets the original run.
//
// Entry bytes and patched bytes checked against the dump of build 93847; whole instructions, no rip-relative.
#include "quests_rewards.h"

#include "d2r.h"
#include "quests_act1.h"
#include "quests_common.h"
#include "quests_logic.h"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t QuestXpRva = 0x550c20;        // (game, player): the Rite of Passage bonus
constexpr uint64_t QuestUpdateRva = 0x518620;    // (game, player): quest state packet to the client
constexpr uint64_t AddExperienceRva = 0x44f2e0;  // (game, unit, old level, amount)
constexpr uint64_t GetStatRva = 0x2f5020;        // (unit, stat, layer)
constexpr uint64_t AnyaItemRva = 0x546cc0;       // (task node, ctx): node +0x18 = item code
constexpr uint64_t HellforgeRva = 0x5e4540;      // (quest 27, hephasto)
constexpr uint64_t CountessRva = 0x5e3010;       // (quest 5, quest 5 data = quest + 0x20)

constexpr uint64_t QualKehkCallRva = 0x54c9ac;
constexpr uint64_t AkaraCallRva = 0x5d9a6d;
constexpr uint64_t AndarielInitRva = 0x5e1fbd;
constexpr uint64_t AndarielLoopRva = 0x5e1fc7;

constexpr int StatLevel = 12;
constexpr int PrisonItemCode = 0x18;     // u32 of the reward task node
constexpr int HellforgeActive = 0x23;    // byte of quest 27
constexpr int QuestGame = 0x08;          // quest object: the game
constexpr int TowerData = 0x20;          // quest 5 object: the data block
constexpr int TowerCountessDead = 0x118;
constexpr int TowerRewardGiven = 0x119;

// 0x550c20: mov [rsp+0x10],rbp / mov [rsp+0x18],rsi / mov [rsp+0x20],rdi / push r12 / push r14 / push r15 / sub rsp,0x20
constexpr uint8_t QuestXpEntry[] = {0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7c,
                                    0x24, 0x20, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x20};
// 0x546cc0: mov [rsp+8],rbx / mov [rsp+0x10],rdx / push rdi / sub rsp,0x30
constexpr uint8_t AnyaItemEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x54, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x30};
// 0x5e4540: push rbx / push rbp / sub rsp,0x48
constexpr uint8_t HellforgeEntry[] = {0x40, 0x53, 0x55, 0x48, 0x83, 0xec, 0x48};
// 0x5e3010: mov rax,rsp / push rsi / push r13 / sub rsp,0x58
constexpr uint8_t CountessEntry[] = {0x48, 0x8b, 0xc4, 0x56, 0x41, 0x55, 0x48, 0x83, 0xec, 0x58};

// 0x54c9ac: call 0x517530  ->  mov eax,1
constexpr uint8_t QualKehkCall[] = {0xe8, 0x7f, 0xab, 0xfc, 0xff};
constexpr uint8_t MovEax1[] = {0xb8, 0x01, 0x00, 0x00, 0x00};
// 0x5d9a6d: call 0x517530  ->  5-byte nop
constexpr uint8_t AkaraCall[] = {0xe8, 0xbe, 0xda, 0xf3, 0xff};
constexpr uint8_t Nop5[] = {0x0f, 0x1f, 0x44, 0x00, 0x00};
// 0x5e1fbd: lea edi,[rbp+2] -> xor edi,edi / nop; 0x5e1fc7: mov rcx,[rbx+8] (48 8b ..) -> jmp 0x5e201f
constexpr uint8_t AndarielInit[] = {0x8d, 0x7d, 0x02};
constexpr uint8_t AndarielInitNew[] = {0x31, 0xff, 0x90};
constexpr uint8_t AndarielLoop[] = {0x48, 0x8b};
constexpr uint8_t AndarielLoopNew[] = {0xeb, 0x56};

using QuestXpFn = void (*)(Game* game, Unit* player);
using QuestUpdateFn = void (*)(Game* game, Unit* player);
using AddExperienceFn = void (*)(Game* game, Unit* unit, int32_t oldLevel, int32_t amount);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using AnyaItemFn = void (*)(uint8_t* node, void* ctx);
using HellforgeFn = void (*)(uint8_t* quest, Unit* hephasto);
using CountessFn = void (*)(uint8_t* quest, uint8_t* data);

const D2RL::PluginContext* g_context = nullptr;
QuestXpFn g_xpOriginal = nullptr;
AnyaItemFn g_anyaOriginal = nullptr;
HellforgeFn g_hellforgeOriginal = nullptr;
CountessFn g_countessOriginal = nullptr;
QuestUpdateFn g_questUpdate = nullptr;
AddExperienceFn g_addExperience = nullptr;
GetStatFn g_getStat = nullptr;

std::atomic<int> g_xpGrants{0};
std::atomic<int> g_lastXp{0};
std::atomic<int> g_hellforge{0};
std::atomic<int> g_countess{0};
std::atomic<int> g_prison{0};

template <typename T>
void Put(void* p, int offset, T value) noexcept {
	*reinterpret_cast<T*>(static_cast<uint8_t*>(p) + offset) = value;
}

void Log(const char* what, int a, int b) noexcept {
	if (g_context == nullptr) {
		return;
	}
	char line[160];
	std::snprintf(line, sizeof(line), "mxl-core: quest rewards: %s (%d, %d)", what, a, b);
	g_context->LogInfo(line);
}

// ---- experience ------------------------------------------------------------------------------------------------------

void QuestXpHook(Game* game, Unit* player) {
	if (game == nullptr || player == nullptr) {
		return;
	}
	g_questUpdate(game, player);
	const int32_t level = g_getStat(player, StatLevel, 0);
	const int32_t amount = quests::QuestExperienceFor(level);
	g_lastXp = amount;
	++g_xpGrants;
	if (amount > 0) {
		g_addExperience(game, player, level, amount);
	}
	Log("quest experience", level, amount);
}

// ---- Prison of Ice -----------------------------------------------------------------------------------------------------

void AnyaItemHook(uint8_t* node, void* ctx) {
	if (node != nullptr) {
		Put<uint32_t>(node, PrisonItemCode, quests::CodePrisonAmulet);
		++g_prison;
	}
	g_anyaOriginal(node, ctx);
}

// ---- Hellforge ----------------------------------------------------------------------------------------------------------

void HellforgeHook(uint8_t* quest, Unit* hephasto) {
	Log("hellforge handler called, active byte", quest != nullptr ? d2r::At<uint8_t>(quest, HellforgeActive) : -1,
	    hephasto != nullptr ? static_cast<int32_t>(d2r::At<uint32_t>(hephasto, questeng::UnitClass)) : -1);
	if (quest != nullptr && hephasto != nullptr && d2r::At<uint8_t>(quest, HellforgeActive) != 0) {
		Game* game = d2r::At<Game*>(quest, QuestGame);
		const uint16_t index = game != nullptr ? questeng::BonusTcIndex(game, quests::TcHellforge(questeng::Difficulty(game))) : 0;
		if (index != 0) {
			const int32_t ilvl = static_cast<int32_t>(questeng::api.defaultIlvl(hephasto, 0));
			questeng::api.tcDrop(game, hephasto, hephasto, index, 0, ilvl, 0, 0, 0, 0);
			Put<uint8_t>(quest, HellforgeActive, 0);
			++g_hellforge;
			Log("hellforge class dropped", index, ilvl);
		} else {
			Log("hellforge class not found, the runes stay", 0, 0);
		}
	}
	g_hellforgeOriginal(quest, hephasto);
}

// ---- Countess ------------------------------------------------------------------------------------------------------------

void CountessHook(uint8_t* quest, uint8_t* data) {
	void* victim = nullptr;
	void* killer = nullptr;
	Log("countess reward called, flags", data != nullptr ? data[TowerCountessDead] : -1, data != nullptr ? data[TowerRewardGiven] : -1);
	if (quest != nullptr && data != nullptr && QuestsTowerKill(&victim, &killer) &&
	    quests::CountessRewardDue(data[TowerCountessDead] != 0, data[TowerRewardGiven] != 0) &&
	    static_cast<int32_t>(d2r::At<uint32_t>(victim, questeng::UnitClass)) == quests::MonsterCountess) {
		Game* game = d2r::At<Game*>(quest, QuestGame);
		if (game != nullptr && questeng::DropBonusPerPlayer(game, static_cast<Unit*>(victim), static_cast<Unit*>(killer),
		                                                    quests::TcCountess(questeng::Difficulty(game))) > 0) {
			data[TowerRewardGiven] = 1;
			++g_countess;
			Log("countess class dropped", questeng::Difficulty(game), g_countess);
			return;
		}
	}
	g_countessOriginal(quest, data);
}

auto Matches(uint64_t base, uint64_t rva, const uint8_t* bytes, size_t size) noexcept -> bool {
	return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes, size) == 0;
}

}  // namespace

auto QuestRewardsCounts() noexcept -> QuestRewardCounts {
	return {g_xpGrants.load(), g_lastXp.load(), g_hellforge.load(), g_countess.load(), g_prison.load()};
}

auto InstallQuestXp(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	const uint64_t base = context->exeBase;
	if (!Matches(base, QuestXpRva, QuestXpEntry, sizeof(QuestXpEntry))) {
		std::snprintf(report, reportSize, "off: entry bytes of 0x550c20 differ");
		return false;
	}
	g_questUpdate = reinterpret_cast<QuestUpdateFn>(base + QuestUpdateRva);
	g_addExperience = reinterpret_cast<AddExperienceFn>(base + AddExperienceRva);
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	const bool hooked = context->InstallInlineHook(QuestXpRva, QuestXpEntry, sizeof(QuestXpEntry), reinterpret_cast<void*>(&QuestXpHook),
	                                               reinterpret_cast<void**>(&g_xpOriginal));
	std::snprintf(report, reportSize, hooked ? "on: Rite of Passage experience flat %d (none above level %d)"
	                                         : "off: hook on 0x550c20 failed (%d, %d)",
	              quests::QuestExperienceAmount, quests::QuestExperienceLastLevel);
	return hooked;
}

auto InstallQuestRewards(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	const uint64_t base = context->exeBase;
	questeng::Bind(base);
	int ok = 0;
	char failed[96] = "";
	auto note = [&](bool done, const char* name) {
		if (done) {
			++ok;
		} else {
			const size_t used = std::strlen(failed);
			std::snprintf(failed + used, sizeof(failed) - used, "%s%s", used > 0 ? ", " : "", name);
		}
	};
	note(context->PatchBytes(QualKehkCallRva, QualKehkCall, sizeof(QualKehkCall), MovEax1, sizeof(MovEax1)), "Qual-Kehk");
	note(context->PatchBytes(AkaraCallRva, AkaraCall, sizeof(AkaraCall), Nop5, sizeof(Nop5)), "Akara");
	// both halves or neither: the jump lands on the standard gem only when edi was cleared
	bool andariel = context->PatchBytes(AndarielInitRva, AndarielInit, sizeof(AndarielInit), AndarielInitNew, sizeof(AndarielInitNew));
	if (andariel) {
		andariel = context->PatchBytes(AndarielLoopRva, AndarielLoop, sizeof(AndarielLoop), AndarielLoopNew, sizeof(AndarielLoopNew));
		if (!andariel) {
			(void)context->PatchBytes(AndarielInitRva, AndarielInitNew, sizeof(AndarielInitNew), AndarielInit, sizeof(AndarielInit));
		}
	}
	note(andariel, "Andariel");
	note(Matches(base, AnyaItemRva, AnyaItemEntry, sizeof(AnyaItemEntry)) &&
	         context->InstallInlineHook(AnyaItemRva, AnyaItemEntry, sizeof(AnyaItemEntry), reinterpret_cast<void*>(&AnyaItemHook),
	                                    reinterpret_cast<void**>(&g_anyaOriginal)),
	     "Prison of Ice");
	note(Matches(base, HellforgeRva, HellforgeEntry, sizeof(HellforgeEntry)) &&
	         context->InstallInlineHook(HellforgeRva, HellforgeEntry, sizeof(HellforgeEntry), reinterpret_cast<void*>(&HellforgeHook),
	                                    reinterpret_cast<void**>(&g_hellforgeOriginal)),
	     "Hellforge");
	note(Matches(base, CountessRva, CountessEntry, sizeof(CountessEntry)) &&
	         context->InstallInlineHook(CountessRva, CountessEntry, sizeof(CountessEntry), reinterpret_cast<void*>(&CountessHook),
	                                    reinterpret_cast<void**>(&g_countessOriginal)),
	     "Countess");
	if (ok == 6) {
		std::snprintf(report, reportSize,
		              "on: no vanilla runes (Qual-Kehk), ring (Akara) or gems (Andariel); Prison of Ice amulet; Hellforge and Countess classes");
	} else {
		std::snprintf(report, reportSize, "partial: %d of 6 installed, failed: %s", ok, failed);
	}
	return ok == 6;
}

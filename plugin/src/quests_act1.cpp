// Median's Act 1 quest hooks (D2Quests.cpp 0x9e730, 0x9e9f0, 0x9eb40) on D2R's own quest handlers.
//
// Every hook runs in front of the vanilla handler of the same quest and event slot and then calls it, exactly like
// Median's. Handlers take (quest*, event*); the event block holds game (+0), the NPC or victim unit (+0x10), the
// player (+0x18), the NPC class (u16 +0x28) and the dialog message (u16 +0x30).
//
//   a1q4 event 11  0x5e0830  Akara, message 0x76 with quest-4 bit 1 (Cain rescued): Griswold's Heart of the game's
//                            difficulty goes into the player's inventory; the vanilla handler then clears the bit and
//                            gives the ring.
//   a1q6 event 8   0x5e1f00  kill: Andariel in Normal, once: catalyst of disenchantment into the killer's inventory
//                            (D2Sigma drops treasure class "Quest Cata Disenchant" per player; D2R's treasure class
//                            executor is not located, so the item is created directly), flag bit set.
//   a1q6 event 0   0x5e1d30  NPC activated: flag set, NPC is Warriv (class 0x9b, D2Sigma 0xbff40(0)) and no catalyst
//                            held: create it again.
//   a1q5 event 8   0x5e10c0  kill: if the killer's quest 5 (Forgotten Tower) already holds bit 0, 0xe or 0xf the vanilla
//                            handler is skipped (D2Sigma 0x9e8b0: the finish branch must not run again for a killer who
//                            is done).
//
// A unique cannot be requested through 0x517530 (it builds the item-generator block itself), so the generator core
// 0x43cd50 is hooked and, when a request is armed on this thread, writes the unique index + 1 into the block's field
// +0x50 (read by 0x443910, the quality-7 chooser) before the original runs.
//
// Entry bytes verified against the dump of build 93847 (docs/d2r-addresses.md); whole instructions, no rip-relative.
#include "quests_act1.h"

#include "client_messages.h"
#include "client_messages_logic.h"
#include "d2r.h"
#include "quests_common.h"
#include "quests_logic.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t HandlerHeartRva = 0x5e0830;     // a1q4 event 11
constexpr uint64_t HandlerKillRva = 0x5e1f00;      // a1q6 event 8
constexpr uint64_t HandlerActivateRva = 0x5e1d30;  // a1q6 event 0
constexpr uint64_t HandlerTowerRva = 0x5e10c0;     // a1q5 event 8
constexpr uint64_t ItemGenRva = 0x43cd50;          // (game, block*, int): item generator core
constexpr uint64_t ItemUniqueIdRva = 0x36ce70;     // (item) -> unique index (0x443910 reads it back after choosing)

// 0x5e0830: push rbx / push rbp / push rsi / push rdi / push r14 / sub rsp,0x60 / movzx r14d,word [rdx+0x28]
constexpr uint8_t HeartEntry[] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x60, 0x44, 0x0f, 0xb7, 0x72, 0x28};
// 0x5e1f00: push rbx / push rbp / push r14 / sub rsp,0x40 / xor ebp,ebp / mov [rsp+0x68],rsi
constexpr uint8_t KillEntry[] = {0x40, 0x53, 0x55, 0x41, 0x56, 0x48, 0x83, 0xec, 0x40, 0x33, 0xed, 0x48, 0x89, 0x74, 0x24, 0x68};
// 0x5e1d30: mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x18],rsi / push rdi / push r12
constexpr uint8_t ActivateEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x54};
// 0x5e10c0: mov [rsp+8],rcx / push rbx / push rbp / push r15 / sub rsp,0x60 / mov r15,rdx (16 bytes)
constexpr uint8_t TowerEntry[] = {0x48, 0x89, 0x4c, 0x24, 0x08, 0x53, 0x55, 0x41, 0x57, 0x48, 0x83, 0xec, 0x60, 0x4c, 0x8b, 0xfa};
// 0x43cd50: push rbp / push rbx / push rsi / push r12 / push r15 / lea rbp,[rsp-0x37] / sub rsp,0x90
constexpr uint8_t ItemGenEntry[] = {0x40, 0x55, 0x53, 0x56, 0x41, 0x54, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xc9,
                                    0x48, 0x81, 0xec, 0x90, 0x00, 0x00, 0x00};

using questeng::Bit;
using questeng::Difficulty;
using questeng::DropBonusPerPlayer;
using questeng::EventMessage;
using questeng::EventNpcClass;
using questeng::EventPlayer;
using questeng::EventUnit;
using questeng::Record;
using questeng::UnitClass;
using questeng::api;

constexpr int GenQuality = 0x40;  // item-generator block: quality (7 = unique)
constexpr int GenFixed = 0x3c;    // non-zero: "fixed properties" path, no chooser
constexpr int GenUnique = 0x50;   // unique index + 1 asked for

using HandlerFn = void (*)(void* quest, uint8_t* event);
using ItemGenFn = uint64_t (*)(Game* game, uint8_t* block, int32_t flag);
using UniqueIdFn = uint32_t (*)(Unit* item);

HandlerFn g_heartOriginal = nullptr;
HandlerFn g_killOriginal = nullptr;
HandlerFn g_activateOriginal = nullptr;
HandlerFn g_towerOriginal = nullptr;
int g_towerSkipped = 0;
ItemGenFn g_itemGenOriginal = nullptr;
UniqueIdFn g_uniqueId = nullptr;
const D2RL::PluginContext* g_context = nullptr;

thread_local int32_t g_wantUnique = 0;  // unique index + 1 for the next generator call on this thread
// The kill the quest 5 hook is handing to the vanilla handler: the Countess reward hook (quests_rewards.cpp) needs the
// victim and the killer, which the vanilla reward function 0x5e3010 does not get.
thread_local Unit* g_towerVictim = nullptr;
thread_local Unit* g_towerKiller = nullptr;

void Log(const char* what, int a, int b) noexcept {
	if (g_context == nullptr) {
		return;
	}
	char line[160];
	std::snprintf(line, sizeof(line), "mxl-core: quests: %s (%d, %d)", what, a, b);
	g_context->LogInfo(line);
}

uint64_t ItemGenHook(Game* game, uint8_t* block, int32_t flag) {
	if (g_wantUnique != 0 && block != nullptr) {
		if (d2r::At<int32_t>(block, GenQuality) == 7 && d2r::At<int32_t>(block, GenFixed) == 0) {
			*reinterpret_cast<int32_t*>(block + GenUnique) = g_wantUnique;
		}
		g_wantUnique = 0;
	}
	return g_itemGenOriginal(game, block, flag);
}

// Griswold's Heart of the difficulty into the player's inventory (on the floor when it does not fit).
void GiveHeart(Game* game, Unit* player) noexcept {
	const quests::Heart* heart = quests::HeartFor(Difficulty(game));
	if (heart == nullptr) {
		return;
	}
	// A unique that "already exists in this game" fails to generate; the quest hands out one per player, so clear it.
	if (heart->uniqueIndex < quests::UniqueSpawnedLimit) {
		auto* word = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(game) + quests::UniqueSpawnedOffset(heart->uniqueIndex));
		*word &= ~quests::UniqueSpawnedMask(heart->uniqueIndex);
	}
	g_wantUnique = heart->uniqueIndex + 1;
	// item level 255 passes the unique row's "lvl" (255); quality 7 = unique
	Unit* item = api.createItem(game, player, heart->baseCode, 255, 7, 1);
	g_wantUnique = 0;
	if (item == nullptr) {
		Log("Griswold's Heart not created, base code", static_cast<int>(heart->baseCode), heart->uniqueIndex);
		return;
	}
	const uint32_t got = g_uniqueId(item);
	if (got != static_cast<uint32_t>(heart->uniqueIndex)) {
		Log("Griswold's Heart came out as another unique, got/wanted", static_cast<int>(got), heart->uniqueIndex);
	}
	SendRewardMessage(player, clientmsg::MsgRewardArmor, "Griswold's Heart", 1);
}

void HeartHook(void* quest, uint8_t* event) {
	if (event != nullptr) {
		auto* game = d2r::At<Game*>(event, 0);
		auto* player = d2r::At<Unit*>(event, EventPlayer);
		const auto npc = d2r::At<uint16_t>(event, EventNpcClass);
		const auto message = d2r::At<uint16_t>(event, EventMessage);
		if (game != nullptr && player != nullptr && npc == quests::NpcAkara && message == quests::MsgCainRescued) {
			void* record = Record(game, player);
			if (quests::ShouldGiveHeart(npc, message, Bit(record, quests::QuestSearchForCain, quests::BitCainRescued))) {
				GiveHeart(game, player);
			}
		}
	}
	g_heartOriginal(quest, event);
}

void KillHook(void* quest, uint8_t* event) {
	if (event != nullptr) {
		auto* game = d2r::At<Game*>(event, 0);
		auto* victim = d2r::At<Unit*>(event, EventUnit);
		auto* player = d2r::At<Unit*>(event, EventPlayer);
		if (game != nullptr && victim != nullptr && player != nullptr) {
			void* record = Record(game, player);
			const bool flag = Bit(record, quests::FlagQuest, quests::FlagCatalyst);
			const auto monster = static_cast<int32_t>(d2r::At<uint32_t>(victim, UnitClass));
			if (record != nullptr && quests::ShouldGiveCatalystOnKill(Difficulty(game), monster, flag)) {
				// Median rolls the class on the ground once per player and sets the flag whatever dropped; the
				// inventory item is only the fallback for a class that could not be resolved.
				if (DropBonusPerPlayer(game, victim, player, quests::TcCatalyst) > 0 ||
				    api.createItem(game, player, quests::CodeCatalyst, 0, 2, 1) != nullptr) {
					api.bitSet(record, quests::FlagQuest, quests::FlagCatalyst);
				}
			}
		}
	}
	g_killOriginal(quest, event);
}

void ActivateHook(void* quest, uint8_t* event) {
	if (event != nullptr) {
		auto* game = d2r::At<Game*>(event, 0);
		auto* npc = d2r::At<Unit*>(event, EventUnit);
		auto* player = d2r::At<Unit*>(event, EventPlayer);
		if (game != nullptr && npc != nullptr && player != nullptr) {
			void* record = Record(game, player);
			const auto npcClass = static_cast<int32_t>(d2r::At<uint32_t>(npc, UnitClass));
			if (record != nullptr && npcClass == quests::NpcWarriv) {
				const bool flag = Bit(record, quests::FlagQuest, quests::FlagCatalyst);
				const bool has = flag && api.findItem(game, player, quests::CodeCatalyst) != nullptr;
				if (quests::ShouldReissueCatalyst(npcClass, flag, has)) {
					api.createItem(game, player, quests::CodeCatalyst, 0, 2, 1);
				}
			}
		}
	}
	g_activateOriginal(quest, event);
}

// Median's guard: the vanilla kill handler of the Forgotten Tower does not run for a killer who is done with the quest.
// True when the vanilla kill handler must not run: the killer is done with the Forgotten Tower. `count` adds the Countess
// to the skip counter (the harness probe does not).
bool TowerSkips(Game* game, Unit* victim, Unit* player, bool count) noexcept {
	if (game == nullptr || victim == nullptr || player == nullptr) {
		return false;
	}
	void* record = Record(game, player);
	if (!quests::SkipTowerKill(Bit(record, quests::QuestForgottenTower, quests::BitTowerGranted),
	                           Bit(record, quests::QuestForgottenTower, quests::BitTowerCompletedNow),
	                           Bit(record, quests::QuestForgottenTower, quests::BitTowerCompletedBefore))) {
		return false;
	}
	if (count && static_cast<int32_t>(d2r::At<uint32_t>(victim, UnitClass)) == quests::MonsterCountess) {
		++g_towerSkipped;
		Log("quest 5 kill handler skipped, killer is done with the Forgotten Tower", g_towerSkipped, 0);
	}
	return true;
}

void TowerKillHook(void* quest, uint8_t* event) {
	if (event != nullptr && TowerSkips(d2r::At<Game*>(event, 0), d2r::At<Unit*>(event, EventUnit),
	                                   d2r::At<Unit*>(event, EventPlayer), true)) {
		return;
	}
	if (event != nullptr) {
		g_towerVictim = d2r::At<Unit*>(event, EventUnit);
		g_towerKiller = d2r::At<Unit*>(event, EventPlayer);
	}
	g_towerOriginal(quest, event);
	g_towerVictim = nullptr;
	g_towerKiller = nullptr;
}

}  // namespace

auto QuestsTowerProbe(void* game, void* victim, void* player) noexcept -> bool {
	return TowerSkips(static_cast<Game*>(game), static_cast<Unit*>(victim), static_cast<Unit*>(player), false);
}

auto QuestsTowerKill(void** victim, void** killer) noexcept -> bool {
	if (g_towerVictim == nullptr || g_towerKiller == nullptr) {
		return false;
	}
	*victim = g_towerVictim;
	*killer = g_towerKiller;
	return true;
}

void QuestsTowerKillSet(void* victim, void* killer) noexcept {
	g_towerVictim = static_cast<Unit*>(victim);
	g_towerKiller = static_cast<Unit*>(killer);
}

auto QuestsTowerSkips() noexcept -> int {
	return g_towerSkipped;
}

void QuestsWantUnique(int indexPlusOne) noexcept {
	g_wantUnique = indexPlusOne;
}

auto InstallQuestsAct1(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	g_context = context;
	questeng::Bind(base);
	g_uniqueId = reinterpret_cast<UniqueIdFn>(base + ItemUniqueIdRva);
	int ok = 0;
	ok += context->InstallInlineHook(ItemGenRva, ItemGenEntry, sizeof(ItemGenEntry), reinterpret_cast<void*>(&ItemGenHook),
	                                 reinterpret_cast<void**>(&g_itemGenOriginal)) ? 1 : 0;
	ok += context->InstallInlineHook(HandlerHeartRva, HeartEntry, sizeof(HeartEntry), reinterpret_cast<void*>(&HeartHook),
	                                 reinterpret_cast<void**>(&g_heartOriginal)) ? 1 : 0;
	ok += context->InstallInlineHook(HandlerKillRva, KillEntry, sizeof(KillEntry), reinterpret_cast<void*>(&KillHook),
	                                 reinterpret_cast<void**>(&g_killOriginal)) ? 1 : 0;
	ok += context->InstallInlineHook(HandlerActivateRva, ActivateEntry, sizeof(ActivateEntry),
	                                 reinterpret_cast<void*>(&ActivateHook), reinterpret_cast<void**>(&g_activateOriginal)) ? 1 : 0;
	ok += context->InstallInlineHook(HandlerTowerRva, TowerEntry, sizeof(TowerEntry), reinterpret_cast<void*>(&TowerKillHook),
	                                 reinterpret_cast<void**>(&g_towerOriginal)) ? 1 : 0;
	std::snprintf(report, reportSize,
	              ok == 5 ? "on: Griswold's Heart (Akara), Andariel catalyst, catalyst re-issue, Forgotten Tower guard"
	                      : "partial: %d of 5 hooks installed (a hook failed; the others stay)",
	              ok);
	return ok == 5;
}

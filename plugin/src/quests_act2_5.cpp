// Median's quest changes of Acts 2-5 (D2Sigma D2Quests.cpp) on D2R's own quest handlers; Act 1 is quests_act1.cpp.
//
// Same scheme as Act 1: a hook runs in front of the vanilla handler of the same quest and event slot and then calls it
// (handlers take (quest*, event*); event block: game +0, NPC or victim +0x10, player +0x18, NPC class u16 +0x28,
// dialog message u16 +0x30). Exceptions are marked.
//
//   a2q5 event 8   0x5a5490  Summoner dies in Nightmare, once: catalyst of learning (0o[ ) into the killer's inventory
//   a2q5 event 0   0x5a5360  any NPC activated, flag set, no catalyst held: create it again (D2Sigma tests no class)
//   a2q6 event 11  0x5a66b0  Jerhyn, message 0x1ba: one Arcane Crystal (qum )
//   a3q3 event 11  0x5ef040  Ormus, message 0x251, bit 6: 1 / 3 / 5 random shrine items (type flsh) by difficulty
//   a3q5 event 11  0x5f0df0  Cain (act 3), message 0x272: Arcane Crystal
//   a4q1 event 8   0x5e67c0  Izual dies in Nightmare, once: catalyst of jewelcraft (jcat)
//   a4q1 event 0   0x5e6660  Tyrael activated, flag set, no jcat held: create it again
//   a5q2 event 11  0x54c810  Qual-Kehk, message 0x4e8e, bit 0 clear and bit 1 set: 3 (2 / 1) runes of the difficulty
//   a5q6 event 8   0x54fa70  AFTER the vanilla handler: Baal dies in Hell, every player in the Worldstone Chamber above
//                            level 109 gets a relic orb (qrel) once (quest 40 bit 0xb)
//   respec bookkeeping 0x5d9ae0  REPLACED (D2Sigma replaces the 1.13c call 0x6fc83bb0 by 0x9e600): above level 50
//                            quest 41 bit 0 is cleared and bit 1 set; the respec token is never used up
//
// Not ported (see docs/subsystems/quests-requirements.md section 5c): the quest 5 repeat guard (a Sigma artifact),
// the quest 38 re-arm (D2R's own handler 0x54dd30 already does it), the empty act-intro event 2 handlers.
//
// Flags "bonus catalyst handed out" are bits of quest record 47 of the difficulty (Act 1 uses bit 0 there). The treasure
// class drops of D2Sigma become direct creations in the killer's inventory, as in Act 1. Messages 0x5f-0x61 (client
// notices) are lines in the player's message log (client_messages.h; the text is neutral, the D2Sigma text is not known).
//
// Entry bytes are whole instructions from the dump of build 93847 (docs/d2r-addresses.md section 11 and Appendix A);
// no rip-relative operands.
#include "quests_act2_5.h"

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
using questeng::api;
using questeng::Bit;
using questeng::Difficulty;
using questeng::DropBonusPerPlayer;
using questeng::Record;

constexpr uint64_t SummonerKillRva = 0x5a5490;
constexpr uint64_t SummonerActivateRva = 0x5a5360;
constexpr uint64_t JerhynRva = 0x5a66b0;
constexpr uint64_t OrmusRva = 0x5ef040;
constexpr uint64_t CainRva = 0x5f0df0;
constexpr uint64_t IzualKillRva = 0x5e67c0;
constexpr uint64_t TyraelActivateRva = 0x5e6660;
constexpr uint64_t QualKehkRva = 0x54c810;
constexpr uint64_t BaalKillRva = 0x54fa70;
constexpr uint64_t RespecDoneRva = 0x5d9ae0;      // (game, player): quest 41 bookkeeping after a respec
constexpr uint64_t FindQuestRva = 0x518220;       // (game, quest id) -> quest object or null
constexpr uint64_t ForEachPlayerRva = 0x490de0;   // (game, 0, arg, callback(game, player, arg)): stops when it returns 1
constexpr uint64_t GetStatRva = 0x2f5020;         // (unit, stat, layer)
constexpr uint64_t UnitRoomRva = 0x34b440;        // (unit) -> room
constexpr uint64_t RoomLevelIdRva = 0x2efc10;     // (room) -> level id

// 0x5a5490: mov [rsp+8],rbx / push rdi / sub rsp,0x20
constexpr uint8_t SummonerKillEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20};
// 0x5a5360: mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x18],rsi
constexpr uint8_t SummonerActivateEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
// 0x5a66b0: mov [rsp+0x18],rbx / mov [rsp+0x20],rsi / push rdi / sub rsp,0x50
constexpr uint8_t JerhynEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x50};
// 0x5ef040: push rbx / push rbp / push rsi / push rdi / push r14 / sub rsp,0x60 / movzx r14d,word [rdx+0x28]
constexpr uint8_t OrmusEntry[] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x60, 0x44, 0x0f, 0xb7, 0x72, 0x28};
// 0x5f0df0: mov [rsp+0x10],rbx / mov [rsp+0x18],rbp / push rsi / push rdi / push r14 / sub rsp,0x30
constexpr uint8_t CainEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x30};
// 0x5e67c0: mov [rsp+8],rbx / mov [rsp+0x10],rsi / push rdi / sub rsp,0x20
constexpr uint8_t IzualKillEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20};
// 0x5e6660: mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x18],rsi
constexpr uint8_t TyraelActivateEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
// 0x54c810: push rbp / push rsi / push rdi / sub rsp,0x40
constexpr uint8_t QualKehkEntry[] = {0x40, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x40};
// 0x54fa70: mov [rsp+0x20],rbx / push rbp / push rsi / push rdi / push r14 / push r15
constexpr uint8_t BaalKillEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57};
// 0x5d9ae0: mov [rsp+0x10],rbx / push rdi / sub rsp,0x20
constexpr uint8_t RespecDoneEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20};

constexpr int QuestFlagRecord = quests::FlagQuest;
constexpr int StatLevel = 12;
constexpr int QuestObjectActive = 0x12;  // byte of the quest object

using HandlerFn = void (*)(void* quest, uint8_t* event);
using ForEachCallback = int32_t (*)(Game* game, Unit* player, void* arg);
using ForEachPlayerFn = Unit* (*)(Game* game, int32_t kind, void* arg, ForEachCallback callback);
using FindQuestFn = uint8_t* (*)(Game* game, int32_t id);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using UnitRoomFn = void* (*)(Unit* unit);
using RoomLevelIdFn = int32_t (*)(void* room);
using RespecDoneFn = void (*)(Game* game, Unit* player);

HandlerFn g_summonerKillOriginal = nullptr;
HandlerFn g_summonerActivateOriginal = nullptr;
HandlerFn g_jerhynOriginal = nullptr;
HandlerFn g_ormusOriginal = nullptr;
HandlerFn g_cainOriginal = nullptr;
HandlerFn g_izualKillOriginal = nullptr;
HandlerFn g_tyraelActivateOriginal = nullptr;
HandlerFn g_qualKehkOriginal = nullptr;
HandlerFn g_baalKillOriginal = nullptr;
RespecDoneFn g_respecDoneOriginal = nullptr;
ForEachPlayerFn g_forEachPlayer = nullptr;
FindQuestFn g_findQuest = nullptr;
GetStatFn g_getStat = nullptr;
UnitRoomFn g_unitRoom = nullptr;
RoomLevelIdFn g_roomLevelId = nullptr;
const D2RL::PluginContext* g_context = nullptr;

void Log(const char* what, int a, int b) noexcept {
	if (g_context == nullptr) {
		return;
	}
	char line[160];
	std::snprintf(line, sizeof(line), "mxl-core: quests: %s (%d, %d)", what, a, b);
	g_context->LogInfo(line);
}

// The pieces of a quest event every hook reads.
struct Event {
	Game* game;
	Unit* unit;    // NPC (events 0 and 11: the class is in npc) or victim (event 8)
	Unit* player;
	uint16_t npc;
	uint16_t message;
};

auto ReadEvent(const uint8_t* event) noexcept -> Event {
	if (event == nullptr) {
		return {};
	}
	return {d2r::At<Game*>(event, 0), d2r::At<Unit*>(event, questeng::EventUnit), d2r::At<Unit*>(event, questeng::EventPlayer),
	        d2r::At<uint16_t>(event, questeng::EventNpcClass), d2r::At<uint16_t>(event, questeng::EventMessage)};
}

auto ClassOf(Unit* unit) noexcept -> int32_t {
	return unit != nullptr ? static_cast<int32_t>(d2r::At<uint32_t>(unit, questeng::UnitClass)) : -1;
}

// Creates item `code` in the player's inventory (on the floor when it does not fit). Quality 2 = normal, item level 0 =
// the player's level, as the Act 1 catalyst.
auto Give(Game* game, Unit* player, uint32_t code, int32_t itemLevel = 0) noexcept -> bool {
	return api.createItem(game, player, code, itemLevel, 2, 1) != nullptr;
}

// ---- bonus catalysts --------------------------------------------------------------------------------------------

void GiveOnKill(const Event& e, int flagBit, bool (*rule)(int, int32_t, bool), const quests::BonusTc& tc, uint32_t code,
                const char* name) noexcept {
	if (e.game == nullptr || e.unit == nullptr || e.player == nullptr) {
		return;
	}
	void* record = Record(e.game, e.player);
	if (record == nullptr || !rule(Difficulty(e.game), ClassOf(e.unit), Bit(record, QuestFlagRecord, flagBit))) {
		return;
	}
	// Once per player on the ground like Median; the inventory item is the fallback (class not resolved).
	if (DropBonusPerPlayer(e.game, e.unit, e.player, tc) > 0 || Give(e.game, e.player, code)) {
		api.bitSet(record, QuestFlagRecord, flagBit);
	} else {
		Log(name, static_cast<int>(code), 0);
	}
}

void SummonerKillHook(void* quest, uint8_t* event) {
	GiveOnKill(ReadEvent(event), quests::FlagLearning, quests::ShouldGiveLearningOnKill, quests::TcLearning,
	           quests::CodeCatalystLearning, "learning catalyst not created, code");
	g_summonerKillOriginal(quest, event);
}

void IzualKillHook(void* quest, uint8_t* event) {
	GiveOnKill(ReadEvent(event), quests::FlagJewelcraft, quests::ShouldGiveJewelcraftOnKill, quests::TcJewelcraft,
	           quests::CodeCatalystJewelcraft, "jewelcraft catalyst not created, code");
	g_izualKillOriginal(quest, event);
}

void SummonerActivateHook(void* quest, uint8_t* event) {
	const Event e = ReadEvent(event);
	if (e.game != nullptr && e.player != nullptr) {
		void* record = Record(e.game, e.player);
		const bool flag = Bit(record, QuestFlagRecord, quests::FlagLearning);
		if (flag && quests::ShouldReissueLearning(flag, api.findItem(e.game, e.player, quests::CodeCatalystLearning) != nullptr)) {
			Give(e.game, e.player, quests::CodeCatalystLearning);
		}
	}
	g_summonerActivateOriginal(quest, event);
}

void TyraelActivateHook(void* quest, uint8_t* event) {
	const Event e = ReadEvent(event);
	if (e.game != nullptr && e.unit != nullptr && e.player != nullptr) {
		const int32_t npcClass = ClassOf(e.unit);
		void* record = Record(e.game, e.player);
		const bool flag = Bit(record, QuestFlagRecord, quests::FlagJewelcraft);
		if (flag && npcClass == quests::NpcTyraelAct4 &&
		    quests::ShouldReissueJewelcraft(npcClass, flag, api.findItem(e.game, e.player, quests::CodeCatalystJewelcraft) != nullptr)) {
			Give(e.game, e.player, quests::CodeCatalystJewelcraft);
		}
	}
	g_tyraelActivateOriginal(quest, event);
}

// ---- dialog rewards ---------------------------------------------------------------------------------------------

void Notice(Unit* player, const char* what, int count) noexcept {
	char line[96];
	clientmsg::RewardLine(what, count, line, sizeof(line));
	SendClientNotice(player, line, clientmsg::Tan);
}

void JerhynHook(void* quest, uint8_t* event) {
	const Event e = ReadEvent(event);
	if (e.game != nullptr && e.player != nullptr && e.npc == quests::NpcJerhyn && e.message == quests::MsgJerhynCrystal) {
		void* record = Record(e.game, e.player);
		if (quests::ShouldGiveCrystalJerhyn(e.npc, e.message, Bit(record, quests::QuestDuriel, quests::BitDurielTalk))) {
			if (Give(e.game, e.player, quests::CodeArcaneCrystal)) {
				Notice(e.player, "Arcane Crystal", 1);
			}
		}
	}
	g_jerhynOriginal(quest, event);
}

void CainHook(void* quest, uint8_t* event) {
	const Event e = ReadEvent(event);
	if (e.game != nullptr && e.player != nullptr && e.npc == quests::NpcCainAct3 && e.message == quests::MsgCainCrystal) {
		void* record = Record(e.game, e.player);
		if (quests::ShouldGiveCrystalCain(e.npc, e.message, Bit(record, quests::QuestKhalimsWill, quests::BitKhalimDone),
		                                  Bit(record, quests::QuestKhalimsWill, quests::BitKhalimTalk))) {
			if (Give(e.game, e.player, quests::CodeArcaneCrystal)) {
				Notice(e.player, "Arcane Crystal", 1);
			}
		}
	}
	g_cainOriginal(quest, event);
}

void OrmusHook(void* quest, uint8_t* event) {
	const Event e = ReadEvent(event);
	if (e.game != nullptr && e.player != nullptr && e.npc == quests::NpcOrmus && e.message == quests::MsgOrmusShrines) {
		void* record = Record(e.game, e.player);
		if (quests::ShouldGiveShrines(e.npc, e.message, Bit(record, quests::QuestBlade, quests::BitBladeReturned),
		                              Bit(record, quests::QuestBlade, quests::BitBladeRewarded),
		                              Bit(record, quests::QuestBlade, quests::BitBladeDone))) {
			uint32_t* seed = d2r::api.unitSeed(e.player);
			const int count = quests::ShrineCount(Difficulty(e.game));
			int given = 0;
			for (int i = 0; seed != nullptr && i < count; ++i) {
				given += Give(e.game, e.player, quests::ShrineCodes[d2r::RandomBelow(seed, quests::ShrineCodeCount)]) ? 1 : 0;
			}
			if (given > 0) {
				Notice(e.player, "Shrine", given);
			}
		}
	}
	g_ormusOriginal(quest, event);
}

void QualKehkHook(void* quest, uint8_t* event) {
	const Event e = ReadEvent(event);
	if (e.game != nullptr && e.player != nullptr && e.npc == quests::NpcQualKehk && e.message == quests::MsgQualKehkRunes) {
		void* record = Record(e.game, e.player);
		if (quests::ShouldGiveRunes(e.npc, e.message, Bit(record, quests::QuestRescue, quests::BitRescueDone),
		                            Bit(record, quests::QuestRescue, quests::BitRescueReady))) {
			const int difficulty = Difficulty(e.game);
			const int count = quests::RuneCount(Bit(record, quests::QuestRescue, quests::BitRescueTwo),
			                                    Bit(record, quests::QuestRescue, quests::BitRescueOne));
			if (difficulty >= 0 && difficulty < 3) {
				for (int i = 0; i < count; ++i) {
					Give(e.game, e.player, quests::RescueRunes[difficulty][i]);
				}
			}
		}
	}
	g_qualKehkOriginal(quest, event);
}

// ---- Baal in Hell -----------------------------------------------------------------------------------------------

int32_t RelicCallback(Game* game, Unit* player, void*) {
	void* record = Record(game, player);
	if (record == nullptr) {
		return 0;
	}
	void* room = g_unitRoom(player);
	const int area = room != nullptr ? g_roomLevelId(room) : -1;
	if (quests::ShouldGiveRelic(g_getStat(player, StatLevel, 0), area, Bit(record, quests::QuestBaal, quests::BitBaalRelic))) {
		if (Give(game, player, quests::CodeRelicOrb, quests::RelicItemLevel)) {
			api.bitSet(record, quests::QuestBaal, quests::BitBaalRelic);
		} else {
			Log("relic orb not created, area", area, 0);
		}
	}
	return 0;  // go on with the next player
}

void BaalKillHook(void* quest, uint8_t* event) {
	g_baalKillOriginal(quest, event);  // Median runs the vanilla handler first here
	const Event e = ReadEvent(event);
	if (e.game != nullptr && e.unit != nullptr && quests::IsBaalHellKill(Difficulty(e.game), ClassOf(e.unit))) {
		g_forEachPlayer(e.game, 0, nullptr, RelicCallback);
	}
}

// ---- quest 41 ---------------------------------------------------------------------------------------------------

// D2Sigma 0x9e600, hooked on the one caller of the respec bookkeeping (the respec packet and the NPC lane's respec
// action both end in it). Replaces the original: vanilla would set bit 0 and clear bit 1 (token used up).
void RespecDoneHook(Game* game, Unit* player) {
	if (game == nullptr || player == nullptr || !quests::RespecRearms(g_getStat(player, StatLevel, 0))) {
		return;
	}
	void* record = Record(game, player);
	if (record == nullptr) {
		return;
	}
	api.bitClear(record, quests::QuestRespec, quests::BitRespecUsed);
	api.bitSet(record, quests::QuestRespec, quests::BitRespecReady);
	if (!Bit(record, quests::QuestRespec, quests::BitRespecKeep)) {
		uint8_t* object = g_findQuest(game, quests::QuestRespecObject);
		if (object != nullptr) {
			object[QuestObjectActive] = 0;
		}
	}
}

}  // namespace

auto InstallQuestsAct2To5(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	g_context = context;
	d2r::Bind(base);
	questeng::Bind(base);
	g_forEachPlayer = reinterpret_cast<ForEachPlayerFn>(base + ForEachPlayerRva);
	g_findQuest = reinterpret_cast<FindQuestFn>(base + FindQuestRva);
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	g_unitRoom = reinterpret_cast<UnitRoomFn>(base + UnitRoomRva);
	g_roomLevelId = reinterpret_cast<RoomLevelIdFn>(base + RoomLevelIdRva);
	constexpr int Total = 10;
	int ok = 0;
	const auto hook = [&](uint64_t rva, const uint8_t* entry, size_t length, void* detour, void** original) {
		ok += context->InstallInlineHook(rva, entry, static_cast<uint32_t>(length), detour, original) ? 1 : 0;
	};
	hook(SummonerKillRva, SummonerKillEntry, sizeof(SummonerKillEntry), reinterpret_cast<void*>(&SummonerKillHook),
	     reinterpret_cast<void**>(&g_summonerKillOriginal));
	hook(SummonerActivateRva, SummonerActivateEntry, sizeof(SummonerActivateEntry), reinterpret_cast<void*>(&SummonerActivateHook),
	     reinterpret_cast<void**>(&g_summonerActivateOriginal));
	hook(JerhynRva, JerhynEntry, sizeof(JerhynEntry), reinterpret_cast<void*>(&JerhynHook), reinterpret_cast<void**>(&g_jerhynOriginal));
	hook(OrmusRva, OrmusEntry, sizeof(OrmusEntry), reinterpret_cast<void*>(&OrmusHook), reinterpret_cast<void**>(&g_ormusOriginal));
	hook(CainRva, CainEntry, sizeof(CainEntry), reinterpret_cast<void*>(&CainHook), reinterpret_cast<void**>(&g_cainOriginal));
	hook(IzualKillRva, IzualKillEntry, sizeof(IzualKillEntry), reinterpret_cast<void*>(&IzualKillHook),
	     reinterpret_cast<void**>(&g_izualKillOriginal));
	hook(TyraelActivateRva, TyraelActivateEntry, sizeof(TyraelActivateEntry), reinterpret_cast<void*>(&TyraelActivateHook),
	     reinterpret_cast<void**>(&g_tyraelActivateOriginal));
	hook(QualKehkRva, QualKehkEntry, sizeof(QualKehkEntry), reinterpret_cast<void*>(&QualKehkHook),
	     reinterpret_cast<void**>(&g_qualKehkOriginal));
	hook(BaalKillRva, BaalKillEntry, sizeof(BaalKillEntry), reinterpret_cast<void*>(&BaalKillHook),
	     reinterpret_cast<void**>(&g_baalKillOriginal));
	hook(RespecDoneRva, RespecDoneEntry, sizeof(RespecDoneEntry), reinterpret_cast<void*>(&RespecDoneHook),
	     reinterpret_cast<void**>(&g_respecDoneOriginal));
	std::snprintf(report, reportSize,
	              ok == Total ? "on: Summoner/Izual catalysts and re-issue, Jerhyn/Cain crystals, Ormus shrines, Qual-Kehk runes, Baal relic, respec flag"
	                          : "partial: %d of 10 hooks installed (a hook failed; the others stay)",
	              ok);
	return ok == Total;
}

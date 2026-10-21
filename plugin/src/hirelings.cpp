// Median's mercenary rules on D2R (D2Sigma D2Hirelings.cpp; docs/subsystems/npc-hirelings.md section 2).
//
// Data already carries the hireling rows (hireling.txt: nine types, four hire classes, skills, costs, equivalentcharclass
// for all four classes since txt_migrate's hireling_tables). Two pieces of code remain:
//
//   class bonuses  D2Sigma sets a list of stats on a mercenary when it is hired (0x10066900): resistances, damage,
//                  regeneration, speed, dodge, energy, masteries, by hire class and level. D2R derives a mercenary's stats
//                  from its hireling.txt row in one function, 0x544f60 (game, player, mercenary, level), which the hire,
//                  the save load and every level-up all run. It is hooked: the original runs, then the bonuses are set
//                  (they are stat *sets* in D2Sigma, so running them again after a level-up is exact, and a mercenary
//                  loaded from a save gets them too). Strength and dexterity come from the hireling.txt row, the way D2R
//                  itself computes them, not from what the mercenary wears.
//   hire gate      D2R lets a hirer through when the quest is done or the player is at least the level in byte +0x11 of
//                  the NPC table entry (Kashya: Blood Raven or level 8). Median's gate is the quest alone, so that byte is
//                  cleared for Kashya when it holds D2R's value.
//
// Entry bytes checked against the dump of build 93847; whole instructions, no rip-relative.
#include "hirelings.h"

#include "d2r.h"
#include "hirelings_logic.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t LevelStatsRva = 0x544f60;   // server: (game, player, mercenary or null, level or 0): hireling.txt stats
constexpr uint64_t FindMercRva = 0x4ff1a0;     // server: (game, player, 7, 0) -> the player's mercenary unit or null
constexpr uint64_t MercRecordRva = 0x4ff2d0;   // server: (player, unit id) -> record; the hireling id at +8
constexpr uint64_t HirelingRowRva = 0x3965d0;  // (dataFlag, dataFlag != 1, hireling id, level) -> hireling.txt row or null
constexpr uint64_t GetStatRva = 0x2f5020;      // (unit, stat, layer)
constexpr uint64_t SetStatRva = 0x2f7d10;      // (unit, stat, value, layer)
constexpr uint64_t NpcTableRva = 0x237ea60;    // 43 entries of 0x1c bytes, writable data
constexpr size_t NpcTableEntries = 43;

constexpr int16_t KashyaClass = 150;
constexpr uint8_t KashyaQuestGate = 3;  // quest 2 (Blood Raven) + 1
constexpr uint8_t KashyaLevelGate = 8;

constexpr int StatLevel = 12;
constexpr int UnitClass = 0x04;  // u32
constexpr int UnitId = 0x08;     // u32
constexpr int RowLevel = 0x1c;
constexpr int RowStr = 0x34;
constexpr int RowStrPerLevel = 0x38;
constexpr int RowDex = 0x3c;
constexpr int RowDexPerLevel = 0x40;
constexpr int MercSlot = 7;
constexpr int MercRecordHirelingId = 8;

// 0x544f60: mov rax,rsp / push rbx / push rbp / push r13 / push r15 / sub rsp,0x58
constexpr uint8_t LevelStatsEntry[] = {0x48, 0x8b, 0xc4, 0x53, 0x55, 0x41, 0x55, 0x41, 0x57, 0x48, 0x83, 0xec, 0x58};
// 0x4ff1a0: mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x18],rsi / mov [rsp+0x20],rdi / push r14
constexpr uint8_t FindMercEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
                                     0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x56};
// 0x4ff2d0: mov [rsp+0x10],rbx / push rbp / push rdi / push r14 / sub rsp,0x20 / mov ebx,edx / mov rbp,rcx
constexpr uint8_t MercRecordEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x55, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x20, 0x8b,
                                       0xda, 0x48, 0x8b, 0xe9};
// 0x3965d0: mov [rsp+8],rbx / mov [rsp+0x10],rsi / push rdi / sub rsp,0x20 / mov edi,r9d / movsxd rbx,r8d / mov esi,edx
constexpr uint8_t HirelingRowEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83,
                                        0xec, 0x20, 0x41, 0x8b, 0xf9, 0x49, 0x63, 0xd8, 0x8b, 0xf2};

using LevelStatsFn = void (*)(Game* game, Unit* player, Unit* mercenary, int32_t level);
using FindMercFn = Unit* (*)(Game* game, Unit* player, int32_t slot, int32_t any);
using MercRecordFn = const uint8_t* (*)(Unit* player, uint32_t unitId);
using HirelingRowFn = const uint8_t* (*)(uint64_t dataFlag, int32_t notClassic, int32_t hirelingId, int32_t level);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using SetStatFn = void (*)(Unit* unit, int32_t stat, int32_t value, int32_t layer);

const D2RL::PluginContext* g_context = nullptr;
LevelStatsFn g_original = nullptr;
FindMercFn g_findMerc = nullptr;
MercRecordFn g_mercRecord = nullptr;
HirelingRowFn g_row = nullptr;
GetStatFn g_getStat = nullptr;
SetStatFn g_setStat = nullptr;
int g_applied = 0;

auto Matches(uint64_t base, uint64_t rva, const uint8_t* bytes, size_t size) noexcept -> bool {
	return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes, size) == 0;
}

void Apply(Game* game, Unit* player, Unit* mercenary) noexcept {
	if (player == nullptr) {
		return;
	}
	if (mercenary == nullptr) {
		mercenary = g_findMerc(game, player, MercSlot, 0);
	}
	if (mercenary == nullptr || d2r::At<int32_t>(mercenary, d2r::UnitType) != 1) {
		return;
	}
	const hire::Kind kind = hire::KindOf(d2r::At<int32_t>(mercenary, UnitClass));
	if (kind == hire::Kind::None) {
		return;
	}
	const int32_t level = g_getStat(mercenary, StatLevel, 0);
	int32_t hirelingId = 0;
	if (const uint8_t* record = g_mercRecord(player, d2r::At<uint32_t>(mercenary, UnitId)); record != nullptr) {
		hirelingId = d2r::At<int32_t>(record, MercRecordHirelingId);
	}
	// attributes as D2R sets them from the hireling.txt row; the mercenary's own total as the fallback
	int32_t strength = g_getStat(mercenary, 0, 0);
	int32_t dexterity = g_getStat(mercenary, 2, 0);
	const uint8_t* row = g_row(d2r::DataFlag(game), d2r::DataFlag(game) != 1 ? 1 : 0, hirelingId, level);
	if (row != nullptr) {
		const int32_t delta = level - d2r::At<int32_t>(row, RowLevel);
		strength = hire::Attribute(d2r::At<int32_t>(row, RowStr), d2r::At<int32_t>(row, RowStrPerLevel), delta);
		dexterity = hire::Attribute(d2r::At<int32_t>(row, RowDex), d2r::At<int32_t>(row, RowDexPerLevel), delta);
	}
	const hire::Bonuses bonuses = hire::Compute(kind, level, hirelingId, strength, dexterity);
	for (size_t i = 0; i < bonuses.count; ++i) {
		g_setStat(mercenary, bonuses.sets[i].stat, bonuses.sets[i].value, 0);
	}
	if (g_applied < 8 && g_context != nullptr) {
		++g_applied;
		char line[160];
		std::snprintf(line, sizeof(line), "mxl-core: hirelings: class %d id %d level %d str %d dex %d: %d stats set%s",
		              d2r::At<int32_t>(mercenary, UnitClass), hirelingId, level, strength, dexterity,
		              static_cast<int>(bonuses.count), row == nullptr ? " (no hireling row, totals used)" : "");
		g_context->LogInfo(line);
	}
}

void LevelStatsHook(Game* game, Unit* player, Unit* mercenary, int32_t level) {
	g_original(game, player, mercenary, level);
	Apply(game, player, mercenary);
}

auto ClearKashyaGate(uint64_t base) noexcept -> bool {
	auto* table = reinterpret_cast<uint8_t*>(base + NpcTableRva);
	DWORD old = 0;
	if (!VirtualProtect(table, NpcTableEntries * hire::NpcRecordLayout::Size, PAGE_READWRITE, &old)) {
		return false;
	}
	const bool done = hire::ClearLevelGate(table, NpcTableEntries, KashyaClass, KashyaQuestGate, KashyaLevelGate);
	DWORD ignored = 0;
	VirtualProtect(table, NpcTableEntries * hire::NpcRecordLayout::Size, old, &ignored);
	return done;
}

}  // namespace

auto InstallHirelings(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	const uint64_t base = context->exeBase;
	if (!Matches(base, LevelStatsRva, LevelStatsEntry, sizeof(LevelStatsEntry)) ||
	    !Matches(base, FindMercRva, FindMercEntry, sizeof(FindMercEntry)) ||
	    !Matches(base, MercRecordRva, MercRecordEntry, sizeof(MercRecordEntry)) ||
	    !Matches(base, HirelingRowRva, HirelingRowEntry, sizeof(HirelingRowEntry))) {
		std::snprintf(report, reportSize, "off: entry bytes of 0x544f60 / 0x4ff1a0 / 0x4ff2d0 / 0x3965d0 differ");
		return false;
	}
	g_findMerc = reinterpret_cast<FindMercFn>(base + FindMercRva);
	g_mercRecord = reinterpret_cast<MercRecordFn>(base + MercRecordRva);
	g_row = reinterpret_cast<HirelingRowFn>(base + HirelingRowRva);
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	g_setStat = reinterpret_cast<SetStatFn>(base + SetStatRva);
	const bool hooked = context->InstallInlineHook(LevelStatsRva, LevelStatsEntry, sizeof(LevelStatsEntry),
	                                               reinterpret_cast<void*>(&LevelStatsHook),
	                                               reinterpret_cast<void**>(&g_original));
	const bool gate = ClearKashyaGate(base);
	std::snprintf(report, reportSize, "%s: class bonuses on 0x544f60 %s, Kashya level gate %s",
	              hooked ? "on" : "partial", hooked ? "hooked" : "NOT hooked", gate ? "cleared" : "left (not D2R's value)");
	return hooked;
}

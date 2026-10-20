// Horadric Cube recipe conditions as D2Sigma checks them (docs/plugin-spec.md, "Cube conditions").
//
// D2R's recipe search (0x529090, D2Game PlrTrade.cpp) walks the cubemain records and passes a record's player
// condition only for ops 1-14; every other op counts as passed. D2Sigma's checker (D2Craft.cpp 0x10059d00) also knows
// ops 29-42, tests a second condition from cubemainext and wants the gold cost (cost x 100) in the player's hands.
// tools/cube_tables.py writes those into mxl_cube.h (recipes with such a condition or a cost, D2R ids). The search
// hook remembers game and player; the input matcher hook (0x529d50, called for inputs 0..6, all must match) answers
// "no match" for input 0 when the recipe's conditions fail, so the recipe is not found.
//
// Needs D2R's entry bytes of both functions (TODO below); until they are recorded the hooks refuse to install.
#include "cube_conditions.h"

#include "d2r.h"
#include "mxl_cube.h"

#define NOMINMAX
#include <windows.h>

#include <bitset>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <unordered_map>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t SearchRva = 0x529090;       // (int* result, game, player, out): the recipe search
constexpr uint64_t MatchRva = 0x529d50;        // (game, inventory, record, input#): non-zero when the input matches
constexpr uint64_t RecordRva = 0x3e3f60;       // (dataFlag, index) -> cubemain record (assumed; checked at run time)
constexpr uint64_t GetStatRva = 0x2f5020;      // (unit, stat, layer): total value
constexpr uint64_t BaseStatRva = 0x2f48c0;     // (unit, stat, layer): base value (stat() mode 1)
constexpr uint64_t HasStateRva = 0x3351b0;     // (unit, state) -> non-zero when set
constexpr uint64_t UnitRoomRva = 0x34b440;     // (unit) -> room
constexpr uint64_t RoomLevelIdRva = 0x2efc10;  // (room) -> level id
constexpr uint64_t RoomIsTownRva = 0x2f0750;   // (room) -> non-zero in town
constexpr uint64_t CalcCodeRva = 0x33b3b0;     // (dataFlag, unit, code, skillId, level): skill calc code
constexpr uint64_t D2RCodeBlvl = 41;           // base level of the skill

constexpr int RecordSize = 0x16c;

// TODO(D2R bytes): the first instructions of 0x529090 and 0x529d50 are not recorded anywhere (the D2R Ghidra project
// needs a memory dump of the running game; the on-disk D2R.exe is not the dumped image). Fill in the bytes (at least
// as many as the hook's patch needs, whole instructions) and the lengths below; the hooks stay off while a length is
// 0. Read them with tools/dump_image.py or the D2R Ghidra project: `Disasm.java 0x529090 0x529d50`.
constexpr uint8_t SearchEntry[] = {0};
constexpr size_t SearchEntryLen = 0;
constexpr uint8_t MatchEntry[] = {0};
constexpr size_t MatchEntryLen = 0;

constexpr int32_t StatStrength = 0, StatEnergy = 1, StatGold = 14, StatGoldBank = 15;

using SearchFn = uint64_t (*)(int32_t* result, Game* game, Unit* player, void* out);
using MatchFn = uint64_t (*)(Game* game, void* inventory, const uint8_t* record, int32_t input);
using RecordFn = const uint8_t* (*)(uint32_t dataFlag, int32_t index);
using StatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using HasStateFn = uint64_t (*)(Unit* unit, int32_t state);
using UnitRoomFn = void* (*)(Unit* unit);
using RoomLevelIdFn = int32_t (*)(void* room);
using RoomIsTownFn = int64_t (*)(void* room);
using CalcCodeFn = uint64_t (*)(uint64_t dataFlag, void* unit, uint64_t code, int32_t skillId, uint32_t level);

SearchFn g_searchOriginal = nullptr;
MatchFn g_matchOriginal = nullptr;
RecordFn g_record = nullptr;
StatFn g_getStat = nullptr;
StatFn g_baseStat = nullptr;
HasStateFn g_hasState = nullptr;
UnitRoomFn g_unitRoom = nullptr;
RoomLevelIdFn g_roomLevelId = nullptr;
RoomIsTownFn g_roomIsTown = nullptr;
CalcCodeFn g_calcCode = nullptr;
const D2RL::PluginContext* g_context = nullptr;
bool g_warned = false;

struct Search {
	Game* game;
	Unit* player;
};
thread_local Search g_search {};

std::unordered_map<const void*, std::bitset<256>> g_playerFlags;
std::unordered_map<const void*, std::bitset<256>> g_gameFlags;

auto FlagSet(const std::unordered_map<const void*, std::bitset<256>>& map, const void* key, int32_t flag) noexcept
	-> bool {
	const auto it = map.find(key);
	return flag >= 0 && flag < 256 && it != map.end() && it->second.test(static_cast<size_t>(flag));
}

auto AreaOf(Unit* player, void** roomOut) noexcept -> int32_t {
	void* room = g_unitRoom(player);
	if (roomOut != nullptr) {
		*roomOut = room;
	}
	return room == nullptr ? -1 : g_roomLevelId(room);
}

// Skill level for ops 30/31. D2Sigma asks D2Common 10306 with and without item bonuses; D2R's base level calc code is
// used for both (TODO: ops 30 should add +skills; 18 recipes, all in cubemainext).
auto SkillLevel(Unit* player, int32_t skill) noexcept -> int32_t {
	if (d2r::api.getSkillRecord(d2r::DataFlag(g_search.game), skill) == nullptr) {
		return 0;
	}
	return static_cast<int32_t>(g_calcCode(d2r::DataFlag(g_search.game), player, D2RCodeBlvl, skill, 1));
}

// One condition of D2Sigma's checker (D2Craft.cpp 0x10059d00): true when the player passes.
auto Check(const mxl_cube::Cond& c, Unit* player) noexcept -> bool {
	switch (c.op) {
	case 3: return g_getStat(player, c.param, c.layer) >= c.value;
	case 4: return g_getStat(player, c.param, c.layer) <= c.value;
	case 5: return g_getStat(player, c.param, c.layer) != c.value;
	case 6: return g_getStat(player, c.param, c.layer) == c.value;
	case 7: return g_baseStat(player, c.param, c.layer) >= c.value;
	case 8: return g_baseStat(player, c.param, c.layer) <= c.value;
	case 9: return g_baseStat(player, c.param, c.layer) != c.value;
	case 10: return g_baseStat(player, c.param, c.layer) == c.value;
	case 11: case 12: case 13: case 14: {  // bonus part: total - base
		const int32_t layer = c.layer & 0xffff;
		const int32_t bonus = g_getStat(player, c.param, layer) - g_baseStat(player, c.param, layer);
		return c.op == 11 ? bonus >= c.value : c.op == 12 ? bonus <= c.value : c.op == 13 ? bonus != c.value
		                                                                                    : bonus == c.value;
	}
	case 29: {  // current month (1-12)
		const std::time_t now = std::time(nullptr);
		std::tm local {};
		return localtime_s(&local, &now) == 0 && local.tm_mon + 1 == c.param;
	}
	case 30: case 31: return SkillLevel(player, c.param) >= c.value;
	case 32: return g_hasState(player, c.param) != 0;
	case 33: return g_hasState(player, c.param) == 0;
	case 34: return AreaOf(player, nullptr) == c.param;
	case 35: {  // act of the area
		const int32_t area = AreaOf(player, nullptr);
		return area >= 0 && area < mxl_cube::ActCount && mxl_cube::ActOfArea[area] == c.param;
	}
	case 36: {  // in a town
		void* room = nullptr;
		AreaOf(player, &room);
		return room != nullptr && g_roomIsTown(room) != 0;
	}
	case 37: {  // value% chance, rolled on the player's seed
		const int32_t chance = c.value < 0 ? 0 : c.value > 100 ? 100 : c.value;
		uint32_t* seed = d2r::api.unitSeed(player);
		return seed != nullptr && d2r::RandomBelow(seed, 100) < chance;
	}
	case 38: return FlagSet(g_playerFlags, player, c.param);
	case 39: return !FlagSet(g_playerFlags, player, c.param);
	case 40: return FlagSet(g_gameFlags, g_search.game, c.param);
	case 41: return !FlagSet(g_gameFlags, g_search.game, c.param);
	case 42: return static_cast<int64_t>(g_getStat(player, StatStrength, 0)) + g_getStat(player, StatEnergy, 0) > 9000;
	default: return true;  // ops D2R handles itself
	}
}

auto Passes(const mxl_cube::Entry& e, Unit* player) noexcept -> bool {
	for (int i = 0; i < e.count; ++i) {
		if (!Check(e.cond[i], player)) {
			return false;
		}
	}
	// the gold cost: carried plus banked gold must cover it (D2Sigma 0x100be8a0; the payment is not ported)
	return e.cost <= 0 || static_cast<int64_t>(g_getStat(player, StatGold, 0)) + g_getStat(player, StatGoldBank, 0) >=
	                          static_cast<int64_t>(e.cost) * 100;
}

auto Find(int32_t recipe) noexcept -> const mxl_cube::Entry* {
	int lo = 0, hi = static_cast<int>(sizeof(mxl_cube::Entries) / sizeof(mxl_cube::Entries[0])) - 1;
	while (lo <= hi) {
		const int mid = (lo + hi) / 2;
		const int32_t r = mxl_cube::Entries[mid].recipe;
		if (r == recipe) {
			return &mxl_cube::Entries[mid];
		}
		if (r < recipe) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return nullptr;
}

// Record -> recipe index, checked against D2R's own getter; -1 when the guess about the getter is wrong.
auto RecipeIndex(Game* game, const uint8_t* record) noexcept -> int32_t {
	const uint32_t flag = d2r::DataFlag(game);
	const uint8_t* first = g_record(flag, 0);
	if (first == nullptr || record < first) {
		return -1;
	}
	const auto delta = static_cast<uint64_t>(record - first);
	const auto index = static_cast<int32_t>(delta / RecordSize);
	if (delta % RecordSize != 0 || index >= mxl_cube::RecipeCount || g_record(flag, index) != record) {
		return -1;
	}
	return index;
}

uint64_t SearchHook(int32_t* result, Game* game, Unit* player, void* out) {
	const Search before = g_search;
	g_search = {game, player};
	const uint64_t r = g_searchOriginal(result, game, player, out);
	g_search = before;
	return r;
}

uint64_t MatchHook(Game* game, void* inventory, const uint8_t* record, int32_t input) {
	if (input == 0 && g_search.player != nullptr && g_search.game == game && record != nullptr) {
		const int32_t index = RecipeIndex(game, record);
		if (index < 0) {
			if (!g_warned && g_context != nullptr) {
				g_warned = true;
				g_context->LogInfo("mxl-core: cube conditions: record pointer is not 0x3e3f60(dataFlag, index) + n*0x16c; "
				                   "conditions not applied");
			}
		} else if (const mxl_cube::Entry* e = Find(index); e != nullptr && !Passes(*e, g_search.player)) {
			return 0;
		}
	}
	return g_matchOriginal(game, inventory, record, input);
}

}  // namespace

void SetCraftFlag(const void* player, int flag, bool game, bool on) noexcept {
	if (flag < 0 || flag > 255) {
		return;
	}
	(game ? g_gameFlags : g_playerFlags)[player].set(static_cast<size_t>(flag), on);
}

auto InstallCubeConditions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	if (SearchEntryLen == 0 || MatchEntryLen == 0) {
		std::snprintf(report, reportSize,
		              "off: D2R's entry bytes of 0x529090 / 0x529d50 are not recorded yet (cube_conditions.cpp TODO); "
		              "%d recipes carry conditions or a gold cost",
		              static_cast<int>(sizeof(mxl_cube::Entries) / sizeof(mxl_cube::Entries[0])));
		return false;
	}
	const uint64_t base = context->exeBase;
	g_context = context;
	g_record = reinterpret_cast<RecordFn>(base + RecordRva);
	g_getStat = reinterpret_cast<StatFn>(base + GetStatRva);
	g_baseStat = reinterpret_cast<StatFn>(base + BaseStatRva);
	g_hasState = reinterpret_cast<HasStateFn>(base + HasStateRva);
	g_unitRoom = reinterpret_cast<UnitRoomFn>(base + UnitRoomRva);
	g_roomLevelId = reinterpret_cast<RoomLevelIdFn>(base + RoomLevelIdRva);
	g_roomIsTown = reinterpret_cast<RoomIsTownFn>(base + RoomIsTownRva);
	g_calcCode = reinterpret_cast<CalcCodeFn>(base + CalcCodeRva);
	const bool ok =
		context->InstallInlineHook(SearchRva, SearchEntry, SearchEntryLen, reinterpret_cast<void*>(&SearchHook),
	                               reinterpret_cast<void**>(&g_searchOriginal)) &&
		context->InstallInlineHook(MatchRva, MatchEntry, MatchEntryLen, reinterpret_cast<void*>(&MatchHook),
	                               reinterpret_cast<void**>(&g_matchOriginal));
	std::snprintf(report, reportSize, ok ? "on: %d recipes (ops 29-42, second condition, gold cost)"
	                                     : "off: cube hook failed",
	              static_cast<int>(sizeof(mxl_cube::Entries) / sizeof(mxl_cube::Entries[0])));
	return ok;
}

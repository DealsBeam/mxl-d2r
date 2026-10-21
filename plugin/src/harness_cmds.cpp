// Gameplay commands for the test harness (docs/testing.md "Harness commands"): they call the game's own functions
// directly, no simulated mouse or keyboard, so an unattended background run can kill a boss, talk to an NPC, set a
// quest bit, create an item with chosen stats, craft in the cube and read the result.
//
// Only active while mxl-testharness.txt sits next to the DLL (testharness.cpp installs it). A line "h <verb> ..." coming
// through the harness console mailbox is routed here on the UI thread. Server-side verbs are queued and run on the game
// thread inside the frame hook (goblins.cpp calls HarnessGameFrame after 0x406550); client-side verbs (menu) run on the
// UI thread. Output goes to mxl-testharness-console.txt as "h: ..." lines, the last one of a command is
// "h: <verb> OK" or "h: <verb> FAIL <reason>".
#include "harness_cmds_logic.h"

#include "craft_flags.h"
#include "d2r.h"
#include "dungeon_lockout.h"
#include "goblins.h"
#include "quests_act1.h"
#include "testharness.h"

#define NOMINMAX
#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

uint64_t g_base = 0;
volatile LONG g_ready = 0;
volatile LONG g_frames = 0, g_lastGame = 0;

// --- addresses (docs/d2r-addresses.md; the ones not listed there were read from the Ghidra decompile this session) ---
constexpr uint64_t RvaGetStat = 0x2f5020;       // (unit, stat, layer)
constexpr uint64_t RvaSetStat = 0x2f7d10;       // (unit, stat, value, layer)
constexpr uint64_t RvaPlayerData = 0x34b240;    // (player) -> player data; +0x40 + difficulty * 8 = quest record
constexpr uint64_t RvaQuestTest = 0x325c50;     // (record, quest, bit)
constexpr uint64_t RvaQuestSet = 0x325c00;
constexpr uint64_t RvaQuestClear = 0x325990;
constexpr uint64_t RvaUnitId = 0x34a330;        // (unit, file, line)
constexpr uint64_t RvaUnitClass = 0x349860;     // (unit, file, line)
constexpr uint64_t RvaUnitMode = 0x34ab60;
constexpr uint64_t RvaUnitRoom = 0x34b440;
constexpr uint64_t RvaRoomLevel = 0x2efc10;
constexpr uint64_t RvaClientUnit = 0x484c90;    // (client, 0) -> player unit
constexpr uint64_t RvaNpcRecord = 0x3971a0;     // (dataFlag, class) -> NPC table row or null
constexpr uint64_t RvaDistance = 0x325140;      // (unit, unit) -> distance
constexpr uint64_t RvaKill = 0x44db20;          // (game, victim, killer unit, 1): the unit death function
constexpr uint64_t RvaSpawn = 0x4929e0;         // (game, room, x, y, class, 1, radius, 0, 0)
constexpr uint64_t RvaInRoom = 0x38dfb0;        // (room, x, y)
constexpr uint64_t RvaTravel = 0x4f2e40;        // (game, player, level, 0): the NPC travel / waypoint warp
constexpr uint64_t RvaMove = 0x491ca0;          // (game, unit, 0, x, y, 0, 0)
constexpr uint64_t RvaFreeSpot = 0x517f90;
constexpr uint64_t RvaPacketTable = 0x1d2a790;  // server packet handlers: (game, player, packet, size)
constexpr uint64_t RvaInventory = 0x34a360;     // (unit, file, line)
constexpr uint64_t RvaInvFirst = 0x388c10;
constexpr uint64_t RvaInvNext = 0x38aba0;
constexpr uint64_t RvaItemPage = 0x36cfe0;      // (item) -> page byte (0 inventory, 3 cube, 4 stash)
constexpr uint64_t RvaItemLocation = 0x382d20;  // (int32 location[4], item): unit id, page, packed x/y
constexpr uint64_t RvaItemRecord = 0x314110;    // (dataFlag, class id)
constexpr uint64_t RvaItemByCode = 0x314070;    // (dataFlag, code, &class id) -> record
constexpr uint64_t RvaDefaultIlvl = 0x43e8a0;   // (player, 0)
constexpr uint64_t RvaMakeItem = 0x43d530;
constexpr uint64_t RvaMaxDurability = 0x2f4b60;
constexpr uint64_t RvaSetStatItem = 0x2f7940;   // (item, stat, value, layer)
constexpr uint64_t RvaLocInit = 0x382e10;       // (loc*, page, packed xy)
constexpr uint64_t RvaInvRecord = 0x34a410;     // (player, page kind, expansion)
constexpr uint64_t RvaFindSpot = 0x3865b0;      // (inventory, item, record, &x, &y, 0) -> found
constexpr uint64_t RvaPlace = 0x471500;         // (player, loc*, ids*, count, 0)
constexpr uint64_t RvaSendItem = 0x535f60;
constexpr uint64_t RvaIdentifyHook = 0x46e8c0;  // (game, player, item, 0)
constexpr uint64_t RvaAfterPlace = 0x3e2b61c;   // (item, player) -> nonzero: re-check equipment
constexpr uint64_t RvaRecheck = 0x470c90;       // (game, player, 0, 0)
constexpr uint64_t RvaDropFlags = 0x36d8f0;     // (item, flags, 0)
constexpr uint64_t RvaGround1 = 0x36de20, RvaGround2 = 0x36d730, RvaGround3 = 0x34e5f0, RvaGround4 = 0x34e210, RvaGround5 = 0x34e280;
constexpr uint64_t RvaUnitXY = 0x34a040;        // (unit, int32 xy[2])
constexpr uint64_t RvaGroundSpot = 0x43de10;    // (game, room, xy, &out, 1)
constexpr uint64_t RvaGroundDrop = 0x43dd00;    // (game, player, item, room, x, y)
constexpr uint64_t RvaSearch = 0x529090;        // (int* result, game, player, out)
constexpr uint64_t RvaCubeRecord = 0x3e3f60;    // (dataFlag, index)
constexpr uint64_t RvaCubeExec = 0x5269c0;      // (game, player, record, ctx)
constexpr uint64_t RvaUseCheck = 0x5817f0;      // (game, player, item, target)
constexpr uint64_t RvaUseItem = 0x581680;
constexpr uint64_t RvaSpellTable = 0x236fe30;   // 12 bytes per pSpell, flag (1 = not used up) at +8
constexpr uint64_t RvaSetByte5c = 0x36dea0;
constexpr uint64_t RvaFreeItem = 0x43ec10;      // (game, item)
constexpr uint64_t RvaInvChange = 0x472590;     // (player, adds, removes, addIds, removeIds, nAdds, nRemoves, 0)
constexpr uint64_t RvaStatsChanged = 0x34dbe0;  // (unit, 1)
constexpr uint64_t RvaUnitPosition = 0x34a110;
constexpr uint64_t RvaItemByte54 = 0x36ca10;
constexpr uint64_t RvaMoveBlocked = 0x350a20;   // (unit, x, y, mask)
// client
constexpr uint64_t RvaClientFind = 0x9a5d0;     // (id, type) -> client unit
constexpr uint64_t RvaMenuRecord = 0x113990;    // (npc unit) -> row index in the NPC menu table
constexpr uint64_t RvaMenuOpenFlag = 0x2a486a4, RvaMenuNpcValid = 0x2a48760, RvaMenuNpcId = 0x2a4875c, RvaMenuNpcClass = 0x2a48764;
constexpr uint64_t RvaMenuTable = 0x2a48830, RvaMenuCount = 0x2a48838;
// offsets
constexpr int GameClients = 0x138, ClientNext = 0x628, ClientState = 0x04, GameFrame = 0x170, GameDifficulty = 0x104;
constexpr int HashMonsters = 0x4c6, HashItems = 0x5c6, HashObjects = 0x546;  // game + (base + (id & 0x7f)) * 8
constexpr int UnitNextInHash = 0x158, UnitClass = 0x04, UnitId = 0x08, UnitMode = 0x0c;
constexpr int StatLife = 6, StatMana = 8, StatLevel = 12, StatGold = 14, StatGoldBank = 15, StatDurability = 0x48;

using GetStatFn = int32_t (*)(Unit*, int32_t, int32_t);
using SetStatFn = void (*)(Unit*, int32_t, int32_t, int32_t);
using PlayerDataFn = uint8_t* (*)(Unit*);
using QuestFn = int32_t (*)(void*, int32_t, int32_t);
using UnitIdFn = uint32_t (*)(Unit*, const char*, int32_t);
using UnitClassFn = int32_t (*)(Unit*, const char*, int32_t);
using UnitModeFn = int32_t (*)(Unit*);
using UnitRoomFn = void* (*)(Unit*);
using RoomLevelFn = int32_t (*)(void*);
using ClientUnitFn = Unit* (*)(void*, int32_t);
using NpcRecordFn = void* (*)(uint32_t, int32_t);
using DistanceFn = uint32_t (*)(Unit*, Unit*);
using KillFn = void (*)(Game*, Unit*, Unit*, int32_t);
using SpawnFn = Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint16_t);
using InRoomFn = void* (*)(void*, int32_t, int32_t);
using TravelFn = void (*)(Game*, Unit*, int32_t, int32_t);
using MoveFn = int64_t (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t);
using FreeSpotFn = void (*)(Game*, void*, int32_t*, int32_t, int32_t, void**, int64_t, int32_t);
using PacketFn = void (*)(Game*, Unit*, const uint8_t*, int32_t);
using InventoryFn = void* (*)(Unit*, const char*, int32_t);
using InvFirstFn = Unit* (*)(void*);
using InvNextFn = Unit* (*)(Unit*);
using ItemByteFn = uint8_t (*)(Unit*);
using ItemLocationFn = int32_t* (*)(int32_t*, Unit*);
using ItemRecordFn = const uint8_t* (*)(uint32_t, int32_t);
using ItemByCodeFn = const uint8_t* (*)(uint32_t, uint32_t, int32_t*);
using DefaultIlvlFn = int32_t (*)(Unit*, int32_t);
using MakeItemFn = Unit* (*)(Unit*, int32_t, Game*, int32_t, int64_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
using ItemStatFn = int32_t (*)(Unit*);
using SetItemStatFn = void (*)(Unit*, int32_t, int32_t, int32_t);
using LocInitFn = int32_t* (*)(int32_t*, int32_t, int32_t);
using InvRecordFn = int32_t (*)(Unit*, int32_t, int32_t);
using FindSpotFn = int32_t (*)(void*, Unit*, int32_t, uint16_t*, uint16_t*, int64_t);
using PlaceFn = uint8_t (*)(Unit*, int32_t*, uint32_t*, uint64_t, int64_t);
using SendItemFn = void (*)(Game*, Unit*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
using IdentifyHookFn = void (*)(Game*, Unit*, Unit*, int32_t);
using AfterPlaceFn = int32_t (*)(Unit*, Unit*);
using RecheckFn = void (*)(Game*, Unit*, int32_t, int32_t);
using DropFlagsFn = void (*)(Unit*, int32_t, int32_t);
using ByteSetFn = void (*)(Unit*, int32_t);
using UnitXYFn = void (*)(Unit*, int32_t*);
using GroundSpotFn = void* (*)(Game*, void*, int32_t*, int32_t*, int32_t);
using GroundDropFn = void (*)(Game*, Unit*, Unit*, void*, int32_t, int32_t);
using SearchFn = void (*)(int32_t*, Game*, Unit*, void*);
using CubeRecordFn = const uint8_t* (*)(uint32_t, int32_t);
using CubeExecFn = Unit* (*)(Game*, Unit*, const uint8_t*, void*);
using UseFn = uint64_t (*)(Game*, Unit*, Unit*, Unit*);
using FreeItemFn = void (*)(Game*, Unit*);
using InvChangeFn = uint8_t (*)(Unit*, int32_t*, int32_t*, uint32_t*, uint32_t*, uint64_t, uint64_t, uint8_t);
using StatsChangedFn = void (*)(Unit*, int32_t);
using UnitPositionFn = uint32_t (*)(Unit*);
using BlockedFn = int32_t (*)(Unit*, int32_t, int32_t, int32_t);
using ClientFindFn = Unit* (*)(uint32_t, int32_t);
using MenuRecordFn = int32_t (*)(Unit*);
using MenuHandlerFn = uint8_t (*)(uint8_t);

template <typename F>
auto Fn(uint64_t rva) noexcept -> F {
	return reinterpret_cast<F>(g_base + rva);
}

// --- output -----------------------------------------------------------------------------------------------------
void Out(const char* fmt, ...) noexcept {
	char body[400];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(body, sizeof(body), fmt, ap);
	va_end(ap);
	char line[420];
	std::snprintf(line, sizeof(line), "h: %s", body);
	HarnessEcho(line);
}

struct Ctx {
	Game* game = nullptr;
	Unit* player = nullptr;
	uint32_t dataFlag = 2;
	char verb[24] = "";
	char why[160] = "";
};

auto Fail(Ctx* c, const char* fmt, ...) noexcept -> bool {
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(c->why, sizeof(c->why), fmt, ap);
	va_end(ap);
	return false;
}

auto TypeOf(const Unit* u) noexcept -> int32_t { return d2r::At<int32_t>(u, d2r::UnitType); }

auto FindPlayer(Game* game) noexcept -> Unit* {
	int guard = 0;
	for (auto* client = d2r::At<uint8_t*>(game, GameClients); client != nullptr && guard < 64;
	     client = d2r::At<uint8_t*>(client, ClientNext), ++guard) {
		if (d2r::At<int32_t>(client, ClientState) != 4) {
			continue;
		}
		Unit* unit = Fn<ClientUnitFn>(RvaClientUnit)(client, 0);
		if (unit != nullptr && TypeOf(unit) == 0) {
			return unit;
		}
	}
	return nullptr;
}

auto QuestRecord(Ctx* c) noexcept -> void* {
	uint8_t* data = Fn<PlayerDataFn>(RvaPlayerData)(c->player);
	if (data == nullptr) {
		return nullptr;
	}
	return d2r::At<void*>(data, 0x40 + static_cast<int>(d2r::At<uint8_t>(c->game, GameDifficulty)) * 8);
}

auto Stat(Unit* u, int stat, int layer = 0) noexcept -> int32_t { return Fn<GetStatFn>(RvaGetStat)(u, stat, layer); }

auto IdOf(Unit* u) noexcept -> uint32_t { return d2r::At<uint32_t>(u, UnitId); }
auto ClassOf(Unit* u) noexcept -> int32_t { return d2r::At<int32_t>(u, UnitClass); }
auto ModeOf(Unit* u) noexcept -> int32_t { return d2r::At<int32_t>(u, UnitMode); }
auto IsAlive(Unit* u) noexcept -> bool {
	const int32_t m = ModeOf(u);
	return m != 0 && m != 12;
}

auto DistTo(Unit* a, Unit* b) noexcept -> double {
	int32_t ax = 0, ay = 0, bx = 0, by = 0;
	if (!d2r::UnitXY(a, &ax, &ay) || !d2r::UnitXY(b, &bx, &by)) {
		return 1e9;
	}
	return std::sqrt(static_cast<double>(ax - bx) * (ax - bx) + static_cast<double>(ay - by) * (ay - by));
}

// Calls f(unit) for every unit in one of the game's hash tables (type 1 monsters, 2 objects, 4 items).
template <typename F>
void EachUnit(Game* game, int hashBase, F f) noexcept {
	int guard = 0;
	for (int b = 0; b < 128; ++b) {
		for (auto* u = d2r::At<Unit*>(game, (hashBase + b) * 8); u != nullptr && guard < 20000;
		     u = d2r::At<Unit*>(u, UnitNextInHash), ++guard) {
			f(u);
		}
	}
}

auto NearestMonster(Ctx* c, long long cls, bool npcOnly) noexcept -> Unit* {
	Unit* best = nullptr;
	double bestD = 1e18;
	EachUnit(c->game, HashMonsters, [&](Unit* u) {
		if (TypeOf(u) != 1 || !IsAlive(u) || (cls >= 0 && ClassOf(u) != cls)) {
			return;
		}
		if (npcOnly && Fn<NpcRecordFn>(RvaNpcRecord)(c->dataFlag, ClassOf(u)) == nullptr) {
			return;
		}
		const double d = DistTo(c->player, u);
		if (d < bestD) {
			bestD = d;
			best = u;
		}
	});
	return best;
}

auto FindMonsterById(Ctx* c, uint32_t id) noexcept -> Unit* {
	Unit* found = nullptr;
	EachUnit(c->game, HashMonsters, [&](Unit* u) {
		if (found == nullptr && TypeOf(u) == 1 && IdOf(u) == id) {
			found = u;
		}
	});
	return found;
}

// Items of the player's inventory (all pages: backpack, cube, stash, equipped).
template <typename F>
void EachItem(Ctx* c, F f) noexcept {
	void* inv = Fn<InventoryFn>(RvaInventory)(c->player, "harness", 0);
	if (inv == nullptr) {
		return;
	}
	int guard = 0;
	Unit* item = Fn<InvFirstFn>(RvaInvFirst)(inv);
	while (item != nullptr && guard++ < 2000) {
		Unit* next = Fn<InvNextFn>(RvaInvNext)(item);
		if (TypeOf(item) == 4) {
			f(item);
		}
		item = next;
	}
}

auto ItemCode(Ctx* c, Unit* item, char out[5]) noexcept -> void {
	const int32_t cls = Fn<UnitClassFn>(RvaUnitClass)(item, "harness", 0);
	const uint8_t* rec = Fn<ItemRecordFn>(RvaItemRecord)(c->dataFlag, cls);
	hcmd::DwordToCode(rec != nullptr ? d2r::At<uint32_t>(rec, 0x80) : 0x3f3f3f3fU, out);
}

void DescribeItem(Ctx* c, Unit* item) noexcept {
	char code[5];
	ItemCode(c, item, code);
	int32_t loc[8] = {};
	Fn<ItemLocationFn>(RvaItemLocation)(loc, item);
	const auto* data = Fn<uint8_t* (*)(Unit*)>(0x34a500)(item);  // item data: +0 quality, +0x34 unique index
	Out("item id=%u code=%s class=%d page=%d x=%d y=%d qty=%d stat532=%d quality=%d unique=%d", IdOf(item), code, ClassOf(item),
	    static_cast<int>(Fn<ItemByteFn>(RvaItemPage)(item)), loc[2] & 0xffff, (loc[2] >> 16) & 0xffff, Stat(item, 70), Stat(item, 532),
	    data != nullptr ? d2r::At<int32_t>(data, 0) : -1, data != nullptr ? d2r::At<int32_t>(data, 0x34) : -1);
}

auto FindItemByCode(Ctx* c, uint32_t code, int page) noexcept -> Unit* {
	Unit* found = nullptr;
	EachItem(c, [&](Unit* it) {
		if (found != nullptr) {
			return;
		}
		const uint8_t* rec = Fn<ItemRecordFn>(RvaItemRecord)(c->dataFlag, Fn<UnitClassFn>(RvaUnitClass)(it, "harness", 0));
		if (rec != nullptr && d2r::At<uint32_t>(rec, 0x80) == code &&
		    (page < 0 || Fn<ItemByteFn>(RvaItemPage)(it) == page)) {
			found = it;
		}
	});
	return found;
}

// --- verbs ------------------------------------------------------------------------------------------------------

auto CmdState(Ctx* c, const hcmd::Tokens&) noexcept -> bool {
	Unit* p = c->player;
	void* room = Fn<UnitRoomFn>(RvaUnitRoom)(p);
	int32_t x = 0, y = 0;
	d2r::UnitXY(p, &x, &y);
	Out("player id=%u class=%d level=%d life=%d/%d mana=%d area=%d x=%d y=%d mode=%d gold=%d bank=%d difficulty=%d frame=%u",
	    IdOf(p), ClassOf(p), Stat(p, StatLevel), Stat(p, StatLife) >> 8, Stat(p, 7) >> 8, Stat(p, StatMana) >> 8,
	    room != nullptr ? Fn<RoomLevelFn>(RvaRoomLevel)(room) : -1, x, y, ModeOf(p), Stat(p, StatGold), Stat(p, StatGoldBank),
	    static_cast<int>(d2r::At<uint8_t>(c->game, GameDifficulty)), d2r::At<uint32_t>(c->game, GameFrame));
	int items = 0;
	EachItem(c, [&](Unit* it) {
		++items;
		DescribeItem(c, it);
	});
	Out("items %d", items);
	int mons = 0;
	EachUnit(c->game, HashMonsters, [&](Unit* u) {
		if (TypeOf(u) == 1 && IsAlive(u) && DistTo(p, u) < 40 && mons < 12) {
			++mons;
			Out("mon id=%u class=%d mode=%d dist=%.0f npc=%d", IdOf(u), ClassOf(u), ModeOf(u), DistTo(p, u),
			    Fn<NpcRecordFn>(RvaNpcRecord)(c->dataFlag, ClassOf(u)) != nullptr ? 1 : 0);
		}
	});
	int objs = 0;
	EachUnit(c->game, HashObjects, [&](Unit* u) {
		if (TypeOf(u) == 2 && DistTo(p, u) < 30 && objs < 40) {
			++objs;
			int32_t ox = 0, oy = 0;
			d2r::UnitXY(u, &ox, &oy);
			Out("obj id=%u class=%d mode=%d dist=%.0f at %d,%d", IdOf(u), ClassOf(u), ModeOf(u), DistTo(p, u), ox, oy);
		}
	});
	char goblins[160] = "";
	GoblinsStatus(goblins, sizeof(goblins));
	int refused = 0, started = 0;
	DungeonLockoutCounts(&refused, &started);
	Out("plugin goblins: %s | lockout refused=%d clocks=%d", goblins, refused, started);
	const auto* b = reinterpret_cast<const uint8_t*>(g_base);
	Out("npcmenu open=%u npc=%u class=%u rows=%u", *reinterpret_cast<const uint32_t*>(b + RvaMenuOpenFlag),
	    *reinterpret_cast<const uint32_t*>(b + RvaMenuNpcId), static_cast<unsigned>(*reinterpret_cast<const uint16_t*>(b + RvaMenuNpcClass)),
	    *reinterpret_cast<const uint32_t*>(b + RvaMenuCount));
	return true;
}

auto CmdQuest(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	long long q = 0, bit = 0;
	if (t.n < 3 || !hcmd::ParseInt(t.tok[1], &q)) {
		return Fail(c, "usage: quest <id> <bit>|dump set|clear|get");
	}
	void* rec = QuestRecord(c);
	if (rec == nullptr) {
		return Fail(c, "no quest record");
	}
	if (hcmd::LowerEq(t.tok[2], "dump")) {
		char bits[17] = {};
		for (int i = 0; i < 16; ++i) {
			bits[i] = Fn<QuestFn>(RvaQuestTest)(rec, static_cast<int>(q), i) != 0 ? '1' : '0';
		}
		Out("quest %lld bits0-15 %s", q, bits);
		return true;
	}
	if (t.n < 4 || !hcmd::ParseInt(t.tok[2], &bit) || bit < 0 || bit > 15) {
		return Fail(c, "usage: quest <id> <bit 0-15> set|clear|get");
	}
	if (hcmd::LowerEq(t.tok[3], "set")) {
		Fn<QuestFn>(RvaQuestSet)(rec, static_cast<int>(q), static_cast<int>(bit));
	} else if (hcmd::LowerEq(t.tok[3], "clear")) {
		Fn<QuestFn>(RvaQuestClear)(rec, static_cast<int>(q), static_cast<int>(bit));
	} else if (!hcmd::LowerEq(t.tok[3], "get")) {
		return Fail(c, "quest: set, clear or get");
	}
	Out("quest %lld bit %lld = %d", q, bit, Fn<QuestFn>(RvaQuestTest)(rec, static_cast<int>(q), static_cast<int>(bit)));
	return true;
}

auto CmdFlag(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	long long n = 0;
	if (t.n < 4 || !hcmd::ParseInt(t.tok[2], &n)) {
		return Fail(c, "usage: flag player|game <n> get|set|clear");
	}
	const bool game = hcmd::LowerEq(t.tok[1], "game");
	if (!game && !hcmd::LowerEq(t.tok[1], "player")) {
		return Fail(c, "flag: player or game");
	}
	if (hcmd::LowerEq(t.tok[3], "set") || hcmd::LowerEq(t.tok[3], "clear")) {
		SetCraftFlag(c->player, static_cast<int>(n), game, hcmd::LowerEq(t.tok[3], "set"));
	} else if (!hcmd::LowerEq(t.tok[3], "get")) {
		return Fail(c, "flag: set, clear or get");
	}
	Out("flag %s %lld = %d", game ? "game" : "player", n, CraftFlagTest(c->player, static_cast<int>(n), game) ? 1 : 0);
	return true;
}

auto CmdGold(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	if (t.n >= 4 && hcmd::LowerEq(t.tok[1], "set")) {
		long long carried = 0, bank = 0;
		if (!hcmd::ParseInt(t.tok[2], &carried) || !hcmd::ParseInt(t.tok[3], &bank)) {
			return Fail(c, "usage: gold get | gold set <carried> <bank>");
		}
		Fn<SetStatFn>(RvaSetStat)(c->player, StatGold, static_cast<int32_t>(carried), 0);
		Fn<SetStatFn>(RvaSetStat)(c->player, StatGoldBank, static_cast<int32_t>(bank), 0);
	} else if (!(t.n >= 2 && hcmd::LowerEq(t.tok[1], "get"))) {
		return Fail(c, "usage: gold get | gold set <carried> <bank>");
	}
	Out("gold carried=%d bank=%d", Stat(c->player, StatGold), Stat(c->player, StatGoldBank));
	return true;
}

auto CmdStat(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	long long who = 0, stat = 0, layer = 0, value = 0;
	int next = 3;
	if (t.n < 3 || !hcmd::ParseInt(t.tok[2], &stat)) {
		return Fail(c, "usage: stat player|<item unit id>|<item code> <stat> [layer] [set <value>]");
	}
	if (t.n > next && !hcmd::LowerEq(t.tok[next], "set")) {
		if (!hcmd::ParseInt(t.tok[next], &layer)) {
			return Fail(c, "stat: layer must be a number");
		}
		++next;
	}
	const bool set = t.n > next && hcmd::LowerEq(t.tok[next], "set");
	if (set && (t.n <= next + 1 || !hcmd::ParseInt(t.tok[next + 1], &value))) {
		return Fail(c, "stat: set needs a number");
	}
	Unit* unit = nullptr;
	if (hcmd::LowerEq(t.tok[1], "player")) {
		unit = c->player;
	} else if (hcmd::ParseInt(t.tok[1], &who)) {
		EachItem(c, [&](Unit* it) {
			if (unit == nullptr && IdOf(it) == static_cast<uint32_t>(who)) {
				unit = it;
			}
		});
	} else {
		unit = FindItemByCode(c, hcmd::CodeToDword(t.tok[1]), -1);
	}
	if (unit == nullptr) {
		return Fail(c, "stat: unit not found");
	}
	if (set) {
		Fn<SetStatFn>(RvaSetStat)(unit, static_cast<int>(stat), static_cast<int32_t>(value), static_cast<int>(layer));
	}
	Out("stat %s %lld:%lld = %d", t.tok[1], stat, layer, Stat(unit, static_cast<int>(stat), static_cast<int>(layer)));
	return true;
}

// Packet 0x31 (a dialog line of an NPC was played to its end): u8 id, u8 kind (1 NPC), u16 message, u32 unit id. The server
// answers with quest event 11 (NPC message): Akara's Griswold's Heart, Cain, Jerhyn... react to it.
auto CmdMsg(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	long long cls = -1, msg = 0;
	if (t.n < 3 || (!hcmd::LowerEq(t.tok[1], "nearest") && !hcmd::ParseInt(t.tok[1], &cls)) || !hcmd::ParseInt(t.tok[2], &msg)) {
		return Fail(c, "usage: msg nearest|<npc class> <message id>");
	}
	Unit* npc = NearestMonster(c, cls, true);
	if (npc == nullptr) {
		return Fail(c, "msg: no such NPC alive in this area");
	}
	uint8_t pkt[8] = {0x31, 1};
	const uint16_t m = static_cast<uint16_t>(msg);
	const uint32_t id = IdOf(npc);
	std::memcpy(pkt + 2, &m, 2);
	std::memcpy(pkt + 4, &id, 4);
	const auto handler = reinterpret_cast<PacketFn>(*reinterpret_cast<const uint64_t*>(g_base + RvaPacketTable + 0x31 * 8));
	handler(c->game, c->player, pkt, 8);
	Out("msg npc id=%u class=%d message 0x%x sent", id, ClassOf(npc), static_cast<unsigned>(m));
	return true;
}

// Walk into a portal object, as the click on it does (the portal object's operate function 0x58f680, ctx {game, portal,
// player, 0, class}): `enter nearest` (portal classes 59 town portal, 60 red portal, 600 Median one-way) or `enter <object id>`.
auto CmdEnter(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	long long id = -1;
	if (t.n < 2 || (!hcmd::LowerEq(t.tok[1], "nearest") && !hcmd::ParseInt(t.tok[1], &id))) {
		return Fail(c, "usage: enter nearest | enter <portal object id>");
	}
	Unit* portal = nullptr;
	double best = 1e18;
	EachUnit(c->game, HashObjects, [&](Unit* u) {
		const int32_t cls = ClassOf(u);
		const bool match = id >= 0 ? IdOf(u) == static_cast<uint32_t>(id) : (cls == 59 || cls == 60 || cls == 600);
		if (TypeOf(u) == 2 && match && DistTo(c->player, u) < best) {
			best = DistTo(c->player, u);
			portal = u;
		}
	});
	if (portal == nullptr) {
		return Fail(c, "enter: no such portal object");
	}
	int64_t ctx[8] = {reinterpret_cast<int64_t>(c->game), reinterpret_cast<int64_t>(portal), reinterpret_cast<int64_t>(c->player), 0,
	                  ClassOf(portal), 0, 0, 0};
	const int64_t moved = Fn<int64_t (*)(int64_t*)>(0x58f680)(ctx);
	Out("enter portal id=%u class=%d: %s", IdOf(portal), ClassOf(portal), moved != 0 ? "moved" : "refused / failed");
	return true;
}

// Use the nearest warp tile (stairs, doors to another area): the server's warp action 0x491ff0 (game, player, tile).
auto CmdTile(Ctx* c, const hcmd::Tokens&) noexcept -> bool {
	Unit* tile = nullptr;
	double best = 1e18;
	int guard = 0;
	for (auto* u = d2r::At<Unit*>(c->game, 0x3630); u != nullptr && guard < 2000; u = d2r::At<Unit*>(u, UnitNextInHash), ++guard) {
		if (TypeOf(u) == 5 && DistTo(c->player, u) < best) {
			best = DistTo(c->player, u);
			tile = u;
		}
	}
	if (tile == nullptr) {
		return Fail(c, "tile: no warp tile in this game");
	}
	Fn<void (*)(Game*, Unit*, Unit*)>(0x491ff0)(c->game, c->player, tile);
	Out("tile id=%u class=%d dist %.0f used", IdOf(tile), ClassOf(tile), best);
	return true;
}

// Raw client packet to the server's handler table: "pkt 13 02 00 00 00 <id bytes>" (hex bytes, the first is the packet id).
// For what no verb covers yet (clicking an object or a portal: packet 0x13).
auto CmdPkt(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	uint8_t pkt[64] = {};
	int n = 0;
	for (int i = 1; i < t.n && n < 64; ++i) {
		long long v = 0;
		char hex[8] = "0x";
		std::snprintf(hex + 2, sizeof(hex) - 2, "%.2s", t.tok[i]);
		if (!hcmd::ParseInt(hex, &v)) {
			return Fail(c, "pkt: '%.10s' is not a hex byte", t.tok[i]);
		}
		pkt[n++] = static_cast<uint8_t>(v);
	}
	if (n < 1 || pkt[0] >= 0x66 || *reinterpret_cast<const uint64_t*>(g_base + RvaPacketTable + pkt[0] * 8) == 0) {
		return Fail(c, "pkt: no server handler for that id");
	}
	const auto handler = reinterpret_cast<PacketFn>(*reinterpret_cast<const uint64_t*>(g_base + RvaPacketTable + pkt[0] * 8));
	handler(c->game, c->player, pkt, n);
	Out("pkt 0x%02x (%d bytes) handled", pkt[0], n);
	return true;
}

auto CmdLevel(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	long long level = 0;
	if (t.n < 2 || !hcmd::ParseInt(t.tok[1], &level)) {
		return Fail(c, "usage: level <level id>");
	}
	Fn<TravelFn>(RvaTravel)(c->game, c->player, static_cast<int>(level), 0);
	return true;  // the move itself takes a few frames; `state` shows the new area
}

auto CmdSpawn(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	long long cls = 0, count = 1;
	if (t.n < 2 || !hcmd::ParseInt(t.tok[1], &cls) || (t.n > 2 && !hcmd::ParseInt(t.tok[2], &count))) {
		return Fail(c, "usage: spawn <monster class> [count]");
	}
	void* room = Fn<UnitRoomFn>(RvaUnitRoom)(c->player);
	int32_t px = 0, py = 0;
	if (room == nullptr || !d2r::UnitXY(c->player, &px, &py)) {
		return Fail(c, "spawn: player has no room");
	}
	int made = 0;
	for (long long i = 0; i < count && i < 20; ++i) {
		const int32_t x = px + 6 + static_cast<int32_t>(i % 5) * 2, y = py + 6 + static_cast<int32_t>(i / 5) * 2;
		void* spawnRoom = Fn<InRoomFn>(RvaInRoom)(room, x, y);
		if (spawnRoom == nullptr) {
			continue;
		}
		if (Unit* m = Fn<SpawnFn>(RvaSpawn)(c->game, spawnRoom, x, y, static_cast<int32_t>(cls), 1, 2, 0, 0)) {
			Out("spawned class %lld id=%u at %d,%d", cls, IdOf(m), x, y);
			++made;
		}
	}
	return made > 0 ? true : Fail(c, "spawn: nothing spawned");
}

auto CmdKill(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	if (t.n < 2) {
		return Fail(c, "usage: kill <unit id> | kill nearest [class]");
	}
	Unit* victim = nullptr;
	long long v = 0;
	if (hcmd::LowerEq(t.tok[1], "nearest")) {
		long long cls = -1;
		if (t.n > 2 && !hcmd::ParseInt(t.tok[2], &cls)) {
			return Fail(c, "kill nearest: class must be a number");
		}
		victim = NearestMonster(c, cls, false);
	} else if (hcmd::ParseInt(t.tok[1], &v)) {
		victim = FindMonsterById(c, static_cast<uint32_t>(v));
	}
	if (victim == nullptr) {
		return Fail(c, "kill: no such live monster");
	}
	const uint32_t id = IdOf(victim);
	const int32_t cls = ClassOf(victim);
	Fn<KillFn>(RvaKill)(c->game, victim, c->player, 1);
	Out("kill id=%u class=%d: mode now %d (0 or 12 = dead)", id, cls, ModeOf(victim));
	return true;
}

auto CmdInteract(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	if (t.n < 2) {
		return Fail(c, "usage: interact nearest | interact <npc class>");
	}
	long long cls = -1;
	if (!hcmd::LowerEq(t.tok[1], "nearest") && !hcmd::ParseInt(t.tok[1], &cls)) {
		return Fail(c, "interact: nearest or an NPC class id");
	}
	Unit* npc = NearestMonster(c, cls, true);
	if (npc == nullptr) {
		return Fail(c, "interact: no such NPC alive in this area");
	}
	// The server accepts the click within a few sub-tiles of the NPC: stand next to it first, as a walk would.
	if (DistTo(c->player, npc) > 5.0) {
		int32_t nx = 0, ny = 0;
		d2r::UnitXY(npc, &nx, &ny);
		static const int off[][2] = {{2, 2}, {-2, 2}, {2, -2}, {-2, -2}, {3, 0}, {0, 3}, {-3, 0}, {0, -3}, {4, 4}, {-4, -4}};
		for (const auto& o : off) {
			if (Fn<BlockedFn>(RvaMoveBlocked)(c->player, nx + o[0], ny + o[1], 0x804) != 0) {
				continue;
			}
			Fn<MoveFn>(RvaMove)(c->game, c->player, 0, nx + o[0], ny + o[1], 0, 0);
			if (DistTo(c->player, npc) <= 5.0) {
				break;
			}
		}
	}
	// Packet 0x2f (start talking to an NPC): u8 id, u32 unit id. The handler answers the client with 0x27, which opens
	// the NPC menu through the client's own builder (0x1141d0 -> 0x1147a0), and fires quest event 0 (NPC activate).
	uint8_t pkt[5] = {0x2f};
	const uint32_t id = IdOf(npc);
	std::memcpy(pkt + 1, &id, 4);
	const auto handler = reinterpret_cast<PacketFn>(*reinterpret_cast<const uint64_t*>(g_base + RvaPacketTable + 0x2f * 8));
	handler(c->game, c->player, pkt, 5);
	Out("interact npc id=%u class=%d distance %.1f", id, ClassOf(npc), DistTo(c->player, npc));
	return true;
}

int g_uniq = -1;
using ItemDataFn = uint8_t* (*)(Unit*);
constexpr uint64_t RvaItemData = 0x34a500;  // (item) -> item data (+0 quality, +0x34 unique index)

// `difficulty <0-2>`: the running game's difficulty byte (game +0x104), for checks that need Hell rules without a Hell
// character. Quest records are per difficulty, so use it for lockout / recipe checks only.
auto CmdDifficulty(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	long long n = 0;
	if (t.n >= 2 && hcmd::ParseInt(t.tok[1], &n) && n >= 0 && n <= 2) {
		*reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(c->game) + GameDifficulty) = static_cast<uint8_t>(n);
	}
	Out("difficulty = %d", static_cast<int>(d2r::At<uint8_t>(c->game, GameDifficulty)));
	return true;
}

// Class change for tests (`class <n>`): cube recipes can be class bound. The save keeps it, put the class back.
auto CmdClass(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	long long n = 0;
	if (t.n >= 2 && hcmd::ParseInt(t.tok[1], &n) && n >= 0 && n <= 6) {
		*reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(c->player) + UnitClass) = static_cast<int32_t>(n);
	}
	Out("class = %d", ClassOf(c->player));
	return true;
}

// Port of 0x517530 with a choice of where the item goes and stats set before the client hears of it.
auto MakeItem(Ctx* c, uint32_t code, int quality, int ilvl, int page, const int (*stats)[3], const long long* values, int nstats,
              char* why, size_t whySize) noexcept -> Unit* {
	int32_t classId = 0;
	const uint8_t* rec = Fn<ItemByCodeFn>(RvaItemByCode)(c->dataFlag, code, &classId);
	if (rec == nullptr) {
		std::snprintf(why, whySize, "item: unknown code");
		return nullptr;
	}
	if (ilvl == 0) {
		ilvl = Fn<DefaultIlvlFn>(RvaDefaultIlvl)(c->player, 0);
	}
	if (g_uniq >= 0) {  // the quest module's generator hook picks the unique (as for Griswold's Heart): quality 7, item level 255
		quality = 7;
		ilvl = 255;
		QuestsWantUnique(g_uniq + 1);
	}
	Unit* item = Fn<MakeItemFn>(RvaMakeItem)(c->player, classId, c->game, 4, quality, 0, 1, ilvl, 0, 0, 0);
	QuestsWantUnique(0);
	if (item == nullptr) {
		std::snprintf(why, whySize, "item: creation failed");
		return nullptr;
	}
	if (g_uniq >= 0) {  // a named unique (the cube matches uniques by index): item data +0 quality, +0x34 unique index
		if (auto* data = Fn<ItemDataFn>(RvaItemData)(item); data != nullptr && *reinterpret_cast<int32_t*>(data + 0x34) != g_uniq) {
			*reinterpret_cast<int32_t*>(data) = 7;
			*reinterpret_cast<int32_t*>(data + 0x34) = g_uniq;
		}
	}
	if (const int32_t dur = Fn<ItemStatFn>(RvaMaxDurability)(item); dur != 0) {
		Fn<SetItemStatFn>(RvaSetStatItem)(item, StatDurability, dur, 0);
	}
	for (int i = 0; i < nstats; ++i) {
		Fn<SetStatFn>(RvaSetStat)(item, stats[i][0], static_cast<int32_t>(values[i]), stats[i][1]);
	}
	if (page < 0) {  // on the ground next to the player
		Fn<DropFlagsFn>(RvaDropFlags)(item, 0x4000, 0);
		Fn<ByteSetFn>(RvaGround1)(item, 0xff);
		Fn<ByteSetFn>(RvaGround2)(item, 0);
		Fn<ByteSetFn>(RvaGround3)(item, 3);
		Fn<ByteSetFn>(RvaGround4)(item, 0);
		Fn<ByteSetFn>(RvaGround5)(item, 0);
		int32_t xy[2] = {};
		Fn<UnitXYFn>(RvaUnitXY)(c->player, xy);
		void* room = Fn<UnitRoomFn>(RvaUnitRoom)(c->player);
		int32_t out[2] = {};
		void* spot = Fn<GroundSpotFn>(RvaGroundSpot)(c->game, room, xy, out, 1);
		if (spot == nullptr) {
			Fn<FreeItemFn>(RvaFreeItem)(c->game, item);
			std::snprintf(why, whySize, "item: no free ground spot");
			return nullptr;
		}
		Fn<GroundDropFn>(RvaGroundDrop)(c->game, c->player, item, spot, out[0], out[1]);
		return item;
	}
	void* inv = Fn<InventoryFn>(RvaInventory)(c->player, "harness", 0);
	const int32_t record = Fn<InvRecordFn>(RvaInvRecord)(c->player, page == 3 ? 3 : 0, d2r::At<uint8_t>(c->game, 0x106) != 1);
	uint16_t x = 0, y = 0;
	if (inv == nullptr || Fn<FindSpotFn>(RvaFindSpot)(inv, item, record, &x, &y, page) == 0) {
		Fn<FreeItemFn>(RvaFreeItem)(c->game, item);
		std::snprintf(why, whySize, "item: no room in %s", page == 3 ? "the cube" : "the inventory");
		return nullptr;
	}
	int32_t loc[4] = {};
	Fn<LocInitFn>(RvaLocInit)(loc, page, static_cast<int32_t>((static_cast<uint32_t>(y) << 16) | x));
	uint32_t itemId = IdOf(item);
	if (loc[0] != 0 || Fn<PlaceFn>(RvaPlace)(c->player, loc, &itemId, 1, 0) == 0) {
		Fn<FreeItemFn>(RvaFreeItem)(c->game, item);
		std::snprintf(why, whySize, "item: placing failed");
		return nullptr;
	}
	Fn<SendItemFn>(RvaSendItem)(c->game, c->player, item, 1, 2, loc[0], loc[1], loc[3], loc[2]);
	Fn<IdentifyHookFn>(RvaIdentifyHook)(c->game, c->player, item, 0);
	if (Fn<AfterPlaceFn>(RvaAfterPlace)(item, c->player) != 0) {
		Fn<RecheckFn>(RvaRecheck)(c->game, c->player, 0, 0);
	}
	return item;
}

auto CmdItem(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	if (t.n < 2) {
		return Fail(c, "usage: item <code> [stat=value ...] [q=quality] [ilvl=n] [inv|cube|ground]");
	}
	int stats[8][3] = {};
	long long values[8] = {};
	int nstats = 0, quality = 2, ilvl = 0, page = 0;
	g_uniq = -1;
	for (int i = 2; i < t.n; ++i) {
		long long v = 0;
		if (hcmd::LowerEq(t.tok[i], "inv")) {
			page = 0;
		} else if (hcmd::LowerEq(t.tok[i], "cube")) {
			page = 3;
		} else if (hcmd::LowerEq(t.tok[i], "ground")) {
			page = -1;
		} else if (std::strncmp(t.tok[i], "q=", 2) == 0 && hcmd::ParseInt(t.tok[i] + 2, &v)) {
			quality = static_cast<int>(v);
		} else if (std::strncmp(t.tok[i], "uniq=", 5) == 0 && hcmd::ParseInt(t.tok[i] + 5, &v)) {
			g_uniq = static_cast<int>(v);
		} else if (std::strncmp(t.tok[i], "ilvl=", 5) == 0 && hcmd::ParseInt(t.tok[i] + 5, &v)) {
			ilvl = static_cast<int>(v);
		} else if (nstats < 8 && hcmd::ParseStatArg(t.tok[i], &stats[nstats][0], &stats[nstats][1], &values[nstats])) {
			++nstats;
		} else {
			return Fail(c, "item: cannot read '%.40s'", t.tok[i]);
		}
	}
	char why[120] = "";
	Unit* item = MakeItem(c, hcmd::CodeToDword(t.tok[1]), quality, ilvl, page, stats, values, nstats, why, sizeof(why));
	if (item == nullptr) {
		return Fail(c, "%s", why);
	}
	DescribeItem(c, item);
	for (int i = 0; i < nstats; ++i) {
		Out("  stat %d:%d = %d", stats[i][0], stats[i][1], Stat(item, stats[i][0], stats[i][1]));
	}
	return true;
}

// `cuberec <index>`: hex dump of a cubemain record (0x16c bytes) for diagnosing a recipe that does not match.
auto CmdCuberec(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	long long index = 0;
	if (t.n < 2 || !hcmd::ParseInt(t.tok[1], &index)) {
		return Fail(c, "usage: cuberec <recipe index>");
	}
	const uint8_t* rec = Fn<CubeRecordFn>(RvaCubeRecord)(c->dataFlag, static_cast<int32_t>(index));
	if (rec == nullptr) {
		return Fail(c, "cuberec: no such record");
	}
	for (int row = 0; row < 0x16c; row += 32) {
		char line[200];
		int n = std::snprintf(line, sizeof(line), "rec %04x:", row);
		for (int i = row; i < row + 32 && i < 0x16c; ++i) {
			n += std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " %02x", rec[i]);
		}
		Out("%s", line);
	}
	return true;
}

auto CmdCubecraft(Ctx* c, const hcmd::Tokens&) noexcept -> bool {
	int32_t result[8] = {};
	alignas(16) uint8_t ctx[512] = {};
	Fn<SearchFn>(RvaSearch)(result, c->game, c->player, ctx);
	if (reinterpret_cast<const char*>(result)[4] == 0) {
		return Fail(c, "cubecraft: no recipe matches the cube contents");
	}
	const int32_t index = result[0];
	const uint8_t* record = Fn<CubeRecordFn>(RvaCubeRecord)(c->dataFlag, index);
	Unit* out = record != nullptr ? Fn<CubeExecFn>(RvaCubeExec)(c->game, c->player, record, ctx) : nullptr;
	if (out == nullptr) {  // portal / useitem outputs return no unit: `state` shows the effect
		Out("cubecraft recipe %d ran, returned no item (portal and special outputs return none; a failed recipe puts the inputs back)", index);
		return true;
	}
	char code[5];
	ItemCode(c, out, code);
	Out("cubecraft recipe %d made item id=%u code=%s", index, IdOf(out), code);
	return true;
}

// Takes an item out of the inventory and frees it, the way the game removes a used-up tome (0x4712a0).
auto RemoveItem(Ctx* c, Unit* item) noexcept -> bool {
	int32_t location[8] = {};
	Fn<ItemLocationFn>(RvaItemLocation)(location, item);
	uint32_t unitId = IdOf(item);
	if (Fn<InvChangeFn>(RvaInvChange)(c->player, nullptr, location, nullptr, &unitId, 0, 1, 0) == 0) {
		return false;
	}
	Fn<ByteSetFn>(RvaSetByte5c)(item, 0);
	Fn<SendItemFn>(RvaSendItem)(c->game, c->player, item, 0x20, 4, 0, 0, 0, location[2]);
	Fn<ByteSetFn>(RvaSetByte5c)(item, 0);
	Fn<FreeItemFn>(RvaFreeItem)(c->game, item);
	return true;
}

auto CmdUse(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	if (t.n < 2) {
		return Fail(c, "usage: use <item code>");
	}
	Unit* item = FindItemByCode(c, hcmd::CodeToDword(t.tok[1]), 0);
	if (item == nullptr) {
		item = FindItemByCode(c, hcmd::CodeToDword(t.tok[1]), -1);
	}
	if (item == nullptr) {
		return Fail(c, "use: no such item in the inventory");
	}
	const auto check = Fn<UseFn>(RvaUseCheck);
	if ((check(c->game, c->player, item, nullptr) & 0xff) == 0) {
		return Fail(c, "use: the game refuses to use it (check returned 0)");
	}
	Fn<UseFn>(RvaUseItem)(c->game, c->player, item, nullptr);
	const int32_t cls = Fn<UnitClassFn>(RvaUnitClass)(item, "harness", 0);
	const uint8_t* rec = Fn<ItemRecordFn>(RvaItemRecord)(c->dataFlag, cls);
	const int32_t spell = rec != nullptr ? d2r::At<int32_t>(rec, 0x94) : -1;
	const bool keep = spell >= 0 && spell < 24 && *reinterpret_cast<const uint32_t*>(g_base + RvaSpellTable + static_cast<uint64_t>(spell) * 12 + 8) == 1;
	if (!keep) {
		RemoveItem(c, item);  // used up
	}
	Out("use %s: used (pSpell %d, %s)", t.tok[1], spell, keep ? "kept" : "consumed");
	return true;
}

// `cubeclear`: removes every item from the Horadric Cube (page 3), so a recipe test starts from an empty cube.
auto CmdCubeclear(Ctx* c, const hcmd::Tokens&) noexcept -> bool {
	Unit* batch[64];
	int n = 0;
	EachItem(c, [&](Unit* it) {
		if (n < 64 && Fn<ItemByteFn>(RvaItemPage)(it) == 3) {
			batch[n++] = it;
		}
	});
	int removed = 0;
	for (int i = 0; i < n; ++i) {
		removed += RemoveItem(c, batch[i]) ? 1 : 0;
	}
	Out("cubeclear: %d items removed", removed);
	return true;
}

// `packclear`: removes every backpack item except the Horadric Cube, so created items always find room.
auto CmdPackclear(Ctx* c, const hcmd::Tokens&) noexcept -> bool {
	Unit* batch[200];
	int n = 0;
	EachItem(c, [&](Unit* it) {
		char code[5];
		ItemCode(c, it, code);
		if (n < 200 && Fn<ItemByteFn>(RvaItemPage)(it) == 0 && std::strcmp(code, "box") != 0) {
			batch[n++] = it;
		}
	});
	int removed = 0;
	for (int i = 0; i < n; ++i) {
		removed += RemoveItem(c, batch[i]) ? 1 : 0;
	}
	Out("packclear: %d items removed", removed);
	return true;
}

auto CmdRemove(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	if (t.n < 2) {
		return Fail(c, "usage: remove <item code> [count]");
	}
	long long count = 1;
	if (t.n > 2 && !hcmd::ParseInt(t.tok[2], &count)) {
		return Fail(c, "remove: count must be a number");
	}
	int removed = 0;
	while (removed < count) {
		Unit* item = FindItemByCode(c, hcmd::CodeToDword(t.tok[1]), -1);
		if (item == nullptr || !RemoveItem(c, item)) {
			break;
		}
		++removed;
	}
	Out("remove %s: %d removed", t.tok[1], removed);
	return removed > 0 ? true : Fail(c, "remove: no such item");
}

auto CmdHelp(Ctx*, const hcmd::Tokens&) noexcept -> bool {
	Out("harness frames seen %ld last game %ld", static_cast<long>(g_frames), static_cast<long>(g_lastGame));
	Out("verbs: state | kill <id>|nearest [class] | spawn <class> [n] | interact nearest|<npc class> | menu list|<line>|cancel | "
	    "msg nearest|<npc class> <message id> | quest <id> <bit>|dump set|clear|get | item <code> [stat=value..] [q=n] [ilvl=n] [inv|cube|ground] | cubecraft | remove <code> [n] | pkt <hex bytes> | enter nearest|<portal id> | tile | packclear | cubeclear | cuberec <n> | class <0-6> | difficulty <0-2> | "
	    "flag player|game <n> get|set|clear | gold get|set <carried> <bank> | level <id> | use <code> | stat player|<id>|<code> <stat> [layer]");
	return true;
}

// Client side (UI thread): the NPC menu the 0x27 packet opened.
auto CmdMenu(Ctx* c, const hcmd::Tokens& t) noexcept -> bool {
	const auto* b = reinterpret_cast<const uint8_t*>(g_base);
	if (*reinterpret_cast<const uint32_t*>(b + RvaMenuNpcValid) == 0 || *reinterpret_cast<const uint32_t*>(b + RvaMenuOpenFlag) == 0) {
		return Fail(c, "menu: no NPC menu is open (interact first, then wait a moment for the client)");
	}
	Unit* npc = Fn<ClientFindFn>(RvaClientFind)(*reinterpret_cast<const uint32_t*>(b + RvaMenuNpcId), 1);
	const int32_t row = npc != nullptr ? Fn<MenuRecordFn>(RvaMenuRecord)(npc) : -1;
	const uint32_t count = *reinterpret_cast<const uint32_t*>(b + RvaMenuCount);
	const auto* table = *reinterpret_cast<const uint8_t* const*>(b + RvaMenuTable);
	if (row < 0 || static_cast<uint32_t>(row) >= count || table == nullptr) {
		return Fail(c, "menu: NPC row not found");
	}
	const uint8_t* r = table + static_cast<size_t>(row) * 0x48;
	const int32_t lines = d2r::At<int32_t>(r, 4);  // includes the final Cancel
	if (t.n < 2) {
		return Fail(c, "usage: menu list | menu <line index> | menu cancel");
	}
	if (hcmd::LowerEq(t.tok[1], "list")) {
		Out("menu npc id=%u class=%u row=%d lines=%d (+ Cancel)", *reinterpret_cast<const uint32_t*>(b + RvaMenuNpcId),
		    static_cast<unsigned>(*reinterpret_cast<const uint16_t*>(b + RvaMenuNpcClass)), row, lines - 1);
		for (int i = 0; i < lines - 1 && i < 5; ++i) {
			const uint64_t handler = d2r::At<uint64_t>(r, 0x18 + 8 * i);
			const bool inImage = handler >= g_base && handler < g_base + 0x4000000;
			Out("  line %d string=0x%x handler=%s0x%llx", i, static_cast<unsigned>(d2r::At<uint16_t>(r, 8 + 2 * i)),
			    inImage ? "image+" : "plugin@", static_cast<unsigned long long>(inImage ? handler - g_base : handler));
		}
		return true;
	}
	long long line = 0;
	uint64_t handler = 0;
	if (hcmd::LowerEq(t.tok[1], "cancel")) {
		handler = g_base + 0x112c30;
	} else if (hcmd::ParseInt(t.tok[1], &line) && line >= 0 && line < lines - 1 && line < 5) {
		handler = d2r::At<uint64_t>(r, 0x18 + 8 * static_cast<int>(line));
	} else {
		return Fail(c, "menu: line %.20s is not in 0..%d", t.tok[1], lines - 2);
	}
	if (handler == 0) {
		return Fail(c, "menu: that line has no handler");
	}
	const uint8_t res = reinterpret_cast<MenuHandlerFn>(handler)(static_cast<uint8_t>(c->dataFlag));
	Out("menu line %s handler 0x%llx returned %u", t.tok[1], static_cast<unsigned long long>(handler), static_cast<unsigned>(res));
	return true;
}

using VerbFn = bool (*)(Ctx*, const hcmd::Tokens&) noexcept;
struct Verb {
	const char* name;
	VerbFn fn;
	bool clientSide;
};
const Verb Verbs[] = {
    {"state", CmdState, false},   {"quest", CmdQuest, false}, {"flag", CmdFlag, false},     {"gold", CmdGold, false},
    {"stat", CmdStat, false},     {"level", CmdLevel, false}, {"spawn", CmdSpawn, false},   {"kill", CmdKill, false},
    {"interact", CmdInteract, false}, {"msg", CmdMsg, false}, {"pkt", CmdPkt, false}, {"enter", CmdEnter, false}, {"packclear", CmdPackclear, false},{"cubeclear", CmdCubeclear, false},{"cuberec", CmdCuberec, false},{"difficulty", CmdDifficulty, false},{"tile", CmdTile, false},{"class", CmdClass, false},{"remove", CmdRemove, false},{"item", CmdItem, false}, {"cubecraft", CmdCubecraft, false}, {"use", CmdUse, false},
    {"help", CmdHelp, true},     {"menu", CmdMenu, true},
};

const Verb* FindVerb(const char* name) noexcept {
	for (const Verb& v : Verbs) {
		if (hcmd::LowerEq(name, v.name)) {
			return &v;
		}
	}
	return nullptr;
}

// SEH wrapper: a bad pointer in a game call must not take the whole game down.
auto RunVerb(const Verb* verb, Ctx* c, const hcmd::Tokens& t) noexcept -> int {
	__try {
		return verb->fn(c, t) ? 1 : 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

void Finish(Ctx* c, int rc) noexcept {
	if (rc == 1) {
		Out("%s OK", c->verb);
	} else if (rc == -2) {
		Out("%s FAIL crashed (exception %s)", c->verb, c->why[0] != '\0' ? c->why : "in the game call");
	} else {
		Out("%s FAIL %s", c->verb, c->why[0] != '\0' ? c->why : "failed");
	}
}

// One queued server-side command (single slot: the mailbox runs one command at a time).
struct Job {
	hcmd::Tokens tokens;
	const Verb* verb = nullptr;
	volatile LONG state = 0;  // 0 idle, 1 queued, 2 running, 3 done
	int rc = 0;
	Ctx ctx;
};
Job g_job;

}  // namespace

void HarnessCmdsInit(uint64_t exeBase) noexcept {
	g_base = exeBase;
	InterlockedExchange(&g_ready, 1);
}

// Game thread, after the server frame.
void HarnessGameFrame(void* gamePtr) noexcept {
	InterlockedIncrement(&g_frames);
	g_lastGame = gamePtr != nullptr;
	if (g_ready == 0 || g_job.state != 1 || gamePtr == nullptr) {
		return;
	}
	if (InterlockedCompareExchange(&g_job.state, 2, 1) != 1) {
		return;
	}
	Game* game = static_cast<Game*>(gamePtr);
	Ctx& c = g_job.ctx;
	c = Ctx();
	std::snprintf(c.verb, sizeof(c.verb), "%s", g_job.verb->name);
	c.game = game;
	c.dataFlag = d2r::At<uint8_t>(game, d2r::GameDataFlag);
	c.player = FindPlayer(game);
	if (c.player == nullptr) {
		g_job.rc = 0;
		std::snprintf(c.why, sizeof(c.why), "no player in the game yet");
	} else {
		g_job.rc = RunVerb(g_job.verb, &c, g_job.tokens);
	}
	Finish(&c, g_job.rc);
	InterlockedExchange(&g_job.state, 3);
}

// UI thread (testharness.cpp ServiceCommands): "h <verb> ...". Returns 1 handled and OK, 0 failed, -2 crashed, -3 timed out.
auto HarnessCommand(const char* line) noexcept -> int {
	if (g_ready == 0) {
		return 0;
	}
	hcmd::Tokens all;
	hcmd::Split(line, &all);
	if (all.n < 2) {
		Out("usage: h <verb> ...   (h help)");
		return 0;
	}
	hcmd::Tokens t;  // the same tokens without the leading "h": t.tok[0] is the verb
	t.n = all.n - 1;
	for (int i = 0; i < t.n; ++i) {
		t.tok[i] = all.tok[i + 1];
	}
	const Verb* verb = FindVerb(t.tok[0]);
	if (verb == nullptr) {
		Out("%s FAIL unknown verb (h help)", t.tok[0]);
		return 0;
	}
	if (verb->clientSide) {
		Ctx c;
		std::snprintf(c.verb, sizeof(c.verb), "%s", verb->name);
		c.dataFlag = 2;
		const int rc = RunVerb(verb, &c, t);
		Finish(&c, rc);
		return rc;
	}
	if (InterlockedCompareExchange(&g_job.state, 0, 1) == 1 || g_job.state == 3) {  // a never-run or finished job is replaced
		InterlockedExchange(&g_job.state, 0);
	}
	if (g_job.state != 0) {
		Out("%s FAIL previous command still running", verb->name);
		return 0;
	}
	g_job.verb = verb;
	std::memcpy(g_job.tokens.text, all.text, sizeof(all.text));  // the job keeps its own copy of the line
	g_job.tokens.n = t.n;
	for (int i = 0; i < t.n; ++i) {
		g_job.tokens.tok[i] = g_job.tokens.text + (t.tok[i] - all.text);
	}
	g_job.rc = 0;
	// Queued, not waited for: the server frames are driven from this (UI) thread's loop, so blocking here would starve
	// the very frame that runs the job. The game thread ends the command with an "h: <verb> OK|FAIL" line.
	InterlockedExchange(&g_job.state, 1);
	return 1;
}

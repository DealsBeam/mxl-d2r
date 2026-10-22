// Median's treasure goblins (D2Sigma D2Goblins.cpp) on D2R. docs/subsystems/ubers-events.md section 4,
// docs/d2r-addresses.md section 5.
//
// Median runs a 15 s table entry (D2GS.cpp 0x66230) that calls 0x66120(game, player) for every player. For each
// player that is alive, outside a town and killed something within the last 375 frames it rolls the area's
// GoblinChance (levels +0x15c, less 10% per extra player) against 1024, picks a goblin from the monstats3 rows with
// flag 0x400 whose level bounds fit the area, and spawns it 8 sub-tiles from the player. The last-kill stamp is
// written by D2Achievements.cpp 0x49c40 on every kill of a monster whose monstats3 +0x20 byte is 0 (the killer or its
// owner when a minion kills).
//
// D2R: the tick is a detour of the per-game frame 0x406550 (original first, then frame % 375 == 0; the counter is
// game+0x170), the stamp is a pre-hook of the kill wrapper 0x515640 (game, victim, killer; once per death), the
// spawn is 0x4929e0(game, room, x, y, class, 1, radius, 0, 0). If the frame hook cannot be installed the kill
// wrapper also runs the tick (once per 375-frame bucket, only while somebody is killing, which is when the roll
// matters anyway). Per-game state resets when the game pointer changes or the frame counter goes backwards.
//
// Entry bytes verified against the dump of build 93847 (docs/d2r-addresses.md); whole instructions, no rip-relative.
// MXL_GOBLINS_FORCE=1 in the environment makes every roll pass, =2 also ignores the last-kill requirement (testing).
#include "goblins.h"

#include "client_messages.h"
#include "client_messages_logic.h"
#include "d2r.h"
#include "goblins_logic.h"
#include "testharness.h"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t FrameRva = 0x406550;       // (Game*) the per-game frame
constexpr uint64_t KillRva = 0x515640;        // (game, victim, killer)
constexpr uint64_t SpawnRva = 0x4929e0;       // (game, room, x, y, class, a6, radius, a8, flags) -> monster or null
constexpr uint64_t UnitRoomRva = 0x34b440;    // (unit) -> room
constexpr uint64_t RoomLevelRva = 0x2efc10;   // (room) -> level id
constexpr uint64_t InRoomRva = 0x38dfb0;      // (room, x, y) -> the room (or a neighbour) that contains the point, or null
constexpr uint64_t ClientUnitRva = 0x484c90;  // (client, 0) -> the client's player unit
constexpr uint64_t OwnerIdRva = 0x4a5480;     // (unit, &ownerId, &error): the owner of a minion, error 0 = found
constexpr uint64_t FindUnitRva = 0x48fe80;    // (game, type, id) -> unit

// 0x406550: mov [rsp+0x20],rsi / push rbp / push rdi / push r15 / lea rbp,[rsp-0x47]   (14 bytes)
constexpr uint8_t FrameEntry[] = {0x48, 0x89, 0x74, 0x24, 0x20, 0x55, 0x57, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xb9};
// 0x515640: mov [rsp+0x10],rbx / mov [rsp+0x18],rbp / push rdi / sub rsp,0x70   (15 bytes)
constexpr uint8_t KillEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x70};

constexpr int GameClients = 0x138;   // first client; client+0x628 = next
constexpr int GameDifficulty = 0x104;
constexpr int GameFrame = 0x170;
constexpr int ClientNext = 0x628;
constexpr int ClientState = 0x04;    // 4 = in game
constexpr int UnitClass = 0x04;
constexpr int UnitId = 0x08;
constexpr int UnitMode = 0x0c;
constexpr int ModeDeath = 0, ModeDead = 17;  // player modes of a dying or dead character (Sigma 0xbf440)
constexpr int MaxPlayers = 8;
constexpr int SpawnRadius = 2;  // 0x493cc0 rings of 3 and 6 sub-tiles around the point when the exact spot is blocked

using FrameFn = void (*)(Game* game);
using KillFn = void (*)(Game* game, Unit* victim, Unit* killer);
using SpawnFn = Unit* (*)(Game* game, void* room, int32_t x, int32_t y, int32_t monster, int32_t a6, int32_t radius,
                          int32_t a8, uint16_t flags);
using UnitRoomFn = void* (*)(Unit* unit);
using RoomLevelFn = int32_t (*)(void* room);
using InRoomFn = void* (*)(void* room, int32_t x, int32_t y);
using ClientUnitFn = Unit* (*)(void* client, int32_t zero);
using OwnerIdFn = void (*)(Unit* unit, uint32_t* ownerId, uint64_t* error);
using FindUnitFn = Unit* (*)(Game* game, int32_t type, uint32_t id);

FrameFn g_frameOriginal = nullptr;
KillFn g_killOriginal = nullptr;
SpawnFn g_spawn = nullptr;
UnitRoomFn g_unitRoom = nullptr;
RoomLevelFn g_roomLevel = nullptr;
InRoomFn g_inRoom = nullptr;
ClientUnitFn g_clientUnit = nullptr;
OwnerIdFn g_ownerId = nullptr;
FindUnitFn g_findUnit = nullptr;
const D2RL::PluginContext* g_context = nullptr;
bool g_frameHooked = false;
int g_force = 0;

goblins::State g_state;
uint64_t g_rng = 0;
std::atomic<uint32_t> g_ticks{0}, g_rolls{0}, g_spawned{0}, g_failed{0};
int32_t g_lastMonster = -1, g_lastLevel = -1;

auto Rand() noexcept -> uint32_t {  // splitmix64; the game's own LCG has weak low bits for `% 1024`
	g_rng += 0x9e3779b97f4a7c15ULL;
	uint64_t z = g_rng;
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return static_cast<uint32_t>((z ^ (z >> 31)) >> 16);
}

auto UnitType(const Unit* unit) noexcept -> int32_t {
	return d2r::At<int32_t>(unit, d2r::UnitType);
}

// One player's turn of 0x66120. `players` is the number of players in the game.
void PlayerTick(Game* game, Unit* player, uint32_t frame, int32_t players) noexcept {
	const int32_t mode = d2r::At<int32_t>(player, UnitMode);
	if (mode == ModeDeath || mode == ModeDead) {
		return;
	}
	void* room = g_unitRoom(player);
	if (room == nullptr) {
		return;
	}
	const int32_t level = g_roomLevel(room);
	if (goblins::IsTown(level)) {
		return;
	}
	if (g_force < 2 && !goblins::KilledRecently(frame, g_state.LastKill(d2r::At<uint32_t>(player, UnitId)))) {
		return;
	}
	const mxl_goblins::Chance* chance = goblins::FindChance(level);
	if (chance == nullptr) {
		return;
	}
	g_rolls.fetch_add(1);
	if (g_force == 0 && !goblins::ChancePasses(goblins::EffectiveChance(chance->chance, players), Rand())) {
		return;
	}
	const uint32_t difficulty = d2r::At<uint8_t>(game, GameDifficulty);
	const int32_t monster = goblins::Pick(level, chance->monLevel[difficulty < 3 ? difficulty : 2], Rand());
	if (monster < 0) {
		return;
	}
	int32_t x = 0, y = 0;
	if (!d2r::UnitXY(player, &x, &y)) {
		return;
	}
	x += goblins::SpawnOffset;
	y += goblins::SpawnOffset;
	void* spawnRoom = g_inRoom(room, x, y);
	if (spawnRoom == nullptr) {
		g_failed.fetch_add(1);
		return;
	}
	if (g_spawn(game, spawnRoom, x, y, monster, 1, SpawnRadius, 0, 0) == nullptr) {
		g_failed.fetch_add(1);
		return;
	}
	const uint32_t n = g_spawned.fetch_add(1) + 1;
	char notice[96];
	clientmsg::GoblinAppears(goblins::NameOf(monster), notice, sizeof(notice));
	SendClientMessage(player, notice, clientmsg::Yellow);
	g_lastMonster = monster;
	g_lastLevel = level;
	if (n <= 20 && g_context != nullptr) {
		char line[128];
		std::snprintf(line, sizeof(line), "mxl-core: goblin %d spawned in level %d near player %u (frame %u)", monster, level,
		              d2r::At<uint32_t>(player, UnitId), frame);
		g_context->LogInfo(line);
	}
}

void Tick(Game* game, uint32_t frame) noexcept {
	g_ticks.fetch_add(1);
	Unit* players[MaxPlayers];
	int32_t count = 0;
	int guard = 0;
	for (auto* client = d2r::At<uint8_t*>(game, GameClients); client != nullptr && count < MaxPlayers && guard < 64;
	     client = d2r::At<uint8_t*>(client, ClientNext), ++guard) {
		if (d2r::At<int32_t>(client, ClientState) != 4) {
			continue;
		}
		Unit* unit = g_clientUnit(client, 0);
		if (unit != nullptr && UnitType(unit) == 0) {
			players[count++] = unit;
		}
	}
	for (int32_t i = 0; i < count; ++i) {
		PlayerTick(game, players[i], frame, count);
	}
}

void FrameHook(Game* game) {
	g_frameOriginal(game);
	if (game == nullptr) {
		return;
	}
	HarnessGameFrame(game);  // test harness gameplay commands (off in normal play)
	FlushClientMessages(game);  // quest notices held until the NPC menu is up
	const uint32_t frame = d2r::At<uint32_t>(game, GameFrame);
	g_state.Touch(game, frame);
	if (goblins::IsTickFrame(frame)) {
		Tick(game, frame);
	}
}

// The player who gets the credit: the killer, or its owner when a minion kills (0x515640 resolves it the same way).
auto Credited(Game* game, Unit* killer) noexcept -> Unit* {
	if (killer == nullptr) {
		return nullptr;
	}
	const int32_t type = UnitType(killer);
	if (type == 0) {
		return killer;
	}
	if (type != 1) {
		return nullptr;
	}
	uint32_t owner[2] = {};
	uint64_t error = 1;
	g_ownerId(killer, owner, &error);
	if (static_cast<int32_t>(error) != 0) {
		return nullptr;
	}
	Unit* player = g_findUnit(game, 0, owner[0]);
	return player != nullptr && UnitType(player) == 0 ? player : nullptr;
}

void KillHook(Game* game, Unit* victim, Unit* killer) {
	if (game != nullptr && victim != nullptr && UnitType(victim) == 1) {
		const uint32_t frame = d2r::At<uint32_t>(game, GameFrame);
		g_state.Touch(game, frame);
		if (!goblins::NoKillStamp(d2r::At<int32_t>(victim, UnitClass))) {
			if (const Unit* player = Credited(game, killer)) {
				g_state.NoteKill(d2r::At<uint32_t>(player, UnitId), frame);
			}
		}
		if (!g_frameHooked && g_state.BucketDue(frame)) {
			Tick(game, frame);
		}
	}
	g_killOriginal(game, victim, killer);
}

}  // namespace

auto InstallGoblins(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	const uint64_t base = context->exeBase;
	g_spawn = reinterpret_cast<SpawnFn>(base + SpawnRva);
	g_unitRoom = reinterpret_cast<UnitRoomFn>(base + UnitRoomRva);
	g_roomLevel = reinterpret_cast<RoomLevelFn>(base + RoomLevelRva);
	g_inRoom = reinterpret_cast<InRoomFn>(base + InRoomRva);
	g_clientUnit = reinterpret_cast<ClientUnitFn>(base + ClientUnitRva);
	g_ownerId = reinterpret_cast<OwnerIdFn>(base + OwnerIdRva);
	g_findUnit = reinterpret_cast<FindUnitFn>(base + FindUnitRva);
	g_rng = GetTickCount64() ^ (base << 7);

	char env[8] = "";
	if (GetEnvironmentVariableA("MXL_GOBLINS_FORCE", env, sizeof(env)) > 0) {
		g_force = env[0] == '2' ? 2 : (env[0] == '1' ? 1 : 0);
	}

	const bool kill = context->InstallInlineHook(KillRva, KillEntry, sizeof(KillEntry), reinterpret_cast<void*>(&KillHook),
	                                             reinterpret_cast<void**>(&g_killOriginal));
	if (!kill) {  // without the last-kill stamp no player ever qualifies
		std::snprintf(report, reportSize, "off: kill wrapper hook failed");
		return false;
	}
	g_frameHooked = context->InstallInlineHook(FrameRva, FrameEntry, sizeof(FrameEntry), reinterpret_cast<void*>(&FrameHook),
	                                           reinterpret_cast<void**>(&g_frameOriginal));
	std::snprintf(report, reportSize, "%s: tick on %s, last-kill stamp on kill wrapper, %d goblins, %d level chances%s",
	              g_frameHooked ? "on" : "partial", g_frameHooked ? "game frame" : "kill wrapper (frame hook failed)",
	              static_cast<int>(mxl_goblins::GoblinCount), static_cast<int>(mxl_goblins::ChanceCount),
	              g_force != 0 ? (g_force == 2 ? " (FORCED 2)" : " (FORCED 1)") : "");
	return true;
}

void GoblinsStatus(char* out, size_t outSize) noexcept {
	std::snprintf(out, outSize, "%u spawned (last %d in level %d), %u rolls, %u ticks, %u spawn failures", g_spawned.load(),
	              g_lastMonster, g_lastLevel, g_rolls.load(), g_ticks.load(), g_failed.load());
}

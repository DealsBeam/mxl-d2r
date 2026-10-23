// A player who dies leaves no corpse and respawns after 5 seconds (Median XL rules, docs/subsystems/death.md).
//
// D2R's death path (docs/d2r-addresses.md section 12):
//   - 0x42a5e0 (Dead mode 0x11 enter) and 0x42de40 (the player mode-change dispatcher, mode 0 branch) both call 0x42c110
//     (Game*, Unit* player, x, y, room): it builds the corpse unit, moves every equipped item onto it and drops the gold.
//     Nothing uses its return value; the code after it sets mode 0x11 and sends the death updates.
//   - 0x429d10 (Death mode 0 enter) calls 0x424ac0 (Game*, Unit* victim, Unit* killer): the gold penalty (0x425cb0) and the
//     experience penalty (stat 13 lowered, stat 0xaf = gold lost, packet via 0x486260). Single caller, return value unused.
// Median's D2Sigma NOPs exactly these two calls in the 1.13c D2Game, so both are replaced here by a function that returns.
// The hooks are replacements: the trampoline is never called. They also stamp the frame of the death for the respawn wait.
//   - 0x4b6160 (Game*, Unit* player, packet, int) is the respawn request (client packet 0x41): for a unit in mode 0x11 it
//     refills life / mana / stamina, clears the dead state and moves the character to the town; returns 0.
// A corpse that is already stored in a save (D2R's own death, before this part) is still loaded: the level-entry function
// 0x485350 (client, player, game, room, x, y) walks the player's corpse list (inventory + 0x50, nodes {flag, id, .., next at
// +0x10}) and places every body in the room (0x4245c0) - that is also how "the body follows you to town". The pickup hook
// runs after it and does what a click on the body does (the interact handler 0x4b2be0): the pickup 0x42b220 (out, game,
// player, corpse) puts the body-location items back, then the corpse leaves the list (0x389730), the room (0x2ef4d0), the
// clients (0x476fc0, packet 0x8e) and the game (0x348b00 clears its collision, 0x48faa0 frees it). docs/subsystems/death.md.
#include "no_corpse.h"

#include "d2r.h"
#include "game_end.h"
#include "no_corpse_logic.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t CorpseRva = 0x42c110;
constexpr uint64_t PenaltyRva = 0x424ac0;
constexpr uint64_t RespawnRva = 0x4b6160;
constexpr int GameFrame = 0x170;  // game + 0x170: server frame counter, 25 per second (harness `state` frame=)
constexpr int UnitId = 0x08, UnitMode = 0x0c;
constexpr int32_t ModeDead = 0x11;
// 0x42c110: push rbp / rbx / rsi / rdi / r12-r15, lea rbp,[rsp-0x6e8], sub rsp,0x7e8 (28 bytes, whole instructions, no
// rip-relative operand; dump of build 93847, timestamp 0x6ab3782c)
constexpr uint8_t CorpseEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                                   0x48, 0x8d, 0xac, 0x24, 0x18, 0xf9, 0xff, 0xff, 0x48, 0x81, 0xec, 0xe8, 0x07, 0x00, 0x00};
// 0x424ac0: push rbx / rsi / rdi, sub rsp,0x40, mov rbx,r8 / rsi,rdx / rdi,rcx (17 bytes; the call at +0x11 is rip-relative)
constexpr uint8_t PenaltyEntry[] = {0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x40, 0x49, 0x8b, 0xd8, 0x48, 0x8b, 0xf2, 0x48, 0x8b, 0xf9};
// 0x4b6160: push rbp / r13 / r14, lea rbp,[rsp-0x47], sub rsp,0xb0 (18 bytes; the next instruction is rip-relative)
constexpr uint8_t RespawnEntry[] = {0x40, 0x55, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8d, 0x6c, 0x24, 0xb9, 0x48, 0x81, 0xec, 0xb0, 0x00, 0x00, 0x00};

using CorpseFn = void (*)(void* game, void* player, int x, int y, void* room);
using PenaltyFn = uint64_t (*)(void* game, void* victim, void* killer);
using RespawnFn = int (*)(void* game, void* player, void* packet, int flag);

CorpseFn g_corpseOriginal = nullptr;
PenaltyFn g_penaltyOriginal = nullptr;
RespawnFn g_respawnOriginal = nullptr;
nocorpse::Counters g_counts;
nocorpse::DeathStamps g_stamps;
SRWLOCK g_lock = SRWLOCK_INIT;
bool g_installed = false;

// --- stored corpse pickup (docs/subsystems/death.md section 8) --------------------------------------------------------
constexpr uint64_t EnterRva = 0x485350;
constexpr uint64_t PickupRva = 0x42b220;      // (PickupOut*, game, player, corpse)
constexpr uint64_t ListRemoveRva = 0x389730;  // (inventory, unit id, flag) -> 1 when the entry was removed
constexpr uint64_t FirstCorpseRva = 0x388780; // (inventory) -> first corpse list node (inventory + 0x50)
constexpr uint64_t InventoryRva = 0x34a360;   // (unit, file, line) -> inventory
constexpr uint64_t FirstItemRva = 0x388c10;   // (inventory) -> first item
constexpr uint64_t NextItemRva = 0x38aba0;    // (item) -> next item of the inventory
constexpr uint64_t UnitByIdRva = 0x48fe80;    // (game, type, id) -> unit
constexpr uint64_t UnitRoomRva = 0x34b440;    // (unit) -> room
constexpr uint64_t RoomRemoveRva = 0x2ef4d0;  // (room, unit type, id): the room drops the unit at its next update (null room: no-op)
constexpr uint64_t CorpseGoneRva = 0x476fc0;  // (game, owner id, corpse id, add flag): packet 0x8e to the clients; 0 = remove
constexpr uint64_t ClearMaskRva = 0x348b00;   // (unit): takes the unit's footprint out of the room's collision map
constexpr uint64_t FreeUnitRva = 0x48faa0;    // (game, unit)
constexpr int NodeFlag = 0x00, NodeId = 0x04, NodeNext = 0x10;
constexpr int PlayerType = 0;
// 0x485350: push rbp / rbx / r12 / r13 / r15, lea rbp,[rsp-0x27], sub rsp,0xb0 (21 bytes; the next instruction is rip-relative)
constexpr uint8_t EnterEntry[] = {0x40, 0x55, 0x53, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xd9,
                                  0x48, 0x81, 0xec, 0xb0, 0x00, 0x00, 0x00};
// Entry bytes of the functions the pickup calls (build 93847; a mismatch keeps the part off instead of calling something else).
struct Probe {
	uint64_t rva;
	uint8_t bytes[16];
	int length;
};
constexpr Probe Probes[] = {
    {PickupRva, {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0x68, 0xfe, 0xff}, 16},
    {ListRemoveRva, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57}, 16},
    {FirstCorpseRva, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x75, 0x1b}, 14},
    {InventoryRva, {0x48, 0x89, 0x5c, 0x24, 0x18, 0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf1, 0x48, 0x85, 0xc9}, 16},
    {FirstItemRva, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x74, 0x2e}, 14},
    {NextItemRva, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x75, 0x10}, 14},
    {UnitByIdRva, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x41}, 16},
    {UnitRoomRva, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x75, 0x13}, 14},
    {RoomRemoveRva, {0x48, 0x85, 0xc9, 0x74, 0x69, 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57}, 16},
    {CorpseGoneRva, {0x48, 0x83, 0xec, 0x48, 0x48, 0x8b, 0x05}, 7},
    {ClearMaskRva, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56}, 16},
    {FreeUnitRva, {0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xf1}, 16},
};

// What 0x42b220 writes: the number of body items it could not place (they stay on the corpse) and a success byte.
struct PickupOut {
	int64_t remaining;
	uint8_t ok;
	uint8_t pad[23];
};
static_assert(offsetof(PickupOut, ok) == 8);

using EnterFn = void (*)(void* client, void* player, void* game, void* room, int x, int y);
using PickupFn = void (*)(PickupOut* out, void* game, void* player, void* corpse);
using ListRemoveFn = int64_t (*)(void* inventory, uint32_t id, int flag);
using NodeFn = void* (*)(void* inventory);
using InventoryFn = void* (*)(void* unit, const char* file, int line);
using ItemFn = void* (*)(void* itemOrInventory);
using UnitByIdFn = void* (*)(void* game, int type, uint32_t id);
using UnitFn = void* (*)(void* unit);
using RoomRemoveFn = void (*)(void* room, int type, uint32_t id);
using CorpseGoneFn = void (*)(void* game, uint32_t ownerId, uint32_t corpseId, uint8_t add);
using VoidUnitFn = void (*)(void* unit);
using FreeUnitFn = void (*)(void* game, void* unit);

EnterFn g_enterOriginal = nullptr;
nocorpse::PickupCounters g_pickup;
bool g_pickupInstalled = false;
uint64_t g_exe = 0;

template <typename F>
auto Call(uint64_t rva) noexcept -> F {
	return reinterpret_cast<F>(g_exe + rva);
}

auto ProbesMatch(uint64_t exe) noexcept -> bool {
	for (const Probe& p : Probes) {
		const auto* at = reinterpret_cast<const uint8_t*>(exe + p.rva);
		for (int i = 0; i < p.length; ++i) {
			if (at[i] != p.bytes[i]) {
				return false;
			}
		}
	}
	return true;
}

// The corpse list entries of a player (flag 1 = a body): copied out because a pickup edits the list.
auto CollectCorpses(void* player, nocorpse::CorpseIds* out) noexcept -> bool {
	__try {
		void* inv = Call<InventoryFn>(InventoryRva)(player, "no_corpse", 0);
		if (inv == nullptr) {
			return false;
		}
		int guard = 0;
		for (void* node = Call<NodeFn>(FirstCorpseRva)(inv); node != nullptr && guard < 64; node = d2r::At<void*>(node, NodeNext), ++guard) {
			out->Add(d2r::At<uint32_t>(node, NodeFlag), d2r::At<uint32_t>(node, NodeId));
		}
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

auto ItemsOn(void* unit) noexcept -> int {
	void* inv = Call<InventoryFn>(InventoryRva)(unit, "no_corpse", 0);
	int n = 0;
	for (void* it = inv != nullptr ? Call<ItemFn>(FirstItemRva)(inv) : nullptr; it != nullptr && n < 256; it = Call<ItemFn>(NextItemRva)(it)) {
		++n;
	}
	return n;
}

// One stored corpse: D2R's pickup, then the removal the interact handler does after a pickup that left nothing behind.
// Returns 1 picked up and removed, 0 kept as it was, -1 exception.
auto PickUpCorpse(void* game, void* player, uint32_t corpseId) noexcept -> int {
	__try {
		void* corpse = Call<UnitByIdFn>(UnitByIdRva)(game, PlayerType, corpseId);
		if (corpse == nullptr || corpse == player) {
			return 0;
		}
		PickupOut out = {};
		Call<PickupFn>(PickupRva)(&out, game, player, corpse);
		nocorpse::PickupResult r;
		r.ok = out.ok != 0;
		r.remaining = out.remaining;
		r.leftItems = ItemsOn(corpse);
		if (!nocorpse::CorpseMayBeRemoved(r)) {
			return 0;
		}
		const uint32_t ownerId = d2r::At<uint32_t>(player, 0x08);
		void* ownerInv = Call<InventoryFn>(InventoryRva)(player, "no_corpse", 0);
		Call<ListRemoveFn>(ListRemoveRva)(ownerInv, corpseId, 1);
		Call<RoomRemoveFn>(RoomRemoveRva)(Call<UnitFn>(UnitRoomRva)(corpse), PlayerType, corpseId);
		Call<CorpseGoneFn>(CorpseGoneRva)(game, ownerId, corpseId, 0);
		Call<VoidUnitFn>(ClearMaskRva)(corpse);
		Call<FreeUnitFn>(FreeUnitRva)(game, corpse);
		return 1;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

void EnterHook(void* client, void* player, void* game, void* room, int x, int y) {
	g_enterOriginal(client, player, game, room, x, y);  // places the character and its stored bodies
	++g_pickup.enters;
	if (game == nullptr || player == nullptr || d2r::At<int32_t>(player, d2r::UnitType) != PlayerType) {
		return;
	}
	nocorpse::CorpseIds ids;
	if (!CollectCorpses(player, &ids)) {
		++g_pickup.faults;
		return;
	}
	for (int i = 0; i < ids.Count(); ++i) {
		const int rc = PickUpCorpse(game, player, ids.At(i));
		if (rc > 0) {
			++g_pickup.picked;
		} else if (rc == 0) {
			++g_pickup.kept;
		} else {
			++g_pickup.faults;
		}
	}
}

auto FrameOf(void* game) noexcept -> uint32_t {
	return d2r::At<uint32_t>(game, GameFrame);
}

auto IdOf(void* unit) noexcept -> uint32_t {
	return d2r::At<uint32_t>(unit, UnitId);
}

void CorpseHook(void* game, void* player, int, int, void*) {
	++g_counts.corpses;
	if (game != nullptr && player != nullptr) {
		AcquireSRWLockExclusive(&g_lock);
		g_stamps.SetIfAbsent(reinterpret_cast<uintptr_t>(game), IdOf(player), FrameOf(game));
		ReleaseSRWLockExclusive(&g_lock);
	}
}

uint64_t PenaltyHook(void* game, void* victim, void*) {
	++g_counts.penalties;
	if (game != nullptr && victim != nullptr) {
		AcquireSRWLockExclusive(&g_lock);
		g_stamps.Set(reinterpret_cast<uintptr_t>(game), IdOf(victim), FrameOf(game));  // the start of the death
		ReleaseSRWLockExclusive(&g_lock);
	}
	return 0;
}

int RespawnHook(void* game, void* player, void* packet, int flag) {
	if (game != nullptr && player != nullptr && d2r::At<int32_t>(player, UnitMode) == ModeDead) {
		AcquireSRWLockExclusive(&g_lock);
		const uint32_t diedAt = g_stamps.Get(reinterpret_cast<uintptr_t>(game), IdOf(player));
		const bool allowed = nocorpse::RespawnAllowed(FrameOf(game), diedAt);
		if (allowed) {
			g_stamps.Clear(reinterpret_cast<uintptr_t>(game), IdOf(player));
		}
		ReleaseSRWLockExclusive(&g_lock);
		if (!allowed) {
			++g_counts.respawnsHeld;
			return 0;  // the request is ignored; the client asks again when the player presses the button again
		}
		++g_counts.respawns;
	}
	return g_respawnOriginal(game, player, packet, flag);
}

void OnGameEnd(const void* game) {
	AcquireSRWLockExclusive(&g_lock);
	g_stamps.ClearGame(reinterpret_cast<uintptr_t>(game));
	ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace

auto NoCorpseCounts(long* corpses, long* penalties, long* respawnsHeld, long* respawns) noexcept -> bool {
	*corpses = g_counts.corpses;
	*penalties = g_counts.penalties;
	if (respawnsHeld != nullptr) {
		*respawnsHeld = g_counts.respawnsHeld;
	}
	if (respawns != nullptr) {
		*respawns = g_counts.respawns;
	}
	return g_installed;
}

void NoCorpseRememberImage(const D2RL::PluginContext* context) noexcept {
	g_exe = context->exeBase;
}

auto InstallNoCorpse(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_exe = context->exeBase;
	const bool corpse = context->InstallInlineHook(CorpseRva, CorpseEntry, sizeof(CorpseEntry), reinterpret_cast<void*>(&CorpseHook),
	                                               reinterpret_cast<void**>(&g_corpseOriginal));
	const bool penalty = context->InstallInlineHook(PenaltyRva, PenaltyEntry, sizeof(PenaltyEntry), reinterpret_cast<void*>(&PenaltyHook),
	                                                reinterpret_cast<void**>(&g_penaltyOriginal));
	g_installed = corpse && penalty;
	if (g_installed) {
		RegisterGameEnd(&OnGameEnd);
		nocorpse::FormatReport(report, reportSize, 0, 0);
	} else {
		std::snprintf(report, reportSize, "off: hook failed (corpse %s, penalty %s)", corpse ? "ok" : "no", penalty ? "ok" : "no");
	}
	return g_installed;
}

auto InstallRespawnWait(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	if (!g_installed) {
		std::snprintf(report, reportSize, "off: needs the nocorpse hooks (they stamp the frame of the death)");
		return false;
	}
	const bool ok = context->InstallInlineHook(RespawnRva, RespawnEntry, sizeof(RespawnEntry), reinterpret_cast<void*>(&RespawnHook),
	                                           reinterpret_cast<void**>(&g_respawnOriginal));
	if (ok) {
		std::snprintf(report, reportSize, "on: respawn request 0x4b6160 waits %u frames (5 s) after the death", nocorpse::RespawnWaitFrames);
	} else {
		std::snprintf(report, reportSize, "off: hook failed");
	}
	return ok;
}

auto InstallCorpsePickup(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	if (!g_installed) {
		std::snprintf(report, reportSize, "off: needs the nocorpse hooks (with D2R's corpse creator on, bodies are D2R's again)");
		return false;
	}
	if (!ProbesMatch(g_exe)) {
		std::snprintf(report, reportSize, "off: a function the pickup calls does not match build 93847");
		return false;
	}
	const bool ok = context->InstallInlineHook(EnterRva, EnterEntry, sizeof(EnterEntry), reinterpret_cast<void*>(&EnterHook),
	                                           reinterpret_cast<void**>(&g_enterOriginal));
	g_pickupInstalled = ok;
	if (ok) {
		nocorpse::FormatPickupReport(report, reportSize, 0, 0, 0);
	} else {
		std::snprintf(report, reportSize, "off: hook failed");
	}
	return ok;
}

auto CorpsePickupCounts(long* enters, long* picked, long* kept, long* faults) noexcept -> bool {
	*enters = g_pickup.enters;
	*picked = g_pickup.picked;
	*kept = g_pickup.kept;
	*faults = g_pickup.faults;
	return g_pickupInstalled;
}

auto StoredCorpseIds(void* player, unsigned* ids, int max) noexcept -> int {
	if (g_exe == 0 || player == nullptr || d2r::At<int32_t>(player, d2r::UnitType) != PlayerType) {
		return -1;
	}
	nocorpse::CorpseIds list;
	if (!CollectCorpses(player, &list)) {
		return -1;
	}
	int n = 0;
	for (; n < list.Count() && n < max; ++n) {
		ids[n] = list.At(n);
	}
	return list.Count();
}

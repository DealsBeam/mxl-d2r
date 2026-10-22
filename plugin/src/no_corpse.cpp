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

auto InstallNoCorpse(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
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

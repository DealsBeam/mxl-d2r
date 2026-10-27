// Bugs #42 / #46: the client crashed at 0xd8cea reading null+0xc7 when a treasure goblin (or the dummy the Den of Evil boss's
// bone spear wrongly spawned, which shares treasure_goblin_monsound) came into view. The client monster update 0x1079e0 plays
// the monsounds Neutral sound (record +0x68, NeuTime +0x6c) through 0x107dd0(unit, sound id); 0x107dd0 calls 0xd8c70(id), which
// returns field +0xc7 of the sounds record and reads null when the id is < 0 or >= the row count. The player's Neutral id was
// -1 (an unresolved sound name; the sounds table had 11913 rows, D2R's own count), so the Median sound treasuregoblin_neutral_1
// was not in the table that game loaded.
//
// 0x107dd0(unit, id) returns the unit's playing sound with that id or null; an id outside the table now returns null (as if
// not playing) and the caller goes on without it. Entry bytes verified against the dump of build 93847; whole instructions, no
// rip-relative. Dev builds: MXL_SOUNDGUARD_TEST=1 turns the Neutral sound id (caller +0x107cd4) into -1 and lets the guard skip
// it, =2 passes the -1 to the game (reproduces the crash).
#include "sound_guard.h"

#define NOMINMAX
#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t PlayingSoundRva = 0x107dd0;  // (unit, sound id) -> the unit's playing sound with this id, or null
constexpr uint64_t SoundCountRva = 0x3b20c0;    // () -> sounds.txt row count
constexpr uint64_t NeutralCallRet = 0x107cd4;   // return address of the Neutral sound call in 0x1079e0

// 0x107dd0: mov [rsp+8],rbx / mov [rsp+0x10],rsi / push rdi / sub rsp,0x20   (15 bytes)
constexpr uint8_t PlayingSoundEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20};

using PlayingSoundFn = void* (*)(void* unit, int id) noexcept;
using SoundCountFn = int (*)() noexcept;

const D2RL::PluginContext* g_context = nullptr;
PlayingSoundFn g_original = nullptr;
SoundCountFn g_count = nullptr;
uint64_t g_base = 0;
int g_test = 0;
std::atomic<uint32_t> g_skipped{0};
std::atomic<int> g_lastId{0};
std::atomic<uint64_t> g_lastCaller{0};

void* PlayingSoundHook(void* unit, int id) noexcept {
	const uint64_t caller = reinterpret_cast<uint64_t>(_ReturnAddress()) - g_base;
	if (g_test != 0 && caller == NeutralCallRet) {
		id = -1;
		if (g_test == 2) {
			return g_original(unit, id);
		}
	}
	if (id >= 0 && id < g_count()) {
		return g_original(unit, id);
	}
	const uint32_t n = ++g_skipped;
	g_lastId = id;
	g_lastCaller = caller;
	if (n <= 5 && g_context != nullptr) {
		char line[160];
		std::snprintf(line, sizeof(line), "mxl-core: sound id %d is not in sounds.txt (%d rows), skipped (caller +0x%llx)", id, g_count(),
		              static_cast<unsigned long long>(caller));
		g_context->LogWarn(line);
	}
	return nullptr;
}

}  // namespace

auto InstallSoundGuard(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	g_base = context->exeBase;
	g_count = reinterpret_cast<SoundCountFn>(g_base + SoundCountRva);
#ifndef MXL_RELEASE
	char env[8] = "";
	if (GetEnvironmentVariableA("MXL_SOUNDGUARD_TEST", env, sizeof(env)) > 0) {
		g_test = env[0] == '2' ? 2 : (env[0] == '1' ? 1 : 0);
	}
#endif
	const bool ok = context->InstallInlineHook(PlayingSoundRva, PlayingSoundEntry, sizeof(PlayingSoundEntry),
	                                           reinterpret_cast<void*>(&PlayingSoundHook), reinterpret_cast<void**>(&g_original));
	std::snprintf(report, reportSize, "%s%s", ok ? "on: unit sound wrapper 0x107dd0 skips ids outside sounds.txt" : "off: hook failed",
	              g_test != 0 ? (g_test == 2 ? " (TEST 2: Neutral id -1 passed on)" : " (TEST 1: Neutral id -1)") : "");
	return ok;
}

void SoundGuardStatus(char* out, size_t outSize) noexcept {
	std::snprintf(out, outSize, "%u bad sound ids skipped (last id %d from +0x%llx)", g_skipped.load(), g_lastId.load(),
	              static_cast<unsigned long long>(g_lastCaller.load()));
}

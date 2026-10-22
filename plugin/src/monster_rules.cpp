// Median's monster level, players-count scaling, treasure-class level step, summon leash and Baal throne delay on D2R
// (docs/sigma-patch-audit.md items 17-19). The rules and their evidence are in monster_rules_logic.h; every site below is
// the D2R twin of a Median patch in D2Game.dll, found by structure and checked against the dump of build 93847.
//
//   monlevel  0x543b50 (inside 0x543120) takes the area level only when the difficulty is above Normal and the monstats flags
//             0x4 and 0x40 are clear. Two nops: the difficulty test (0x543d32) and the 0x40 test (0x543d44).
//   playersx  the record the monster setup 0x543e90 reads (D2RCore fills it through the thunk 0x3e2b43c): +0x24 life %, +0x28
//             experience %, +0x34 players count. The hook rewrites the two percentages before the original runs.
//   normaltc  0x441300 passes the monster's level (stat 12) to the class getter 0x397290 only above Normal: the 9 bytes
//             `cmp byte [rsi+0x104],dil / jbe` (0x44138f) become nops.
//   leash     0x5bebe5 pet think (100), 0x5bd1e0 approach chooser (80 and 50), 0x5c1080 owner think (50), AI 112 0x5c95d0 (50),
//             AI 107 0x5cca50 (50): all to 40 sub-tiles.
//   throne    AI 134 0x5c7070: the first-wave countdown `add ecx,0xfa` becomes 0x7d.
//
// Hook entries: 0x543e90 `40 55 53 57 41 54` (6 bytes, whole instructions) and 0x397290 `48 89 5c 24 08` (5 bytes); neither
// holds a rip-relative operand.
#include "monster_rules.h"

#include "d2r.h"
#include "monster_rules_logic.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;
using monster_rules::Patch;

constexpr uint64_t SetupRva = 0x543e90;     // (game, monster, record*)
constexpr uint64_t TcClassRva = 0x397290;   // (dataFlag, tc index, level) -> treasure class record or null
constexpr int RecHp = 0x24, RecExp = 0x28, RecPlayers = 0x34;

// 0x543e90: push rbp / push rbx / push rdi / push r12
constexpr uint8_t SetupEntry[] = {0x40, 0x55, 0x53, 0x57, 0x41, 0x54};
// 0x397290: mov [rsp+8],rbx
constexpr uint8_t TcClassEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08};

using SetupFn = void (*)(Game* game, Unit* monster, uint8_t* record);
using TcClassFn = const uint8_t* (*)(uint32_t dataFlag, uint32_t tcIndex, int32_t level);

SetupFn g_setupOriginal = nullptr;
TcClassFn g_tcOriginal = nullptr;

std::atomic<int32_t> g_records{0};
std::atomic<int32_t> g_players{0}, g_hpIn{0}, g_expIn{0}, g_hpOut{0}, g_expOut{0};
std::atomic<int32_t> g_tcCalls{0}, g_tcLeveled{0}, g_tcClass{0}, g_tcLevel{0};

// Changes every site or none (a site that does not match leaves the earlier ones put back).
auto ApplyAll(const D2RL::PluginContext* context, const Patch* patches, size_t count) noexcept -> bool {
	size_t done = 0;
	for (; done < count; ++done) {
		const Patch& p = patches[done];
		if (!context->PatchBytes(p.rva, p.before, p.size, p.after, p.size)) {
			break;
		}
	}
	if (done == count) {
		return true;
	}
	while (done > 0) {
		const Patch& p = patches[--done];
		(void)context->PatchBytes(p.rva, p.after, p.size, p.before, p.size);
	}
	return false;
}

void SetupHook(Game* game, Unit* monster, uint8_t* record) {
	if (record != nullptr) {
		const int32_t players = d2r::At<int32_t>(record, RecPlayers);
		const int32_t hp = d2r::At<int32_t>(record, RecHp);
		const int32_t exp = d2r::At<int32_t>(record, RecExp);
		const monster_rules::Scaling s = monster_rules::Players(players, hp, exp);
		*reinterpret_cast<int32_t*>(record + RecHp) = s.hp;
		*reinterpret_cast<int32_t*>(record + RecExp) = s.exp;
		g_records.fetch_add(1);
		g_players = players;
		g_hpIn = hp;
		g_expIn = exp;
		g_hpOut = s.hp;
		g_expOut = s.exp;
	}
	g_setupOriginal(game, monster, record);
}

const uint8_t* TcClassHook(uint32_t dataFlag, uint32_t tcIndex, int32_t level) {
	g_tcCalls.fetch_add(1);
	if (level > 0) {
		g_tcLeveled.fetch_add(1);
	}
	g_tcClass = static_cast<int32_t>(tcIndex & 0xffff);
	g_tcLevel = level;
	return g_tcOriginal(dataFlag, tcIndex, level);
}

}  // namespace

auto InstallMonsterLevel(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool ok = ApplyAll(context, monster_rules::MonLevelPatches, sizeof(monster_rules::MonLevelPatches) / sizeof(Patch));
	std::snprintf(report, reportSize, ok ? "on: monsters take the area level in every difficulty (0x543d32, 0x543d44)"
	                                     : "off: bytes at 0x543d32 / 0x543d44 differ");
	return ok;
}

auto InstallPlayersTable(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool hooked = context->InstallInlineHook(SetupRva, SetupEntry, sizeof(SetupEntry), reinterpret_cast<void*>(&SetupHook),
	                                               reinterpret_cast<void**>(&g_setupOriginal));
	std::snprintf(report, reportSize, hooked ? "on: players-count life +(n-1)*50 %%, no experience bonus (0x543e90)"
	                                         : "off: hook on 0x543e90 failed");
	return hooked;
}

auto InstallNormalTc(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool patched = ApplyAll(context, &monster_rules::NormalTcPatch, 1);
	bool traced = false;
	if (patched) {
		traced = context->InstallInlineHook(TcClassRva, TcClassEntry, sizeof(TcClassEntry), reinterpret_cast<void*>(&TcClassHook),
		                                    reinterpret_cast<void**>(&g_tcOriginal));
	}
	std::snprintf(report, reportSize, patched ? "on: Normal drops use the level-stepped class (0x44138f)%s"
	                                          : "off: bytes at 0x44138f differ%s",
	              patched && !traced ? " (trace hook on 0x397290 failed)" : "");
	return patched;
}

auto InstallLeash(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const size_t count = sizeof(monster_rules::LeashPatches) / sizeof(Patch);
	const bool ok = ApplyAll(context, monster_rules::LeashPatches, count);
	std::snprintf(report, reportSize, ok ? "on: owned units warp to their owner past %d sub-tiles (%u compares)"
	                                     : "off: a compare differs from the dump (0x5bec51 .. 0x5ccb39) (%d, %u)",
	              monster_rules::LeashSubTiles, static_cast<unsigned>(count));
	return ok;
}

auto InstallThrone(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool ok = ApplyAll(context, &monster_rules::ThronePatch, 1);
	std::snprintf(report, reportSize, ok ? "on: Baal's throne waits %d frames (0x5c71e6)" : "off: bytes at 0x5c71e6 differ (%d)",
	              monster_rules::ThroneDelayMedian);
	return ok;
}

auto PlayersTraceLast() noexcept -> PlayersTrace {
	return {g_records.load(), g_players.load(), g_hpIn.load(), g_expIn.load(), g_hpOut.load(), g_expOut.load()};
}

auto NormalTcTrace(bool reset) noexcept -> TcTrace {
	const TcTrace t = {g_tcCalls.load(), g_tcLeveled.load(), g_tcClass.load(), g_tcLevel.load()};
	if (reset) {
		g_tcCalls = 0;
		g_tcLeveled = 0;
	}
	return t;
}

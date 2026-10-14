// Whirlwind's hit rate as in 1.13c (Median XL keeps 1.13c's srvdofunc 76; docs/plugin-spec.md, "Whirlwind").
//
// D2R 2.4 reworked Whirlwind: its do function (0x568f40) waits `0x56ac00(unit, weapon)` frames between hits, which
// is the weapon's attack animation length at D2R's attack speed:
//   frames * 256 / (clamp(stat 68 + 120*IAS/(120+IAS), 15, 175) * animSpeed / 100)   (10 without a weapon).
// 1.13c (D2Game 0x6fce1470, D2Common 10564) takes the same animation length at its own speed, stat 68 + IAS (stat
// 93, no diminishing returns, no cap) + 100, and turns it into a delay through a table: < 12 frames -> 4, < 15 -> 6,
// < 18 -> 8, < 20 -> 10, < 23 -> 12, < 26 -> 14, else 16 (no weapon: 10). This hook rescales D2R's length to
// 1.13c's speed and applies the table. 0x56ac00 is only called by Whirlwind's do function (0x56916d, 0x569186,
// 0x5691e2), so nothing else changes. D2R's dual wield (both weapons, average of the two delays) stays.
#include "whirlwind.h"

#include <cstdint>
#include <cstdio>
#include <intrin.h>

namespace {

constexpr uint64_t HitDelayRva = 0x56ac00;  // (unit, weapon) -> frames until the next hit
// MOV [RSP+0x18],RBX / PUSH RBP / PUSH RSI / PUSH RDI / PUSH R14 / PUSH R15 / SUB RSP,0x80
constexpr uint8_t HitDelayEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x56,
                                     0x41, 0x57, 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00};
constexpr uint64_t UnitStatRva = 0x2f5020;    // (unit, stat, param) -> value (what 0x56ac00 reads stat 68 with)
constexpr uint64_t UnitStatNpRva = 0x2f5c60;  // (unit, stat) -> value (what D2R's IAS term reads stat 93 with)
constexpr uint64_t SpeedMaxRva = 0x34a5c0;    // (unit) -> 175 (250 for a player in some states)
constexpr int32_t SpeedMin = 15;              // 0x34ab50
constexpr int32_t StatAttackRate = 68;
constexpr int32_t StatIas = 93;

using HitDelayFn = int32_t (*)(void* unit, void* weapon) noexcept;
using UnitStatFn = int32_t (*)(void* unit, int32_t stat, int32_t param) noexcept;
using UnitStatNpFn = int32_t (*)(void* unit, int32_t stat) noexcept;
using SpeedMaxFn = int32_t (*)(void* unit) noexcept;

const D2RL::PluginContext* g_context = nullptr;
volatile long g_logged = 0;
HitDelayFn g_original = nullptr;
UnitStatFn g_unitStat = nullptr;
UnitStatNpFn g_unitStatNp = nullptr;
SpeedMaxFn g_speedMax = nullptr;

auto ClassicDelay(int32_t frames) noexcept -> int32_t {
	if (frames < 12) {
		return 4;
	}
	if (frames < 15) {
		return 6;
	}
	if (frames < 18) {
		return 8;
	}
	if (frames < 20) {
		return 10;
	}
	if (frames < 23) {
		return 12;
	}
	return frames < 26 ? 14 : 16;
}

auto HitDelay(void* unit, void* weapon) noexcept -> int32_t {
	const int32_t d2r = g_original(unit, weapon);
	if (unit == nullptr || weapon == nullptr) {
		return 10;  // 1.13c: no weapon, a hit every 10 frames (D2R returns 10 too)
	}
	const int32_t rate = g_unitStat(unit, StatAttackRate, 0);
	const int32_t ias = g_unitStatNp(unit, StatIas);
	int32_t d2rSpeed = rate + (ias != 0 ? ias * 120 / (ias + 120) : 0);
	const int32_t speedMax = g_speedMax(unit);
	d2rSpeed = d2rSpeed < SpeedMin ? SpeedMin : (d2rSpeed > speedMax ? speedMax : d2rSpeed);
	// 1.13c adds 100 to stat 68 itself; D2R leaves it out, so its units carry a base of 100 in the stat. Take the
	// base once either way (logged below to confirm on real units).
	const int32_t classicSpeed = (rate >= 100 ? rate : rate + 100) + ias;
	const int32_t frames = classicSpeed > 0 ? static_cast<int32_t>(static_cast<int64_t>(d2r) * d2rSpeed / classicSpeed) : d2r;
	const int32_t delay = ClassicDelay(frames);
	if (_InterlockedIncrement(&g_logged) <= 6) {
		char line[192];
		std::snprintf(line, sizeof(line),
		              "mxl-core whirlwind: stat68 %d, IAS %d, D2R speed %d -> %d frames; 1.13c speed %d -> %d frames -> hit every %d",
		              rate, ias, d2rSpeed, d2r, classicSpeed, frames, delay);
		g_context->LogInfo(line);
	}
	return delay;
}

}  // namespace

auto InstallWhirlwind(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	g_unitStat = reinterpret_cast<UnitStatFn>(context->exeBase + UnitStatRva);
	g_unitStatNp = reinterpret_cast<UnitStatNpFn>(context->exeBase + UnitStatNpRva);
	g_speedMax = reinterpret_cast<SpeedMaxFn>(context->exeBase + SpeedMaxRva);
	const bool ok = context->InstallInlineHook(HitDelayRva, HitDelayEntry, sizeof(HitDelayEntry), HitDelay, &g_original);
	std::snprintf(report, reportSize, ok ? "on: 1.13c hit rate (4-16 frames by weapon speed)" : "off: hook failed");
	return ok;
}

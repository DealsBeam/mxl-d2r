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
//
// Chance to cast on striking / on attack while spinning (Median fixes D2's bug that Whirlwind does not trigger it):
// D2R's item-event handler for those stats (function 20) rolls the chance during a spin as it does for any hit, but its
// cast (0x5896e0: caster, skill, level, target, flag) first asks whether the caster has state 54 (uninterruptable) and
// returns 0 if so; Whirlwind holds state 54 for the whole spin (with state 174, whirlwind). Hook on 0x5896e0: for a caster
// in the Whirlwind state the state-54 bit is taken out of the unit's state array for the length of the cast and put
// back after it. Nothing else is touched: other casters and other uninterruptable skills keep D2R's refusal.
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

// --- chance to cast while spinning -----------------------------------------------------------------------------------

constexpr uint64_t ItemCastRva = 0x5896e0;  // (caster, skill, level, target, flag) -> 1 cast, 0 refused
// MOV [RSP+0x10],RBX / MOV [RSP+0x18],RBP / MOV [RSP+0x20],RSI / PUSH RDI / PUSH R14 / PUSH R15
constexpr uint8_t ItemCastEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18,
                                     0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57};
constexpr uint64_t HasStateRva = 0x2f6220;   // (unit, state) -> nonzero when set (what 0x5896e0 asks with state 54)
constexpr uint64_t StatListRva = 0x34b870;   // (unit) -> the unit's stat list
constexpr int32_t StateUninterruptable = 54;
constexpr int32_t StateWhirlwind = 174;
constexpr int StatListFlags = 0x1c;          // negative: an extended list (the unit's own), whose states start at +0xaf0
constexpr int StatesDefault = 0xaf0;         // D2R's state array
constexpr int StatesWide = 0x2b00;           // the wider array of the (default-off) states fix

using ItemCastFn = uint64_t (*)(void* caster, uint32_t skill, uint32_t level, void* target, uint32_t flag) noexcept;
using HasStateFn = uint64_t (*)(void* unit, uint32_t state) noexcept;
using StatListFn = uint8_t* (*)(void* unit) noexcept;

ItemCastFn g_itemCastOriginal = nullptr;
HasStateFn g_hasState = nullptr;
StatListFn g_statList = nullptr;
volatile long g_spinCasts = 0;   // casts let through during a spin
volatile long g_spinRefused = 0; // casts still refused during a spin (the cast itself said no)

// The state-54 bit of the unit, taken out where D2R's own test reads it (the default array or the states fix's), or
// nullptr when the unit is not spinning or the bit cannot be found.
auto TakeOutUninterruptable(void* unit) noexcept -> volatile uint32_t* {
	if (unit == nullptr || g_hasState(unit, StateWhirlwind) == 0 || g_hasState(unit, StateUninterruptable) == 0) {
		return nullptr;
	}
	uint8_t* list = g_statList(unit);
	if (list == nullptr || *reinterpret_cast<const int32_t*>(list + StatListFlags) >= 0) {
		return nullptr;
	}
	const uint32_t mask = 1u << (StateUninterruptable & 31);
	constexpr int Bases[] = {StatesDefault, StatesWide};
	for (const int base : Bases) {
		auto* word = reinterpret_cast<volatile uint32_t*>(list + base + (StateUninterruptable >> 5) * 4);
		if ((*word & mask) == 0) {
			continue;
		}
		*word &= ~mask;
		if (g_hasState(unit, StateUninterruptable) == 0) {
			return word;  // that was the bit D2R reads
		}
		*word |= mask;
	}
	return nullptr;
}

auto ItemCast(void* caster, uint32_t skill, uint32_t level, void* target, uint32_t flag) noexcept -> uint64_t {
	volatile uint32_t* word = TakeOutUninterruptable(caster);
	const uint64_t result = g_itemCastOriginal(caster, skill, level, target, flag);
	if (word != nullptr) {
		*word |= 1u << (StateUninterruptable & 31);
		_InterlockedIncrement(result != 0 ? &g_spinCasts : &g_spinRefused);
	}
	return result;
}

}  // namespace

void WhirlwindProcStats(long* cast, long* refused) noexcept {
	*cast = g_spinCasts;
	*refused = g_spinRefused;
}

auto InstallWhirlwindProcs(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_hasState = reinterpret_cast<HasStateFn>(context->exeBase + HasStateRva);
	g_statList = reinterpret_cast<StatListFn>(context->exeBase + StatListRva);
	const bool ok = context->InstallInlineHook(ItemCastRva, ItemCastEntry, sizeof(ItemCastEntry), ItemCast, &g_itemCastOriginal);
	std::snprintf(report, reportSize, ok ? "on: chance to cast on striking / on attack works during a spin (0x5896e0 hooked)" : "off: hook failed");
	return ok;
}

auto InstallWhirlwind(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	g_unitStat = reinterpret_cast<UnitStatFn>(context->exeBase + UnitStatRva);
	g_unitStatNp = reinterpret_cast<UnitStatNpFn>(context->exeBase + UnitStatNpRva);
	g_speedMax = reinterpret_cast<SpeedMaxFn>(context->exeBase + SpeedMaxRva);
	const bool ok = context->InstallInlineHook(HitDelayRva, HitDelayEntry, sizeof(HitDelayEntry), HitDelay, &g_original);
	std::snprintf(report, reportSize, ok ? "on: 1.13c hit rate (4-16 frames by weapon speed)" : "off: hook failed");
	return ok;
}

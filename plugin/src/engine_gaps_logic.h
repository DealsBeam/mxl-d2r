// Three small engine rules of Median's D2Sigma (docs/sigma-patch-audit.md item 21) as constants and pure functions
// (engine_gaps.cpp patches D2R; tests/engine_gaps_test.cpp).
//
//   * hitfloor (game2-014, D2Game 0x3de79): the missile hit function 20 (D2R 0x45ec20) floors its second calc result (the
//     damage / length value kept in the hit record) at 1; Median floors it at 0, so a skill level whose calc comes out 0 or
//     less does nothing instead of one point. D2R: `cmp eax, esi / mov ebx, eax / cmovl ebx, esi` (esi = 1) at 0x45edce
//     becomes `xor ebx, ebx / test eax, eax / cmovg ebx, eax`.
//   * recast (game4-020, D2Game 0x94b75): srvdofunc 47 (timed self buff with a state, D2R slot 47 = 0x56f750) returns at
//     once while the caster already has the skill's state; Median NOPs the guard, so the skill can be cast again while it
//     runs (3 skills, Howl of the Spirits among them). D2R's guard is `call 0x3351b0 / test eax, eax / jne` at 0x56f859
//     (13 bytes; 1.13c's is 15).
//   * slowfloor (game4-029, D2Game 0x9a256 and the other callers): the floor that keeps a slow (aura, chill) from taking a
//     speed below -50 percent is D2R 0x449f40 (monsters: a monstats byte per difficulty, anything else: -50). Median's
//     replacement, D2Combat 0x10057300, returns the monstats byte for monsters and -12 / -25 / -50 for players on
//     Normal / Nightmare / Hell (it tests the difficulty byte at game +0x6d for 0 and 1).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace gaps {

inline constexpr uint64_t Hit20FloorRva = 0x45edce;
inline constexpr uint8_t Hit20FloorOriginal[] = {0x3b, 0xc6, 0x8b, 0xd8, 0x0f, 0x4c, 0xde};
inline constexpr uint8_t Hit20FloorPatched[] = {0x31, 0xdb, 0x85, 0xc0, 0x0f, 0x4f, 0xd8};
static_assert(sizeof(Hit20FloorOriginal) == sizeof(Hit20FloorPatched));

inline constexpr uint64_t RecastGuardRva = 0x56f859;
inline constexpr uint8_t RecastGuardOriginal[] = {0xe8, 0x52, 0x59, 0xdc, 0xff, 0x85, 0xc0, 0x0f, 0x85, 0xb9, 0x02, 0x00, 0x00};
inline constexpr uint8_t RecastGuardPatched[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
static_assert(sizeof(RecastGuardOriginal) == sizeof(RecastGuardPatched));

inline constexpr uint64_t SlowFloorRva = 0x449f40;
// mov [rsp+8], rbx / push rdi / sub rsp, 0x20 (10 bytes, whole instructions, no rip-relative operand)
inline constexpr uint8_t SlowFloorEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20};

inline constexpr int UnitTypePlayer = 0;

// What the floor function returns for a player: the difficulty (0 Normal, 1 Nightmare, 2 Hell) picks -12 / -25 / -50.
constexpr auto PlayerSlowFloor(int difficulty) noexcept -> int {
	return difficulty == 0 ? -12 : (difficulty == 1 ? -25 : -50);
}

// The value of the damage calc after the floor: D2R's 1, Median's 0 (negative results become the floor).
constexpr auto Hit20Value(int calc, int floorValue) noexcept -> int {
	return calc > floorValue ? calc : floorValue;
}
inline constexpr int Hit20FloorVanilla = 1, Hit20FloorMedian = 0;

// Compare the bytes at an address (as the harness `gaps` verb reads them) with a patch.
inline auto SameBytes(const uint8_t* a, const uint8_t* b, size_t n) noexcept -> bool {
	for (size_t i = 0; i < n; ++i) {
		if (a[i] != b[i]) {
			return false;
		}
	}
	return true;
}

inline void FormatReport(char* out, size_t outSize, bool hit20, bool recast, bool slow, bool offHit20, bool offRecast, bool offSlow) noexcept {
	auto word = [](bool on, bool off) { return on ? "on" : (off ? "off (word)" : "FAILED"); };
	std::snprintf(out, outSize, "hit 20 floor 0x45edce %s | recast guard 0x56f859 %s | player slow floor -12/-25/-50 0x449f40 %s", word(hit20, offHit20),
	              word(recast, offRecast), word(slow, offSlow));
}

}  // namespace gaps

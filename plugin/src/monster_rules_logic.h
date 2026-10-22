// Median's monster level, players-count scaling, summon leash and Baal throne rules as pure data and functions (no game
// calls), so plugin/tests/monster_rules_test.cpp can run them. Source: D2Sigma.dll patch list of the D2Game.dll patch
// (docs/sigma-patch-audit.md items 17-19, research/sigma_patch_audit.tsv game1-012, game1-018..020, game5-001..007),
// read against the 1.13c D2Game.dll Median patches; D2R twins found by structure (docs/d2r-addresses.md section 12).
//
//   monlevel  1.13c monster init 0x6fccfdb0 takes the area level (levels.txt MonLvl) only for Nightmare and Hell, and only for
//             monsters without monstats flag bits 0x4 and 0x40. Median nops the difficulty gate (`test esi,esi / jle`, 0xafee6)
//             and the 0x40 test (0xafef9). D2R twin: 0x543b50, inside the monster setup 0x543120.
//   playersx  The players-count scaling record {life %, experience %, ..., players} (1.13c 0x6fccf840): vanilla gives both
//             percentages the table 0, 0, 50, 100 .. 350 by player count; Median's stub 0x1008f720 gives life the table
//             (n-1)*50 (n 1-8 the same numbers; 8 players: +350 %) and experience 0 for every count.
//   normaltc  1.13c treasure-class executor 0x6fc32d60 steps the class up by the monster's level only for Nightmare and Hell;
//             Median nops the 7-byte difficulty gate (0x12d6a). D2R: 0x441300.
//   leash     Four owned-unit follow compares (pet think 0x1deab, approach chooser 0x1e20f / 0x1e35e, owner think 0x1e70c) and
//             two monster AIs' warp-to-owner limits (AI 112 DruidBear, AI 107 Raven) become 0x28 = 40 sub-tiles.
//   throne    Baal's throne AI (AI 134) starts the wave countdown at frame + 0xfa; Median halves it (0x7d).
#pragma once

#include <cstddef>
#include <cstdint>

namespace monster_rules {

// ---- players-count scaling -----------------------------------------------------------------------------------------------
// Median's life percentage: 0 for n <= 1, (n-1)*50 above (its table covers 1-8, beyond 8 the same formula).
inline constexpr auto HpPercent(int32_t players) noexcept -> int32_t {
	return players <= 1 ? 0 : (players - 1) * 50;
}

// Median's experience percentage: none, at any count.
inline constexpr auto ExpPercent(int32_t) noexcept -> int32_t {
	return 0;
}

// The two percentages of D2R's scaling record (+0x24 life, +0x28 experience) after Median's rule. A record D2R left at 0 / 0
// (one player, or a monster class that opts out of the scaling: Median's stub leaves those at 0 too) stays as it is.
struct Scaling {
	int32_t hp;
	int32_t exp;
};
inline constexpr auto Players(int32_t players, int32_t hp, int32_t exp) noexcept -> Scaling {
	if (hp == 0 && exp == 0) {
		return {0, 0};
	}
	return {HpPercent(players), ExpPercent(players)};
}

// ---- summon / hireling leash ---------------------------------------------------------------------------------------------
inline constexpr int32_t LeashSubTiles = 0x28;  // 40: the owner is farther than this: the follower warps to it

// A warp when the owner is farther than the limit (the compares are `ja` / `jg` / `jle` of the distance against it).
inline constexpr auto Warps(int32_t distance, int32_t limit = LeashSubTiles) noexcept -> bool {
	return distance > limit;
}

// ---- Baal's throne ---------------------------------------------------------------------------------------------------------
inline constexpr int32_t ThroneDelayVanilla = 0xfa;  // frames from the first sight of a player to the first wave
inline constexpr int32_t ThroneDelayMedian = 0x7d;

// ---- patch sites (D2R build 93847; file offset = RVA) ----------------------------------------------------------------------
struct Patch {
	uint64_t rva;
	uint8_t size;
	uint8_t before[9];
	uint8_t after[9];
};

// monlevel: the difficulty gate `test r15d,r15d / je` and the 0x40 flag test `test [rip+..],eax / jne` become nops. What stays:
// the classic test (`cmp dl,1 / je` at 0x543d2d) and the 0x4 flag test (0x543d3c).
inline constexpr Patch MonLevelPatches[] = {
    {0x543d32, 5, {0x45, 0x85, 0xff, 0x74, 0x19}, {0x90, 0x90, 0x90, 0x90, 0x90}},
    {0x543d44, 8, {0x85, 0x05, 0x9e, 0x59, 0x85, 0x01, 0x75, 0x04}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}},
};

// normaltc: `cmp byte [rsi+0x104],dil / jbe 0x4413b1` (game difficulty is above Normal) becomes nops: the monstats null test and
// the two flag tests after it stay.
inline constexpr Patch NormalTcPatch = {0x44138f, 9,
                                        {0x40, 0x38, 0xbe, 0x04, 0x01, 0x00, 0x00, 0x76, 0x19},
                                        {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}};

// leash: the compared immediate is the last byte of each instruction.
inline constexpr Patch LeashPatches[] = {
    // 0x5bebe5 (pet think, twin of 1.13c 0x6fc3db40): `cmp ecx,0x64 / jbe`: warp mode 3 through 0x5c1460
    {0x5bec51, 3, {0x83, 0xf9, 0x64}, {0x83, 0xf9, 0x28}},
    // 0x5bd1e0 (approach chooser, twin of 0x6fc3e140): `cmp r12d,0x50` (no line of sight) and `cmp r12d,0x32` (mode 3)
    {0x5bd29f, 4, {0x41, 0x83, 0xfc, 0x50}, {0x41, 0x83, 0xfc, 0x28}},
    {0x5bd474, 4, {0x41, 0x83, 0xfc, 0x32}, {0x41, 0x83, 0xfc, 0x28}},
    // 0x5c1080 (owner think, twin of 0x6fc3e640): `cmp r15d,0x32 / jle`
    {0x5c1136, 4, {0x41, 0x83, 0xff, 0x32}, {0x41, 0x83, 0xff, 0x28}},
    // 0x5c95d0 (AI 112 DruidBear): `cmp r12d,0x32 / jle`
    {0x5c96aa, 4, {0x41, 0x83, 0xfc, 0x32}, {0x41, 0x83, 0xfc, 0x28}},
    // 0x5cca50 (AI 107 Raven): `cmp eax,0x32 / jle`
    {0x5ccb39, 3, {0x83, 0xf8, 0x32}, {0x83, 0xf8, 0x28}},
};

// throne: `add ecx,0xfa` -> `add ecx,0x7d` in 0x5c7070 (the immediate's first byte).
inline constexpr Patch ThronePatch = {0x5c71e6, 6, {0x81, 0xc1, 0xfa, 0x00, 0x00, 0x00}, {0x81, 0xc1, 0x7d, 0x00, 0x00, 0x00}};

// The leash sites' last byte is the limit.
inline constexpr auto LeashSiteLimit(const Patch& p) noexcept -> uint8_t {
	return p.after[p.size - 1];
}

}  // namespace monster_rules

// Pure rules of the item generation hooks (item_gen.cpp): Median's class lock, ethereal roll and effect, socket roll and
// dye colour index. No engine access, so mxl_tests can run them. docs/subsystems/items-xp.md section 5.
#pragma once

#include <cstdint>

namespace item_gen {

// --- Class lock (D2Sigma D2Items.cpp 0x1007e470) -------------------------------------------------------------------

// Item stat 463 (D2R 492) holds class + 1 when the item is locked to a class (the 14 recipes `properties_422`); 0 means
// no lock, and class 7 (stat value 8) is Median's "no class" so it locks nothing either. A player of `playerClass`
// (0 Amazon .. 6 Assassin) may use the item when it is not locked or locked to its own class.
constexpr auto ClassLockAllows(int32_t lockStat, int32_t playerClass) noexcept -> bool {
	if (lockStat <= 0) {
		return true;
	}
	const int32_t locked = lockStat - 1;
	return locked == 7 || locked == playerClass;
}

// --- Ethereal (D2Sigma 0x1007e120 + 0x1007fdd0 + 0x1007fd70) -----------------------------------------------------

// qualities.txt column `EthChance` (work/sigma/qualities.txt): the chance in 1/1000 that an item of that quality becomes
// ethereal, read by 0x1007e120 as `word [row + 2]` and compared with `rand(1000) <= chance`. 25 for low, normal,
// superior, magic, rare and unique; 0 for set (5) and the crafted qualities (8, 9).
constexpr auto EtherealChancePerMille(int32_t quality) noexcept -> int32_t {
	switch (quality) {
		case 1: case 2: case 3: case 4: case 6: case 7:
			return 25;
		default:
			return 0;
	}
}

// D2R's own roll (0x442d60) lets 5 of 100 items of item level 100+ through to the ethereal function. Median's roll is
// `rand(1000) <= chance`, i.e. chance + 1 of 1000. To get Median's overall rate out of D2R's gate the items that passed
// are accepted with probability (chance + 1) / 50 (capped at 1): 5% x 26/50 = 2.6% = 26/1000.
inline constexpr int32_t D2rEtherealGatePerMille = 50;
constexpr auto EtherealAcceptOutOf(int32_t chancePerMille) noexcept -> int32_t {
	const int32_t n = chancePerMille + 1;
	return chancePerMille < 0 ? 0 : (n > D2rEtherealGatePerMille ? D2rEtherealGatePerMille : n);
}

// Ethereal raises damage (weapons: stats 0x15-0x18, 0x9f, 0xa0) or defence (stat 0x1f) by 25 percent (0x1007fdd0:
// value + MulDiv(value, 25, 100)). D2R's own function adds 50.
constexpr auto EtherealStat(int32_t value) noexcept -> int32_t {
	return static_cast<int32_t>(value + static_cast<int64_t>(value) * 25 / 100);
}

// ... and durability and maximum durability become 75 percent of the maximum, at least 1 (D2R: maximum / 2 + 1).
// Nothing for items without durability.
constexpr auto EtherealDurability(int32_t maxDurability) noexcept -> int32_t {
	if (maxDurability <= 0) {
		return maxDurability;
	}
	const int32_t v = static_cast<int32_t>(static_cast<int64_t>(maxDurability) * 75 / 100);
	return v > 1 ? v : 1;
}

// --- Sockets (D2Sigma 0x1007e240) --------------------------------------------------------------------------------

// Items of quality 1-3 on a base that can have sockets (and without the generator's no-socket flag) get
// `rand() % 7` sockets: none for a remainder of 0 (1 in 7), otherwise the remainder, which D2R's socket setter
// (0x375560) clips to what the base can hold. D2R's own roll is "33 in 100 items get rand(max) + 1".
inline constexpr int32_t SocketRollModulus = 7;
constexpr auto SocketsFromRoll(int32_t remainder) noexcept -> int32_t {
	return remainder > 0 && remainder < SocketRollModulus ? remainder : 0;
}

// --- Dye (D2Sigma 0x1007e6d0, 0x1007e6c0) --------------------------------------------------------------------------

inline constexpr int32_t DyeColours = 21;

// The stat holds 1..21 (stat 368, five bits: values 16 and up arrive as negative numbers when the stat is sent signed).
// Returns the palette index 0..20, or -1 when the item is not dyed.
constexpr auto DyeIndex(int32_t stat) noexcept -> int32_t {
	if (stat < 0) {
		stat += 32;
	}
	return stat >= 1 && stat <= DyeColours ? stat - 1 : -1;
}

// D2R's item colour byte (0x376600 / the code in 0x3710d0): the base's transform kind (items record +0x151, 1..8) in the
// upper three bits plus the colour; 0 without a kind or for a colour above 20.
constexpr auto ComposeColour(int32_t kind, int32_t colour) noexcept -> uint8_t {
	return kind >= 1 && kind <= 8 && colour >= 0 && colour <= 20 ? static_cast<uint8_t>((kind << 5) + (colour & 0x1f)) : 0;
}

}  // namespace item_gen

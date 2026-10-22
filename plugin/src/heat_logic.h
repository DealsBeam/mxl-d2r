// The Fire Sorceress's Heat (docs/plugin-spec.md, "Heat"): Molten Core's passive puts the heat loss rate in stat 297
// (100 + level / 3 - mana regeneration / 10) and the mana bar is the heat bar. D2Sigma's mana regeneration
// (D2Resource.cpp 0x100a1b50) replaces the regeneration with a drain of that rate; Emberstep and the fire missiles put a
// negative 297 on the caster for a while, which turns the drain into a build-up. No engine access, so mxl_tests can run it.
#pragma once

#include <algorithm>
#include <cstdint>

namespace heat {

constexpr int32_t DefaultRegenFrames = 0x1d4c;  // used when the class has no regeneration time
constexpr int32_t HeatBonusExtra = 40;          // the regeneration bonus counts 40% more while heat drains

struct Regen {
	int32_t mana = 0;      // stat 8 (x256)
	int32_t maxMana = 0;   // maximum mana (x256)
	int32_t base = 0;      // maxMana / regeneration frames per tick; 0 under state 0x55 (no regeneration)
	int32_t bonus = 0;     // stat 27, mana regeneration %; 0 under state 0x55
	int32_t recovery = 0;  // stat 26
	int32_t heat = 0;      // stat 297
};

// charstats' regeneration byte x 25 frames, or the default
constexpr auto RegenFrames(int32_t seconds) noexcept -> int32_t {
	return seconds * 25 == 0 ? DefaultRegenFrames : seconds * 25;
}

constexpr auto Scaled(int64_t value, int64_t percent) noexcept -> int32_t {
	return static_cast<int32_t>(value * percent / 100);
}

// The change of the mana stat for one regeneration tick with heat on (stat 297 != 0), outside a town: the regeneration
// scaled by 140 + bonus %, times -heat %, kept inside [-mana, maxMana - mana].
constexpr auto HeatDelta(const Regen& r) noexcept -> int32_t {
	const int32_t regen = Scaled(r.base, r.bonus + 100 + HeatBonusExtra) + r.recovery;
	const int32_t delta = Scaled(regen, -static_cast<int64_t>(r.heat));
	return std::max(std::min(delta, r.maxMana - r.mana), -r.mana);
}

}  // namespace heat

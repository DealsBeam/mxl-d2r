// Median's added property functions (properties.txt func 27, 28, 29; pure rules, properties_ext.cpp installs them).
//
// D2Sigma keeps replacements of D2Game's property functions in its own table (code 0x1009cb9d-0x1009d1a0, D2Properties.cpp);
// the three that rows use beyond D2R's 1-25 and 36 each add one stat from the property's {param, min, max} without a
// random roll:
//
//   27  layer = param << 8 | min (16 bits), value = max    innate_fire_damage_1f_of: param 2 / element / percent
//   28  layer = param (16 bits),            value = min    properties_419 (a marker on amulets: 1 / 1 / 1)
//   29  layer = param (16 bits),            value = max    item_mana_bytime_2/3 (stat "syn1" for the skill in param)
//
// D2Sigma 0x1009cbe0 (27), 0x1009ccf0 (28), 0x1009cc30 (29); the function number is the slot in D2Sigma's table minus 28,
// which matches the data (func 11 and 24 sit at the slots that wrap D2Common's 11 and 24).
#pragma once

#include <cstdint>

namespace mxl_props {

struct Layered {
	int32_t layer;
	int32_t value;
};

inline constexpr int FirstFunction = 27;
inline constexpr int LastFunction = 29;

constexpr auto Compute(int func, int32_t param, int32_t min, int32_t max) noexcept -> Layered {
	const auto u16 = [](int32_t v) { return static_cast<int32_t>(static_cast<uint16_t>(v)); };
	switch (func) {
	case 27: return {u16(static_cast<int32_t>(static_cast<uint32_t>(u16(param)) * 256u + static_cast<uint32_t>(u16(min)))), max};
	case 28: return {u16(param), min};
	case 29: return {u16(param), max};
	default: return {0, 0};
	}
}

}  // namespace mxl_props

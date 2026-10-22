// Pure rules of the three Median item/skill rules that remove D2's old bookkeeping (docs/median-rules-audit.md IT1,
// IT2, IT3, SK3). No engine access, so mxl_tests can run them.
#pragma once

#include <cstdint>

namespace itemrules {

constexpr uint32_t ItemFlagIdentified = 0x10;  // item data flag word (+0x18)

// IT3: Median's generators push 1 into SetItemFlag(0x10 identified) for magic, rare, set and unique items (D2Game 0x104c1,
// 0x15443, 0x13daa and the unique picker); low, normal, superior and crafted items are left as the generator makes them.
// Qualities: 1 low, 2 normal, 3 superior, 4 magic, 5 set, 6 rare, 7 unique, 8 crafted.
constexpr auto DropsIdentified(int32_t quality) noexcept -> bool {
	return quality >= 4 && quality <= 7;
}

// IT2: D2R's quantity-use function (0x439130) returns the quantity it left plus one (0 when the item had none to
// use). Median's patch removes the decrement, so the stack stays as it is and the call still reports a use when the
// stack is not empty.
constexpr auto QuantityUseResult(int32_t quantity) noexcept -> int32_t {
	return quantity > 0 ? quantity : 0;
}

// SK3: skills cost no mana in a town; elsewhere the cost is unchanged.
constexpr auto ManaCharge(int32_t cost, bool inTown, bool freeInTown) noexcept -> int32_t {
	return (inTown && freeInTown) ? 0 : cost;
}

}  // namespace itemrules

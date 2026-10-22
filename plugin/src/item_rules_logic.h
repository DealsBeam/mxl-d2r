// Pure rules of the three Median item/skill rules that remove D2's old bookkeeping (docs/median-rules-audit.md IT1,
// IT2, IT3, SK3). No engine access, so mxl_tests can run them.
#pragma once

#include <cstdint>

namespace itemrules {

constexpr uint32_t ItemFlagIdentified = 0x10;  // item data flag word (+0x18)

// IT3: an item that was picked up (the pickup function reported success) and is not identified yet gets identified.
constexpr auto NeedsIdentify(uint32_t flags) noexcept -> bool {
	return (flags & ItemFlagIdentified) == 0;
}

// The pickup function (0x471950) returns 1 on success; a refused pickup (too far, no room) identifies nothing.
constexpr auto PickupSucceeded(uint64_t result) noexcept -> bool {
	return (result & 0xff) != 0;
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

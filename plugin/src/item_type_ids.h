// D2R's fixed item type ids against ours, a startup self-check (item_type_ids.cpp, bugs #49 / #50 / #55). Off word: typeids.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Checks the 25 "item is of type N" immediates (N >= 67) of D2R.exe against our id of the same type and logs each mismatch. True when all match.
auto InstallItemTypeIds(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;
auto ItemTypeIdsMismatched() noexcept -> int;

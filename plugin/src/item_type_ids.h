// D2R's fixed item type ids moved to Median's (item_type_ids.cpp, bug #49). Off word: typeids.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Rewrites the 25 "item is of type N" immediates (N >= 67) of D2R.exe to Median's id of the same type. True when every site is done.
auto InstallItemTypeIds(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;
auto ItemTypeIdsPatched() noexcept -> int;

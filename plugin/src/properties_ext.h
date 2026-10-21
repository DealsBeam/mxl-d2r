// Median's property functions 27-29 in D2R's property function table (properties_ext.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Fills the empty slots 27-29 of D2R's property function table (0x2386ab0, indexed by properties.txt func). Number
// installed (0-3).
auto InstallPropertiesExt(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;

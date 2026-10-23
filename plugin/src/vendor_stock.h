// Median's vendor stock on D2R (vendor_stock.cpp, rules in vendor_stock_logic.h). Off word: vendorstock.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Removes D2R's extra magic copies above item level 24 (0x53cd2b) and caps the Nightmare vendor item level (0x53cfa0) and traces it. True when the part is on.
auto InstallVendorStock(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

struct VendorStockTrace {
	long builds;  // item level cap calls (one per stock build)
	int ilvlIn, ilvlOut, difficulty;
};
auto VendorStockTraceNow() noexcept -> VendorStockTrace;

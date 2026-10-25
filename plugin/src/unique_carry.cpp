// D2R's carry1 test compares the number in the unique record (+0x30) instead of the unique itself; Median gives 1 to every
// carry1 unique. The data-table listener rewrites each non-zero number to the row + 1 (unique_carry_logic.h). Nothing else
// reads the number but the placement test (0x473960: refuse when a carried unique has the same non-zero number) and the
// record comparison it shares with 0x5269c0, so the only change is that two different uniques no longer count as one.
#include "unique_carry.h"

#include "unique_carry_logic.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t RvaDataTables = 0x300a90;  // (data flag 0-3) -> table block or null; +0x13c8 unique records, +0x13d0 their count
constexpr int DataUniqueRecords = 0x13c8;
constexpr int DataUniqueCount = 0x13d0;
constexpr uint32_t DataFlags = 4;

uint64_t g_base = 0;
volatile long g_loads = 0;
volatile long g_rewritten = 0;
volatile long g_carry = 0;
bool g_installed = false;

// The data tables are loaded (game thread, after D2R's loader and the plugins' tables).
void __cdecl OnDataTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	InterlockedIncrement(&g_loads);
	const auto blocks = reinterpret_cast<const uint8_t* (*)(uint32_t)>(g_base + RvaDataTables);
	long rewritten = 0, carry = 0;
	const uint8_t* done[DataFlags] = {};  // the data flags share blocks (1 and 2 are one): each record array once
	for (uint32_t flag = 0; flag < DataFlags; ++flag) {
		const uint8_t* block = blocks(flag);
		if (block == nullptr || IsBadReadPtr(block + DataUniqueRecords, 16)) {
			continue;
		}
		uint8_t* records = *reinterpret_cast<uint8_t* const*>(block + DataUniqueRecords);
		const int64_t count = *reinterpret_cast<const int64_t*>(block + DataUniqueCount);
		if (records == nullptr || count <= 0 || count > 0xffff || IsBadWritePtr(records, static_cast<UINT_PTR>(count) * unique_carry::RecordSize)) {
			continue;
		}
		bool seen = false;
		for (uint32_t k = 0; k < flag; ++k) {
			seen = seen || done[k] == records;
		}
		if (seen) {
			continue;
		}
		done[flag] = records;
		for (int64_t row = 0; row < count; ++row) {
			auto* value = reinterpret_cast<int32_t*>(records + row * unique_carry::RecordSize + unique_carry::CarryOffset);
			if (*value > 0) {
				++carry;
				const int32_t want = unique_carry::CarryValue(static_cast<int32_t>(row), *value);
				if (want != *value) {
					*value = want;
					++rewritten;
				}
			}
		}
	}
	InterlockedExchange(&g_carry, carry);
	InterlockedExchangeAdd(&g_rewritten, rewritten);
}

}  // namespace

auto InstallUniqueCarry(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_base = context->exeBase;
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success && lifecycle != nullptr &&
	    D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		const D2RL::Lifecycle::DataTablesLoadedListener listener{D2RL::Lifecycle::DataTablesLoadedListenerSize, 0, OnDataTablesLoaded, nullptr};
		D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
		g_installed = lifecycle->registerDataTablesLoadedListener(context, &listener, &handle) == D2RL::Lifecycle::Result::Success &&
		              handle != D2RL::Lifecycle::InvalidHandle;
	}
	if (!g_installed) {
		std::snprintf(report, reportSize, "off: no data-table listener");
		return false;
	}
	std::snprintf(report, reportSize, "on: each carry1 unique gets its own carry number (row + 1) after the table load (loads 0, carry1 uniques 0, rewritten 0)");
	return true;
}

auto UniqueCarryStats(long* loads, long* rewritten, long* carry) noexcept -> bool {
	*loads = g_loads;
	*rewritten = g_rewritten;
	*carry = g_carry;
	return g_installed;
}

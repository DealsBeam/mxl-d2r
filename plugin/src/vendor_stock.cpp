// Median's vendor stock rules on D2R (rules and addresses in vendor_stock_logic.h).
#include "vendor_stock.h"

#include "vendor_stock_logic.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

// 0x349860 (unit, file, line) -> the unit's class id, 0x3971a0 (data flag, class id) -> the NPC's record, act at +10 (the calls the cap
// function itself makes; file and line only feed its assert text).
constexpr uint64_t UnitClassRva = 0x349860;
constexpr uint64_t NpcRecordRva = 0x3971a0;
using CapFn = int (*)(uint8_t* game, void* npc, int ilvl);
using UnitClassFn = uint32_t (*)(void* unit, const char* file, int line);
using NpcRecordFn = const uint8_t* (*)(uint32_t dataFlag, uint32_t classId);
CapFn g_capOriginal = nullptr;
UnitClassFn g_unitClass = nullptr;
NpcRecordFn g_npcRecord = nullptr;
std::atomic<long> g_builds{0};
int g_ilvlIn = 0, g_ilvlOut = 0, g_difficulty = 0;

int CapHook(uint8_t* game, void* npc, int ilvl) {
	int out = g_capOriginal(game, npc, ilvl);
	if (game != nullptr && npc != nullptr && game[0x104] == vendor_stock::DifficultyNightmare) {
		const uint8_t* record = g_npcRecord(game[0x106], g_unitClass(npc, "vendor_stock", 0x19b));
		out = record != nullptr ? vendor_stock::NightmareLevel(record[10], out) : out;
	}
	g_builds.fetch_add(1);
	g_ilvlIn = ilvl;
	g_ilvlOut = out;
	g_difficulty = game != nullptr ? game[0x104] : -1;
	return out;
}

}  // namespace

auto InstallVendorStock(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool patched = context->PatchBytes(vendor_stock::ExtraCopiesRva, vendor_stock::ExtraCopiesOriginal, sizeof(vendor_stock::ExtraCopiesOriginal),
	                                         vendor_stock::ExtraCopiesPatched, sizeof(vendor_stock::ExtraCopiesPatched));
	g_unitClass = reinterpret_cast<UnitClassFn>(context->exeBase + UnitClassRva);
	g_npcRecord = reinterpret_cast<NpcRecordFn>(context->exeBase + NpcRecordRva);
	const bool hooked = context->InstallInlineHook(vendor_stock::CapRva, vendor_stock::CapEntry, sizeof(vendor_stock::CapEntry),
	                                               reinterpret_cast<void*>(&CapHook), reinterpret_cast<void**>(&g_capOriginal));
	std::snprintf(report, reportSize, "%s: no extra magic copies above item level 24 (0x53cd2b %s), Nightmare item level cap (0x53cfa0 %s)",
	              patched && hooked ? "on" : "off", patched ? "patched" : "bytes differ", hooked ? "hooked" : "hook failed");
	return patched && hooked;
}

auto VendorStockTraceNow() noexcept -> VendorStockTrace {
	return {g_builds.load(), g_ilvlIn, g_ilvlOut, g_difficulty};
}

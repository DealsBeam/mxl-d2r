// Missile CollideType 9 and 10 (collide_types_logic.h has the background and the rules).
//
// The server's tick (0x463100) and hit finder, and the client's, index a table of 16-byte rows {check function, mask} with the
// missile's CollideType; D2R's tables hold types 0-8 and are followed by unrelated data, so a Median missile with type 9 would
// read garbage (and one of the plugin's missiles with it would crash or never collide). The tables are moved into pages of the
// plugin with rows 9 and 10 filled in, and the nine instructions that load a table's address (`lea reg, [rip + table]`, 7 bytes,
// only the disp32 changes) are pointed at the copies; the originals stay as they were.
#include "collide_types.h"

#include "collide_types_logic.h"
#include "mxl_collide_types.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

struct Site {
	uint64_t rva;
	uint8_t bytes[7];
	collide::Table table;
};

constexpr Site Sites[] = {
    {0x455aa6, {0x48, 0x8d, 0x0d, 0xcb, 0xb8, 0xf3, 0x01}, collide::Table::ServerMask},
    {0x457b7f, {0x48, 0x8d, 0x15, 0xf2, 0x97, 0xf3, 0x01}, collide::Table::ServerMask},
    {0x4621a7, {0x48, 0x8d, 0x15, 0xc2, 0xf1, 0xf2, 0x01}, collide::Table::ServerFn},
    {0x4632b4, {0x4c, 0x8d, 0x3d, 0xb5, 0xe0, 0xf2, 0x01}, collide::Table::ServerFn},
    {0x46627a, {0x4c, 0x8d, 0x2d, 0xef, 0xb0, 0xf2, 0x01}, collide::Table::ServerFn},
    {0x5378cc, {0x48, 0x8d, 0x0d, 0xa5, 0x9a, 0xe5, 0x01}, collide::Table::ServerMask},
    {0x1b52d6, {0x48, 0x8d, 0x05, 0xfb, 0x36, 0x1a, 0x02}, collide::Table::ClientMask},
    {0x1b5642, {0x48, 0x8d, 0x0d, 0x87, 0x33, 0x1a, 0x02}, collide::Table::ClientFn},
    {0x1b9fdf, {0x48, 0x8d, 0x05, 0xf2, 0xe9, 0x19, 0x02}, collide::Table::ClientMask},
};
constexpr int SiteCount = static_cast<int>(sizeof(Sites) / sizeof(Sites[0]));

constexpr uint64_t RvaDataTables = 0x300a90;    // (data flag 0-3) -> table block or null; +0x1128 = rows of missiles.txt (int64)
constexpr uint64_t RvaMissileRecord = 0x166040;  // (data flag, row) -> record or null; CollideType is the byte at +0x1ab
constexpr int DataMissileCount = 0x1128;
constexpr int RecordCollideType = 0x1ab;
constexpr int DataFlags = 4;

uint64_t g_base = 0;
collide::Row* g_server = nullptr;
collide::Row* g_client = nullptr;
volatile long g_loads = 0;
volatile long g_restored = 0;

// The data tables are loaded (game thread, after D2R's loader and the plugins' tables): D2R clamped CollideType to 8, so every
// record Median gave a higher type is written back (only where the clamp left its mark; see collide::NeedsRecordFix).
void __cdecl OnDataTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	InterlockedIncrement(&g_loads);
	const auto blocks = reinterpret_cast<const uint8_t* (*)(uint32_t)>(g_base + RvaDataTables);
	const auto record = reinterpret_cast<uint8_t* (*)(uint32_t, int32_t)>(g_base + RvaMissileRecord);
	long restored = 0;
	for (uint32_t flag = 0; flag < DataFlags; ++flag) {
		const uint8_t* block = blocks(flag);
		if (block == nullptr) {
			continue;
		}
		const int64_t rows = *reinterpret_cast<const int64_t*>(block + DataMissileCount);
		for (const mxl_collide::Record& r : mxl_collide::Records) {
			if (r.row < 0 || r.row >= rows) {
				continue;
			}
			uint8_t* rec = record(flag, r.row);
			if (rec != nullptr && collide::NeedsRecordFix(rec[RecordCollideType], r.type)) {
				rec[RecordCollideType] = static_cast<uint8_t>(r.type);
				++restored;
			}
		}
	}
	InterlockedExchangeAdd(&g_restored, restored);
}

// A page within reach of the image (the lea's disp32 spans +-2 GB from the instruction), 64 KB steps outwards.
auto AllocNear(uint64_t base, size_t size) noexcept -> void* {
	constexpr uint64_t Reach = 0x60000000;
	constexpr uint64_t Step = 0x10000;
	constexpr uint64_t Gap = 0x4000000;
	for (uint64_t d = 0; d < Reach; d += Step) {
		const uint64_t above = base + Gap + d;
		const uint64_t below = base > Gap + d ? base - Gap - d : 0;
		for (const uint64_t at : {above, below}) {
			if (at == 0) {
				continue;
			}
			void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
			if (p != nullptr) {
				return p;
			}
		}
	}
	return nullptr;
}

auto TableOf(collide::Table t) noexcept -> collide::Row* {
	return collide::IsServer(t) ? g_server : g_client;
}

}  // namespace

auto InstallCollideTypes(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_base = context->exeBase;
	const auto* engineServer = reinterpret_cast<const collide::Row*>(g_base + collide::ServerBase);
	const auto* engineClient = reinterpret_cast<const collide::Row*>(g_base + collide::ClientBase);
	if (!collide::RowsMatch(engineServer, collide::ServerRows, g_base) || !collide::RowsMatch(engineClient, collide::ClientRows, g_base)) {
		std::snprintf(report, reportSize, "off: the engine's collide tables 0x2391370 / 0x23589d0 are not as analysed");
		return false;
	}
	for (const Site& s : Sites) {
		const auto* at = reinterpret_cast<const uint8_t*>(g_base + s.rva);
		uint8_t now[7];
		std::memcpy(now, at, sizeof(now));
		if (std::memcmp(now, s.bytes, sizeof(now)) != 0 || !collide::IsRipLea(now) ||
		    collide::LeaTarget(s.rva, now) != static_cast<int64_t>(collide::BaseOf(s.table))) {
			std::snprintf(report, reportSize, "off: table reader at 0x%llx differs", static_cast<unsigned long long>(s.rva));
			return false;
		}
	}
	const size_t bytes = static_cast<size_t>(collide::Rows) * collide::RowSize;
	g_server = static_cast<collide::Row*>(AllocNear(g_base, bytes));
	g_client = static_cast<collide::Row*>(AllocNear(g_base, bytes));
	if (g_server == nullptr || g_client == nullptr) {
		std::snprintf(report, reportSize, "off: no memory within reach of the image");
		g_server = g_client = nullptr;
		return false;
	}
	collide::BuildTable(engineServer, g_server);
	collide::BuildTable(engineClient, g_client);
	uint8_t patched[SiteCount][7];
	for (int i = 0; i < SiteCount; ++i) {
		const uint64_t target = reinterpret_cast<uint64_t>(TableOf(Sites[i].table)) + collide::OffsetOf(Sites[i].table);
		if (!collide::Redirect(g_base + Sites[i].rva, Sites[i].bytes, target, patched[i])) {
			std::snprintf(report, reportSize, "off: table out of rip-relative reach");
			g_server = g_client = nullptr;
			return false;
		}
	}
	int done = 0;
	for (; done < SiteCount; ++done) {
		if (!context->PatchBytes(Sites[done].rva, Sites[done].bytes, sizeof(Sites[done].bytes), patched[done], sizeof(patched[done]))) {
			break;
		}
	}
	if (done != SiteCount) {
		for (int i = 0; i < done; ++i) {
			(void)context->PatchBytes(Sites[i].rva, patched[i], sizeof(patched[i]), Sites[i].bytes, sizeof(Sites[i].bytes));
		}
		std::snprintf(report, reportSize, "off: %d of %d table readers patched (undone)", done, SiteCount);
		g_server = g_client = nullptr;
		return false;
	}
	// The records: D2R's loader clamped the types above 8, the listener writes them back after every table load.
	const D2RL::LifecycleService* lifecycle = nullptr;
	bool listening = false;
	if (context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success && lifecycle != nullptr &&
	    D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		const D2RL::Lifecycle::DataTablesLoadedListener listener{D2RL::Lifecycle::DataTablesLoadedListenerSize, 0, OnDataTablesLoaded, nullptr};
		D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
		listening = lifecycle->registerDataTablesLoadedListener(context, &listener, &handle) == D2RL::Lifecycle::Result::Success &&
		            handle != D2RL::Lifecycle::InvalidHandle;
	}
	std::snprintf(report, reportSize, "on: %d table readers moved to %d-row tables, types 9 (mask 0x%x) and 10 (mask 0x%x) added; %zu records restored after the table load%s",
	              SiteCount, collide::Rows, collide::Additions[0].mask, collide::Additions[1].mask, sizeof(mxl_collide::Records) / sizeof(mxl_collide::Records[0]),
	              listening ? "" : " (NO LISTENER: records stay clamped)");
	return true;
}

auto CollideTypeRow(bool server, int type, uint64_t* fnRva, uint32_t* mask) noexcept -> bool {
	const collide::Row* table = server ? g_server : g_client;
	if (table == nullptr || type < 0 || type >= collide::Rows) {
		return false;
	}
	*fnRva = table[type].fn == 0 ? 0 : table[type].fn - g_base;
	*mask = table[type].mask;
	return true;
}

auto CollideRecordStats(long* loads, long* restored) noexcept -> void {
	*loads = g_loads;
	*restored = g_restored;
}

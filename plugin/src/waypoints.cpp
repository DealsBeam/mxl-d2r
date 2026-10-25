// Waypoint numbers above 38 and the waypoint panel (waypoints_logic.h; docs/subsystems/areas-drlg.md "Waypoints").
//
// A waypoint number up to 111 is stored by the engine (record bit table at RVA 0x2389010, saved in the "WS" section), tested by
// the server when a waypoint is used (0x4b6f10) and sent to the client (packet 0x63): VerifyWaypoints only checks that.
//
// What a number needs on top is a button in the waypoint panel. The panel (WaypointsPanel) keeps one tab per actinfo.txt row
// with nine level slots, and a button table of nine {level, other} entries at RVA 0x2a969e0 that the tab fill 0x1ea9b0 writes and
// the button builder 0x1e9b40, the click handler 0x1ebf50 and the refresh 0x1ec430 read (the buttons themselves are made at
// run time from the layout's template: Act<tab>_Button<i>, one per table entry). InstallWaypointPanel
//   * hooks the tab fill: for a tab it runs the engine's own fill over synthetic 0x34-byte actinfo rows built from the generated
//     list (mxl_waypoints.h: every waypoint level under its act, nine to a row), so the bit test, the number lookup and the
//     "not the level you stand in" flag are the engine's, and collects the entries of every row into a longer table;
//   * points the three readers' `lea reg, [rip + table]` at that table (verified 7-byte instructions, only the disp32
//     changes) and sets the count dword 0x2a969d4 to the total.
// The longer table lives in a page allocated within +-2 GB of the image so the rip-relative reach holds.
#include "waypoints.h"

#include "waypoints_logic.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint64_t BitTableRva = 0x2389010;    // 112 x {u16 word, u16 mask}
constexpr uint64_t BitCountRva = 0x23891d0;    // dword 0x70
constexpr uint64_t ConvertRva = 0x3d6670;      // (version, level, u16* number) -> 1 when the level has a waypoint
constexpr uint64_t SetRva = 0x3d6890;          // (record, number)
constexpr uint8_t ConvertEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83};
constexpr uint8_t SetEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x0f, 0xb7, 0xda, 0x48, 0x8b, 0xf9};

// the panel (dump of build 93847)
constexpr uint64_t FillRva = 0x1ea9b0;        // (panel, tab): fills the button table and the count for one tab
constexpr uint8_t FillEntry[] = {0x40, 0x55, 0x53, 0x56, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8d, 0x6c, 0x24, 0xc9};  // push x5, lea rbp,[rsp-0x37]
constexpr uint64_t CountRva = 0x2a969d4;      // dword: entries in the button table
constexpr uint64_t TableRva = 0x2a969e0;      // 9 x ButtonEntry (the engine's own)
constexpr uint64_t RowsPtrRva = 0x2a96a28;    // actinfo entry vector: data pointer
constexpr uint64_t RowsSizeRva = 0x2a96a30;   // ... and size (entries)
constexpr int EngineEntries = wp::PanelSlots;
constexpr int TableCapacity = wp::MaxButtons;  // entries of the plugin's table

// The three readers of the table, `lea reg, [rip + disp32]` (7 bytes: REX, 0x8d, modrm, disp32).
struct Reader {
	uint64_t rva;
	uint8_t bytes[7];
};
constexpr Reader Readers[] = {
    {0x1ea198, {0x48, 0x8d, 0x0d, 0x41, 0xc8, 0x8a, 0x02}},  // 0x1e9b40 button builder: lea rcx
    {0x1ec05a, {0x48, 0x8d, 0x15, 0x7f, 0xa9, 0x8a, 0x02}},  // 0x1ebf50 click handler: lea rdx
    {0x1ec582, {0x4c, 0x8d, 0x35, 0x57, 0xa4, 0x8a, 0x02}},  // 0x1ec430 refresh: lea r14
};
constexpr int ReaderCount = static_cast<int>(sizeof(Readers) / sizeof(Readers[0]));

using FillFn = void (*)(void* panel, int tab);

FillFn g_fill = nullptr;
uint64_t g_base = 0;
wp::ButtonEntry* g_table = nullptr;  // TableCapacity entries
volatile long g_active = 0;
volatile long g_fills = 0;
volatile long g_lastTab = -1;
volatile long g_lastCount = 0;

// One run of the engine's fill for row `row` of the current actinfo vector; appends the entries it made.
auto RunFill(void* panel, int row, int& total) noexcept -> void {
	g_fill(panel, row);
	const int made = *reinterpret_cast<const int32_t*>(g_base + CountRva);
	const auto* engine = reinterpret_cast<const wp::ButtonEntry*>(g_base + TableRva);
	for (int i = 0; i < made && i < EngineEntries && total < TableCapacity; ++i) {
		g_table[total++] = engine[i];
	}
}

void FillHook(void* panel, int tab) {
	if (g_active == 0 || tab < 0) {
		g_fill(panel, tab);
		return;
	}
	auto* rowsPtr = reinterpret_cast<uint64_t*>(g_base + RowsPtrRva);
	auto* rowsSize = reinterpret_cast<uint64_t*>(g_base + RowsSizeRva);
	wp::ActRow rows[wp::MaxRows] = {};
	const int n = wp::FillRows(tab + 1, rows, wp::MaxRows);
	int total = 0;
	if (n <= 0) {  // a tab the list has nothing for: the engine's own row (actinfo.txt)
		RunFill(panel, tab, total);
	} else {
		const uint64_t savedPtr = *rowsPtr;
		const uint64_t savedSize = *rowsSize;
		*rowsPtr = reinterpret_cast<uint64_t>(rows);
		*rowsSize = static_cast<uint64_t>(n);
		for (int r = 0; r < n; ++r) {
			RunFill(panel, r, total);
		}
		*rowsPtr = savedPtr;
		*rowsSize = savedSize;
	}
	*reinterpret_cast<int32_t*>(g_base + CountRva) = total;
	InterlockedIncrement(&g_fills);
	InterlockedExchange(&g_lastTab, tab);
	InterlockedExchange(&g_lastCount, total);
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

// The reader with its disp32 pointing at `table`; false when the table is out of reach.
auto Redirected(const Reader& r, const void* table, uint8_t (&out)[7]) noexcept -> bool {
	std::memcpy(out, r.bytes, sizeof(out));
	const int64_t disp = static_cast<int64_t>(reinterpret_cast<uint64_t>(table)) - static_cast<int64_t>(g_base + r.rva + sizeof(out));
	const int32_t d32 = static_cast<int32_t>(disp);
	if (static_cast<int64_t>(d32) != disp) {
		return false;
	}
	std::memcpy(out + 3, &d32, sizeof(d32));
	return true;
}

}  // namespace

auto VerifyWaypoints(uint64_t exeBase, char* report, size_t reportSize) noexcept -> bool {
	const auto* table = reinterpret_cast<const uint16_t*>(exeBase + BitTableRva);
	int32_t count = 0;
	std::memcpy(&count, reinterpret_cast<const void*>(exeBase + BitCountRva), sizeof(count));
	const bool bytes = std::memcmp(reinterpret_cast<const void*>(exeBase + ConvertRva), ConvertEntry, sizeof(ConvertEntry)) == 0 &&
	                   std::memcmp(reinterpret_cast<const void*>(exeBase + SetRva), SetEntry, sizeof(SetEntry)) == 0;
	if (!bytes || !wp::TableMatches(table, count)) {
		std::snprintf(report, reportSize, "off: waypoint record not as analysed (%s), numbers above 38 unchecked",
		              bytes ? "bit table differs" : "entry bytes of 0x3d6670 / 0x3d6890 differ");
		return false;
	}
	const wp::Summary s = wp::Summarise();
	char names[96] = "";
	size_t used = 0;
	for (int i = 0; i < s.unlisted && i < 8; ++i) {
		const int w = std::snprintf(names + used, sizeof(names) - used, "%s%d", i > 0 ? ", " : "", s.firstUnlisted[i]);
		used = w > 0 && used + static_cast<size_t>(w) < sizeof(names) ? used + static_cast<size_t>(w) : used;
	}
	std::snprintf(report, reportSize, "%d waypoint levels, numbers up to %d of %d bits%s%s; panel list has a button for %d%s%s",
	              s.levels, s.highest, wp::RecordBits, s.duplicates > 0 ? ", DUPLICATE numbers" : "",
	              s.outOfRange > 0 ? ", NUMBER PAST THE RECORD" : "", s.listed, s.unlisted > 0 ? ", none for levels " : "",
	              s.unlisted > 0 ? names : "");
	return s.duplicates == 0 && s.outOfRange == 0;
}

auto InstallWaypointPanel(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_base = context->exeBase;
	const wp::Summary sum = wp::Summarise();
	if (sum.duplicates != 0 || sum.outOfRange != 0) {
		std::snprintf(report, reportSize, "panel off: waypoint list is not usable");
		return false;
	}
	if (std::memcmp(reinterpret_cast<const void*>(g_base + FillRva), FillEntry, sizeof(FillEntry)) != 0) {
		std::snprintf(report, reportSize, "panel off: entry bytes of the tab fill 0x1ea9b0 differ");
		return false;
	}
	for (const Reader& r : Readers) {
		if (std::memcmp(reinterpret_cast<const void*>(g_base + r.rva), r.bytes, sizeof(r.bytes)) != 0) {
			std::snprintf(report, reportSize, "panel off: button table reader at 0x%llx differs", static_cast<unsigned long long>(r.rva));
			return false;
		}
	}
	g_table = static_cast<wp::ButtonEntry*>(AllocNear(g_base, 4096));
	if (g_table == nullptr) {
		std::snprintf(report, reportSize, "panel off: no memory within reach of the image");
		return false;
	}
	std::memset(g_table, 0, 4096);
	uint8_t patched[ReaderCount][7];
	for (int i = 0; i < ReaderCount; ++i) {
		if (!Redirected(Readers[i], g_table, patched[i])) {
			std::snprintf(report, reportSize, "panel off: table out of rip-relative reach");
			return false;
		}
	}
	if (!context->InstallInlineHook(FillRva, FillEntry, sizeof(FillEntry), reinterpret_cast<void*>(&FillHook), reinterpret_cast<void**>(&g_fill))) {
		std::snprintf(report, reportSize, "panel off: tab fill hook failed");
		return false;
	}
	int done = 0;
	for (; done < ReaderCount; ++done) {
		if (!context->PatchBytes(Readers[done].rva, Readers[done].bytes, sizeof(Readers[done].bytes), patched[done], sizeof(patched[done]))) {
			break;
		}
	}
	if (done != ReaderCount) {
		// The hook stays idle (g_active is 0), so the fill is the engine's; put back the readers already moved.
		for (int i = 0; i < done; ++i) {
			(void)context->PatchBytes(Readers[i].rva, patched[i], sizeof(patched[i]), Readers[i].bytes, sizeof(Readers[i].bytes));
		}
		std::snprintf(report, reportSize, "panel off: %d of %d table readers patched (undone)", done, ReaderCount);
		return false;
	}
	InterlockedExchange(&g_active, 1);
	std::snprintf(report, reportSize, "panel on: tab fill 0x1ea9b0 + %d table readers, up to %d buttons per tab (widest %d)", ReaderCount,
	              wp::MaxButtons, sum.widest);
	return true;
}

auto WaypointPanelStats(char* out, size_t outSize) noexcept -> void {
	std::snprintf(out, outSize, "panel %s, %ld tab fills, last tab %ld with %ld buttons", g_active != 0 ? "on" : "off", g_fills, g_lastTab,
	              g_lastCount);
}

auto WaypointPanelTable(int* count, const void** entries) noexcept -> bool {
	if (g_active == 0 || g_table == nullptr) {
		return false;
	}
	*count = *reinterpret_cast<const int32_t*>(g_base + CountRva);
	*entries = g_table;
	return true;
}

namespace {
// The town predicate called ONLY by waypoint travel (0x5933f0). A true result selects
// entry kind 13: 0x3dad90 finds the waypoint room and its object (Objects flags +0x127,
// bit 0x40). Kind 0 instead selects a preset entry in 0x3dac20. Leave every other
// caller of the town predicate, including portals and NPC travel, untouched.
auto WaypointObjectEntry(int level) noexcept -> int {
	return wp::NeedsObjectEntry(level) ? 1 : 0;
}
}  // namespace

auto InstallWaypointArrival(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	constexpr uint64_t site = 0x59347b;
	constexpr uint8_t expected[] = {0xe8, 0x00, 0xd3, 0xd5, 0xff}; // call town predicate 0x2f0780
	if (std::memcmp(reinterpret_cast<const void*>(context->exeBase + site), expected, sizeof(expected)) != 0) {
		std::snprintf(report, reportSize, "off: waypoint entry selector bytes differ");
		return false;
	}
	auto* stub = static_cast<uint8_t*>(AllocNear(context->exeBase, 4096));
	if (stub == nullptr) {
		std::snprintf(report, reportSize, "off: no memory within call reach");
		return false;
	}
	// mov rax, destination; jmp rax. The call site's return address remains the engine's.
	uint8_t code[] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
	const auto destination = reinterpret_cast<uint64_t>(&WaypointObjectEntry);
	std::memcpy(code + 2, &destination, sizeof(destination));
	std::memcpy(stub, code, sizeof(code));
	DWORD previous = 0;
	const int64_t displacement = reinterpret_cast<int64_t>(stub) - static_cast<int64_t>(context->exeBase + site + 5);
	const int32_t relative = static_cast<int32_t>(displacement);
	bool ok = displacement == relative && VirtualProtect(stub, 4096, PAGE_EXECUTE_READ, &previous) != 0;
	if (ok) {
		FlushInstructionCache(GetCurrentProcess(), stub, sizeof(code));
		uint8_t call[5] = {0xe8};
		std::memcpy(call + 1, &relative, sizeof(relative));
		ok = context->PatchBytes(site, expected, sizeof(expected), call, sizeof(call));
	}
	if (!ok) {
		VirtualFree(stub, 0, MEM_RELEASE);
	}
	std::snprintf(report, reportSize, "%s: waypoint travel selects the waypoint object for Median preset levels (entry 13)", ok ? "on" : "off");
	return ok;
}

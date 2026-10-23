// Item tooltip text readback for the test harness (docs/item-tooltips.md). See tooltip_probe.h.
//
//   0x2d6520 (viewer, record, value, layer, flag, char out[256], mode) -> al: one property line (the descfunc switch)
//   0x2dc4b0 (item, out, size, p4, p5, p6, p7, p8, p9, p10): the whole property text of an item (D2Client\src\UI\mods.cpp)
//
// The line formatter is hooked by item_text.cpp (which also writes Median's own lines); the probe is its observer. The build
// hook passes everything through and only copies what the game wrote. Entry bytes checked against the dump of build 93847
// (whole instructions, no rip-relative bytes).
#include "tooltip_probe.h"

#include "d2r.h"
#include "item_text.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint64_t BuildRva = 0x2dc4b0;

// 0x2dc4b0: mov [rsp+0x20],rbx / push rbp / rsi / rdi / r12 / r13 / r14 / r15 (16 bytes)
constexpr uint8_t BuildEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};

using BuildFn = void (*)(void* item, char* out, int32_t size, uint64_t p4, uint64_t p5, uint64_t p6, uint64_t p7, uint64_t p8,
                         uint64_t p9, uint64_t p10);

constexpr uint32_t RingSize = 512;
BuildFn g_buildOriginal = nullptr;
tip::Line g_ring[RingSize];
volatile LONG g_seq = 0;
tip::Build g_build = {};
volatile LONG g_buildSeq = 0;
bool g_replaying = false;
bool g_installed = false;
uint64_t g_base = 0;

auto Matches(uint64_t base, uint64_t rva, const uint8_t* bytes, size_t size) noexcept -> bool {
	return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes, size) == 0;
}

void CopyText(char* to, size_t size, const char* from, size_t limit) noexcept {
	size_t n = 0;
	if (from != nullptr) {
		__try {
			while (n + 1 < size && n < limit && from[n] != '\0') {
				to[n] = from[n];
				++n;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
	to[n] = '\0';
}

// Called after every formatter call (item_text.cpp clears the buffer first: a descfunc without text leaves it as it was).
void OnFormat(void* viewer, const int16_t* record, int32_t value, uint32_t layer, char flag, int32_t mode, char ret, const char* out) {
	const LONG seq = InterlockedIncrement(&g_seq);
	tip::Line& line = g_ring[(static_cast<uint32_t>(seq) - 1) % RingSize];
	line.seq = 0;
	line.stat = -1;
	if (record != nullptr) {
		__try {
			line.stat = *record;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
	line.value = value;
	line.layer = layer;
	line.mode = mode;
	line.flag = static_cast<unsigned char>(flag);
	line.viewerType = -1;
	line.viewerId = 0;
	if (viewer != nullptr) {
		__try {
			line.viewerType = *reinterpret_cast<const int32_t*>(viewer);
			line.viewerId = *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(viewer) + 8);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
	line.ret = static_cast<unsigned char>(ret);
	CopyText(line.text, sizeof(line.text), out, 255);
	line.seq = static_cast<uint32_t>(seq);
}

void BuildHook(void* item, char* out, int32_t size, uint64_t p4, uint64_t p5, uint64_t p6, uint64_t p7, uint64_t p8, uint64_t p9,
               uint64_t p10) {
	if (!g_replaying) {
		g_build.valid = false;
		g_build.arg[0] = reinterpret_cast<uint64_t>(item);
		g_build.arg[1] = reinterpret_cast<uint64_t>(out);
		g_build.arg[2] = static_cast<uint32_t>(size);
		g_build.arg[3] = p4;
		g_build.arg[4] = p5;
		g_build.arg[5] = p6;
		g_build.arg[6] = p7;
		g_build.arg[7] = p8;
		g_build.arg[8] = p9;
		g_build.arg[9] = p10;
		std::memset(g_build.optional, 0, sizeof(g_build.optional));
		if (p10 != 0) {
			__try {
				std::memcpy(g_build.optional, reinterpret_cast<const void*>(p10), sizeof(g_build.optional));
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
		}
	}
	g_buildOriginal(item, out, size, p4, p5, p6, p7, p8, p9, p10);
	if (!g_replaying) {
		CopyText(g_build.text, sizeof(g_build.text), out, sizeof(g_build.text) - 1);
		g_build.seq = static_cast<uint32_t>(InterlockedIncrement(&g_buildSeq));
		g_build.valid = true;
	}
}

}  // namespace

auto InstallTooltipProbe(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_base = context->exeBase;
	if (!ItemTextHooked() || !Matches(g_base, BuildRva, BuildEntry, sizeof(BuildEntry))) {
		std::snprintf(report, reportSize, "off: the line formatter is not hooked or the entry bytes of 0x2dc4b0 differ");
		return false;
	}
	SetFormatObserver(&OnFormat);
	const bool format = true;
	const bool build = context->InstallInlineHook(BuildRva, BuildEntry, sizeof(BuildEntry), reinterpret_cast<void*>(&BuildHook),
	                                              reinterpret_cast<void**>(&g_buildOriginal));
	g_installed = format && build;
	std::snprintf(report, reportSize, "%s: test harness readback of item property text (0x2d6520 line formatter %s, 0x2dc4b0 item text %s)",
	              g_installed ? "on" : "partial", format ? "hooked" : "NOT hooked", build ? "hooked" : "NOT hooked");
	return g_installed;
}

auto TipLineCount() noexcept -> uint32_t {
	return static_cast<uint32_t>(g_seq);
}

auto TipLineAt(uint32_t seq, tip::Line* out) noexcept -> bool {
	if (seq == 0 || seq > static_cast<uint32_t>(g_seq) || static_cast<uint32_t>(g_seq) - seq >= RingSize) {
		return false;
	}
	const tip::Line& line = g_ring[(seq - 1) % RingSize];
	if (line.seq != seq) {
		return false;
	}
	*out = line;
	return true;
}

void TipReset() noexcept {
	InterlockedExchange(&g_seq, 0);
	std::memset(g_ring, 0, sizeof(g_ring));
}

auto TipLastBuild(tip::Build* out) noexcept -> bool {
	if (!g_installed || !g_build.valid) {
		return false;
	}
	*out = g_build;
	return true;
}

auto TipRender(void* item, char* out, int size) noexcept -> bool {
	if (!g_installed || !g_build.valid || item == nullptr || out == nullptr || size <= 0) {
		return false;
	}
	std::memset(out, 0, static_cast<size_t>(size));
	uint8_t optional[16];
	std::memcpy(optional, g_build.optional, sizeof(optional));
	g_replaying = true;  // p9 (the group filter list) points into the caller's frame: not kept, the replay has no filter
	g_buildOriginal(item, out, size, g_build.arg[3], g_build.arg[4], g_build.arg[5], g_build.arg[6], g_build.arg[7], 0,
	                reinterpret_cast<uint64_t>(optional));
	g_replaying = false;
	return true;
}

auto TipFormat(void* viewer, int stat, int value, uint32_t layer, int mode, int flag, char* out) noexcept -> int {
	if (!g_installed || viewer == nullptr || out == nullptr) {
		return -1;
	}
	// The records of itemstatcost: D2R's data manager keeps them in an array at +0x1258, 0x144 bytes each (stat_columns.cpp).
	const int16_t* record = nullptr;
	for (int idx = 0; idx < 4 && record == nullptr; ++idx) {
		const auto* manager = *reinterpret_cast<const uint8_t* const*>(g_base + 0x2a9a580 + idx * 16);
		const auto* array = manager != nullptr ? d2r::At<const uint8_t*>(manager, 0x1258) : nullptr;
		if (array != nullptr && stat >= 0 && stat < d2r::At<int64_t>(manager, 0x1260)) {
			record = reinterpret_cast<const int16_t*>(array + static_cast<size_t>(stat) * 0x144);
		}
	}
	if (record == nullptr || *record != stat) {
		return -1;
	}
	out[0] = '\0';
	return static_cast<unsigned char>(ItemTextFormat(viewer, record, value, layer, static_cast<char>(flag), out, mode));
}

// Every layer of a stat on a unit. D2R's stat functions are runtime thunks (0x2f5020 etc.), so the stat array is
// read directly: unit +0x88 is the unit's stat list (0x34b870), a StatListEx when its flags (+0x1c) are negative
// (0x2f5b80; +0x20 state, +0x68 next, +0x90/+0x98 sub-list chains). Its totals array ({u16 layer, u16 stat, i32
// value}, sorted by stat then layer) is a {pointer, count} pair whose offset is not pinned down statically, so it is
// found at run time: the first candidate offset whose entries are sorted and all equal what D2R's getter (0x2f5020
// (unit, stat, layer)) returns for them. Reads are guarded; if no offset qualifies, nothing is read.
#include "stat_list.h"

#include "d2r.h"

#include <windows.h>

#include <cstdint>

namespace {

constexpr uint64_t GetUnitStatRva = 0x2f5020;
constexpr int UnitStatList = 0x88;
constexpr int ListFlags = 0x1c;
constexpr int FirstCandidate = 0x28;
constexpr int LastCandidate = 0x140;
constexpr uint64_t MaxEntries = 4096;

using GetUnitStatFn = int32_t (*)(d2r::Unit* unit, int32_t stat, int32_t layer);
GetUnitStatFn g_getStat = nullptr;
int g_arrayOffset = -1;  // found offset, -2 once given up on

// Guarded copy (no C++ objects in this function, so __try is allowed).
auto SafeCopy(const void* from, void* to, size_t size) noexcept -> bool {
	__try {
		const auto* s = static_cast<const uint8_t*>(from);
		auto* d = static_cast<uint8_t*>(to);
		for (size_t i = 0; i < size; ++i) {
			d[i] = s[i];
		}
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

auto ReadArray(const uint8_t* list, int offset, const StatEntry** entries, uint64_t* count) noexcept -> bool {
	struct {
		const StatEntry* data;
		uint64_t count;
	} pair{};
	if (!SafeCopy(list + offset, &pair, sizeof(pair)) || pair.data == nullptr || pair.count == 0 ||
	    pair.count > MaxEntries) {
		return false;
	}
	*entries = pair.data;
	*count = pair.count;
	return true;
}

// Does the array at this offset look like the unit's totals?
auto Validates(d2r::Unit* unit, const uint8_t* list, int offset) noexcept -> bool {
	const StatEntry* entries = nullptr;
	uint64_t count = 0;
	if (!ReadArray(list, offset, &entries, &count) || count < 4) {
		return false;
	}
	uint32_t previous = 0;
	for (uint64_t i = 0; i < count; ++i) {
		StatEntry e{};
		if (!SafeCopy(entries + i, &e, sizeof(e))) {
			return false;
		}
		const uint32_t key = (static_cast<uint32_t>(e.stat) << 16) | e.layer;
		if (i > 0 && key <= previous) {
			return false;
		}
		previous = key;
		if (g_getStat(unit, e.stat, e.layer) != e.value) {
			return false;
		}
	}
	return true;
}

auto StatList(d2r::Unit* unit) noexcept -> const uint8_t* {
	const uint8_t* list = nullptr;
	int32_t flags = 0;
	if (!SafeCopy(reinterpret_cast<const uint8_t*>(unit) + UnitStatList, &list, sizeof(list)) || list == nullptr ||
	    !SafeCopy(list + ListFlags, &flags, sizeof(flags)) || flags >= 0) {
		return nullptr;
	}
	return list;
}

}  // namespace

auto UnitStatsById(d2r::Unit* unit, const int32_t* stats, int statCount, StatEntry* out, int max) noexcept -> int {
	if (unit == nullptr || g_getStat == nullptr || g_arrayOffset == -2) {
		return 0;
	}
	const uint8_t* list = StatList(unit);
	if (list == nullptr) {
		return 0;
	}
	if (g_arrayOffset < 0) {
		for (int offset = FirstCandidate; offset <= LastCandidate; offset += 8) {
			if (Validates(unit, list, offset)) {
				g_arrayOffset = offset;
				break;
			}
		}
		if (g_arrayOffset < 0) {
			return 0;  // try again with another unit (e.g. one with more stats)
		}
	}
	const StatEntry* entries = nullptr;
	uint64_t count = 0;
	if (!ReadArray(list, g_arrayOffset, &entries, &count)) {
		return 0;
	}
	int written = 0;
	for (uint64_t i = 0; i < count && written < max; ++i) {
		StatEntry e{};
		if (!SafeCopy(entries + i, &e, sizeof(e))) {
			return written;
		}
		for (int k = 0; k < statCount; ++k) {
			if (e.stat == stats[k]) {
				out[written++] = e;
				break;
			}
		}
	}
	return written;
}

void InstallStatList(uint64_t exeBase) noexcept {
	g_getStat = reinterpret_cast<GetUnitStatFn>(exeBase + GetUnitStatRva);
}

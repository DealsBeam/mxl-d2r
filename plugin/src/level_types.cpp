// Level types above D2R's 36 (docs/plugin-spec.md, "Level types").
//
// Median has 52 level types (36-51 are its own; 44 areas use them). D2R loads LvlTypes.txt into a vector, so their
// tile sets are fine, but AutoMap.txt is matched against a fixed list of 36 level-type names (`0x32cae0`) and each
// type's row range lives in a 36-entry array (0x2a9a8e0). The automap tile lookup `0x32bdf0` (levelType, tileType,
// style, sequence) indexes that array without a bounds check, so in Median's extra areas it would read past it.
// The loader files Median's rows (LevelName mxl_lvltype_<n>) under type 0 but keeps every row in file order in its
// vector (0x2a9a8c8: data, +8 count; 0x20 bytes a row). For types >= 36 the hook searches those rows itself, by the
// file positions tools/txt_migrate.py writes to mxl_automap.h, with the lookup's own rules: tile type equal, style
// equal unless 0xff, sequence within start..end unless start is 0xff; the cel is the first, or one of up to four
// picked with the game's automap seed (0x2372a40), as D2R does.
#include "level_types.h"

#include "mxl_automap.h"

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t AutomapLookupRva = 0x32bdf0;
constexpr uint64_t AutomapRowsRva = 0x2a9a8c8;  // uint8_t* data, then uint64_t count
constexpr uint64_t AutomapSeedRva = 0x2372a40;  // pointer to {uint32 lo, uint32 hi}
constexpr int32_t D2RLevelTypes = 36;
// MOV [RSP+0x10],RBX / MOV [RSP+0x18],RSI / MOV [RSP+0x20],RDI
constexpr uint8_t AutomapLookupEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74,
                                          0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20};

struct AutomapRow {      // 0x20 bytes
	int32_t levelType;    // 0x00 (0 for Median's rows)
	int32_t tileType;     // 0x04
	uint8_t style;        // 0x08, 0xff any
	uint8_t startSeq;     // 0x09, 0xff any
	uint8_t endSeq;       // 0x0a
	uint8_t pad0b;
	uint32_t cels[4];     // 0x0c
	int32_t celCount;     // 0x1c
};
static_assert(sizeof(AutomapRow) == 0x20);

using AutomapLookupFn = uint64_t (*)(int32_t levelType, int32_t tileType, uint32_t style, int32_t sequence) noexcept;
AutomapLookupFn g_original = nullptr;
uint64_t g_exeBase = 0;

auto MedianLookup(int32_t levelType, int32_t tileType, uint32_t style, int32_t sequence) noexcept -> uint64_t {
	const auto* rows = *reinterpret_cast<const AutomapRow* const*>(g_exeBase + AutomapRowsRva);
	const uint64_t count = *reinterpret_cast<const uint64_t*>(g_exeBase + AutomapRowsRva + 8);
	if (rows == nullptr || count != static_cast<uint64_t>(mxl_automap::RowCount)) {
		return 0xffffffffULL;  // not the AutoMap.txt the table was generated from
	}
	for (const mxl_automap::TypeRows& range : mxl_automap::Rows) {
		if (range.levelType != levelType) {
			continue;
		}
		for (int32_t i = range.first; i < range.end; ++i) {
			const AutomapRow& row = rows[i];
			if (row.tileType != tileType || (row.style != 0xff && row.style != style)) {
				continue;
			}
			if (row.startSeq != 0xff && (sequence < row.startSeq || sequence > row.endSeq)) {
				continue;
			}
			if (row.celCount < 1) {
				return row.cels[0];
			}
			auto* seed = *reinterpret_cast<uint32_t* const*>(g_exeBase + AutomapSeedRva);
			const uint64_t next = static_cast<uint64_t>(seed[0]) * 0x6ac690c5ULL + seed[1];
			seed[0] = static_cast<uint32_t>(next);
			seed[1] = static_cast<uint32_t>(next >> 32);
			const uint32_t n = static_cast<uint32_t>(row.celCount > 4 ? 4 : row.celCount);
			return row.cels[(n & (n - 1)) == 0 ? (seed[0] & (n - 1)) : (seed[0] % n)];
		}
	}
	return 0xffffffffULL;
}

auto AutomapLookup(int32_t levelType, int32_t tileType, uint32_t style, int32_t sequence) noexcept -> uint64_t {
	if (levelType >= D2RLevelTypes) {
		return MedianLookup(levelType, tileType, style, sequence);
	}
	if (levelType < 0) {
		return 0xffffffffULL;
	}
	return g_original(levelType, tileType, style, sequence);
}

}  // namespace

auto InstallLevelTypeGuards(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_exeBase = context->exeBase;
	const bool ok = context->InstallInlineHook(AutomapLookupRva, AutomapLookupEntry, sizeof(AutomapLookupEntry),
	                                           AutomapLookup, &g_original);
	std::snprintf(report, reportSize, ok ? "on: automap tiles for level types >= 36 (%d Median row ranges)"
	                                     : "off: automap hook failed (%d row ranges)",
	              static_cast<int>(sizeof(mxl_automap::Rows) / sizeof(mxl_automap::Rows[0])));
	return ok;
}

// Level types above D2R's 36 (docs/plugin-spec.md, "Level types").
//
// Median has 52 level types (36-51 are its own; 44 areas use them). D2R loads LvlTypes.txt into a vector, so their
// tile sets are fine, but AutoMap.txt is matched against a fixed list of 36 level-type names (`0x32cae0`) and each
// type's row range lives in a 36-entry array (0x2a9a8e0). The automap tile lookup `0x32bdf0` (levelType, tileType,
// style, sequence) indexes that array without a bounds check, so in Median's extra areas it would read past it. Until
// the automap tables are relocated, the hook answers "no automap tile" (-1, as for any unknown tile) for those types.
#include "level_types.h"

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t AutomapLookupRva = 0x32bdf0;
constexpr int32_t D2RLevelTypes = 36;
// MOV [RSP+0x10],RBX / MOV [RSP+0x18],RSI / MOV [RSP+0x20],RDI
constexpr uint8_t AutomapLookupEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74,
                                          0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20};

using AutomapLookupFn = uint64_t (*)(int32_t levelType, int32_t tileType, uint32_t style, int32_t sequence) noexcept;
AutomapLookupFn g_original = nullptr;

auto AutomapLookup(int32_t levelType, int32_t tileType, uint32_t style, int32_t sequence) noexcept -> uint64_t {
	if (levelType >= D2RLevelTypes || levelType < 0) {
		return 0xffffffffULL;
	}
	return g_original(levelType, tileType, style, sequence);
}

}  // namespace

auto InstallLevelTypeGuards(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool ok = context->InstallInlineHook(AutomapLookupRva, AutomapLookupEntry, sizeof(AutomapLookupEntry),
	                                           AutomapLookup, &g_original);
	std::snprintf(report, reportSize, ok ? "on: automap lookup guarded for level types >= 36"
	                                     : "off: automap hook failed");
	return ok;
}

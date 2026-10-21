// Median's property functions 27, 28 and 29 (properties.txt func), in D2R's property function table.
//
// D2R applies a property (item, affix, monster property) with D2Common-style functions chosen per slot by the properties.txt
// `func` column: 0x3d4620 reads the function number (byte +0x18+slot of the properties record), checks it against the
// count at 0x2386bd8 (37) and calls the entry of the 8-byte table at 0x2386ab0, skipping the slot when the entry is
// empty. D2R fills 1-25 and 36; 26-35 are empty, and Median's rows use 27 (innate elemental damage on 107 automagic
// rows, 5 uniques, 14 cube recipes), 28 (35 amulet uniques) and 29 (the monster properties of bosses such as Radament).
// Without them those slots are skipped and the stat never reaches the item or monster.
//
// The functions D2Sigma replaces (0x1009cbe0, 0x1009ccf0, 0x1009cc30) add one stat from {param, min, max} with no roll;
// the rules are in properties_ext_logic.h. Here the stat is added the way D2R's own function 1 (0x3cf960) adds its
// own: through the shared adder 0x3d5940 (dataFlag, type, unit, item, property, set, stat, layer, value, state, flags,
// extra) with the stat from the row's stat column and the layer and value computed here.
//
// The property is { int16 id; int16 flag; int32 param; int32 min; int32 max }; arguments after the first four are on the
// stack, as D2R's dispatcher passes them: property, set, stat, val, result of slot 0, state, stat list flags, extra.
//
// Entry bytes checked against the dump of build 93847; whole instructions, no rip-relative.
#include "properties_ext.h"

#include "properties_ext_logic.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint64_t PropertyTableRva = 0x2386ab0;  // 8-byte entries, index = func
constexpr uint64_t PropertyCountRva = 0x2386bd8;  // int32, 37
constexpr uint64_t AddStatRva = 0x3d5940;

// mov [rsp+8],rbx / mov [rsp+0x10],rdi / mov [rsp+0x18],r12 / mov [rsp+0x20],r14 / push r15 / sub rsp,0x30
constexpr uint8_t AddStatEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x7c, 0x24, 0x10, 0x4c, 0x89, 0x64,
                                    0x24, 0x18, 0x4c, 0x89, 0x74, 0x24, 0x20, 0x41, 0x57, 0x48, 0x83, 0xec, 0x30};

struct Property {
	int16_t id;
	int16_t flag;
	int32_t param;
	int32_t min;
	int32_t max;
};
static_assert(sizeof(Property) == 16);

using AddStatFn = int32_t (*)(uint32_t dataFlag, int32_t type, void* unit, void* item, const Property* property,
                              int32_t set, int32_t stat, int32_t layer, int32_t value, int32_t state, int32_t flags,
                              void* extra);

AddStatFn g_addStat = nullptr;

template <int Func>
auto Handler(uint32_t dataFlag, int32_t type, void* unit, void* item, const Property* property, int32_t set, int32_t stat,
             int32_t val, int32_t previous, int32_t state, int32_t flags, void* extra) noexcept -> int32_t {
	(void)val;
	(void)previous;
	if (property == nullptr || g_addStat == nullptr) {
		return 0;
	}
	const mxl_props::Layered r = mxl_props::Compute(Func, property->param, property->min, property->max);
	return g_addStat(dataFlag, type, unit, item, property, set, stat, r.layer, r.value, state, flags, extra);
}

using HandlerFn = decltype(&Handler<27>);

}  // namespace

auto InstallPropertiesExt(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const uint64_t base = context->exeBase;
	if (std::memcmp(reinterpret_cast<const void*>(base + AddStatRva), AddStatEntry, sizeof(AddStatEntry)) != 0) {
		std::snprintf(report, reportSize, "off: entry bytes of 0x3d5940 differ");
		return 0;
	}
	int32_t count = 0;
	std::memcpy(&count, reinterpret_cast<const void*>(base + PropertyCountRva), sizeof(count));
	if (count <= mxl_props::LastFunction) {
		std::snprintf(report, reportSize, "off: D2R's property table holds %d functions", count);
		return 0;
	}
	g_addStat = reinterpret_cast<AddStatFn>(base + AddStatRva);
	static constexpr uint8_t Empty[8] = {};
	const HandlerFn handlers[] = {&Handler<27>, &Handler<28>, &Handler<29>};
	int installed = 0;
	for (int func = mxl_props::FirstFunction; func <= mxl_props::LastFunction; ++func) {
		const uint64_t rva = PropertyTableRva + static_cast<uint64_t>(func) * 8;
		installed += context->PatchWriteU64(rva, Empty, sizeof(Empty),
		                                    reinterpret_cast<uint64_t>(handlers[func - mxl_props::FirstFunction]))
		                 ? 1
		                 : 0;
	}
	std::snprintf(report, reportSize, "%s: property functions 27, 28, 29 in D2R's table (%d/3)", installed == 3 ? "on" : "partial",
	              installed);
	return installed;
}

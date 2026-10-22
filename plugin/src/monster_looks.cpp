// Median's monster looks on D2R: the name colour by rank (monstats3 Flags rank bits, NameColor), the automap marker and name
// of flagged monsters (Automap, AutomapShowName, AutomapNameColor) and the overlay that plays when a monster spawns
// (Overlay1/2, BossOverlay1/2). Rules: monster_looks_logic.h; data: mxl_monster_looks.h (tools/sigma_data.py);
// addresses: docs/d2r-addresses.md section "Monster looks".
//
// Name colour. The hover name is built by 0xcea50(unit, a2, a3); it picks a colour index (0 white, 3 champion, 4 unique,
// 1 corpse, 0x1d possessed) and turns it into the text colour with 0x908690(&rgba, index, 5), once. The first hook notes
// the unit, the second swaps the index when the call comes from that function.
//
// Automap. 0xd76e0(unit, ctx) draws one unit: 0xd78f0(unit, &marker, &nameType) classifies it (marker = sprite frame,
// nameType 4 = the monster's own name in gold), 0xd6db0 draws the marker, 0xd6b20(text, pos, scale, colour) the name.
// A flagged monster that D2R does not classify gets the town NPC marker (6) and name type 4; the name's colour index is
// swapped for AutomapNameColor. The marker is a sprite frame, so AutomapVectorColor (a palette index) cannot tint it.
//
// Spawn overlays. 0x493cc0(params) creates a monster and returns the unit; the overlay is added with 0x349020(unit, id, 0)
// (the stat-list route D2R's own skills use: the client plays it when it next receives the unit's update).
//
// Entry bytes verified against the dump of build 93847; whole instructions, no rip-relative.
#include "monster_looks.h"

#include "d2r.h"
#include "monster_looks_logic.h"

#define NOMINMAX
#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

using d2r::Unit;

constexpr uint64_t HoverRva = 0xcea50;        // (unit, a2, a3): the hover name; 0x1634 bytes long
constexpr uint64_t HoverSize = 0x1650;
constexpr uint64_t ColourRva = 0x908690;      // (&rgba[4], index, mode) -> &rgba: colour index to colour
constexpr uint64_t ClassifyRva = 0xd78f0;     // (unit, &marker, &nameType) -> non-zero when the unit is drawn
constexpr uint64_t AutomapUnitRva = 0xd76e0;  // (unit, ctx): draws one unit on the automap
constexpr uint64_t AutomapNameRva = 0xd6b20;  // (text, pos, scale, colour index)
constexpr uint64_t CreateRva = 0x493cc0;      // (params) -> the new monster unit or null
constexpr uint64_t SetOverlayRva = 0x349020;  // (unit, overlay row, value)

// 0xcea50: mov [rsp+0x18],rbx / push rbp / push rsi / push rdi   (8 bytes)
constexpr uint8_t HoverEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57};
// 0x908690: push rbx / push rdi / sub rsp,0x38   (7 bytes; the encoding is 40 53 57 48 83 ec 38)
constexpr uint8_t ColourEntry[] = {0x40, 0x53, 0x57, 0x48, 0x83, 0xec, 0x38};
// 0xd78f0: push rbx / push rsi / push rdi / sub rsp,0x60   (8 bytes)
constexpr uint8_t ClassifyEntry[] = {0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x60};
// 0xd76e0: mov [rsp+0x10],rbp / push rdi / sub rsp,0x40   (10 bytes)
constexpr uint8_t AutomapUnitEntry[] = {0x48, 0x89, 0x6c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40};
// 0xd6b20: mov r11,rsp / push rbp / push rbx   (5 bytes)
constexpr uint8_t AutomapNameEntry[] = {0x4c, 0x8b, 0xdc, 0x55, 0x53};
// 0x493cc0: mov [rsp+0x10],rbx / mov [rsp+0x18],rsi / mov [rsp+0x20],rdi   (15 bytes)
constexpr uint8_t CreateEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20};

constexpr int UnitClass = 0x04;
constexpr int UnitMode = 0x0c;
constexpr int UnitData = 0x10;      // monster data
constexpr int MonsterTypeFlags = 0x1a;  // u16 inside the monster data
constexpr int ColourToRgba = 5;     // the mode the hover name passes to 0x908690
constexpr int32_t MarkerNpc = 6;    // sprite frame D2R draws for town NPCs
constexpr int32_t NameOwn = 4;      // nameType: the monster's own name
constexpr int32_t NameNone = -1;

using HoverFn = void (*)(Unit* unit, void* a2, char a3);
using ColourFn = uint32_t* (*)(uint32_t* rgba, int32_t index, int32_t mode);
using ClassifyFn = uint64_t (*)(Unit* unit, int32_t* marker, int32_t* nameType);
using AutomapUnitFn = void (*)(Unit* unit, void* ctx);
using AutomapNameFn = void (*)(const char* text, uint64_t pos, float scale, uint32_t index);
using CreateFn = Unit* (*)(void* params);
using SetOverlayFn = void (*)(Unit* unit, int32_t overlay, int32_t value);

HoverFn g_hoverOriginal = nullptr;
ColourFn g_colourOriginal = nullptr;
ClassifyFn g_classifyOriginal = nullptr;
AutomapUnitFn g_automapUnitOriginal = nullptr;
AutomapNameFn g_automapNameOriginal = nullptr;
CreateFn g_createOriginal = nullptr;
SetOverlayFn g_setOverlay = nullptr;
uint64_t g_base = 0;

thread_local Unit* t_hoverUnit = nullptr;
thread_local Unit* t_automapUnit = nullptr;

std::atomic<uint32_t> g_recoloured{0}, g_marked{0}, g_named{0}, g_overlays{0};
std::atomic<int32_t> g_lastOverlayMonster{-1}, g_lastOverlay{-1};

// Not every caller of the creation function gets a unit back: the result can be 0 or a small number (1 was seen), so a
// pointer below the first allocation granule is not read.
auto IsMonster(const Unit* unit) noexcept -> bool {
	return reinterpret_cast<uintptr_t>(unit) >= 0x10000 && d2r::At<int32_t>(unit, d2r::UnitType) == 1;
}

auto LookOf(const Unit* unit) noexcept -> const mxl_looks::Look* {
	return IsMonster(unit) ? looks::Find(d2r::At<int32_t>(unit, UnitClass)) : nullptr;
}

auto TypeFlags(const Unit* unit) noexcept -> uint16_t {
	const auto* data = d2r::At<const uint8_t*>(unit, UnitData);
	return data != nullptr ? d2r::At<uint16_t>(data, MonsterTypeFlags) : uint16_t{0};
}

// ---- name colour -------------------------------------------------------------------------------------------------------

void HoverHook(Unit* unit, void* a2, char a3) {
	Unit* const previous = t_hoverUnit;
	t_hoverUnit = unit;
	g_hoverOriginal(unit, a2, a3);
	t_hoverUnit = previous;
}

uint32_t* ColourHook(uint32_t* rgba, int32_t index, int32_t mode) {
	if (t_hoverUnit != nullptr && mode == ColourToRgba) {
		const uint64_t caller = reinterpret_cast<uint64_t>(_ReturnAddress()) - g_base;
		if (caller >= HoverRva && caller < HoverRva + HoverSize) {
			if (const mxl_looks::Look* look = LookOf(t_hoverUnit)) {
				const int32_t merged = looks::MergeNameColour(index, looks::NameColour(*look, TypeFlags(t_hoverUnit)));
				if (merged != index) {
					g_recoloured.fetch_add(1);
					index = merged;
				}
			}
		}
	}
	return g_colourOriginal(rgba, index, mode);
}

// ---- automap -----------------------------------------------------------------------------------------------------------

uint64_t ClassifyHook(Unit* unit, int32_t* marker, int32_t* nameType) {
	const uint64_t result = g_classifyOriginal(unit, marker, nameType);
	if ((result & 0xff) == 0 && marker != nullptr && nameType != nullptr) {
		if (const mxl_looks::Look* look = LookOf(unit)) {
			if (looks::AutomapMarked(*look, d2r::At<int32_t>(unit, UnitMode))) {
				*marker = MarkerNpc;
				*nameType = looks::AutomapNamed(*look) ? NameOwn : NameNone;
				g_marked.fetch_add(1);
				return 1;
			}
		}
	}
	return result;
}

void AutomapUnitHook(Unit* unit, void* ctx) {
	Unit* const previous = t_automapUnit;
	t_automapUnit = unit;
	g_automapUnitOriginal(unit, ctx);
	t_automapUnit = previous;
}

void AutomapNameHook(const char* text, uint64_t pos, float scale, uint32_t index) {
	if (t_automapUnit != nullptr && index == static_cast<uint32_t>(looks::DefaultAutomapNameColour)) {
		if (const mxl_looks::Look* look = LookOf(t_automapUnit)) {
			if (looks::AutomapNamed(*look)) {
				index = look->automapNameColor;
				g_named.fetch_add(1);
			}
		}
	}
	g_automapNameOriginal(text, pos, scale, index);
}

// ---- spawn overlays ----------------------------------------------------------------------------------------------------

Unit* CreateHook(void* params) {
	Unit* const unit = g_createOriginal(params);
	if (const mxl_looks::Look* look = LookOf(unit)) {
		const looks::Overlays pair = looks::SpawnOverlays(*look, TypeFlags(unit));
		for (const int32_t overlay : {pair.first, pair.second}) {
			if (overlay >= 0) {
				g_setOverlay(unit, overlay, 0);
				g_overlays.fetch_add(1);
				g_lastOverlayMonster.store(look->monster);
				g_lastOverlay.store(overlay);
			}
		}
	}
	return unit;
}

template <typename Fn, size_t N>
auto Hook(const D2RL::PluginContext* context, uint64_t rva, const uint8_t (&entry)[N], void* hook, Fn* original) noexcept -> bool {
	return context->InstallInlineHook(rva, entry, N, hook, reinterpret_cast<void**>(original));
}

}  // namespace

auto InstallMonsterLooks(const D2RL::PluginContext* context, bool nameColours, bool automap, bool spawnOverlays, char* report,
                         size_t reportSize) noexcept -> bool {
	g_base = context->exeBase;
	g_setOverlay = reinterpret_cast<SetOverlayFn>(g_base + SetOverlayRva);
	char parts[160] = "";
	size_t used = 0;
	int on = 0;
	auto note = [&](const char* what, bool ok) {
		used += std::snprintf(parts + used, sizeof(parts) - used, "%s%s %s", used != 0 ? ", " : "", what, ok ? "on" : "FAILED");
		on += ok ? 1 : 0;
	};
	if (nameColours) {
		const bool ok = Hook(context, HoverRva, HoverEntry, reinterpret_cast<void*>(&HoverHook), &g_hoverOriginal) &&
		                Hook(context, ColourRva, ColourEntry, reinterpret_cast<void*>(&ColourHook), &g_colourOriginal);
		note("name colours", ok);
	}
	if (automap) {
		const bool ok = Hook(context, ClassifyRva, ClassifyEntry, reinterpret_cast<void*>(&ClassifyHook), &g_classifyOriginal) &&
		                Hook(context, AutomapUnitRva, AutomapUnitEntry, reinterpret_cast<void*>(&AutomapUnitHook), &g_automapUnitOriginal) &&
		                Hook(context, AutomapNameRva, AutomapNameEntry, reinterpret_cast<void*>(&AutomapNameHook), &g_automapNameOriginal);
		note("automap", ok);
	}
	if (spawnOverlays) {
		note("spawn overlays", Hook(context, CreateRva, CreateEntry, reinterpret_cast<void*>(&CreateHook), &g_createOriginal));
	}
	std::snprintf(report, reportSize, "%s: %s (%d monsters in the table)", on != 0 ? "on" : "off", parts[0] != '\0' ? parts : "nothing asked",
	              static_cast<int>(mxl_looks::LookCount));
	return on != 0;
}

void MonsterLooksStatus(char* out, size_t outSize) noexcept {
	std::snprintf(out, outSize, "%u names recoloured, %u automap markers, %u automap names, %u spawn overlays (last %d on monster %d)",
	              g_recoloured.load(), g_marked.load(), g_named.load(), g_overlays.load(), g_lastOverlay.load(),
	              g_lastOverlayMonster.load());
}

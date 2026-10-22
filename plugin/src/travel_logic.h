// Median's travel conveniences as pure functions (travel.cpp reads the game; tests/travel_test.cpp).
//
//   * Waypoint auto-activation. Median's D2AutoPickup.cpp (D2Sigma 0x4c7e0, called after a walk order from 0xada40) takes
//     gold and waypoints the same way: when the distance to the unit is below 0x24 the unit is used. The waypoint branch
//     (0x4c7e0, second half) tests the unit with 0xbffa0, and if the player does not know that waypoint yet sets it. It is a
//     proximity rule, not "entering the area". D2R's distance unit is the sub-tile, the same as D2's 1.13c.
//   * Hotkeys (Median's table at D2Sigma 0x1d9110): the no-pickup flag toggle (0x68cf0), the act reveal (0x887c0) and the
//     transmute while the Horadric Cube panel is open (0x69820). Default keys X, L, K below.
//   * Stash pages: the page text of Median's stash panel (0x1130a0) counts up to a constant 10 in both modes.
#pragma once

#include "mxl_waypoints.h"

#include <cstdint>

namespace travel {

// The waypoint number of a level (levels.txt Waypoint, generated into mxl_waypoints.h), -1 when the level has none.
constexpr auto WaypointNumberOfLevel(int32_t level) noexcept -> int {
	for (const mxl_wp::Level& l : mxl_wp::Levels) {
		if (l.level == level) {
			return l.number;
		}
	}
	return -1;
}

// objects.txt rows with OperateFn 23 (waypoint): the same 16 classes in Median's table and in vanilla (Median's extra
// waypoint levels, Fauztinville and the rest, reuse these objects).
inline constexpr int32_t WaypointObjects[] = {119, 145, 156, 157, 237, 238, 288, 323, 324, 398, 402, 429, 494, 496, 511, 539};

constexpr auto IsWaypointObject(int32_t objectClass) noexcept -> bool {
	for (const int32_t c : WaypointObjects) {
		if (c == objectClass) {
			return true;
		}
	}
	return false;
}

inline constexpr int PickupRadius = 36;       // D2AutoPickup.cpp 0x4c7e0: `distance < 0x24`
inline constexpr int ScanEveryFrames = 5;     // the scan runs every fifth server frame (25 frames a second)

constexpr auto IsScanFrame(uint32_t frame) noexcept -> bool {
	return frame % ScanEveryFrames == 0;
}

// True when (px, py) is closer than PickupRadius to (ox, oy). Squared integer maths, no overflow for map coordinates.
constexpr auto InPickupRange(int32_t px, int32_t py, int32_t ox, int32_t oy) noexcept -> bool {
	const int64_t dx = static_cast<int64_t>(px) - ox;
	const int64_t dy = static_cast<int64_t>(py) - oy;
	return dx * dx + dy * dy < static_cast<int64_t>(PickupRadius) * PickupRadius;
}

// What the scan does for one player standing in `level` near a waypoint object.
enum class Decision : uint8_t {
	Skip,          // auto pickup is switched off (X), or the player is not in range
	NoWaypoint,    // the level has no waypoint number (levels.txt Waypoint = 255) or it does not fit the record
	AlreadyKnown,  // the bit is set
	Activate,      // set the bit
};

// `number` is the waypoint number of the level (-1 when the level has none), `known` is the record's bit.
constexpr auto Decide(bool autoPickupOn, bool inRange, int number, bool known) noexcept -> Decision {
	if (!autoPickupOn || !inRange) {
		return Decision::Skip;
	}
	if (number < 0 || number >= 112) {
		return Decision::NoWaypoint;
	}
	return known ? Decision::AlreadyKnown : Decision::Activate;
}

// Hotkeys. InputService wants lowercase ASCII ids that stay stable (saved bindings key on them).
struct Hotkey {
	const char* id;
	const char* name;
	uint32_t key;  // virtual key, D2RL::Input::Key
};

// Default keys. Median's table (D2Sigma 0x1d9110) names the actions (hotkey_nopickup, hotkey_revealmap, hotkey_transmute) but keeps the
// keys in the player's preferences.json; the Median install on the test machine has nopickup 0x58 (X) and revealmap 0x4c (L). Neither is
// bound by a D2R action, so both stay. Median's transmute key there is V (0x56), which D2R binds to one of its own actions (the character's
// .key file), and the keys the first port picked, R and C, are D2R's run / walk toggle and Character screen: a press did both. The
// transmute default is K, which no D2R action uses. Players can rebind all three in the Controls menu.
inline constexpr uint32_t KeyK = 0x4b, KeyL = 0x4c, KeyX = 0x58;

inline constexpr Hotkey Hotkeys[] = {
    {"mxl_pickup_toggle", "Toggle auto pickup (Median XL)", KeyX},
    {"mxl_map_reveal", "Reveal the map (Median XL)", KeyL},
    {"mxl_transmute", "Transmute with the cube open (Median XL)", KeyK},
};
inline constexpr int HotkeyCount = static_cast<int>(sizeof(Hotkeys) / sizeof(Hotkeys[0]));

// The automap layer D2R keeps per level: four std::map-like containers; the cells of a revealed level are the sum of their
// element counts (travel.cpp reads them at the offsets below). A reveal worked when the total grew.
inline constexpr int LayerCountOffsets[4] = {0x28, 0x50, 0x78, 0xa0};

constexpr auto LayerCells(const uint64_t (&counts)[4]) noexcept -> uint64_t {
	return counts[0] + counts[1] + counts[2] + counts[3];
}

constexpr auto RevealGrew(uint64_t before, uint64_t after) noexcept -> bool {
	return after > before;
}

// Whether a UI message (D2RLoader SharedEventService: target and command strings) opens or closes the Horadric Cube panel.
// +1 opened, -1 closed, 0 not about the cube. The C hotkey only transmutes while the panel is open. The exact message names are
// read from the game (the `hotkeys` check logs them); the match is on "cube" in the target and open/show vs close/hide in the command.
constexpr auto ContainsNoCase(const char* text, const char* word) noexcept -> bool {
	if (text == nullptr || word == nullptr) {
		return false;
	}
	for (const char* t = text; *t != '\0'; ++t) {
		const char* a = t;
		const char* b = word;
		while (*a != '\0' && *b != '\0' && ((*a | 0x20) == (*b | 0x20))) {
			++a;
			++b;
		}
		if (*b == '\0') {
			return true;
		}
	}
	return false;
}

constexpr auto CubePanelState(const char* target, const char* command, const char* text = nullptr) noexcept -> int {
	if (!ContainsNoCase(target, "cube") && !ContainsNoCase(text, "cube")) {
		return 0;
	}
	if (ContainsNoCase(command, "close") || ContainsNoCase(command, "hide") || ContainsNoCase(command, "unload")) {
		return -1;
	}
	if (ContainsNoCase(command, "open") || ContainsNoCase(command, "show")) {
		return 1;
	}
	return 0;
}

// The stash: Median shows "page / 10" in both its stash modes (0x1130a0 converts the constant 10 for the total).
inline constexpr int MedianStashPages = 10;

// D2R's normal shared-stash tabs before D2RLoader adds any (RotW 3.3: three normal tabs; the toml option counts additional
// tabs only). Median's shared stash then needs `MedianStashPages - VanillaSharedTabs` more.
inline constexpr int VanillaSharedTabs = 3;

constexpr auto SharedTabsToAdd(int wantedPages, int vanillaTabs) noexcept -> int {
	return wantedPages > vanillaTabs ? wantedPages - vanillaTabs : 0;
}

}  // namespace travel

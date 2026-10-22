// Tests of the travel rules (travel_logic.h): waypoint auto-activation range and decision, the hotkey table, the cube panel
// message match, the automap cell count and the stash tab arithmetic.
#include "../src/travel_logic.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL (travel): %s\n", what);
	}
}

}  // namespace

int RunTravelTests() {
	// waypoint number of a level: Rogue Encampment (1) is 0, Catacombs level 2 (35) is 3, Median's Duncraig (153) is 50, none for 2
	Check(travel::WaypointNumberOfLevel(1) == 0, "level 1 is waypoint 0");
	Check(travel::WaypointNumberOfLevel(153) == 50, "level 153 is waypoint 50");
	Check(travel::WaypointNumberOfLevel(242) == 90, "level 242 is waypoint 90");
	Check(travel::WaypointNumberOfLevel(2) == -1, "Blood Moor has no waypoint");
	Check(travel::WaypointNumberOfLevel(0) == -1 && travel::WaypointNumberOfLevel(-5) == -1, "no level, no number");

	// objects: the 16 waypoint classes, nothing else
	int count = 0;
	for (const int32_t c : travel::WaypointObjects) {
		Check(travel::IsWaypointObject(c), "a listed class is a waypoint");
		++count;
	}
	Check(count == 16, "sixteen waypoint object classes");
	Check(!travel::IsWaypointObject(0) && !travel::IsWaypointObject(59) && !travel::IsWaypointObject(120), "portals and others are not waypoints");

	// range: strictly below 36 sub-tiles (D2AutoPickup.cpp: distance < 0x24)
	Check(travel::InPickupRange(100, 100, 100, 100), "on top of it");
	Check(travel::InPickupRange(100, 100, 135, 100), "35 away");
	Check(!travel::InPickupRange(100, 100, 136, 100), "36 away is out");
	Check(travel::InPickupRange(100, 100, 124, 126), "diagonal 24,26 = 35.4");
	Check(!travel::InPickupRange(100, 100, 126, 126), "diagonal 26,26 = 36.8");
	Check(travel::InPickupRange(0, 0, 0, 35) && !travel::InPickupRange(0, 0, 0, 36), "vertical edge");
	Check(travel::InPickupRange(1000000, 1000000, 1000010, 1000010), "large coordinates do not overflow");

	// scan cadence
	int scans = 0;
	for (uint32_t f = 0; f < 100; ++f) {
		scans += travel::IsScanFrame(f) ? 1 : 0;
	}
	Check(scans == 20, "every fifth frame");

	// decision
	using D = travel::Decision;
	Check(travel::Decide(true, true, 3, false) == D::Activate, "unknown waypoint in range is activated");
	Check(travel::Decide(true, true, 3, true) == D::AlreadyKnown, "known waypoint is left alone");
	Check(travel::Decide(false, true, 3, false) == D::Skip, "X off: nothing");
	Check(travel::Decide(true, false, 3, false) == D::Skip, "out of range: nothing");
	Check(travel::Decide(true, true, -1, false) == D::NoWaypoint, "level without a waypoint");
	Check(travel::Decide(true, true, 112, false) == D::NoWaypoint, "number past the record");
	Check(travel::Decide(true, true, 111, false) == D::Activate, "the last number in the record");

	// hotkeys: Median's table names; X / L / K (not D2R's run toggle R or Character screen C)
	Check(travel::HotkeyCount == 3, "three hotkeys");
	Check(travel::Hotkeys[0].key == 'X' && travel::Hotkeys[1].key == 'L' && travel::Hotkeys[2].key == 'K', "X, L, K");
	for (int i = 0; i < travel::HotkeyCount; ++i) {
		Check(travel::Hotkeys[i].key != 'R' && travel::Hotkeys[i].key != 'C', "no default on D2R's run toggle or Character screen");
	}
	for (int i = 0; i < travel::HotkeyCount; ++i) {
		const char* id = travel::Hotkeys[i].id;
		bool lowerAscii = id != nullptr && id[0] != '\0';
		for (const char* p = id; p != nullptr && *p != '\0'; ++p) {
			lowerAscii = lowerAscii && ((*p >= 'a' && *p <= 'z') || *p == '_' || *p == '-' || (*p >= '0' && *p <= '9'));
		}
		Check(lowerAscii, "hotkey ids are lowercase ascii");
		for (int j = i + 1; j < travel::HotkeyCount; ++j) {
			Check(std::strcmp(id, travel::Hotkeys[j].id) != 0 && travel::Hotkeys[i].key != travel::Hotkeys[j].key, "ids and keys are unique");
		}
	}

	// cube panel messages
	Check(travel::CubePanelState("CubePanel", "Open") == 1 && travel::CubePanelState("cubepanel", "show") == 1, "cube opens");
	Check(travel::CubePanelState("HoradricCubePanel", "Close") == -1 && travel::CubePanelState("CUBE", "Hide") == -1, "cube closes");
	Check(travel::CubePanelState("InventoryPanel", "Open") == 0, "other panels do not count");
	Check(travel::CubePanelState("CubePanel", "Refresh") == 0, "other cube messages do not change the state");
	Check(travel::CubePanelState(nullptr, "Open") == 0 && travel::CubePanelState("Cube", nullptr) == 0, "null strings");
	Check(travel::ContainsNoCase("abcCUBEdef", "cube") && !travel::ContainsNoCase("cub", "cube") && !travel::ContainsNoCase("", "cube"), "contains");

	// automap cells
	const uint64_t counts[4] = {10, 20, 30, 40};
	Check(travel::LayerCells(counts) == 100, "cells are the sum");
	Check(travel::RevealGrew(100, 101) && !travel::RevealGrew(100, 100) && !travel::RevealGrew(100, 90), "reveal grew");
	Check(travel::LayerCountOffsets[0] == 0x28 && travel::LayerCountOffsets[3] == 0xa0, "layer offsets");

	// stash tabs
	Check(travel::MedianStashPages == 10, "ten pages");
	Check(travel::SharedTabsToAdd(10, 3) == 7, "seven more tabs");
	Check(travel::SharedTabsToAdd(3, 3) == 0 && travel::SharedTabsToAdd(2, 3) == 0, "no negative count");
	return failures;
}

// Tests of the item event port's pure rules (plugin/src/item_events_logic.h).
#include "../src/item_events_logic.h"

#include <cstdio>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: item events: %s\n", what);
	}
}

}  // namespace

int RunItemEventsTests() {
	using namespace mxl_itemevents;

	// open wounds / crushing blow skip a monster with the immunity stat, as the attacker or as the one hit
	Check(UnitBlocks(UnitMonster, 1) && !UnitBlocks(UnitMonster, 0) && !UnitBlocks(0, 1), "only a monster with the stat blocks");
	Check(BlocksWoundsOrCrush(0, 0, UnitMonster, 1), "an immune victim blocks");
	Check(BlocksWoundsOrCrush(UnitMonster, 1, 0, 0), "an immune attacker blocks");
	Check(!BlocksWoundsOrCrush(0, 0, UnitMonster, 0), "a monster without the stat is hit");
	Check(!BlocksWoundsOrCrush(0, 7, 0, 7), "players never block");
	Check(!BlocksWoundsOrCrush(-1, 0, -1, 0), "missing units do not block");

	// D2RCore's (stat << 32) | layer to D2R's (stat << 16) | layer
	Check(ToD2rCode((uint64_t{376} << 32) | 3013) == ((376u << 16) | 3013u), "stat 376 layer 3013");
	Check(ToD2rCode((uint64_t{265} << 32) | 0) == (265u << 16), "stat 265 without a layer");
	Check(ToD2rCode(0x00170005u) == 0x00170005u, "a code that is already D2R's stays");
	Check(ToD2rCode((uint64_t{0x1ffff} << 32) | 0x12345) == ((0xffffu << 16) | 0x2345u), "both parts are cut to 16 bits");

	Check(FuncOpenWounds == 15 && FuncCrushingBlow == 16 && FuncHealOnKill == 33 && FuncCastOnKill == 34, "function ids");
	return failures;
}
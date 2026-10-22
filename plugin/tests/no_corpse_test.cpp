// Standalone test of the no-corpse death rules (no_corpse_logic.h; not part of the plugin DLL).
#include "../src/no_corpse_logic.h"

#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: %s\n", what);
	}
}
}  // namespace

int main() {
	using namespace nocorpse;

	// Median's rules: no corpse, equipment stays, no gold or experience penalty
	static_assert(!CorpseCreated && !EquipmentMoved && !GoldPenalty && !ExperiencePenalty);
	Check(GoldAfterDeath(0) == 0 && GoldAfterDeath(12345) == 12345 && GoldAfterDeath(2147483647) == 2147483647, "gold unchanged");
	Check(ExperienceAfterDeath(0) == 0 && ExperienceAfterDeath(987654321u) == 987654321u && ExperienceAfterDeath(4294967295u) == 4294967295u,
	      "experience unchanged");

	// counters start at zero
	Counters c;
	Check(c.corpses == 0 && c.penalties == 0, "counters start at 0");

	// report line
	char line[200];
	FormatReport(line, sizeof(line), 0, 0);
	Check(std::strcmp(line, "on: corpse 0x42c110 + penalty 0x424ac0 suppressed (corpses skipped 0, penalties skipped 0)") == 0, "report at 0");
	FormatReport(line, sizeof(line), 3, 2);
	Check(std::strstr(line, "corpses skipped 3, penalties skipped 2") != nullptr, "report with counts");
	char small[16];
	FormatReport(small, sizeof(small), 1, 1);
	Check(std::strlen(small) == sizeof(small) - 1, "report truncates to the buffer");

	// respawn wait: 125 frames (5 s at 25 Hz) after the death; no stamp never holds a character back
	Check(RespawnWaitFrames == 125, "5 s = 125 frames");
	Check(RespawnAllowed(5000, 0), "no stamp: allowed");
	Check(!RespawnAllowed(1000, 1000) && !RespawnAllowed(1124, 1000) && RespawnAllowed(1125, 1000) && RespawnAllowed(9999, 1000), "wait window");
	Check(RespawnAllowed(10, 4294967100u), "frame counter wrap: 206 frames later is allowed");
	Check(!RespawnAllowed(10, 4294967250u), "frame counter wrap: 56 frames later is held");

	// death stamps per game and unit
	DeathStamps st;
	Check(st.Get(1, 7) == 0, "empty table");
	st.Set(1, 7, 100);
	st.Set(1, 8, 200);
	st.Set(2, 7, 300);
	Check(st.Get(1, 7) == 100 && st.Get(1, 8) == 200 && st.Get(2, 7) == 300, "stamps are per game and unit");
	st.SetIfAbsent(1, 7, 150);
	Check(st.Get(1, 7) == 100, "SetIfAbsent keeps the death-start stamp");
	st.Set(1, 7, 400);
	Check(st.Get(1, 7) == 400, "Set replaces");
	st.Clear(1, 7);
	Check(st.Get(1, 7) == 0 && st.Get(1, 8) == 200, "Clear one unit");
	st.SetIfAbsent(1, 7, 500);
	Check(st.Get(1, 7) == 500, "SetIfAbsent after Clear");
	st.Set(3, 9, 0);
	Check(st.Get(3, 9) == 1, "frame 0 is stored as 1 (never no-stamp)");
	st.ClearGame(1);
	Check(st.Get(1, 7) == 0 && st.Get(1, 8) == 0 && st.Get(2, 7) == 300, "ClearGame takes only that game's stamps");
	for (uint32_t i = 0; i < 40; ++i) {
		st.Set(10, i, 1000 + i);
	}
	Check(st.Get(10, 39) == 1039 && st.Get(10, 38) == 1038, "full table keeps the newest stamps");
	Check(st.Get(10, 0) == 0, "full table overwrites the oldest");

	if (failures == 0) {
		std::printf("no_corpse tests: ok\n");
	}
	return failures == 0 ? 0 : 1;
}

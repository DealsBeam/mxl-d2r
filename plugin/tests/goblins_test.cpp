// Standalone test of the treasure goblin rules (goblins_logic.h; not part of the plugin DLL).
#include "../src/goblins_logic.h"

#include <cstdio>

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
	using namespace goblins;

	// tick clock: every 375 frames, never frame 0
	Check(!IsTickFrame(0) && !IsTickFrame(374) && IsTickFrame(375) && IsTickFrame(750) && !IsTickFrame(751), "tick frames");

	// recent kill: frame - last < 375; a stamp from the future (counter reset) is not recent
	Check(KilledRecently(1000, 1000) && KilledRecently(1000, 626) && !KilledRecently(1000, 625), "recent kill window");
	Check(!KilledRecently(1000, 0), "never killed");
	Check(!KilledRecently(100, 500), "future stamp is not recent");

	// chance: 10% less per player beyond the first, never below 1, 0 stays 0
	Check(EffectiveChance(10, 1) == 10, "solo chance unchanged");
	Check(EffectiveChance(10, 2) == 9 && EffectiveChance(10, 5) == 6 && EffectiveChance(10, 8) == 3, "chance per player");
	Check(EffectiveChance(10, 11) == 1 && EffectiveChance(10, 40) == 1, "chance floor 1");
	Check(EffectiveChance(16, 3) == 13, "chance 16 with 3 players (16 - 16*20/100)");
	Check(EffectiveChance(0, 1) == 0, "no chance stays none");
	Check(EffectiveChance(10, 0) == 10, "players below 1 count as 1");

	// the die: roll < chance of 1024
	Check(ChancePasses(10, 9) && !ChancePasses(10, 10) && ChancePasses(10, 1024 + 3) && !ChancePasses(10, 1023), "die");
	int hits = 0;
	for (uint32_t r = 0; r < 1024; ++r) {
		hits += ChancePasses(12, r) ? 1 : 0;
	}
	Check(hits == 12, "exactly chance of 1024 rolls pass");

	// tables
	Check(mxl_goblins::ChanceCount == 161 && mxl_goblins::GoblinCount == 21, "table sizes");
	Check(FindChance(2) != nullptr && FindChance(2)->chance == 4, "Blood Moor chance 4");
	Check(FindChance(2)->monLevel[0] == 1 && FindChance(2)->monLevel[2] == 100, "Blood Moor monster levels 1 / 51 / 100");
	Check(FindChance(1) == nullptr && FindChance(40) == nullptr, "towns have no chance");
	Check(FindChance(256) != nullptr && FindChance(256)->chance == 16, "mirror temple chance 16");
	Check(IsTown(1) && IsTown(40) && IsTown(75) && IsTown(103) && IsTown(109) && !IsTown(2), "towns");
	for (int32_t i = 0; i < mxl_goblins::TownCount; ++i) {
		Check(FindChance(mxl_goblins::Towns[i]) == nullptr, "no town has a goblin chance");
	}
	Check(NoKillStamp(2791), "killing a treasure goblin does not stamp");
	Check(!NoKillStamp(156) && !NoKillStamp(1), "ordinary monsters stamp");

	// pick: Act 1 Normal level 2 (monster level 1): treasure_goblin, shiny_hoarder, arcane_collector, glowing_baron
	const uint32_t total = TotalWeight(2, 1);
	Check(total == 2325u + 184u + 300u + 553u, "weights at monster level 1");
	Check(Pick(2, 1, 0) == 2791 && Pick(2, 1, 2324) == 2791, "first draw is the treasure goblin");
	Check(Pick(2, 1, 2325) == 2792 && Pick(2, 1, 2325 + 184) == 2796, "next rows by weight");
	Check(Pick(2, 1, 2325 + 184 + 300) == 2798, "glowing baron last");
	Check(Pick(2, 1, total) == 2791, "draw wraps modulo the total");
	// every candidate respects its bounds
	for (int32_t ml = 0; ml <= 160; ++ml) {
		for (int32_t k = 0; k < mxl_goblins::GoblinCount; ++k) {
			const mxl_goblins::Goblin& g = mxl_goblins::Goblins[k];
			const bool c = IsCandidate(g, 2, ml);
			if (c) {
				Check(ml >= g.minLevel && (g.maxLevel == 0 || ml <= g.maxLevel) && g.levelRestrict == -1, "bounds hold");
			}
		}
	}
	// glowing baron stops at monster level 99, the relic thieves start at 100 / 120, voided horror from 100
	Check(!IsCandidate(mxl_goblins::Goblins[6], 2, 100) && IsCandidate(mxl_goblins::Goblins[6], 2, 99), "baron max 99");
	Check(Pick(2, 100, 0) == 2791, "high level draw still starts with the treasure goblin");
	bool sawThief = false, sawThief2 = false, sawHorror = false, sawRainbow = false, sawBaron = false;
	const uint32_t t100 = TotalWeight(2, 100), t125 = TotalWeight(2, 125);
	for (uint32_t r = 0; r < t100; ++r) {
		const int32_t m = Pick(2, 100, r);
		sawThief |= m == 2799;
		sawHorror |= m == 2793;
		sawBaron |= m == 2798;
		sawRainbow |= m == 2797;
	}
	for (uint32_t r = 0; r < t125; ++r) {
		sawThief2 |= Pick(2, 125, r) == 2800;
	}
	Check(sawThief && sawHorror && !sawBaron && !sawRainbow, "monster level 100: thief, horror, no baron, never rainbow");
	Check(sawThief2, "monster level 125: relic thief 2");
	Check(Pick(2, 125, 0) >= 0 && TotalWeight(2, 125) > 0, "pick at 125");
	// rift inspectors only in their level (140 = island of the sunless sea)
	bool inspector140 = false, inspector2 = false;
	for (uint32_t r = 0, n = TotalWeight(140, 105); r < n; ++r) {
		const int32_t m = Pick(140, 105, r);
		inspector140 |= m == 2801;
		inspector2 |= m == 2802;
	}
	Check(inspector140 && !inspector2, "rift inspector 1 only in level 140");
	Check(Pick(2, 1, 0) != 2801, "no inspector in the wilderness");

	// per-game state
	State s;
	int g1 = 0, g2 = 0;
	Check(s.Touch(&g1, 10), "first touch resets");
	Check(!s.Touch(&g1, 11), "same game keeps state");
	s.NoteKill(7, 11);
	Check(s.LastKill(7) == 11 && s.LastKill(8) == 0, "stamp per player");
	s.NoteKill(7, 30);
	Check(s.LastKill(7) == 30, "stamp updates");
	Check(s.Touch(&g2, 31) && s.LastKill(7) == 0, "new game pointer clears stamps");
	s.NoteKill(7, 500);
	Check(s.Touch(&g2, 20) && s.LastKill(7) == 0, "frame counter going backwards clears stamps (address reuse)");
	for (uint32_t id = 1; id <= 40; ++id) {
		s.NoteKill(id, id);
	}
	Check(s.LastKill(40) == 40, "full slots recycle");
	// fallback clock: once per bucket
	State b;
	b.Touch(&g1, 1);
	Check(!b.BucketDue(100) && b.BucketDue(375) && !b.BucketDue(376) && !b.BucketDue(749) && b.BucketDue(750), "bucket clock");

	if (failures == 0) {
		std::printf("goblins: all tests passed\n");
	}
	return failures;
}

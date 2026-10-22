// Tests of Median's monster level, players-count, leash and throne rules (monster_rules_logic.h).
#include "../src/monster_rules_logic.h"

#include <cstdio>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: monster rules: %s\n", what);
	}
}
}  // namespace

int RunMonsterRulesTests() {
	using namespace monster_rules;

	// players-count life percentage: Median's table 0, 50 .. 350 for 1-8 players, the same formula beyond
	Check(HpPercent(0) == 0 && HpPercent(-3) == 0, "no players: nothing");
	Check(HpPercent(1) == 0, "one player: no bonus");
	Check(HpPercent(2) == 50 && HpPercent(3) == 100 && HpPercent(5) == 200, "two players +50 %, each one more +50 %");
	Check(HpPercent(8) == 350, "eight players: +350 %");
	Check(HpPercent(9) == 400 && HpPercent(16) == 750, "beyond the table (n - 1) * 50");
	Check(ExpPercent(1) == 0 && ExpPercent(8) == 0 && ExpPercent(99) == 0, "no experience bonus at any count");

	// the record: D2R's own table (vanilla 0, 0, 50 .. 350 for both percentages) is replaced, an unscaled record is left alone
	const Scaling eight = Players(8, 350, 350);
	Check(eight.hp == 350 && eight.exp == 0, "8 players: life kept at +350 %, experience cleared");
	const Scaling three = Players(3, 100, 100);
	Check(three.hp == 100 && three.exp == 0, "3 players: experience cleared");
	const Scaling none = Players(1, 0, 0);
	Check(none.hp == 0 && none.exp == 0, "one player stays unscaled");
	const Scaling optOut = Players(5, 0, 0);
	Check(optOut.hp == 0 && optOut.exp == 0, "a record D2R left unscaled (class opts out) stays unscaled");
	const Scaling odd = Players(4, 120, 0);
	Check(odd.hp == 150 && odd.exp == 0, "a record that differs from the table gets Median's table");

	// leash
	Check(LeashSubTiles == 40, "40 sub-tiles");
	Check(!Warps(40) && Warps(41) && Warps(100) && !Warps(0), "warp past 40, not at 40");
	Check(Warps(51, 50) && !Warps(50, 50), "the vanilla limit of 50 stays a strict compare");

	// throne
	Check(ThroneDelayMedian * 2 == ThroneDelayVanilla, "the throne delay halves");
	Check(ThroneDelayVanilla == 250 && ThroneDelayMedian == 125, "250 frames become 125");

	// patch tables
	for (const Patch& p : MonLevelPatches) {
		bool nops = true;
		for (int i = 0; i < p.size; ++i) {
			nops = nops && p.after[i] == 0x90;
		}
		Check(nops && p.size >= 5 && p.size <= 9, "monlevel patches are nops");
	}
	{
		bool nops = true;
		for (int i = 0; i < NormalTcPatch.size; ++i) {
			nops = nops && NormalTcPatch.after[i] == 0x90;
		}
		Check(nops && NormalTcPatch.size == 9 && NormalTcPatch.before[7] == 0x76, "normaltc nops the compare and its jbe");
	}
	int sites = 0;
	for (const Patch& p : LeashPatches) {
		++sites;
		bool sameBefore = true;
		for (int i = 0; i + 1 < p.size; ++i) {
			sameBefore = sameBefore && p.before[i] == p.after[i];
		}
		Check(sameBefore && p.before[p.size - 1] != p.after[p.size - 1], "leash: only the compared immediate changes");
		Check(LeashSiteLimit(p) == LeashSubTiles, "leash: every site compares against 40");
		Check(p.before[p.size - 1] == 0x64 || p.before[p.size - 1] == 0x50 || p.before[p.size - 1] == 0x32, "leash: vanilla limit 100 / 80 / 50");
	}
	Check(sites == 6, "six leash compares");
	Check(ThronePatch.before[2] == 0xfa && ThronePatch.after[2] == ThroneDelayMedian && ThronePatch.size == 6, "throne: add ecx,0xfa -> 0x7d");
	return failures;
}

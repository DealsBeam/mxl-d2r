// Tests of the monster looks rules (monster_looks_logic.h) against the compiled monstats3 data (mxl_monster_looks.h).
#include "../src/monster_looks_logic.h"

#include <cstdio>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: monster looks: %s\n", what);
	}
}
using mxl_looks::Look;
constexpr Look Plain{1, 0, 0, 0, 0, 0, {-1, -1, -1, -1}};
}  // namespace

int RunMonsterLooksTests() {
	using namespace looks;

	// the table is sorted by monster id, ids are unique, and Find finds every row
	bool sorted = true;
	for (int32_t i = 1; i < mxl_looks::LookCount; ++i) {
		sorted = sorted && mxl_looks::Looks[i - 1].monster < mxl_looks::Looks[i].monster;
	}
	Check(sorted && mxl_looks::LookCount > 0, "Looks sorted by monster id");
	bool found = true;
	for (int32_t i = 0; i < mxl_looks::LookCount; ++i) {
		found = found && Find(mxl_looks::Looks[i].monster) == &mxl_looks::Looks[i];
	}
	Check(found, "Find returns every row");
	Check(Find(-1) == nullptr && Find(0x7fffffff) == nullptr, "Find outside the table");

	// name colour (FUN_1008b570)
	Look l = Plain;
	Check(NameColour(l, 0) == 0, "plain monster has no colour");
	l.nameColor = 5;
	Check(NameColour(l, 0) == 5, "NameColor when no rank");
	l.flags = mxl_looks::RankElite;
	Check(NameColour(l, 0) == 9, "Elite 9");
	l.flags = mxl_looks::RankVeteran;
	Check(NameColour(l, 0) == 12, "Veteran 12");
	l.flags = mxl_looks::RankUber;
	Check(NameColour(l, 0) == 8, "Uber 8");
	l.flags = mxl_looks::RankHeroic;
	Check(NameColour(l, 0) == 2, "Heroic 2");
	l.flags = mxl_looks::RankBoss;
	Check(NameColour(l, 0) == 4, "Boss 4");
	l.flags = mxl_looks::RankVeteran | mxl_looks::RankBoss;
	Check(NameColour(l, 0) == 12, "the first rank bit wins");
	l.flags = mxl_looks::RankVeteran;
	Check(NameColour(l, TypeUnique) == 4, "unique (not champion) 4 whatever the rank");
	Check(NameColour(l, TypeSuperUnique) == 4, "super unique 4");
	Check(NameColour(l, TypeSuperUnique | TypeChampion) == 4, "super unique champion 4");
	Check(NameColour(l, TypeChampion) == 9, "champion 9");
	Check(NameColour(l, TypeChampion | TypeUnique) == 9, "unique champion 9");
	Check(NameColour(l, 0x10) == 12, "minion flag alone changes nothing");

	// merge with D2R's own colour: replaces white / champion / unique, keeps corpse (1) and possessed (0x1d)
	Check(MergeNameColour(0, 12) == 12 && MergeNameColour(3, 9) == 9 && MergeNameColour(4, 8) == 8, "Median colour replaces 0 / 3 / 4");
	Check(MergeNameColour(1, 12) == 1 && MergeNameColour(0x1d, 12) == 0x1d, "corpse and possessed keep D2R's colour");
	Check(MergeNameColour(0, 0) == 0 && MergeNameColour(4, 0) == 4 && MergeNameColour(3, 0) == 3, "Median 0 leaves D2R's colour");

	// automap
	l = Plain;
	Check(!AutomapMarked(l, 1), "no Automap bit, no marker");
	l.flags = mxl_looks::FlagAutomap | mxl_looks::FlagAutomapName;
	Check(AutomapMarked(l, 1) && AutomapMarked(l, 8) && AutomapNamed(l), "Automap + name while alive");
	Check(!AutomapMarked(l, 0) && !AutomapMarked(l, 12), "dying and dead monsters are not marked");
	l.flags = mxl_looks::FlagAutomap;
	Check(AutomapMarked(l, 1) && !AutomapNamed(l), "marker without name");

	// spawn overlays: unique and super unique play the boss pair
	l = Plain;
	l.overlay[0] = 53;
	l.overlay[1] = 54;
	l.overlay[2] = 100;
	l.overlay[3] = 101;
	Overlays o = SpawnOverlays(l, 0);
	Check(o.first == 53 && o.second == 54, "normal monster plays Overlay1/2");
	o = SpawnOverlays(l, TypeChampion);
	Check(o.first == 53 && o.second == 54, "champion plays Overlay1/2");
	o = SpawnOverlays(l, TypeUnique);
	Check(o.first == 100 && o.second == 101, "unique plays BossOverlay1/2");
	o = SpawnOverlays(l, TypeSuperUnique);
	Check(o.first == 100 && o.second == 101, "super unique plays BossOverlay1/2");

	// the real data: what Median sets (treasure goblin, Mephisto, rift inspector, town NPC)
	int overlays = 0, automap = 0, named = 0;
	for (const Look& row : mxl_looks::Looks) {
		if (row.overlay[0] >= 0 || row.overlay[1] >= 0 || row.overlay[2] >= 0 || row.overlay[3] >= 0) {
			++overlays;
		}
		automap += (row.flags & mxl_looks::FlagAutomap) != 0;
		named += AutomapNamed(row);
	}
	Check(overlays == 23, "23 monsters play a spawn overlay");
	Check(automap == 62 && named == 62, "62 monsters with an automap marker and name");

	return failures;
}

// Standalone test of the requirements evaluator against the generated table (not part of the plugin DLL).
// Build and run: cmake --build build/plugin --config Release --target mxl_tests; build/plugin/Release/mxl_tests.exe
#include "../src/requirements.h"

#include <cstdio>
#include <set>
#include <string>
#include <utility>

int RunQuestsTests();  // quests_test.cpp
int RunDungeonTests();  // dungeon_test.cpp
int RunNpcActionsTests();  // npc_actions_test.cpp
int RunNpcServicesTests();  // npc_services_test.cpp
int RunRequirementGatesTests();  // requirement_gates_test.cpp
int RunHirelingsTests();  // hirelings_test.cpp
int RunCombatTests();  // combat_test.cpp
int RunStatColumnsTests();  // stat_columns_test.cpp
int RunInfoPanelsTests();   // info_panels_test.cpp
int RunSkillDamageTests();  // skill_damage_test.cpp
int RunItemsExtTests();  // items_ext_test.cpp
int RunItemGenTests();  // item_gen_test.cpp
int RunItemRulesTests();  // item_rules_test.cpp
int RunWaypointsTests();  // waypoints_test.cpp
int RunTravelTests();     // travel_test.cpp
int RunMonsterAiTests();  // monster_ai_test.cpp
int RunItemEventsTests();  // item_events_test.cpp
int RunMonsterLooksTests();  // monster_looks_test.cpp
int RunSkillRestrictTests();  // skill_restrict_test.cpp
int RunHeatTests();  // heat_test.cpp
int RunStatRulesTests();  // stat_rules_test.cpp
int RunTrophyTimerTests();  // trophy_timer_test.cpp

namespace {

struct Fixture {
	int cls = 0;
	int gameDifficulty = 0;
	std::set<std::string> items;
	std::set<std::pair<int, int>> bitsNormal;  // quest, bit
	std::set<std::pair<int, int>> bitsNightmare;
	std::set<std::pair<int, int>> bitsHell;
};

bool QuestBit(void* u, int quest, int bit, int difficulty) {
	auto& f = *static_cast<Fixture*>(u);
	const int d = difficulty < 0 ? f.gameDifficulty : difficulty;
	const auto& s = d == 0 ? f.bitsNormal : d == 1 ? f.bitsNightmare : f.bitsHell;
	return s.count({quest, bit}) != 0;
}
int PlayerClass(void* u) { return static_cast<Fixture*>(u)->cls; }
bool Holds(void* u, const char* code) { return static_cast<Fixture*>(u)->items.count(code) != 0; }

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: %s\n", what);
	}
}

}  // namespace

int main() {
	using mxl_requirements::Evaluate;
	Fixture f;
	mxl_requirements::Queries q{&f, QuestBit, PlayerClass, Holds};

	// none / out of range / constant false
	Check(Evaluate(0, q), "row 0 allows");
	Check(Evaluate(-1, q), "-1 allows");
	Check(!Evaluate(33, q), "past the table denies");
	Check(!Evaluate(32, q), "row 32 is always false");

	// class rows 1-7
	for (int c = 0; c < 7; ++c) {
		f.cls = c;
		for (int row = 1; row <= 7; ++row) {
			Check(Evaluate(row, q) == (row == c + 1), "class row matches only its class");
		}
	}

	// quest bit rows, game difficulty (row 9 = quest 2 bit 0)
	Check(!Evaluate(9, q), "Blood Raven not done");
	f.bitsNormal.insert({2, 0});
	Check(Evaluate(9, q), "Blood Raven done (Normal game)");
	f.gameDifficulty = 1;
	Check(!Evaluate(9, q), "Blood Raven in Nightmare is separate");
	f.bitsNightmare.insert({2, 0});
	Check(Evaluate(9, q), "Blood Raven done (Nightmare game)");
	// fixed-difficulty rows: 19 = quest 6 bit 0 Normal, 20 = quest 13 bit 0 Nightmare
	f.gameDifficulty = 2;
	Check(!Evaluate(19, q), "row 19 needs Normal Andariel");
	f.bitsNormal.insert({6, 0});
	Check(Evaluate(19, q), "row 19 ignores the game difficulty");
	f.bitsHell.insert({13, 0});
	Check(!Evaluate(20, q), "row 20 reads Nightmare, not Hell");
	f.bitsNightmare.insert({13, 0});
	Check(Evaluate(20, q), "row 20 Nightmare");

	// does-not-hold rows 22-25
	Check(Evaluate(22, q), "no cube: allowed");
	f.items.insert("box ");
	Check(!Evaluate(22, q), "holds the cube: denied");
	Check(Evaluate(23, q) && Evaluate(24, q) && Evaluate(25, q), "other catalysts still missing");

	// Cain: row 27 = AND(Cain found (10), no cube (22)); row 28 = AND(10, 19, 23); row 31 = OR(27..30)
	f.gameDifficulty = 0;
	f.items.clear();
	f.bitsNormal.clear();
	f.bitsNightmare.clear();
	f.bitsHell.clear();
	Check(!Evaluate(27, q) && !Evaluate(31, q), "Cain: nothing done");
	f.bitsNormal.insert({4, 0});
	Check(Evaluate(27, q) && Evaluate(31, q), "Cain found, no cube: re-issue the cube");
	f.items.insert("box ");
	Check(!Evaluate(27, q) && !Evaluate(31, q), "Cain found, has the cube: no gift yet");
	f.bitsNormal.insert({6, 0});  // Andariel done (row 19 reads Normal)
	Check(Evaluate(28, q) && Evaluate(31, q), "Andariel done, no 3o[ catalyst: gift offered");
	f.items.insert("3o[ ");
	Check(!Evaluate(28, q) && !Evaluate(31, q), "catalyst held: no gift");

	// Akara row 26 = AND(Den done (8), flag 41/bit 1 (15))
	f.bitsNormal.insert({1, 0});
	Check(!Evaluate(26, q), "Akara needs the flag too");
	f.bitsNormal.insert({41, 1});
	Check(Evaluate(26, q), "Akara: Den done and flag");

	// missing callbacks deny
	mxl_requirements::Queries none{};
	Check(!Evaluate(9, none) && !Evaluate(1, none) && !Evaluate(22, none), "missing callbacks deny");
	Check(Evaluate(0, none), "none allows without callbacks");

	failures += RunQuestsTests();
	failures += RunDungeonTests();
	failures += RunNpcActionsTests();
	failures += RunNpcServicesTests();
	failures += RunRequirementGatesTests();
	failures += RunHirelingsTests();
	failures += RunCombatTests();
	failures += RunStatColumnsTests();
	failures += RunInfoPanelsTests();
	failures += RunSkillDamageTests();
	failures += RunItemsExtTests();
	failures += RunItemGenTests();
	failures += RunItemRulesTests();
	failures += RunWaypointsTests();
	failures += RunTravelTests();
	failures += RunMonsterAiTests();
	failures += RunItemEventsTests();
	failures += RunMonsterLooksTests();
	failures += RunSkillRestrictTests();
	failures += RunHeatTests();
	failures += RunStatRulesTests();
	failures += RunTrophyTimerTests();
	std::printf(failures == 0 ? "requirements: all tests passed\n" : "requirements: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}

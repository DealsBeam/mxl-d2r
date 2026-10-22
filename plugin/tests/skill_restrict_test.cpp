// Tests of Median's skill restriction modes 3 to 7 (skill_restrict_logic.h).
#include "../src/skill_restrict_logic.h"

#include <cstdio>
#include <set>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: skill restrict: %s\n", what);
	}
}
constexpr int32_t Devotion = 671;
constexpr int32_t Cold = 126;
bool Allowed(int restrict, const int16_t (&states)[3], std::set<int> have) {
	return skill_restrict::Allowed(restrict, states, Devotion, Cold, [&](int32_t s) { return have.count(s) != 0; });
}
}  // namespace

int RunSkillRestrictTests() {
	const int16_t one[3] = {10, -1, -1};
	const int16_t two[3] = {10, 11, -1};
	const int16_t three[3] = {10, 11, 12};
	const int16_t same[3] = {10, 126, 126};

	Check(Allowed(1, one, {}) && Allowed(2, one, {}) && Allowed(0, one, {}), "modes D2R handles pass through");
	// 3: refused while any listed state is set
	Check(Allowed(3, two, {}) && Allowed(3, two, {99}), "3: allowed without the states");
	Check(!Allowed(3, two, {10}) && !Allowed(3, two, {11}) && !Allowed(3, one, {10}), "3: refused with a listed state");
	// 4: every listed state needed
	Check(!Allowed(4, two, {10}) && Allowed(4, two, {10, 11}) && !Allowed(4, one, {}) && Allowed(4, one, {10}),
	      "4: all listed states");
	// 5: first set, the other two not set, all three listed
	Check(Allowed(5, three, {10}) && !Allowed(5, three, {}) && !Allowed(5, three, {10, 11}) && !Allowed(5, three, {10, 12}),
	      "5: first without the others");
	Check(!Allowed(5, two, {10}), "5: needs three listed states");
	Check(Allowed(5, same, {10}) && !Allowed(5, same, {10, 126}), "5: Heartseeker needs the state, not progressive cold");
	// 6: first or second
	Check(Allowed(6, two, {10}) && Allowed(6, two, {11}) && !Allowed(6, two, {12}) && !Allowed(6, one, {10}), "6: first or second");
	// 7: first or second or third; devotion and cold refuse
	Check(Allowed(7, two, {10}) && Allowed(7, two, {11}) && !Allowed(7, two, {}), "7: needs one listed state");
	Check(!Allowed(7, two, {10, Devotion}) && !Allowed(7, two, {10, Cold}), "7: devotion and progressive cold refuse");
	Check(!Allowed(7, one, {10}), "7: needs two listed states");
	Check(Allowed(8, one, {}), "unknown mode passes");
	return failures;
}

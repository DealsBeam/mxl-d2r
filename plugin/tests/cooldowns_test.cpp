// Standalone test of Median's cooldown rules (cooldowns_logic.h; not part of the plugin DLL).
#include "../src/cooldowns_logic.h"
#include "../src/mxl_cooldowns.h"

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

struct M {
	int32_t skill;
	int32_t group;
};
// a small table: skills 10, 11, 12 share group 1; 20 and 21 share group 2; 30 has no group
constexpr M Table[] = {{10, 1}, {11, 1}, {12, 1}, {20, 2}, {21, 2}};
constexpr size_t N = sizeof(Table) / sizeof(Table[0]);
}  // namespace

int main() {
	using namespace cooldowns;

	// the stat: zero leaves the delay, otherwise delay - stat; a start needs a delay above zero
	static_assert(StatCooldownReduction == 309);
	Check(ReducedDelay(100, 0) == 100, "no reduction");
	Check(ReducedDelay(100, 30) == 70, "reduction");
	Check(ReducedDelay(100, 100) == 0 && ReducedDelay(100, 250) == -150, "reduction past the delay");
	Check(ReducedDelay(100, -20) == 120, "a negative stat lengthens the delay (Median's sub)");
	Check(Starts(1) && !Starts(0) && !Starts(-5), "starts only above zero");

	// only a player has cooldowns: monsters (1), missiles (3), items (4), tiles (5) and the rest are always ready
	Check(HasCooldowns(0) && !HasCooldowns(1) && !HasCooldowns(2) && !HasCooldowns(3) && !HasCooldowns(4) && !HasCooldowns(-1),
	      "only unit type 0 (player) has cooldowns");

	// groups
	Check(GroupOf(Table, N, 10) == 1 && GroupOf(Table, N, 21) == 2 && GroupOf(Table, N, 30) == 0, "group of a skill");
	int32_t out[8];
	size_t n = StartList(Table, N, 11, out, 8);
	Check(n == 3 && out[0] == 10 && out[1] == 11 && out[2] == 12, "a group member starts the whole group");
	n = StartList(Table, N, 30, out, 8);
	Check(n == 1 && out[0] == 30, "a skill without a group starts only itself");
	n = StartList(Table, N, 10, out, 2);
	Check(n == 2, "the list is capped");
	Check(StartList(Table, N, 30, out, 0) == 0, "capacity 0");
	Check(GroupCount(Table, N) == 2, "group count");

	// the D2R way (one timer per skill, every member written) is Median's one timer per group: same readiness for every skill at
	// every frame, over a mix of starts with different delays, reductions and a start that must change nothing
	struct Start {
		uint32_t now;
		int32_t skill;
		int32_t delay;
		int32_t reduction;
	};
	const Start starts[] = {{100, 10, 50, 0},  {110, 30, 25, 5},  {120, 21, 40, 0}, {130, 12, 10, 0}, {140, 20, 30, 30},
	                       {150, 11, 0, 0},   {160, 30, 25, 0},  {200, 20, 100, 99}, {210, 10, 80, -20}};
	MedianTimers<M> median(Table, N);
	D2RTimers d2r;
	const int32_t skills[] = {10, 11, 12, 20, 21, 30, 31};
	bool same = true;
	for (const Start& s : starts) {
		median.Start(s.now, s.skill, s.delay, s.reduction);
		d2r.Start(Table, N, s.now, s.skill, s.delay, s.reduction);
		for (uint32_t t = s.now; t < s.now + 160; ++t) {
			for (int32_t k : skills) {
				same = same && median.Ready(t, k) == d2r.Ready(t, k);
			}
		}
	}
	Check(same, "per-skill timers written for the whole group equal Median's group timer");

	// concrete: Median's Blink / Leap / Jitan's Gate case
	MedianTimers<M> m2(Table, N);
	m2.Start(1000, 10, 50, 0);
	Check(!m2.Ready(1010, 11) && !m2.Ready(1049, 12) && m2.Ready(1050, 11), "group members wait for the same end frame");
	Check(m2.Ready(1010, 30) && m2.Ready(1010, 20), "other skills are not touched");
	m2.Start(1020, 30, 100, 0);
	Check(!m2.Ready(1119, 30) && m2.Ready(1120, 30) && !m2.Ready(1030, 10), "a skill without a group has its own timer");
	m2.Start(1030, 11, 10, 0);
	Check(m2.Ready(1040, 10), "the latest start of the group wins (shorter than the first)");
	m2.Start(2000, 20, 100, 100);
	Check(m2.Ready(2000, 20) && m2.Ready(2000, 21), "a delay reduced to zero starts nothing");

	// the generated table: sane, unique skills, group numbers above zero
	constexpr size_t G = sizeof(mxl_cooldowns::Members) / sizeof(mxl_cooldowns::Members[0]);
	Check(G > 100, "the generated table has Median's grouped skills");
	bool unique = true, positive = true;
	for (size_t i = 0; i < G; ++i) {
		positive = positive && mxl_cooldowns::Members[i].group > 0 && mxl_cooldowns::Members[i].skill >= 0;
		for (size_t j = 0; j < i; ++j) {
			unique = unique && mxl_cooldowns::Members[i].skill != mxl_cooldowns::Members[j].skill;
		}
	}
	Check(unique, "each skill is listed once");
	Check(positive, "groups are 1 or more");
	Check(GroupCount(mxl_cooldowns::Members, G) > 20, "many groups");
	int32_t big[64];
	size_t widest = 0;
	for (size_t i = 0; i < G; ++i) {
		const size_t w = StartList(mxl_cooldowns::Members, G, mxl_cooldowns::Members[i].skill, big, 64);
		widest = w > widest ? w : widest;
	}
	Check(widest > 1 && widest < 64, "the widest group fits the plugin's list of 64");

	// report
	Counters c;
	char line[400];
	FormatReport(line, sizeof(line), 156, 67, c);
	Check(std::strstr(line, "156 skills in 67 groups") != nullptr && std::strstr(line, "starts 0, reduced 0, shared 0, dropped 0") != nullptr,
	      "report");
	c.monsters = 7;
	FormatReport(line, sizeof(line), 156, 67, c);
	Check(std::strstr(line, "non-players have none") != nullptr && std::strstr(line, "non-player starts skipped 7)") != nullptr, "report counts non-player starts");
	char small[16];
	FormatReport(small, sizeof(small), 1, 1, c);
	Check(std::strlen(small) == sizeof(small) - 1, "report truncates to the buffer");

	if (failures == 0) {
		std::printf("cooldowns tests: ok\n");
	}
	return failures == 0 ? 0 : 1;
}

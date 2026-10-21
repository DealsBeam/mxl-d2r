// Tests of Median's monster AI decisions (monster_ai_logic.h): the andariel_ai think's branches in the order of the
// assembly (D2Sigma 0x1004a820), the cooldown, the fallback mapping, the Summoner rule and the MosquitoNest init.
#include "../src/monster_ai_logic.h"

#include <cstdio>
#include <vector>

namespace {

using namespace monster_ai;

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL (monster ai): %s\n", what);
	}
}

// Scripted rolls: each call pops the next answer and records the chance it was asked with.
struct Script {
	std::vector<bool> answers;
	std::vector<int> asked;
	size_t next = 0;
	auto operator()(int chance) -> bool {
		asked.push_back(chance);
		return next < answers.size() ? static_cast<bool>(answers[next++]) : false;
	}
};

auto MakeInput(int dist, bool reach, bool enemy, int enemyDist, int aip5) -> Input {
	Input in;
	in.dist = dist;
	in.inReach = reach;
	in.hasEnemy = enemy;
	in.enemyDist = enemyDist;
	in.p = {80, 70, 40, 15, aip5};
	for (int i = 1; i <= 4; ++i) {
		in.skillOk[i] = true;
	}
	return in;
}

auto Run(const Input& in, Ctl& ctl, std::vector<bool> answers, std::vector<int>* asked = nullptr) -> Decision {
	Script s;
	s.answers = std::move(answers);
	const Decision d = Andariel(in, ctl, s);
	if (asked != nullptr) {
		*asked = s.asked;
	}
	return d;
}

}  // namespace

int RunMonsterAiTests() {
	std::vector<int> asked;
	{  // in reach: the aip1 roll fails -> attack or wait on a 33% roll
		Ctl c;
		Decision d = Run(MakeInput(2, true, true, 5, 7), c, {false, true}, &asked);
		Check(d.kind == Kind::Attack && !d.onEnemy, "reach, aip1 fails, 33% passes: attack the target");
		Check(asked.size() == 2 && asked[0] == 80 && asked[1] == 33, "reach: rolls aip1 then 33");
		Check(c.phase == 1, "reach engages the phase");
		d = Run(MakeInput(2, true, true, 5, 7), c, {false, false});
		Check(d.kind == Kind::Wait && d.frames == 10, "reach, aip1 fails, 33% fails: wait 10");
	}
	{  // in reach, aip1 passes
		Ctl c;
		// skill 1 allowed, enemy, the 30% roll fails, enemy within 20, 50% passes -> skill 4 on the enemy
		Decision d = Run(MakeInput(2, true, true, 10, 7), c, {true, false, true}, &asked);
		Check(d.kind == Kind::Skill && d.skill == 4 && d.onEnemy, "reach: 50% pass is skill 4 on the enemy");
		Check(asked.size() == 3 && asked[1] == 30 && asked[2] == 50, "reach: rolls aip1, 30, 50");
		d = Run(MakeInput(2, true, true, 10, 7), c, {true, false, false});
		Check(d.kind == Kind::Skill && d.skill == 1 && d.onEnemy, "reach: 50% fail is skill 1 on the enemy");
		d = Run(MakeInput(2, true, true, 25, 7), c, {true, false, true}, &asked);
		Check(d.kind == Kind::Attack && !d.onEnemy && asked.size() == 3 && asked[2] == 33, "reach: enemy past 20 -> attack or wait 33");
		d = Run(MakeInput(2, true, true, 10, 7), c, {true, true});
		Check(d.kind == Kind::Attack && d.onEnemy, "reach: the 30% roll passing attacks the enemy");
		d = Run(MakeInput(2, true, false, 0, 7), c, {true}, &asked);
		Check(d.kind == Kind::Attack && !d.onEnemy && asked.size() == 1, "reach: no enemy attacks the target, no 30% roll");
		d = Run(MakeInput(2, true, true, 10, 6), c, {true}, &asked);
		Check(d.kind == Kind::Attack && d.onEnemy && asked.size() == 1, "reach: aip5 bit 1 off attacks the enemy without the skill rolls");
	}
	{  // out of reach, far: phase 0 waits, phase 1 walks
		Ctl c;
		Decision d = Run(MakeInput(60, false, true, 5, 7), c, {}, &asked);
		Check(d.kind == Kind::Wait && d.frames == 15 && asked.empty() && c.phase == 0, "far, idle: wait 15 and stay idle");
		c.phase = 1;
		d = Run(MakeInput(60, false, true, 5, 7), c, {});
		Check(d.kind == Kind::Walk, "far, engaged: walk");
	}
	{  // out of reach inside aip3, the aip2 roll fails
		Ctl c;
		Decision d = Run(MakeInput(30, false, true, 5, 7), c, {false});
		Check(d.kind == Kind::Walk && c.phase == 1, "near, aip2 fails, distance 30: walk and engage");
		d = Run(MakeInput(15, false, true, 5, 7), c, {false, true}, &asked);
		Check(d.kind == Kind::Attack && asked.size() == 2 && asked[1] == 50, "near, aip2 fails, 15: attack on 50%");
		d = Run(MakeInput(15, false, true, 5, 7), c, {false, false});
		Check(d.kind == Kind::Wait && d.frames == 10, "near, aip2 fails, 15, 50% fails: wait 10");
		d = Run(MakeInput(5, false, true, 5, 7), c, {false, true});
		Check(d.kind == Kind::Charge, "near, aip2 fails, under 9, 50% passes: charge");
		d = Run(MakeInput(5, false, true, 5, 7), c, {false, false, true}, &asked);
		Check(d.kind == Kind::Attack && asked.size() == 3, "under 9, charge roll fails: attack or wait 50 (second roll)");
		d = Run(MakeInput(9, false, true, 5, 7), c, {false, true}, &asked);
		Check(d.kind == Kind::Attack && asked.size() == 2, "distance 9 is not a charge range");
		d = Run(MakeInput(20, false, true, 5, 7), c, {false, true});
		Check(d.kind == Kind::Attack, "distance 20 is still attack or wait, 21 walks");
		d = Run(MakeInput(21, false, true, 5, 7), c, {false});
		Check(d.kind == Kind::Walk, "distance 21 walks");
	}
	{  // out of reach inside aip3, the aip2 roll passes: skills
		Ctl c;
		Decision d = Run(MakeInput(30, false, true, 5, 7), c, {true, true}, &asked);
		Check(d.kind == Kind::Skill && d.skill == 2 && !d.onEnemy && c.cd == SkillCooldown, "skill 2 on the target sets the cooldown");
		Check(asked.size() == 2 && asked[0] == 70 && asked[1] == 15, "rolls aip2 then aip4");
		// the cooldown counts down one per think and blocks skills 2 / 3
		d = Run(MakeInput(30, false, true, 5, 7), c, {true, false, false, false, false}, &asked);
		Check(c.cd == SkillCooldown - 1, "cooldown counts down");
		Check(d.kind == Kind::Attack || d.kind == Kind::Skill || d.kind == Kind::Walk, "a decision is made under cooldown");
		Check(asked.size() >= 2 && asked[1] == 75, "under cooldown skips the aip4 rolls and goes to the 75% roll");
		// skill 2 roll fails, skill 3 roll passes
		c = Ctl{};
		d = Run(MakeInput(30, false, true, 5, 7), c, {true, false, true}, &asked);
		Check(d.kind == Kind::Skill && d.skill == 3 && c.cd == SkillCooldown, "skill 3 when the skill 2 roll fails");
		// no skill bits: ranged skill 1/4 on the enemy within 20, else walk
		c = Ctl{};
		d = Run(MakeInput(30, false, true, 15, 1), c, {true, false}, &asked);
		Check(d.kind == Kind::Attack && !d.onEnemy, "75% fails: attack the target");
		d = Run(MakeInput(30, false, true, 15, 1), c, {true, true, true});
		Check(d.kind == Kind::Skill && d.skill == 1 && d.onEnemy, "75% passes, 50% passes: skill 1 on the enemy (inverted against the reach branch)");
		d = Run(MakeInput(30, false, true, 15, 1), c, {true, true, false});
		Check(d.kind == Kind::Skill && d.skill == 4 && d.onEnemy, "75% passes, 50% fails: skill 4 on the enemy");
		d = Run(MakeInput(30, false, true, 25, 1), c, {true});
		Check(d.kind == Kind::Walk, "enemy past 20: walk");
		d = Run(MakeInput(30, false, false, 0, 7), c, {true, false, false});
		Check(d.kind == Kind::Walk, "no enemy, no skill rolls pass: walk");
	}
	{  // rows without usable skills fall back to the attack
		Ctl c;
		Input in = MakeInput(30, false, true, 5, 7);
		in.skillOk[2] = false;
		in.skillOk[3] = false;
		Decision d = Run(in, c, {true, true, true, true}, &asked);
		Check(d.kind == Kind::Attack || d.kind == Kind::Skill, "no skill 2 / 3: never a skill 2 / 3 decision");
		Check(!(d.kind == Kind::Skill && (d.skill == 2 || d.skill == 3)), "skill 2 / 3 are not picked when the row has none");
		in = MakeInput(2, true, true, 10, 7);
		in.skillOk[4] = false;
		d = Run(in, c, {true, false, true});
		Check(d.kind == Kind::Attack && d.onEnemy, "skill 4 missing: the attack on the enemy");
	}
	{  // fallback mapping for D2R's Andariel think
		const DrAips a = FallbackAips(25, 95);
		Check(a.aip1 == 25 && a.aip2 == 3 && a.aip3 == 25 && a.aip4 == 95, "fallback keeps aip1 and aip4, aip2 3, aip3 25");
	}
	Check(SummonerFirstBranch(39, 40) && !SummonerFirstBranch(40, 40) && !SummonerFirstBranch(99, 40), "Summoner first branch is roll < aip3");
	Check(!SummonerFirstBranch(0, 0) && SummonerFirstBranch(0, 1), "aip3 0 never takes the first branch, 1 only on a 0 roll");
	Check(MosquitoFirstSpawn(1000, true, 250) == 1250 && MosquitoFirstSpawn(1000, false, 250) == 1000, "mosquito nest first spawn");
	return failures;
}

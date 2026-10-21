// Median's monster AI decisions as pure functions (no game calls), so they are unit tested (plugin/tests/monster_ai_test.cpp).
// Source: D2Sigma.dll D2AI.cpp, `andariel_ai` 0x1004a820 (read in assembly), its skill chooser 0x1004aef0, the Summoner
// first-branch rule 0x1004a470 and the MosquitoNest init 0x1004a110. docs/subsystems/monsters-ai.md section 4.
//
// andariel_ai, the live path (the one every one of the 47 monsters takes): per think, in this order
//   - the cooldown ctl[7] counts down by one when it is not zero;
//   - the unit is in reach (melee range, args "reach" flag):
//       phase = 1; (the caller advances the unit seed first, Sigma 0xc03f0); roll aip1%:
//         pass: when skill 1 is allowed (aip5 bit 1), an enemy exists and a 30% roll fails: with the enemy farther than 20
//               go to "attack or wait 33%", else a 50% roll picks skill 4 (pass) or skill 1 (fail), both on the enemy;
//               otherwise attack the enemy (the target when there is no enemy)
//         fail: "attack or wait 33%"
//   - out of reach:
//       distance >= aip3: wait 15 while the phase is 0, walk towards the target once it is 1
//       distance <  aip3: phase = 1; roll aip2%:
//         fail: farther than 20 walk; 9..20 "attack or wait 50%"; under 9 a 50% roll charges (mode 8), else "attack or wait 50%"
//         pass: skill 2 (aip5 bit 2) then skill 3 (bit 4), each when the cooldown is not above 0 and an aip4% roll passes, cooldown
//               := 11, on the target; else with skill 1 allowed, an enemy within 20: a 75% roll fails -> attack the target,
//               passes -> a 50% roll picks skill 1 (pass) or skill 4 (fail) on the enemy; else walk
// "attack or wait N%" = an N% roll passes -> attack (mode 4) the target, else wait 10 frames.
// Not ported (documented): the branch for monster data +0x54 in {3, 0x13} (a controlled monster; none of the 47 qualifies) and
// phase 2 (a charge while hit points are under 75%): ctl[5] is never set to 2 anywhere, so it is dead code in Median too.
#pragma once

#include <cstdint>

namespace monster_ai {

inline constexpr int32_t SkillCooldown = 11;  // ctl[7] after skill 2 / 3
inline constexpr int32_t NearEnemy = 20;      // enemy distance limit of the ranged skills
inline constexpr int32_t ChargeRange = 9;     // under this, a 50% roll charges (mode 8)
inline constexpr int32_t ApproachRange = 20;  // over this, an unreached target is walked to

// The three scratch words of the monster's AI control block: Sigma ctl[5..7] = D2R ctl+0x44 / +0x48 / +0x4c.
struct Ctl {
	int32_t phase = 0;  // 0 idle, 1 engaged
	int32_t mark = 0;   // used only by the controlled-monster branch
	int32_t cd = 0;     // skill 2 / 3 cooldown
};

struct Params {
	int32_t aip1 = 0, aip2 = 0, aip3 = 0, aip4 = 0, aip5 = 0;  // the monstats row, current difficulty
};

struct Input {
	int32_t dist = 0;       // distance to the target
	bool inReach = false;   // the target is in melee reach
	bool hasEnemy = false;  // the nearest-enemy search found one
	int32_t enemyDist = 0;  // its distance (only read with hasEnemy)
	Params p;
	bool skillOk[5] = {false, false, false, false, false};  // [1..4]: the row names a usable skill
};

enum class Kind : uint8_t { Wait, Attack, Charge, Skill, Walk };

struct Decision {
	Kind kind = Kind::Wait;
	int32_t frames = 0;         // Wait
	int32_t skill = 0;          // Skill: 1..4
	bool onEnemy = false;       // Attack / Skill: aim at the nearest enemy instead of the target
	bool operator==(const Decision&) const = default;
};

inline auto WaitFor(int32_t frames) noexcept -> Decision {
	Decision d;
	d.kind = Kind::Wait;
	d.frames = frames;
	return d;
}

inline auto AttackOn(bool onEnemy) noexcept -> Decision {
	Decision d;
	d.kind = Kind::Attack;
	d.onEnemy = onEnemy;
	return d;
}

// `roll(chance)` is true when a 0..99 roll is under `chance`; the order of calls is the order of Median's rolls.
template <class Roll>
auto Andariel(const Input& in, Ctl& ctl, Roll&& roll) noexcept -> Decision {
	if (ctl.cd != 0) {
		--ctl.cd;
	}
	const Params& p = in.p;
	const bool b1 = (p.aip5 & 1) != 0 && in.skillOk[1];
	const bool b2 = (p.aip5 & 2) != 0 && in.skillOk[2];
	const bool b4 = (p.aip5 & 4) != 0 && in.skillOk[3];

	const auto skill = [&](int32_t n, bool onEnemy) {
		if (!in.skillOk[n]) {  // a skill the row does not name: the plain attack, like D2R's own Andariel
			return AttackOn(onEnemy);
		}
		Decision d;
		d.kind = Kind::Skill;
		d.skill = n;
		d.onEnemy = onEnemy;
		return d;
	};
	const auto attackOrWait = [&](int32_t chance) { return roll(chance) ? AttackOn(false) : WaitFor(10); };
	const auto walk = [] {
		Decision d;
		d.kind = Kind::Walk;
		return d;
	};

	if (in.inReach) {
		ctl.phase = 1;
		Decision d;
		if (roll(p.aip1)) {
			if (b1 && in.hasEnemy && !roll(30)) {
				if (in.enemyDist > NearEnemy) {
					d = attackOrWait(33);
				} else {
					d = skill(roll(50) ? 4 : 1, true);
				}
			} else {
				d = AttackOn(in.hasEnemy);  // Median attacks the enemy pointer even when it is null; the target is the safe equal
			}
		} else {
			d = attackOrWait(33);
		}
		return d;
	}

	if (in.dist >= p.aip3) {
		return ctl.phase == 1 ? walk() : WaitFor(15);
	}
	ctl.phase = 1;
	if (!roll(p.aip2)) {
		if (in.dist > ApproachRange) {
			return walk();
		}
		if (in.dist >= ChargeRange) {
			return attackOrWait(50);
		}
		if (roll(50)) {
			Decision d;
			d.kind = Kind::Charge;
			return d;
		}
		return attackOrWait(50);
	}
	if (b2 && ctl.cd <= 0 && roll(p.aip4)) {
		ctl.cd = SkillCooldown;
		return skill(2, false);
	}
	if (b4 && ctl.cd <= 0 && roll(p.aip4)) {
		ctl.cd = SkillCooldown;
		return skill(3, false);
	}
	if (b1 && in.hasEnemy && in.enemyDist <= NearEnemy) {
		if (!roll(75)) {
			return AttackOn(false);
		}
		return skill(roll(50) ? 1 : 4, true);
	}
	return walk();
}

// With "medianai" off the 47 monsters run D2R's own Andariel think (skill 1 with aip1% in reach, otherwise wait aip2% /
// skill 2 by aip3% / walk), so the row's parameters get that meaning: tools/monster_ai_data.py's old mapping.
struct DrAips {
	int32_t aip1, aip2, aip3, aip4;
};

inline constexpr auto FallbackAips(int32_t aip1, int32_t aip4) noexcept -> DrAips {
	return {aip1, 3, 25, aip4};
}

// Summoner (0x1004a470): which of the two skill branches runs first. D2R picks by the target's cold and fire resistances.
inline constexpr auto SummonerFirstBranch(int32_t roll, int32_t aip3) noexcept -> bool {
	return roll < aip3;
}

// MosquitoNest init (0x1004a110): ctl[5] = frame + the delay of Skill1, or the frame when the row has no skill.
inline constexpr auto MosquitoFirstSpawn(int32_t frame, bool hasSkill, int32_t delay) noexcept -> int32_t {
	return hasSkill ? frame + delay : frame;
}

}  // namespace monster_ai

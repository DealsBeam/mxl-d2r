// Pure rules of Median XL's skill cooldowns (D2Sigma D2SkillValidation.cpp): no engine access, so mxl_tests can run them.
// docs/sigma-patch-audit.md section 4, docs/plugin-spec.md "Cooldowns".
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace cooldowns {

// What Median does (read from D2Sigma 0x100b0f40 / 0x100b1140 / 0x100b0ab0, called from the patched D2Common ready test,
// D2Game skill finish and D2Client skill use):
//   delay    = the skill's delay formula (D2R: localdelay) - stat 309 at layer = the skill id (unit's total; only when not 0)
//   start    = only for a player unit and delay > 0: the end frame is now + delay, stored in the unit's own table at
//              group != 0 ? timer[group] : timer[skill]  (skills2 byte +0x2c is the group); the client is sent packet 0x2b
//   ready    = non-player: always (`if (*unit != 0) return 1` at 0x100b0a40+0x70); player: now >= timer[group != 0 ? group : skill]
//              (nothing stored = ready)
// So skills of one group share one timer (the latest start wins, whichever skill started it), every other skill has its own,
// and there is no unit-wide delay state any more.
constexpr int32_t StatCooldownReduction = 309;

// Only a player unit (D2 unit type 0) has cooldowns: a monster, pet or hireling is always ready and its start stores nothing,
// so a monster skill with a delay in skills.txt can be cast again at once. D2R's start would give the monster its own timer.
constexpr int32_t UnitTypePlayer = 0;
constexpr auto HasCooldowns(int32_t unitType) noexcept -> bool {
	return unitType == UnitTypePlayer;
}

// The delay after the stat; zero reduction leaves it alone, a negative one lengthens it (Median's `sub ebx, eax`).
constexpr auto ReducedDelay(int32_t delay, int32_t reduction) noexcept -> int32_t {
	return reduction != 0 ? delay - reduction : delay;
}

// A cooldown only starts when the (reduced) delay is above zero.
constexpr auto Starts(int32_t delay) noexcept -> bool {
	return delay > 0;
}

// One skill's cooldown group (0 = none). `T` has `.skill` and `.group` (the generated mxl_cooldowns::Member).
template <typename T>
constexpr auto GroupOf(const T* members, size_t count, int32_t skill) noexcept -> int32_t {
	for (size_t i = 0; i < count; ++i) {
		if (members[i].skill == skill) {
			return members[i].group;
		}
	}
	return 0;
}

// The skills whose D2R cooldown must start together when `skill` is used: every member of its group (the skill included),
// or just the skill when it has no group. Written to `out` (at most `cap`); returns how many. D2R keeps one timer per skill
// (state 0xb9, stat 359 at layer = skill id), so giving every member the same end frame is Median's one shared timer.
template <typename T>
constexpr auto StartList(const T* members, size_t count, int32_t skill, int32_t* out, size_t cap) noexcept -> size_t {
	const int32_t group = GroupOf(members, count, skill);
	size_t n = 0;
	if (group == 0) {
		if (cap > 0) {
			out[n++] = skill;
		}
		return n;
	}
	for (size_t i = 0; i < count && n < cap; ++i) {
		if (members[i].group == group) {
			out[n++] = members[i].skill;
		}
	}
	return n;
}

// Median's own bookkeeping as a model: next-ready frame per skill and per group. Used by the unit tests to prove that
// starting every group member's D2R timer is the same as Median's single group timer.
template <typename T>
class MedianTimers {
public:
	MedianTimers(const T* members, size_t count) noexcept : members_(members), count_(count) {}
	void Start(uint32_t now, int32_t skill, int32_t delay, int32_t reduction) noexcept {
		const int32_t d = ReducedDelay(delay, reduction);
		if (!Starts(d)) {
			return;
		}
		const int32_t group = GroupOf(members_, count_, skill);
		Slot& s = group != 0 ? group_[group & 0xff] : skill_[skill & 0x3ff];
		s.until = now + static_cast<uint32_t>(d);
		s.set = true;
	}
	auto Ready(uint32_t now, int32_t skill) const noexcept -> bool {
		const int32_t group = GroupOf(members_, count_, skill);
		const Slot& s = group != 0 ? group_[group & 0xff] : skill_[skill & 0x3ff];
		return !s.set || now >= s.until;
	}

private:
	struct Slot {
		uint32_t until = 0;
		bool set = false;
	};
	const T* members_;
	size_t count_;
	Slot group_[256] = {};
	Slot skill_[1024] = {};
};

// The same, the way the plugin drives D2R: one end frame per skill, written for every start-list member.
class D2RTimers {
public:
	template <typename T>
	void Start(const T* members, size_t count, uint32_t now, int32_t skill, int32_t delay, int32_t reduction) noexcept {
		const int32_t d = ReducedDelay(delay, reduction);
		if (!Starts(d)) {
			return;
		}
		int32_t list[64];
		const size_t n = StartList(members, count, skill, list, 64);
		for (size_t i = 0; i < n; ++i) {
			Slot& s = skill_[list[i] & 0x3ff];
			s.until = now + static_cast<uint32_t>(d);
			s.set = true;
		}
	}
	auto Ready(uint32_t now, int32_t skill) const noexcept -> bool {
		const Slot& s = skill_[skill & 0x3ff];
		return !s.set || now >= s.until;
	}

private:
	struct Slot {
		uint32_t until = 0;
		bool set = false;
	};
	Slot skill_[1024] = {};
};

// Counters of the two hooks, reported by `mxl` and `h state`.
struct Counters {
	volatile long starts = 0;    // cooldown starts seen on a player (server + client)
	volatile long reduced = 0;   // of those, delays that stat 309 changed
	volatile long shared = 0;    // extra group members started
	volatile long dropped = 0;   // starts whose reduced delay was not above zero
	volatile long monsters = 0;  // starts of a non-player unit that were not passed on (Median: monsters have no cooldown)
};

// "on: D2R local cooldown start 0x439500 (server) / 0x217b90 (client): stat 309 reduces the delay, 156 skills in 67 groups share
// one timer, non-players have none (starts 0, reduced 0, shared 0, dropped 0, non-player starts skipped 0)"
inline void FormatReport(char* out, size_t outSize, size_t skills, size_t groups, const Counters& c) noexcept {
	std::snprintf(out, outSize,
	              "on: local cooldown start 0x439500 (server) / 0x217b90 (client): stat 309 reduces the delay, %zu skills in %zu groups "
	              "share one timer, non-players have none (starts %ld, reduced %ld, shared %ld, dropped %ld, non-player starts skipped %ld)",
	              skills, groups, c.starts, c.reduced, c.shared, c.dropped, c.monsters);
}

// Number of distinct groups in a table.
template <typename T>
constexpr auto GroupCount(const T* members, size_t count) noexcept -> size_t {
	size_t n = 0;
	for (size_t i = 0; i < count; ++i) {
		bool seen = false;
		for (size_t j = 0; j < i && !seen; ++j) {
			seen = members[j].group == members[i].group;
		}
		n += seen ? 0 : 1;
	}
	return n;
}

}  // namespace cooldowns

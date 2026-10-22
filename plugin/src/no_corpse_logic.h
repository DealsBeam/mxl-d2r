// Pure rules of Median XL's player death (D2Sigma patches D2Game.dll 1.13c so a dying player leaves no corpse, keeps the
// equipment, loses no gold and no experience): no engine access, so mxl_tests can run them. docs/subsystems/death.md.
#pragma once

#include <cstdint>
#include <cstdio>

namespace nocorpse {

// What Median does on a player death (D2Sigma patch table 0x101712f0, two NOP records in D2Game):
//   0x6fc99790 (16 bytes, call 0x6fc99210 in the dead-mode enter) - no corpse unit, no equipment move, no gold pile;
//   0x6fc99ed6 (5 bytes, call 0x6fc583a0 in the death-mode enter)  - no gold penalty (0x6fc58180), no XP penalty (0x6fc57730).
// Everything else stays D2's: the player still goes through death mode 0 and dead mode 0x11, the respawn packet 0x41
// (0x6fcf14b0) still restores life, mana and stamina and moves the player to the town, hardcore characters are still
// kicked from the game, a hired mercenary dies and returns as in D2.
constexpr bool CorpseCreated = false;
constexpr bool EquipmentMoved = false;
constexpr bool GoldPenalty = false;
constexpr bool ExperiencePenalty = false;

// Experience and gold the character must have after a death: the same as before (Median's rule). Used by the harness
// self check and by tools/game_checks.py `nocorpse`.
constexpr auto GoldAfterDeath(int32_t before) noexcept -> int32_t {
	return GoldPenalty ? 0 : before;
}
constexpr auto ExperienceAfterDeath(uint32_t before) noexcept -> uint32_t {
	return ExperiencePenalty ? 0 : before;
}

// Respawn wait (audit row DC4; Median's patch notes P1.0.0: "respawning after death has a 5 second cooldown, replaces the
// loss penalties"). The game runs 25 frames per second (goblins_logic.h TickFrames = 375 = 15 s), so 5 s = 125 frames.
// No code for it was found in D2Sigma / D2Game (docs/subsystems/death.md section 6), so this is the patch-note rule.
constexpr uint32_t RespawnWaitFrames = 125;

// diedAt == 0: no stamp (the death did not pass the hooks, or the character already respawned) - never held back.
// Unsigned subtraction as the game's other frame clocks, so a counter wrap does not lock a character out.
constexpr auto RespawnAllowed(uint32_t frame, uint32_t diedAt) noexcept -> bool {
	return diedAt == 0 || frame - diedAt >= RespawnWaitFrames;
}

// Frame of each player's last death, per game and unit id (a few players per game, a few games): fixed 32 slots, the
// oldest stamp is overwritten when full. Frame 0 is stored as 1 (never "no stamp").
class DeathStamps {
public:
	void Set(uintptr_t game, uint32_t unitId, uint32_t frame) noexcept {
		Slot* s = Find(game, unitId);
		if (s == nullptr) {
			s = &slots_[next_ % Count];
			++next_;
			s->game = game;
			s->unitId = unitId;
		}
		s->frame = frame == 0 ? 1 : frame;
	}
	// Keeps an existing stamp (the death-start stamp wins over the later corpse-call stamp of the same death).
	void SetIfAbsent(uintptr_t game, uint32_t unitId, uint32_t frame) noexcept {
		if (Get(game, unitId) == 0) {
			Set(game, unitId, frame);
		}
	}
	auto Get(uintptr_t game, uint32_t unitId) const noexcept -> uint32_t {
		for (const Slot& s : slots_) {
			if (s.frame != 0 && s.game == game && s.unitId == unitId) {
				return s.frame;
			}
		}
		return 0;
	}
	void Clear(uintptr_t game, uint32_t unitId) noexcept {
		Slot* s = Find(game, unitId);
		if (s != nullptr) {
			s->frame = 0;
		}
	}
	// A game that ends takes its stamps with it.
	void ClearGame(uintptr_t game) noexcept {
		for (Slot& s : slots_) {
			if (s.game == game) {
				s.frame = 0;
			}
		}
	}

private:
	static constexpr unsigned Count = 32;
	struct Slot {
		uintptr_t game = 0;
		uint32_t unitId = 0;
		uint32_t frame = 0;
	};
	auto Find(uintptr_t game, uint32_t unitId) noexcept -> Slot* {
		for (Slot& s : slots_) {
			if (s.game == game && s.unitId == unitId) {
				return &s;
			}
		}
		return nullptr;
	}
	Slot slots_[Count] = {};
	unsigned next_ = 0;
};

// Counters of the two suppressed D2R calls (corpse creator 0x42c110, death penalty 0x424ac0), reported by `mxl` and `h state`.
struct Counters {
	volatile long corpses = 0;
	volatile long penalties = 0;
	volatile long respawnsHeld = 0;  // respawn requests refused inside the 5 s wait
	volatile long respawns = 0;      // respawn requests let through
};

// "on: corpse 0x42c110 + penalty 0x424ac0 suppressed (corpses skipped 2, penalties skipped 1)"
inline void FormatReport(char* out, size_t outSize, long corpses, long penalties) noexcept {
	std::snprintf(out, outSize, "on: corpse 0x42c110 + penalty 0x424ac0 suppressed (corpses skipped %ld, penalties skipped %ld)",
	              corpses, penalties);
}

}  // namespace nocorpse

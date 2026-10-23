// Pure rules of Median XL's player death (D2Sigma patches D2Game.dll 1.13c so a dying player leaves no corpse, keeps the
// equipment, loses no gold and no experience): no engine access, so mxl_tests can run them. docs/subsystems/death.md.
#pragma once

#include "mxl_soulstealer.h"

#include <algorithm>
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

// --- Corpses that are already stored in a save (made by D2R's own death before the part existed) ------------------------
// Median's characters never have one, so a loaded corpse is picked up for the player when the character enters the game
// (D2R's own pickup, the one a click on the body runs): items back on the body locations, an item whose place is taken
// goes into the inventory, and the body is gone. docs/subsystems/death.md section 8.

// What a pickup attempt left: `ok` is the pickup function's success byte, `remaining` how many of the corpse's body items
// it could not place (they stay on the corpse), `leftItems` how many items the corpse still holds after the pickup (any
// location: the pickup moves the body-location items only).
struct PickupResult {
	bool ok = false;
	int64_t remaining = 0;
	int leftItems = 0;
};

// The body may only be deleted when nothing is left on it: deleting it with an item still inside would lose the item.
constexpr auto CorpseMayBeRemoved(const PickupResult& r) noexcept -> bool {
	return r.ok && r.remaining == 0 && r.leftItems == 0;
}

// Ids of the corpses in a player's corpse list, copied out first (a pickup edits the list while it is walked). Fixed 16:
// D2R's creator refuses a 16th corpse (count >= 0x10), a save holds at most one.
class CorpseIds {
public:
	static constexpr int Max = 16;
	// A list entry with flag 0 is no corpse (D2R keeps other pending units in the same list); ids repeat only in a damaged list.
	auto Add(uint32_t flag, uint32_t id) noexcept -> bool {
		if (flag == 0 || id == 0 || count_ >= Max) {
			return false;
		}
		for (int i = 0; i < count_; ++i) {
			if (ids_[i] == id) {
				return false;
			}
		}
		ids_[count_++] = id;
		return true;
	}
	auto Count() const noexcept -> int { return count_; }
	auto At(int i) const noexcept -> uint32_t { return ids_[i]; }

private:
	uint32_t ids_[Max] = {};
	int count_ = 0;
};

// Counters of the stored-corpse pickup, reported by `mxl` and `h state`.
struct PickupCounters {
	volatile long enters = 0;   // the hook ran (a character was placed in a level)
	volatile long picked = 0;   // corpses picked up and removed
	volatile long kept = 0;     // corpses left in place (items still on them, or the pickup said no)
	volatile long faults = 0;   // an exception inside a game call (the corpse is left as it was)
};

// "on: stored corpses are picked up when a character enters a level (0x485350): picked 1, kept 0, faults 0"
inline void FormatPickupReport(char* out, size_t outSize, long picked, long kept, long faults) noexcept {
	std::snprintf(out, outSize, "on: stored corpses are picked up when a character enters a level (0x485350): picked %ld, kept %ld, faults %ld",
	              picked, kept, faults);
}

// Soulstealer (audit DC5; Median patch notes sigma 1.1.0, 2.1.0, 2.8.0): a boss that carries the Soulstealer property heals 10% of its
// maximum life when it kills a player. The killer of the death is the boss itself, not a minion or summon of it (a minion has no
// property of its own), and one death heals once.
inline constexpr int SoulstealerDivisor = 10;
constexpr int UnitTypePlayer = 0, UnitTypeMonster = 1;

inline auto IsSoulstealer(int32_t monsterClass) noexcept -> bool {
	return std::binary_search(mxl_soulstealer::Monsters, mxl_soulstealer::Monsters + mxl_soulstealer::MonsterCount, monsterClass);
}

// Whether a player's death by `killerType` / `killerClass` heals the killer (killerType -1: no killer).
inline auto SoulstealerHeals(int killerType, int32_t killerClass) noexcept -> bool {
	return killerType == UnitTypeMonster && IsSoulstealer(killerClass);
}

// The killer's life after the heal; life and maxLife in the engine's units (256 per point), clamped to maxLife.
inline auto SoulstealerLife(int32_t life, int32_t maxLife) noexcept -> int32_t {
	if (maxLife <= 0 || life >= maxLife) {
		return life;
	}
	const int64_t healed = static_cast<int64_t>(life) + maxLife / SoulstealerDivisor;
	return static_cast<int32_t>(std::min<int64_t>(healed, maxLife));
}

// "on: 34 Soulstealer monsters heal 10% of max life when they kill a player (death penalty path 0x424ac0), N heals"
// With player deaths seen: ", player deaths 2 (last killer: type 1 class 2907)"; killer type -1 = no killer credited.
inline void FormatSoulstealerReport(char* out, size_t outSize, long heals, long deaths = 0, long lastType = -1, long lastClass = -1, long lastLife = -1, long lastMax = -1) noexcept {
	const int n = std::snprintf(out, outSize, "on: %d Soulstealer monsters heal 1/%d of max life when they kill a player (0x424ac0 killer), heals %ld",
	                            static_cast<int>(mxl_soulstealer::MonsterCount), SoulstealerDivisor, heals);
	if (deaths > 0 && n > 0 && static_cast<size_t>(n) < outSize) {
		std::snprintf(out + n, outSize - static_cast<size_t>(n), ", player deaths %ld (last killer: type %ld class %ld, life %ld of %ld)", deaths, lastType, lastClass, lastLife, lastMax);
	}
}

}  // namespace nocorpse

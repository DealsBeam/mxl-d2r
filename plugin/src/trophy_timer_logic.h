// Trophy challenge windows (docs/plugin-spec.md, "Trophy timer"). Median's trophy charms are cubed against a challenge
// code: the boss kills apply timed states that put Median stat 202 (D2R 378) on the player, and the recipes (cubemain
// op 6, param 202, value = the item's code) match when the total equals the code. The timer is the states' duration; the
// rules Median's patch notes add (P2.10.0: dying or entering another zone voids the challenge) are in no table. This
// tracker watches the code per player: a window opens when the stat takes a value, it is voided when the player dies or
// the area changes, and it closes after the trophy's time limit. No engine access, so mxl_tests can run it.
#pragma once

#include <cstddef>
#include <cstdint>

namespace trophy {

constexpr int32_t StatChallenge = 378;     // D2R id of Median's stat 202
constexpr uint32_t FramesPerSecond = 25;
constexpr uint32_t PollFrames = 5;         // the frame hook samples the players this often

// Seconds a challenge code stays open in the cube (the trophy texts, mxl_items.h Trophies; "within the time limit" is the
// limit named in the challenge lines). The kill states carry their own durations, so this only caps them: it is never
// shorter than what the text allows, counted from the moment the code appears (the kill). 0 = no plugin limit.
struct Limit {
	int32_t code;     // cubemain value
	int32_t seconds;  // cube window
};
inline constexpr Limit Limits[] = {
    {167166, 10},  // AzmodansHeart2: Heart of Sin, "Cube Azmodan's Heart within 10 seconds"
    {165165, 10},  // IdolOfVanity: The Veiled Prophet
    {175175, 10},  // EternalBonePile: Kingdom of Shadow
    {146146, 10},  // SunstoneOfTheGods: Tran Athulua, both buffs on you
    {162162, 15},  // LegOfBlood: Legacy of Blood, kill within 15 s of entering the chamber
    {393, 60},     // Xtalline2: Judgement Day, both kills within 60 s
    {169169, 10},  // SROTA: Quov Tsin
    {186000, 10},  // VSTI: Astrogha
    {176000, 10},  // TheBookOfLies2: Lord of Lies
    {151152, 10},  // UniqueDemonsbane: Duncraig
    {154154, 25},  // ColdFusionSchematics: Vizjun, "Kill the Master Control System within 25 seconds"
    {205205, 10},  // BookOfLaz: Archbishop Lazarus
    {172172, 10},  // KabraxisSoul3: Banisher of Light
    {204194, 240},  // UldyssianCharm3: Nephalem's Sacrifice, 4 minutes
    {182182, 330},  // AncientRepositories: Yshari Sanctum, 5 minutes 30 seconds
};

constexpr auto LimitFrames(int32_t code) noexcept -> uint32_t {
	for (const Limit& l : Limits) {
		if (l.code == code) {
			return static_cast<uint32_t>(l.seconds) * FramesPerSecond;
		}
	}
	return 0;
}

// The line the player gets when a window is voided or runs out (the game's chat line, colour set by the caller).
inline constexpr const char* VoidedByZone = "Trophy challenge voided: you left the area.";
inline constexpr const char* VoidedByDeath = "Trophy challenge voided: you died.";

class Tracker {
public:
	static constexpr size_t MaxPlayers = 8;

	enum class Event { None, Opened, VoidedZone, VoidedDeath, Closed };

	struct Slot {
		bool used = false;
		bool voided = false;
		uint32_t id = 0;     // player unit id
		int32_t code = 0;    // the total of stat 202 when the window opened
		int32_t area = -1;   // level the player was in then
		uint32_t start = 0;  // game frame
	};

	// A new game starts its frame counter over: forget everything.
	void Touch(uint32_t frame) noexcept {
		if (frame < last_) {
			Reset();
		}
		last_ = frame;
	}

	void Reset() noexcept {
		for (Slot& s : slots_) {
			s = Slot{};
		}
	}

	// One look at a player: `code` is its stat 202 total, `area` its level, `dead` whether it is dying or dead.
	auto Observe(uint32_t id, int32_t code, int32_t area, bool dead, uint32_t frame) noexcept -> Event {
		Slot* s = Find(id);
		if (code == 0) {
			if (s == nullptr) {
				return Event::None;
			}
			*s = Slot{};
			return Event::Closed;
		}
		if (s == nullptr || s->code != code) {
			if (s == nullptr) {
				s = Free();
			}
			if (s == nullptr) {
				return Event::None;
			}
			*s = Slot{true, dead, id, code, area, frame};
			return dead ? Event::VoidedDeath : Event::Opened;
		}
		if (s->voided) {
			return Event::None;
		}
		if (dead) {
			s->voided = true;
			return Event::VoidedDeath;
		}
		if (area != s->area) {
			s->voided = true;
			return Event::VoidedZone;
		}
		return Event::None;
	}

	// May a recipe on `code` still match for this player? A player the tracker has not seen with this code passes (the
	// poll only runs in games the frame hook sees); a voided window or one past its limit does not.
	auto Open(uint32_t id, int32_t code, uint32_t frame) const noexcept -> bool {
		const Slot* s = Find(id);
		if (s == nullptr || s->code != code) {
			return true;
		}
		if (s->voided) {
			return false;
		}
		const uint32_t limit = LimitFrames(code);
		return limit == 0 || frame - s->start <= limit;
	}

private:
	auto Find(uint32_t id) noexcept -> Slot* {
		for (Slot& s : slots_) {
			if (s.used && s.id == id) {
				return &s;
			}
		}
		return nullptr;
	}
	auto Find(uint32_t id) const noexcept -> const Slot* {
		for (const Slot& s : slots_) {
			if (s.used && s.id == id) {
				return &s;
			}
		}
		return nullptr;
	}
	auto Free() noexcept -> Slot* {
		for (Slot& s : slots_) {
			if (!s.used) {
				return &s;
			}
		}
		return nullptr;
	}

	Slot slots_[MaxPlayers];
	uint32_t last_ = 0;
};

}  // namespace trophy

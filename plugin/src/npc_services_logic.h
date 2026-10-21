// Pure check that D2R's own NPC services are the ones Median's menu slots ask for (npc_services.cpp reads the game's tables;
// docs/subsystems/npc-hirelings.md section 1.6). No engine access, so mxl_tests can run it.
//
// Median's menu slots for Charsi (imbue), Larzuk (add sockets), Anya (personalize) and the travel packages of Warriv, Meshif
// and Tyrael open a client page or move the player. D2R has all of them already, bound to the same quest state:
//   * Imbue / sockets / personalize: the client menu opener (0x1141d0) walks a table of "quest bit -> menu fix" entries (RVA
//     0x22a70e0, 24 bytes: class, quest, bit, condition, function) and adds the line "Imbue" / "Socket" / "Personalize" while
//     quest 3 / 35 / 38 bit 1 (reward pending) is set; the line opens ImbueItemsPanel (0x10dd30) and the server packet
//     handler (0x4fc230) refuses unless the NPC table says the service (+0xb: 1 imbue, 2 sockets, 3 personalize) and the quest
//     bit 1 of (+0x10 - 1) is set, then spends the reward (quest bit 0 set, bit 1 cleared).
//   * Travel: the server packet 0x37 handler (0x4a9880) reads the NPC table (+0xc destination level, +0x10 quest + 1 as gate);
//     when the line is quest-gated it also sets the waypoint of the destination act's town (0x3d6890).
// Median's requirement for each slot is a quest-bit row, so the two can be compared row by row.
#pragma once

#include "npc_actions_logic.h"
#include "mxl_requirements.h"

#include <cstdint>

namespace npcsvc {

enum class Service : uint8_t { None = 0, Imbue = 1, Sockets = 2, Personalize = 3, Travel = 4 };  // 1..3 = D2R's NPC table +0xb

// What a Median slot wants.
struct Expect {
	int cls = 0;
	Service service = Service::None;
	int quest = -1;           // requirement row is "quest bit": quest id, else -1 (no requirement)
	int bit = -1;
	int dest = 0;             // travel: destination level
	bool wantsWaypoint = false;  // travel: a waypoint action for the same level
};

// D2R's NPC table entry (RVA 0x237ea60, 0x1c bytes): +0 s16 class, +0xb service, +0xc destination (s32), +0x10 gate byte (quest + 1).
struct D2rNpc {
	bool found = false;
	uint8_t service = 0;
	int32_t dest = 0;
	int32_t gate = 0;
};

// D2R's client menu fix entry (RVA 0x22a70e0): the line is added while quest `quest` bit `bit` is set (cond 1) / clear (0).
struct D2rFix {
	bool found = false;
	int quest = 0;
	int bit = 0;
	int cond = 0;
};

enum class Verdict : uint8_t { Agree, NoNpcEntry, ServiceDiffers, DestDiffers, GateDiffers, WaypointDiffers, NoClientLine, ClientLineDiffers };

namespace detail {

constexpr void Scan(int row, Expect& e, int depth = 0) noexcept {
	if (row <= 0 || row >= mxl_npc::ActionCount || depth > npc::MaxActionDepth) {
		return;
	}
	const mxl_npc::Action& a = mxl_npc::Actions[row];
	switch (a.type) {
	case npc::Imbue: e.service = Service::Imbue; break;
	case npc::Sockets: e.service = Service::Sockets; break;
	case npc::Personalize: e.service = Service::Personalize; break;
	case npc::Travel:
		e.service = Service::Travel;
		e.dest = a.value[0];
		break;
	case npc::Waypoint: e.wantsWaypoint = true; break;
	case npc::Compound:
		for (const int32_t v : a.value) {
			Scan(v, e, depth + 1);
		}
		break;
	default: break;
	}
}

}  // namespace detail

// The expectation of top-level slot `i` (service None when the slot is not one of the native services).
constexpr auto ExpectOf(int i) noexcept -> Expect {
	Expect e;
	const mxl_npc::Slot& s = mxl_npc::Slots[i];
	e.cls = s.npcClass;
	if (s.sub != 0) {
		return e;
	}
	detail::Scan(s.action, e);
	if (s.requirement > 0 && s.requirement < mxl_requirements::RowCount) {
		const mxl_requirements::Row& r = mxl_requirements::Rows[s.requirement];
		if (r.type == 9) {
			e.quest = r.a;
			e.bit = r.b;
		}
	}
	return e;
}

// Compares one expectation with the game's entries.
constexpr auto Judge(const Expect& e, const D2rNpc& npc, const D2rFix& fix) noexcept -> Verdict {
	if (!npc.found) {
		return Verdict::NoNpcEntry;
	}
	const int gate = e.quest >= 0 ? e.quest + 1 : 0;
	if (e.service == Service::Travel) {
		if (npc.dest != e.dest) return Verdict::DestDiffers;
		if (npc.gate != gate) return Verdict::GateDiffers;
		// D2R sets the destination town's waypoint exactly when the line is quest-gated
		if ((npc.gate != 0) != e.wantsWaypoint) return Verdict::WaypointDiffers;
		// Warriv (act 1) and Meshif (act 2) also get a client menu line from the fix table; Tyrael and the ungated west
		// trips have none, which is fine
		if (fix.found && (fix.quest != e.quest || fix.bit != e.bit || fix.cond != 1)) return Verdict::ClientLineDiffers;
		return Verdict::Agree;
	}
	if (npc.service != static_cast<uint8_t>(e.service)) return Verdict::ServiceDiffers;
	if (npc.gate != gate) return Verdict::GateDiffers;
	if (!fix.found) return Verdict::NoClientLine;
	if (fix.quest != e.quest || fix.bit != e.bit || fix.cond != 1) return Verdict::ClientLineDiffers;
	return Verdict::Agree;
}

constexpr auto VerdictName(Verdict v) noexcept -> const char* {
	switch (v) {
	case Verdict::Agree: return "agrees";
	case Verdict::NoNpcEntry: return "no NPC table entry";
	case Verdict::ServiceDiffers: return "service differs";
	case Verdict::DestDiffers: return "destination differs";
	case Verdict::GateDiffers: return "quest gate differs";
	case Verdict::WaypointDiffers: return "waypoint grant differs";
	case Verdict::NoClientLine: return "no client menu line";
	case Verdict::ClientLineDiffers: return "client menu line differs";
	}
	return "?";
}

}  // namespace npcsvc

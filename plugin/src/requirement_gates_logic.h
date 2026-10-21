// Pure rules of the requirement gates (requirement_gates.cpp; docs/subsystems/quests-requirements.md section 1).
// D2Sigma evaluates the `requirements` table in exactly one place: the NPC services (shop / gamble / hire flags of npcext
// and the 16 menu slots, already served by npc_actions.cpp). D2R keeps those three service gates in a static NPC table
// (RVA 0x237ea60, 43 entries of 0x1c bytes: +0 s16 class, +7 hire, +8 trade, +9 gamble, +0x10 hire quest gate = quest id
// + 1), read by the client menu and the server packet handler alike. This header decides, from Median's rows, which bytes
// of that table must change. No engine access, so mxl_tests can run it.
#pragma once

#include "mxl_npc_services.h"
#include "mxl_requirements.h"

#include <cstdint>

namespace gates {

enum class Verdict : uint8_t { Deny, Allow, Dynamic };

// Value of a requirement row without a player: constant rows fold (type 36 false, type 0 / none true), AND / OR combine
// with short-circuit, everything that reads the player (quest bit, class, item) is Dynamic.
constexpr auto Fold(int row, int depth = 0) noexcept -> Verdict {
	if (row <= 0) {
		return Verdict::Allow;
	}
	if (row >= mxl_requirements::RowCount || depth > 16) {
		return Verdict::Deny;  // the evaluator denies an index past the table and a cycle
	}
	const mxl_requirements::Row& r = mxl_requirements::Rows[row];
	switch (r.type) {
	case 0: return Verdict::Allow;
	case 36: return Verdict::Deny;
	case 1:    // OR
	case 2: {  // AND
		const bool isOr = r.type == 1;
		bool dynamic = false;
		for (const int32_t child : r.child) {
			if (child == 0) {
				continue;
			}
			const Verdict v = Fold(child, depth + 1);
			if (v == Verdict::Dynamic) {
				dynamic = true;
			} else if ((v == Verdict::Allow) == isOr) {
				return v;  // OR with an allowed child, AND with a denied child
			}
		}
		if (dynamic) {
			return Verdict::Dynamic;
		}
		return isOr ? Verdict::Deny : Verdict::Allow;  // OR over nothing but denials; AND over nothing but allows
	}
	default: return Verdict::Dynamic;  // quest bit, class, item held: depends on the player
	}
}

// Would Median offer the service? flagBit: the npcext flag bit for it is set; row: its requirement row (-1 = none).
constexpr auto ServiceVerdict(bool flagBit, int row) noexcept -> Verdict {
	return flagBit ? Fold(row) : Verdict::Deny;
}

// The hire quest gate in D2R's table (+0x10 = quest id + 1) can express one thing: "quest `a`, bit 0 set in the game's
// difficulty". Returns that value for such a row, 0 for any other.
constexpr auto HireGateOf(int row) noexcept -> int32_t {
	if (row <= 0 || row >= mxl_requirements::RowCount) {
		return 0;
	}
	const mxl_requirements::Row& r = mxl_requirements::Rows[row];
	return (r.type == 9 && r.b == 0 && r.c == -1) ? r.a + 1 : 0;
}

// What D2R's NPC table says for one entry.
struct D2rEntry {
	int16_t cls;
	uint8_t hire;    // +7
	uint8_t trade;   // +8
	uint8_t gamble;  // +9
	int32_t hireGate;  // +0x10
};

struct Plan {
	bool found = false;     // Median has a service row for this class (false: Median offers no service at all)
	bool clearTrade = false, clearGamble = false, clearHire = false;
	bool setHireGate = false;
	int32_t hireGate = 0;
	// Counters for the report: services where D2R already agrees, where Median allows but D2R has no service
	// (left alone, the packet handler has nothing to run), and where the requirement needs the player (Dynamic).
	int agree = 0, missing = 0, dynamic = 0;
};

constexpr auto FindService(int cls) noexcept -> const mxl_npc::Service* {
	for (const mxl_npc::Service& s : mxl_npc::Services) {
		if (s.cls == cls) {
			return &s;
		}
	}
	return nullptr;
}

// One service against one byte. `have`: D2R offers it.
constexpr void Judge(Verdict v, bool have, bool& clear, Plan& p) noexcept {
	if (v == Verdict::Deny) {
		clear = have;
		p.agree += (have || !p.found) ? 0 : 1;  // an NPC without a Sigma row is not a service to count
	} else if (v == Verdict::Allow) {
		p.agree += have ? 1 : 0;
		p.missing += have ? 0 : 1;
	} else {
		++p.dynamic;
	}
}

constexpr auto PlanFor(const D2rEntry& e) noexcept -> Plan {
	Plan p;
	const mxl_npc::Service* s = FindService(e.cls);
	p.found = s != nullptr;
	const int flags = s != nullptr ? s->flags : 0;
	const int shop = s != nullptr ? s->shop : -1;
	const int gamble = s != nullptr ? s->gamble : -1;
	const int hire = s != nullptr ? s->hire : -1;
	Judge(ServiceVerdict((flags & 1) != 0, shop), e.trade != 0, p.clearTrade, p);
	Judge(ServiceVerdict((flags & 2) != 0, gamble), e.gamble != 0, p.clearGamble, p);
	const Verdict hv = ServiceVerdict((flags & 4) != 0, hire);
	if (hv == Verdict::Dynamic) {
		const int32_t gate = HireGateOf(hire);
		if (gate == 0) {
			++p.dynamic;  // not expressible in the static table
		} else if (e.hire != 0 && e.hireGate == gate) {
			++p.agree;
		} else if (e.hire != 0) {
			p.setHireGate = true;
			p.hireGate = gate;
		} else {
			++p.missing;
		}
	} else {
		Judge(hv, e.hire != 0, p.clearHire, p);
	}
	return p;
}

}  // namespace gates

// Median's NPC service requirements on D2R's static NPC table (docs/subsystems/quests-requirements.md section 1;
// docs/d2r-addresses.md section 6).
//
// D2Sigma's requirement evaluator (D2Requirements.cpp 0xa10f0) has callers only in D2NPC.cpp / D2NpcActions.cpp: the
// shop, gamble and hire flags of npcext with their requirement rows, the 16 menu-slot rows (npc_actions.cpp does those)
// and an unused vendor-stock requirement. Item use and equip, skill learning, areas, waypoints, portals and quest items
// are not gated by it; D2R's own checks (e.g. the item requirement function 0x376de0) stay as they are.
//
// D2R decides the three services from one table: RVA 0x237ea60, 43 entries of 0x1c bytes (+0 s16 class, +7 hire, +8
// trade, +9 gamble, +0x10 hire quest gate = quest id + 1), read by the client menu builder 0x1147a0 (buttons) and the
// server packet handler 0x4b0470 (answer) through the getter 0x3971a0. So a byte written here changes both sides.
// The shipped rows already agree with the table: Atma, Geglash, Cain (acts 2 and 5) and act2vendor1 are gated by the
// always-false row 32 and D2R has no shop for them, and the hire gates of Kashya (row 9) and Qual-Kehk (row 14) are D2R's
// own quest gates. The code makes that a checked fact at load and corrects the table if a future Sigma update drifts.
//
// Dynamic requirements (a row that reads the player, on a service flag) cannot be expressed in the static table; the data
// has none, and they are counted in the report so a new one is noticed. They would need a gate in the packet handler.
#include "requirement_gates.h"

#include "d2r.h"
#include "requirement_gates_logic.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint64_t NpcTableRva = 0x237ea60;  // 43 x 0x1c bytes, writable data
constexpr int NpcTableCount = 43;
constexpr int NpcEntrySize = 0x1c;
constexpr int16_t FirstClass = 148;  // Akara
constexpr int16_t LastClass = 520;   // Cain of act 5 (520)
constexpr int OffHire = 7;
constexpr int OffTrade = 8;
constexpr int OffGamble = 9;
constexpr int OffHireGate = 0x10;

auto ReadClass(const uint8_t* entry) noexcept -> int16_t {
	int16_t cls = 0;
	std::memcpy(&cls, entry, sizeof(cls));
	return cls;
}

}  // namespace

auto InstallRequirementGates(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const auto* table = reinterpret_cast<const uint8_t*>(context->exeBase + NpcTableRva);
	if (ReadClass(table) != FirstClass || ReadClass(table + (NpcTableCount - 1) * NpcEntrySize) != LastClass) {
		std::snprintf(report, reportSize, "off: NPC table at 0x%llx not recognised", static_cast<unsigned long long>(NpcTableRva));
		return false;
	}
	int agree = 0;
	int missing = 0;
	int dynamic = 0;
	int cleared = 0;
	int gates = 0;
	int failed = 0;
	for (int i = 0; i < NpcTableCount; ++i) {
		const uint8_t* e = table + i * NpcEntrySize;
		gates::D2rEntry entry{};
		entry.cls = ReadClass(e);
		entry.hire = e[OffHire];
		entry.trade = e[OffTrade];
		entry.gamble = e[OffGamble];
		std::memcpy(&entry.hireGate, e + OffHireGate, sizeof(entry.hireGate));
		const gates::Plan p = gates::PlanFor(entry);
		agree += p.agree;
		missing += p.missing;
		dynamic += p.dynamic;
		const uint64_t rva = NpcTableRva + static_cast<uint64_t>(i) * NpcEntrySize;
		static constexpr uint8_t One[1] = {1};
		const struct {
			bool clear;
			int offset;
		} bytes[] = {{p.clearTrade, OffTrade}, {p.clearGamble, OffGamble}, {p.clearHire, OffHire}};
		for (const auto& b : bytes) {
			if (b.clear) {
				const bool ok = context->PatchWriteU8(rva + static_cast<uint64_t>(b.offset), One, sizeof(One), 0);
				cleared += ok ? 1 : 0;
				failed += ok ? 0 : 1;
			}
		}
		if (p.setHireGate) {
			uint8_t old[4];
			std::memcpy(old, &entry.hireGate, sizeof(old));
			const bool ok = context->PatchWriteU32(rva + OffHireGate, old, sizeof(old), static_cast<uint32_t>(p.hireGate));
			gates += ok ? 1 : 0;
			failed += ok ? 0 : 1;
		}
	}
	std::snprintf(report, reportSize,
	              "%s: NPC table %d entries vs %d Sigma service rows: %d services agree, %d cleared, %d hire gates set, %d "
	              "Sigma-only (no D2R service), %d dynamic (not in the table)%s",
	              failed == 0 ? "on" : "partial", NpcTableCount, mxl_npc::ServiceCount, agree, cleared, gates, missing, dynamic,
	              failed == 0 ? "" : " (a write FAILED)");
	return failed == 0;
}

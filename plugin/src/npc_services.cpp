// Read-only check that D2R's own Imbue / Add sockets / Personalize lines and NPC travel (with its waypoint grant) match what
// Median's menu slots ask for (npc_services_logic.h; docs/subsystems/npc-hirelings.md section 1.6). Nothing is hooked or
// written: D2R runs these services itself, under the quest state Median's requirement rows name.
//
// Tables read (build 93847): the static NPC table at RVA 0x237ea60 (43 x 0x1c bytes, same table requirement_gates.cpp reads)
// and the client menu fix table at RVA 0x22a70e0 (24-byte entries, count dword at RVA 0x22a71b8; consumer 0x1141d0).
#include "npc_services.h"

#include "npc_services_logic.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint64_t NpcTableRva = 0x237ea60;
constexpr int NpcTableCount = 43;
constexpr int NpcEntrySize = 0x1c;
constexpr int16_t FirstClass = 148;
constexpr int16_t LastClass = 520;
constexpr uint64_t FixTableRva = 0x22a70e0;
constexpr uint64_t FixCountRva = 0x22a71b8;
constexpr int FixEntrySize = 24;
constexpr int FixMaxCount = 32;

template <typename T>
auto Read(const uint8_t* p) noexcept -> T {
	T v{};
	std::memcpy(&v, p, sizeof(T));
	return v;
}

}  // namespace

auto VerifyNativeServices(uint64_t exeBase, char* report, size_t reportSize) noexcept -> int {
	const auto* npcTable = reinterpret_cast<const uint8_t*>(exeBase + NpcTableRva);
	const auto* fixTable = reinterpret_cast<const uint8_t*>(exeBase + FixTableRva);
	const int fixCount = Read<int32_t>(reinterpret_cast<const uint8_t*>(exeBase + FixCountRva));
	if (Read<int16_t>(npcTable) != FirstClass || Read<int16_t>(npcTable + (NpcTableCount - 1) * NpcEntrySize) != LastClass ||
	    fixCount <= 0 || fixCount > FixMaxCount) {
		std::snprintf(report, reportSize, "tables not recognised, D2R's own services unchecked");
		return -1;
	}
	int agree = 0;
	int total = 0;
	char diffs[160] = "";
	size_t used = 0;
	for (int i = 0; i < mxl_npc::SlotCount; ++i) {
		const npcsvc::Expect e = npcsvc::ExpectOf(i);
		if (e.service == npcsvc::Service::None) {
			continue;
		}
		++total;
		npcsvc::D2rNpc npc;
		for (int n = 0; n < NpcTableCount; ++n) {
			const uint8_t* entry = npcTable + n * NpcEntrySize;
			if (Read<int16_t>(entry) == e.cls) {
				npc.found = true;
				npc.service = entry[0xb];
				npc.dest = Read<int32_t>(entry + 0xc);
				npc.gate = entry[0x10];
				break;
			}
		}
		npcsvc::D2rFix fix;
		for (int n = 0; n < fixCount; ++n) {
			const uint8_t* entry = fixTable + n * FixEntrySize;
			if (static_cast<int>(Read<uint16_t>(entry)) == e.cls) {
				fix.found = true;
				fix.quest = Read<int32_t>(entry + 4);
				fix.bit = Read<int32_t>(entry + 8);
				fix.cond = Read<int32_t>(entry + 12);
				break;
			}
		}
		const npcsvc::Verdict v = npcsvc::Judge(e, npc, fix);
		if (v == npcsvc::Verdict::Agree) {
			++agree;
		} else if (used < sizeof(diffs)) {
			const int w = std::snprintf(diffs + used, sizeof(diffs) - used, "%s%d %s", used > 0 ? ", " : "", e.cls,
			                            npcsvc::VerdictName(v));
			used = w > 0 ? used + static_cast<size_t>(w) : used;
		}
	}
	std::snprintf(report, reportSize, "%d of %d D2R-native services match Median's rows%s%s", agree, total, used > 0 ? " (differs: " : "",
	              used > 0 ? diffs : "");
	if (used > 0) {
		const size_t len = std::strlen(report);
		if (len + 2 < reportSize) {
			report[len] = ')';
			report[len + 1] = '\0';
		}
	}
	return total - agree;
}

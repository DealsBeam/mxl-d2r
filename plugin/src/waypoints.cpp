// Read-only check of the waypoint record against Median's waypoint numbers (waypoints_logic.h; docs/subsystems/areas-drlg.md).
//
// Nothing is hooked: a waypoint number up to 111 is stored by the engine (record bit table at RVA 0x2389010, saved in the "WS"
// section), tested by the server when a waypoint is used (0x4b6f10) and sent to the client (packet 0x63). What a number needs
// on top is a button in the waypoint panel, which actinfo.txt provides (tools/waypoints_data.py); levels it could not fit are
// reported, not worked around.
#include "waypoints.h"

#include "waypoints_logic.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint64_t BitTableRva = 0x2389010;    // 112 x {u16 word, u16 mask}
constexpr uint64_t BitCountRva = 0x23891d0;    // dword 0x70
constexpr uint64_t ConvertRva = 0x3d6670;      // (version, level, u16* number) -> 1 when the level has a waypoint
constexpr uint64_t SetRva = 0x3d6890;          // (record, number)
constexpr uint8_t ConvertEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83};
constexpr uint8_t SetEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x0f, 0xb7, 0xda, 0x48, 0x8b, 0xf9};

}  // namespace

auto VerifyWaypoints(uint64_t exeBase, char* report, size_t reportSize) noexcept -> bool {
	const auto* table = reinterpret_cast<const uint16_t*>(exeBase + BitTableRva);
	int32_t count = 0;
	std::memcpy(&count, reinterpret_cast<const void*>(exeBase + BitCountRva), sizeof(count));
	const bool bytes = std::memcmp(reinterpret_cast<const void*>(exeBase + ConvertRva), ConvertEntry, sizeof(ConvertEntry)) == 0 &&
	                   std::memcmp(reinterpret_cast<const void*>(exeBase + SetRva), SetEntry, sizeof(SetEntry)) == 0;
	if (!bytes || !wp::TableMatches(table, count)) {
		std::snprintf(report, reportSize, "off: waypoint record not as analysed (%s), numbers above 38 unchecked",
		              bytes ? "bit table differs" : "entry bytes of 0x3d6670 / 0x3d6890 differ");
		return false;
	}
	const wp::Summary s = wp::Summarise();
	char names[96] = "";
	size_t used = 0;
	for (int i = 0; i < s.unlisted && i < 8; ++i) {
		const int w = std::snprintf(names + used, sizeof(names) - used, "%s%d", i > 0 ? ", " : "", s.firstUnlisted[i]);
		used = w > 0 && used + static_cast<size_t>(w) < sizeof(names) ? used + static_cast<size_t>(w) : used;
	}
	std::snprintf(report, reportSize, "%d waypoint levels, numbers up to %d of %d bits%s%s; panel has a button for %d%s%s%s",
	              s.levels, s.highest, wp::RecordBits, s.duplicates > 0 ? ", DUPLICATE numbers" : "",
	              s.outOfRange > 0 ? ", NUMBER PAST THE RECORD" : "", s.listed, s.unlisted > 0 ? ", none for levels " : "",
	              s.unlisted > 0 ? names : "", s.unlisted > 0 ? " (9 slots per act, actinfo.txt)" : "");
	return s.duplicates == 0 && s.outOfRange == 0;
}

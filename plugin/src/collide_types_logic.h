// Missile CollideType 9 and 10 (docs/sigma-patch-audit.md item 14; plugin/src/collide_types.cpp). No engine access, so
// mxl_tests can run it.
//
// missiles.txt CollideType (byte at record +0x1ab, client copy through the same row) indexes a table of 16-byte rows
// {check function (8), collision mask (4), unused (4)}: D2R has one for the server (RVA 0x2391370) and one for the client
// (0x23589d0), nine rows each (types 0-8). The missile tick tests the tiles it moves over against the row's mask
// (0x364d50 returns the collision flags in the mask) and, when a flag is set, asks the row's check function which unit it
// hit; a flag 4 (a wall) in the result ends the missile. D2Sigma moves both tables (D2Game 0x6fc5f320 / D2Client
// 0x6fb60170 and 0x6fb617c0) and adds two rows:
//   9 = mask 0x180, the check function of type 3 and 8 (units only: flags 0x80 / 0x100, no wall bit 4 or barrier bit 1, so
//       the tick's own wall branch never ends the missile and it hits monsters and players; game check collidetypes,
//       2026-10-23: D2R's movement step ends every type at a wall, so a plain arrow shows no difference); 15 Median
//       missiles use it,
//   10 = mask 0x2080, the check function of type 1; no Median missile uses it.
// (D2Sigma fills its function table at run time, 0x10024660: rows 1 / 2 / 3 / 5 / 7 / 8 from four globals, and rows 9 and 10
// from the globals of rows 3 and 1.) D2R reads rows 9 and up from whatever follows its tables.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace collide {

constexpr int DefinedTypes = 9;     // D2R's rows (types 0-8)
constexpr int Rows = 256;           // CollideType is a byte: the plugin's tables have a row for every value
constexpr int RowSize = 16;
constexpr int MaskOffset = 8;       // mask at row + 8

struct Addition {
	int type;
	int fnFrom;       // takes the check function of this type's row
	uint32_t mask;
};
constexpr Addition Additions[] = {{9, 3, 0x180}, {10, 1, 0x2080}};
constexpr int AdditionCount = static_cast<int>(sizeof(Additions) / sizeof(Additions[0]));

// What D2R's rows 0-8 hold (build 93847), with the functions as RVAs: the plugin builds on these only when they are there.
struct Expected {
	uint64_t fnRva;   // 0 = no check function
	uint32_t mask;
};
constexpr Expected ServerRows[DefinedTypes] = {{0, 0},         {0x457c70, 0x84}, {0x457be0, 0x104}, {0x457d10, 0x184}, {0, 0},
                                               {0x457be0, 0x104}, {0, 4},          {0x457da0, 0x40},  {0x457d10, 0x185}};
constexpr Expected ClientRows[DefinedTypes] = {{0, 0},         {0x1b0080, 0x84}, {0x1b0020, 0x104}, {0x1b00f0, 0x184}, {0, 0},
                                               {0x1b0020, 0x104}, {0, 4},          {0x1b0150, 0x40},  {0x1b00f0, 0x185}};

// One 16-byte row as the engine stores it.
struct Row {
	uint64_t fn;
	uint32_t mask;
	uint32_t pad;
};
static_assert(sizeof(Row) == RowSize, "row layout");

// True when `rows` (the engine's first nine rows, functions as absolute addresses) are the expected ones.
inline auto RowsMatch(const Row* rows, const Expected* expected, uint64_t exeBase) noexcept -> bool {
	for (int i = 0; i < DefinedTypes; ++i) {
		const uint64_t fn = expected[i].fnRva == 0 ? 0 : exeBase + expected[i].fnRva;
		if (rows[i].fn != fn || rows[i].mask != expected[i].mask || rows[i].pad != 0) {
			return false;
		}
	}
	return true;
}

// The full table: the nine engine rows, Median's two, the rest empty (no check function, mask 0).
inline auto BuildTable(const Row* engine, Row* out) noexcept -> void {
	std::memset(out, 0, sizeof(Row) * Rows);
	std::memcpy(out, engine, sizeof(Row) * DefinedTypes);
	for (const Addition& a : Additions) {
		out[a.type].fn = out[a.fnFrom].fn;
		out[a.type].mask = a.mask;
	}
}

// The seven bytes of `lea reg, [rip + disp32]` (REX.W 48 / 4c, 8d, modrm mod 00 rm 101).
constexpr auto IsRipLea(const uint8_t (&b)[7]) noexcept -> bool {
	return (b[0] == 0x48 || b[0] == 0x4c) && b[1] == 0x8d && (b[2] & 0xc7) == 0x05;
}

// The address a rip-relative lea at `rva` (7 bytes) points at, as an RVA.
inline auto LeaTarget(uint64_t rva, const uint8_t (&b)[7]) noexcept -> int64_t {
	int32_t disp = 0;
	std::memcpy(&disp, b + 3, sizeof(disp));
	return static_cast<int64_t>(rva) + 7 + disp;
}

// The lea with its displacement changed to reach `target` (an absolute address) from the instruction at `at` (absolute);
// false when the displacement does not fit 32 bits.
inline auto Redirect(uint64_t at, const uint8_t (&b)[7], uint64_t target, uint8_t (&out)[7]) noexcept -> bool {
	const int64_t disp = static_cast<int64_t>(target) - static_cast<int64_t>(at + 7);
	const auto d32 = static_cast<int32_t>(disp);
	if (static_cast<int64_t>(d32) != disp) {
		return false;
	}
	std::memcpy(out, b, 7);
	std::memcpy(out + 3, &d32, sizeof(d32));
	return true;
}

// Which table a site reads and at which offset into a row.
enum class Table : uint8_t { ServerFn, ServerMask, ClientFn, ClientMask };

constexpr uint64_t ServerBase = 0x2391370;
constexpr uint64_t ClientBase = 0x23589d0;

constexpr auto BaseOf(Table t) noexcept -> uint64_t {
	switch (t) {
	case Table::ServerFn: return ServerBase;
	case Table::ServerMask: return ServerBase + MaskOffset;
	case Table::ClientFn: return ClientBase;
	default: return ClientBase + MaskOffset;
	}
}

constexpr auto IsServer(Table t) noexcept -> bool {
	return t == Table::ServerFn || t == Table::ServerMask;
}

constexpr auto OffsetOf(Table t) noexcept -> uint64_t {
	return t == Table::ServerMask || t == Table::ClientMask ? static_cast<uint64_t>(MaskOffset) : 0;
}

// The check function and mask of `type` read the way the engine does: base + 16 * type, base from one of the sites.
struct Reading {
	uint64_t fn;
	uint32_t mask;
};

inline auto ReadRow(const uint8_t* fnBase, const uint8_t* maskBase, int type) noexcept -> Reading {
	Reading r{};
	std::memcpy(&r.fn, fnBase + static_cast<size_t>(type) * RowSize, sizeof(r.fn));
	std::memcpy(&r.mask, maskBase + static_cast<size_t>(type) * RowSize, sizeof(r.mask));
	return r;
}

// D2R's missiles.txt loader clamps CollideType to 8 (game check collidetypes, 2026-10-23: the 15 rows with 9 loaded as 8, which
// has the wall bit and stops at walls). After the data tables loaded the plugin writes the value back into those records, and
// only where the clamp left its mark (the record holds 8): any other value is somebody else's and stays.
constexpr int LoaderMax = 8;

constexpr auto NeedsRecordFix(int current, int wanted) noexcept -> bool {
	return wanted > LoaderMax && wanted < Rows && current == LoaderMax;
}

}  // namespace collide

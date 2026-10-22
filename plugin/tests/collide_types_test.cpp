// Tests of the missile collide-type tables (collide_types_logic.h).
#include "../src/collide_types_logic.h"

#include <cstdio>
#include <vector>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: collide types: %s\n", what);
	}
}

// The engine's nine rows as the dump holds them, at a made-up image base.
void EngineRows(const collide::Expected* expected, uint64_t base, collide::Row* out) {
	for (int i = 0; i < collide::DefinedTypes; ++i) {
		out[i].fn = expected[i].fnRva == 0 ? 0 : base + expected[i].fnRva;
		out[i].mask = expected[i].mask;
		out[i].pad = 0;
	}
}
}  // namespace

int RunCollideTypesTests() {
	using namespace collide;
	constexpr uint64_t base = 0x140000000;
	Row engine[DefinedTypes];
	EngineRows(ServerRows, base, engine);
	Check(RowsMatch(engine, ServerRows, base), "the expected rows match themselves");
	Check(!RowsMatch(engine, ClientRows, base), "server rows are not the client's");
	engine[3].mask = 0x185;
	Check(!RowsMatch(engine, ServerRows, base), "a changed mask is noticed");
	engine[3].mask = 0x184;
	engine[7].fn += 1;
	Check(!RowsMatch(engine, ServerRows, base), "a changed function is noticed");
	engine[7].fn -= 1;

	std::vector<Row> table(Rows);
	BuildTable(engine, table.data());
	bool same = true;
	for (int i = 0; i < DefinedTypes; ++i) {
		same = same && table[i].fn == engine[i].fn && table[i].mask == engine[i].mask;
	}
	Check(same, "types 0-8 are the engine's rows");
	Check(table[9].mask == 0x180 && table[9].fn == engine[3].fn && table[9].fn == engine[8].fn, "type 9: units only, the check of types 3 / 8");
	Check((table[9].mask & 4) == 0 && (table[9].mask & 1) == 0, "type 9 has no wall bits");
	Check(table[10].mask == 0x2080 && table[10].fn == engine[1].fn, "type 10: mask 0x2080, the check of type 1");
	bool empty = true;
	for (int i = 11; i < Rows; ++i) {
		empty = empty && table[i].fn == 0 && table[i].mask == 0 && table[i].pad == 0;
	}
	Check(empty, "types above 10 are empty rows");
	Check(table[0].mask == 0 && table[6].mask == 4 && table[6].fn == 0, "walls-only type 6 stays");

	// the way the engine indexes: fn base, mask base = fn base + 8
	const auto* bytes = reinterpret_cast<const uint8_t*>(table.data());
	const Reading r9 = ReadRow(bytes, bytes + MaskOffset, 9);
	Check(r9.fn == engine[3].fn && r9.mask == 0x180, "row 9 through base + 16 * type");
	const Reading r255 = ReadRow(bytes, bytes + MaskOffset, 255);
	Check(r255.fn == 0 && r255.mask == 0, "row 255 is inside the table");

	// lea decoding and redirection
	const uint8_t lea[7] = {0x48, 0x8d, 0x15, 0xc2, 0xf1, 0xf2, 0x01};  // lea rdx,[rip+0x1f2f1c2] at 0x4621a7
	Check(IsRipLea(lea), "lea recognised");
	Check(LeaTarget(0x4621a7, lea) == static_cast<int64_t>(ServerBase), "target of the server fn reader");
	const uint8_t notLea[7] = {0x48, 0x8b, 0x15, 0, 0, 0, 0};
	Check(!IsRipLea(notLea), "mov is not a lea");
	const uint8_t r14[7] = {0x4c, 0x8d, 0x35, 0, 0, 0, 0};
	Check(IsRipLea(r14), "REX.WR lea recognised");
	const uint8_t reg[7] = {0x48, 0x8d, 0x45, 0, 0, 0, 0};  // lea rax,[rbp+0]
	Check(!IsRipLea(reg), "a lea off rbp is not rip-relative");

	uint8_t out[7];
	const uint64_t at = base + 0x4621a7;
	const uint64_t target = base + 0x10000000;
	Check(Redirect(at, lea, target, out), "redirect within reach");
	Check(out[0] == 0x48 && out[1] == 0x8d && out[2] == 0x15, "opcode bytes kept");
	int32_t d = 0;
	std::memcpy(&d, out + 3, sizeof(d));
	Check(static_cast<int64_t>(at + 7) + d == static_cast<int64_t>(target), "displacement reaches the target");
	Check(!Redirect(at, lea, base + 0x180000000ULL, out), "out of reach refused");
	Check(Redirect(at, lea, base - 0x4000000, out), "target below the instruction");

	Check(BaseOf(Table::ServerMask) == ServerBase + 8 && BaseOf(Table::ClientFn) == ClientBase, "bases of the tables");
	Check(IsServer(Table::ServerFn) && !IsServer(Table::ClientMask), "side of a table");
	Check(OffsetOf(Table::ClientMask) == 8 && OffsetOf(Table::ServerFn) == 0, "offset in the row");
	Check(NeedsRecordFix(8, 9), "a clamped 9 (loaded as 8) is restored");
	Check(NeedsRecordFix(8, 10), "a clamped 10 is restored");
	Check(!NeedsRecordFix(9, 9), "a record that already holds 9 is left");
	Check(!NeedsRecordFix(3, 9), "a record with another value is somebody else's");
	Check(!NeedsRecordFix(8, 8), "values D2R keeps are not touched");
	Check(!NeedsRecordFix(8, 256), "a value past the table is refused");
	return failures;
}

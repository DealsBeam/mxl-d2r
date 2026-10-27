// D2R's fixed item type ids moved to Median's (bug #49; tools/item_type_ids.py).
//
// D2R 3.x has a type `warl` at id 67 of itemtypes.txt that 1.13 (and so Median) does not have: every type from `h2h` on is
// one id lower in our tables. D2R.exe asks "is the item of type N" (0x373890(item, N)) with N written into the code; at
// these 25 sites N is a type at or above 67, so D2R read our neighbours instead: its rune (75) is our circlet, its healing
// potion (77) our mana potion, ... A circlet picked up or worn took the rune branch of 0x475140, whose assert
// `UnitGetType(hUnit) == UNIT_ITEM` (ItemMode.cpp:676) fires and the game dies right after. Each site's immediate gets the
// Median id of the same code (mxl_item_type_ids.h, from the built itemtypes.txt). The original bytes are checked first.
#include "item_type_ids.h"

#include "mxl_item_type_ids.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

enum class Form : uint8_t { MovEdx, LeaEdx };  // `mov edx, imm32` (ba xx 00 00 00) / `lea edx, [reg + imm8]` (8d 5x xx)

struct Site {
	uint64_t rva;  // the instruction that loads the type into edx before the call 0x373890
	Form form;
	uint8_t reg;   // lea: the ModRM byte (0x50 rax, 0x51 rcx)
	int d2rId;
	const char* code;
};

// Every call of 0x373890 in D2R.exe 93847 whose type argument is an immediate >= 67 (tools/d2r_find.py callers 0x373890).
constexpr Site Sites[] = {
	{0x0cc192, Form::MovEdx, 0, 75, "rune"},  {0x15a026, Form::MovEdx, 0, 77, "hpot"},  {0x15a037, Form::LeaEdx, 0x50, 81, "apot"},
	{0x15a046, Form::LeaEdx, 0x50, 82, "wpot"}, {0x1c85a4, Form::MovEdx, 0, 75, "rune"},  {0x2296d7, Form::MovEdx, 0, 77, "hpot"},
	{0x2296e5, Form::LeaEdx, 0x50, 82, "wpot"}, {0x2296f4, Form::LeaEdx, 0x50, 81, "apot"}, {0x23165a, Form::MovEdx, 0, 75, "rune"},
	{0x2bd438, Form::MovEdx, 0, 75, "rune"},  {0x2c2934, Form::MovEdx, 0, 75, "rune"},  {0x2d9dc2, Form::MovEdx, 0, 75, "rune"},
	{0x3854f9, Form::MovEdx, 0, 68, "h2h"},   {0x40bbef, Form::LeaEdx, 0x50, 73, "pelt"}, {0x422fec, Form::LeaEdx, 0x51, 83, "scha"},
	{0x422ff7, Form::MovEdx, 0, 84, "mcha"},  {0x43c979, Form::MovEdx, 0, 75, "rune"},  {0x43cc33, Form::MovEdx, 0, 75, "rune"},
	{0x475283, Form::MovEdx, 0, 75, "rune"},  {0x4f1e0e, Form::MovEdx, 0, 77, "hpot"},  {0x4f4d1a, Form::MovEdx, 0, 77, "hpot"},
	{0x4f4d2c, Form::LeaEdx, 0x50, 82, "wpot"}, {0x4f4d3c, Form::LeaEdx, 0x50, 81, "apot"}, {0x527eff, Form::MovEdx, 0, 96, "gem4"},
	{0x527f5a, Form::MovEdx, 0, 75, "rune"},
};

auto MedianId(const char* code) noexcept -> int {
	for (const auto& t : mxl_item_type_ids::Median) {
		if (std::strcmp(t.code, code) == 0) {
			return t.id;
		}
	}
	return -1;
}

// Instruction bytes with `id` as the type.
auto Encode(const Site& s, int id, uint8_t* out) noexcept -> uint32_t {
	if (s.form == Form::MovEdx) {
		out[0] = 0xba;
		std::memcpy(out + 1, &id, 4);
		return 5;
	}
	out[0] = 0x8d;
	out[1] = s.reg;
	out[2] = static_cast<uint8_t>(id);  // the displacement is the type: the register holds a failed test's 0 at every lea site
	return 3;
}

int g_patched = 0;

}  // namespace

auto InstallItemTypeIds(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	int patched = 0, same = 0, failed = 0;
	const char* firstFailed = "";
	for (const auto& s : Sites) {
		const int id = MedianId(s.code);
		if (id < 0 || id > 0x7f) {
			++failed;
			firstFailed = s.code;
			continue;
		}
		if (id == s.d2rId) {
			++same;
			continue;
		}
		uint8_t original[5], wanted[5];
		const uint32_t n = Encode(s, s.d2rId, original);
		Encode(s, id, wanted);
		if (context->PatchBytes(s.rva, original, n, wanted, n)) {
			++patched;
		} else {
			++failed;
			firstFailed = s.code;
		}
	}
	g_patched = patched;
	const int rune = MedianId("rune");
	std::snprintf(report, reportSize, "%s: %d of %d fixed type checks moved to Median's ids (rune 75 -> %d, hpot 77 -> %d, ...)%s%s%s",
	              failed == 0 ? "on" : "partly", patched, static_cast<int>(sizeof(Sites) / sizeof(Sites[0])), rune, MedianId("hpot"),
	              same != 0 ? ", some already equal" : "", failed != 0 ? ", bytes differ at a site of " : "", firstFailed);
	return failed == 0;
}

auto ItemTypeIdsPatched() noexcept -> int {
	return g_patched;
}

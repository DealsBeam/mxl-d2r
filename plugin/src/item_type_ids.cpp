// D2R's fixed item type ids against ours (bugs #49 / #50 / #55; tools/item_type_ids.py). A startup self-check, no patch.
//
// D2R.exe asks "is the item of type N" (0x373890(item, N)) with N written into the code; at these 25 sites N is a type at
// or above 67. D2R 3.x has the type `warl` at id 67, which 1.13 (and so Median) does not: until 0.69.16 every type from
// `h2h` on was one id lower in our itemtypes.txt, D2R's rune (75) was our circlet (the ItemMode.cpp:676 crash of #49 /
// #55) and socketed runes applied nothing (#50). 0.69.15 rewrote the 25 immediates to our ids; since 0.69.16
// txt_migrate puts D2R's own rows at D2R's numbers (`warl` 67), so our ids are D2R's and nothing needs moving. This part
// checks that each site still holds its D2R id (the bytes) and that our type of the same code has that id
// (mxl_item_type_ids.h, from the built itemtypes.txt), and logs each mismatch.
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

int g_mismatched = 0;

}  // namespace

auto InstallItemTypeIds(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	int match = 0, idDiffers = 0, bytesDiffer = 0;
	char first[96] = "";
	for (const auto& s : Sites) {
		const int id = MedianId(s.code);
		uint8_t original[5];
		const uint32_t n = Encode(s, s.d2rId, original);
		const bool bytes = std::memcmp(reinterpret_cast<const void*>(context->exeBase + s.rva), original, n) == 0;
		if (bytes && id == s.d2rId) {
			++match;
			continue;
		}
		bytes ? ++idDiffers : ++bytesDiffer;
		char line[160];
		std::snprintf(line, sizeof(line), "mxl-core: item type ids MISMATCH at 0x%llx: D2R %s %d, ours %d%s",
		              static_cast<unsigned long long>(s.rva), s.code, s.d2rId, id, bytes ? "" : ", the instruction bytes differ");
		context->LogInfo(line);
		if (first[0] == 0) {
			std::snprintf(first, sizeof(first), " (first: %s D2R %d, ours %d%s)", s.code, s.d2rId, id, bytes ? "" : ", bytes");
		}
	}
	g_mismatched = idDiffers + bytesDiffer;
	std::snprintf(report, reportSize, "%s: %d of %d fixed type checks match our item types (rune %d, hpot %d)%s%s",
	              g_mismatched == 0 ? "ok" : "MISMATCH", match, static_cast<int>(sizeof(Sites) / sizeof(Sites[0])), MedianId("rune"),
	              MedianId("hpot"), g_mismatched != 0 ? ", no patch: rebuild the data (tools/txt_migrate.py)" : "", first);
	return g_mismatched == 0;
}

auto ItemTypeIdsMismatched() noexcept -> int {
	return g_mismatched;
}

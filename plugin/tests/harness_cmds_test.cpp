// Standalone test of the harness command parsing (harness_cmds_logic.h; not part of the plugin DLL).
#include "../src/harness_cmds_logic.h"

#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: %s\n", what);
	}
}
}  // namespace

int main() {
	using namespace hcmd;

	Tokens t;
	Split("  quest 4  1\tset\r\n", &t);
	Check(t.n == 4 && std::strcmp(t.tok[0], "quest") == 0 && std::strcmp(t.tok[3], "set") == 0, "split on blanks");
	Split("", &t);
	Check(t.n == 0, "empty line");
	Split("a b c d e f g h i j k l m n o p q r s", &t);
	Check(t.n == MaxTokens, "token limit");

	long long v = 0;
	Check(ParseInt("156", &v) && v == 156, "decimal");
	Check(ParseInt("0x9c", &v) && v == 156, "hex");
	Check(ParseInt("-5", &v) && v == -5, "negative");
	Check(!ParseInt("", &v) && !ParseInt("12a", &v) && !ParseInt("0x", &v) && !ParseInt("-", &v), "bad numbers");

	int stat = 0, layer = 0;
	Check(ParseStatArg("532=3", &stat, &layer, &v) && stat == 532 && layer == 0 && v == 3, "stat=value");
	Check(ParseStatArg("70:2=-4", &stat, &layer, &v) && stat == 70 && layer == 2 && v == -4, "stat:layer=value");
	Check(!ParseStatArg("inv", &stat, &layer, &v) && !ParseStatArg("=3", &stat, &layer, &v) && !ParseStatArg("5=x", &stat, &layer, &v),
	      "not a stat argument");

	Check(CodeToDword("tbk") == 0x206b6274U && CodeToDword("box") == 0x20786f62U && CodeToDword("3o[ ") == 0x205b6f33U, "codes");
	char code[5];
	DwordToCode(0x206b6274U, code);
	Check(std::strcmp(code, "tbk") == 0, "code back");
	DwordToCode(CodeToDword("r01"), code);
	Check(std::strcmp(code, "r01") == 0, "rune code");

	Check(LowerEq("Nearest", "nearest") && !LowerEq("near", "nearest"), "case-insensitive compare");

	Check(ParseBodyLoc("rhand") == 4 && ParseBodyLoc("RING") == 6 && ParseBodyLoc("lring") == 7 && ParseBodyLoc("belt") == 8, "body names");
	Check(ParseBodyLoc("3") == 3 && ParseBodyLoc("0x0c") == 12 && ParseBodyLoc("13") == 0 && ParseBodyLoc("0") == 0 &&
	          ParseBodyLoc("tail") == 0 && ParseBodyLoc(nullptr) == 0,
	      "body numbers and rejects");

	uint8_t pkt[256];
	Check(BuildWalkPacket(pkt, 3, 0x1234, 0x0567, 0x1240, 0x0570) == 210 && pkt[0] == 3 && pkt[1] == 0x34 && pkt[2] == 0x12 &&
	          pkt[3] == 0x67 && pkt[4] == 0x05 && pkt[5] == 0x40 && pkt[6] == 0x12 && pkt[7] == 0x70 && pkt[8] == 0x05 && pkt[9] == 0 &&
	          pkt[209] == 0,
	      "walk packet");
	Check(BuildSelectSkillPacket(pkt, 154, true) == 9 && pkt[0] == 0x3c && pkt[1] == 154 && pkt[4] == 0x80 && pkt[5] == 0xff && pkt[8] == 0xff,
	      "select skill packet left");
	Check(BuildSelectSkillPacket(pkt, 300, false) == 9 && pkt[1] == 0x2c && pkt[2] == 0x01 && pkt[4] == 0, "select skill packet right");

	CastTarget ct;
	Split("cast 154", &t);
	Check(ParseCastTarget(t, 2, &ct) && ct.kind == CastTarget::Self, "cast without target");
	Split("cast 154 self", &t);
	Check(ParseCastTarget(t, 2, &ct) && ct.kind == CastTarget::Self, "cast self");
	Split("cast 154 6100 5200", &t);
	Check(ParseCastTarget(t, 2, &ct) && ct.kind == CastTarget::Point && ct.x == 6100 && ct.y == 5200, "cast point");
	Split("cast 154 unit 0x1f", &t);
	Check(ParseCastTarget(t, 2, &ct) && ct.kind == CastTarget::Unit && ct.id == 31, "cast unit");
	Split("cast 154 nearest", &t);
	Check(ParseCastTarget(t, 2, &ct) && ct.kind == CastTarget::Nearest && ct.cls == -1, "cast nearest");
	Split("cast 154 nearest 18", &t);
	Check(ParseCastTarget(t, 2, &ct) && ct.kind == CastTarget::Nearest && ct.cls == 18, "cast nearest class");
	Split("cast 154 5", &t);
	Check(!ParseCastTarget(t, 2, &ct), "one coordinate is not a target");
	Split("cast 154 -5 3", &t);
	Check(!ParseCastTarget(t, 2, &ct), "negative coordinate");
	Split("cast 154 70000 3", &t);
	Check(!ParseCastTarget(t, 2, &ct), "coordinate over 16 bits");
	Split("cast 154 unit", &t);
	Check(!ParseCastTarget(t, 2, &ct), "unit without id");

	SeenUnits seen;
	Check(seen.Add(10, 5) && seen.Add(11, 5) && seen.Add(12, 7) && !seen.Add(10, 5) && seen.n == 3, "seen units are distinct");
	char sum[100];
	Check(seen.Summary(sum, sizeof(sum)) == 2 && std::strcmp(sum, "class 5 x2, class 7 x1") == 0, "seen summary");
	seen.Reset();
	Check(seen.n == 0 && seen.Summary(sum, sizeof(sum)) == 0 && sum[0] == '\0', "seen reset");
	for (uint32_t i = 0; i < SeenUnits::Max + 5; ++i) {
		seen.Add(i, 1);
	}
	Check(seen.n == SeenUnits::Max && seen.overflow == 5, "seen overflow counted");

	std::printf(failures == 0 ? "harness: all tests passed\n" : "harness: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}

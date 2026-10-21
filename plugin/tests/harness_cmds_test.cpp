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

	std::printf(failures == 0 ? "harness: all tests passed\n" : "harness: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}

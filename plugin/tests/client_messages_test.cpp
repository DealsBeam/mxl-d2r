// Standalone test of the chat packet layout and the Median lockout texts (client_messages_logic.h; not part of the plugin DLL).
#include "../src/client_messages_logic.h"

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
	using namespace clientmsg;

	// packet layout
	uint8_t rec[ChatRecordSize];
	std::memset(rec, 0xaa, sizeof(rec));
	BuildChat(rec, TypeSystem, Orange, 0x01020304, "Closing in: 10:00");
	Check(rec[0] == 0x26 && rec[1] == 4 && rec[2] == 0 && rec[3] == 0, "header bytes");
	Check(rec[4] == 4 && rec[5] == 3 && rec[6] == 2 && rec[7] == 1, "speaker id little endian");
	Check(rec[8] == Orange && rec[9] == 0 && rec[0x0a] == 0, "colour, no name");
	Check(std::strcmp(reinterpret_cast<const char*>(rec + 0x47), "Closing in: 10:00") == 0, "text at +0x47");
	Check(rec[0x146] == 0, "tail cleared");

	// text cut at 255 bytes, always NUL terminated inside the record
	char longText[400];
	std::memset(longText, 'x', sizeof(longText));
	longText[399] = '\0';
	BuildChat(rec, TypeSystem, 0, 1, longText);
	Check(std::strlen(reinterpret_cast<const char*>(rec + 0x47)) == ChatTextMax && rec[0x47 + ChatTextMax] == 0, "long text cut");
	BuildChat(rec, TypeOverhead, 0, 1, nullptr);
	Check(rec[1] == 5 && rec[0x47] == 0, "null text, overhead type");

	// Median's lockout texts
	char out[64];
	FormatClock(600, out, sizeof(out));
	Check(std::strcmp(out, "10:00") == 0, "clock 10:00");
	FormatClock(20, out, sizeof(out));
	Check(std::strcmp(out, "00:20") == 0, "clock 00:20");
	FormatClock(3725, out, sizeof(out));
	Check(std::strcmp(out, "62:05") == 0, "clock over an hour");
	ClosingLine(95, out, sizeof(out));
	Check(std::strcmp(out, "Closing in: 01:35") == 0, "closing line");
	RequiredLevelLine(85, out, sizeof(out));
	Check(std::strcmp(out, "(Required Level: 85)") == 0, "required level line");

	// requirement lines: only the failed ones, in Median's order
	char lines[3][64];
	Check(RequirementLines(85, 0, 0, 90, 0, 0, lines) == 0, "all met");
	Check(RequirementLines(85, 0, 0, 80, 0, 0, lines) == 1 && std::strcmp(lines[0], "(Required Level: 85)") == 0, "level only");
	int n = RequirementLines(85, 2, 4, 80, 0, 0, lines);
	Check(n == 3 && std::strcmp(lines[1], "Requires Hell difficulty") == 0 && std::strcmp(lines[2], "Only for the Paladin") == 0, "all three");
	Check(RequirementLines(1, 0, 5, 99, 0, 0, lines) == 1 && std::strcmp(lines[0], "Only for the Barbarian") == 0, "class lock");
	Check(RequirementLines(1, 0, 5, 99, 0, 4, lines) == 0, "own class passes");

	// reward lines
	RewardLine("Arcane Crystal", 1, out, sizeof(out));
	Check(std::strcmp(out, "You have received: Arcane Crystal") == 0, "reward single");
	RewardLine("Shrine", 3, out, sizeof(out));
	Check(std::strcmp(out, "You have received: 3 x Shrine") == 0, "reward count");

	if (failures == 0) {
		std::printf("client_messages_test: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}

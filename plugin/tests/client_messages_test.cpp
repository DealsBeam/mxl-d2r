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

	// quest notices wait for the NPC menu (a chat line during the speech freezes it)
	Check(!NoticeDue(false, 0, 0), "held while the speech runs");
	Check(!NoticeDue(false, 0, NoticeExpireMs - 1), "held until the expiry");
	Check(!NoticeDue(true, 0, 10), "the menu just opened: settle frames first");
	Check(!NoticeDue(true, NoticeSettleFrames - 1, 10), "one frame short");
	Check(NoticeDue(true, NoticeSettleFrames, 10), "sent when the menu has been up for the settle frames");
	Check(NoticeDue(false, 0, NoticeExpireMs), "sent anyway after the expiry");
	Check(NoticeDropMs > NoticeExpireMs, "drop after expiry");

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

	// reward messages: Median's sounds, nothing for any other message (0x5e and 0x62 stay D2R's)
	Check(RewardSound(0x5f) == 217 && RewardSound(0x60) == 2608 && RewardSound(0x61) == 230, "reward sounds");
	Check(RewardSound(0x5e) == 0 && RewardSound(0x62) == 0 && RewardSound(0x15) == 0 && RewardSound(0) == 0, "other messages: no sound");
	Check(RewardSound(MsgWaypointOpen) == 2642, "waypoint activation sound");
	Check(MsgRewardGem == 0x5f && MsgRewardShrine == 0x60 && MsgRewardArmor == 0x61, "reward message ids");

	// goblin notice: Median's StrPushNotifGoblinAppear, " appears!", after the monster's name
	GoblinAppears(nullptr, out, sizeof(out));
	Check(std::strcmp(out, "Treasure Goblin appears!") == 0, "goblin notice, no name: the generic one");
	GoblinAppears("", out, sizeof(out));
	Check(std::strcmp(out, "Treasure Goblin appears!") == 0, "goblin notice, empty name: the generic one");
	GoblinAppears("Shiny Hoarder", out, sizeof(out));
	Check(std::strcmp(out, "Shiny Hoarder appears!") == 0, "goblin notice names the variant");

	// plugin messages: hidden type, marker, kind letter, hex payload
	{
		const uint8_t payload[] = {0x00, 0x01, 0xab, 0xff, 0x7e};
		Check(BuildPluginMessage(rec, 0x11223344, 'N', payload, sizeof(payload)), "plugin message built");
		Check(rec[0] == 0x26 && rec[1] == TypeHidden && rec[0x0a] == 0, "plugin message is a hidden chat packet, no name");
		const char* text = reinterpret_cast<const char*>(rec + 0x47);
		Check(std::strcmp(text, "\x01MXLN0001abff7e") == 0, "marker, kind and lower-case hex");
		char kind = 0;
		uint8_t got[PluginPayloadMax];
		size_t got_n = 0;
		Check(ParsePluginMessage(text, &kind, got, sizeof(got), &got_n) && kind == 'N' && got_n == 5 &&
		          std::memcmp(got, payload, 5) == 0,
		      "plugin message round trip");
		Check(BuildPluginMessage(rec, 1, 'T', nullptr, 0) &&
		          ParsePluginMessage(reinterpret_cast<const char*>(rec + 0x47), &kind, got, sizeof(got), &got_n) && kind == 'T' &&
		          got_n == 0,
		      "empty payload");
		Check(!ParsePluginMessage("Closing in: 01:00", &kind, got, sizeof(got), &got_n), "a normal line is not a plugin message");
		Check(!ParsePluginMessage("\x01MXL", &kind, got, sizeof(got), &got_n), "no kind letter");
		Check(!ParsePluginMessage("\x01MXLN123", &kind, got, sizeof(got), &got_n), "odd hex length");
		Check(!ParsePluginMessage("\x01MXLN12zz", &kind, got, sizeof(got), &got_n), "non-hex digit");
		Check(!ParsePluginMessage("\x01MXLN1234", &kind, got, 1, &got_n), "payload over the capacity");
		uint8_t big[PluginPayloadMax + 1] = {};
		Check(!BuildPluginMessage(rec, 1, 'N', big, sizeof(big)), "payload over the packet's text size");
		Check(BuildPluginMessage(rec, 1, 'N', big, PluginPayloadMax) &&
		          std::strlen(reinterpret_cast<const char*>(rec + 0x47)) <= ChatTextMax,
		      "largest payload fits the text field");
		uint8_t t[TimerPayloadSize];
		BuildTimerPayload(t, 253, 61000);
		Check(Get16(t) == 253 && Get16(t + 2) == 0 && Get32(t + 4) == 61000, "timer payload");
		Check(SecondsLeft(61000) == 61 && SecondsLeft(60001) == 61 && SecondsLeft(1) == 1 && SecondsLeft(0) == 0,
		      "countdown seconds round up");
	}

	if (failures == 0) {
		std::printf("client_messages_test: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}

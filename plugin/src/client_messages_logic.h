// Messages from the server to one player, the pure part of client_messages.cpp (unit-tested in
// tests/client_messages_test.cpp): the layout of D2R's chat packet 0x26 and the Median texts that go into it.
//
// D2R's server sends a line of text as packet 0x26 ("chat"): the server builds a 0x147-byte record, 0x477a30(client,
// record) trims it and writes it to the connection. Layout of the record (D2Game SUnitMsg.cpp 0x53acf0 builds one,
// D2Client SCmd.cpp 0x12c190 reads it):
//   +0x00 u8 packet id 0x26   +0x01 u8 chat type   +0x02 u8 language   +0x03 u8 speaker unit type
//   +0x04 u32 speaker unit id +0x08 u8 colour      +0x09 u8 name flag  +0x0a char name[0x3d] (NUL terminated, <= 60)
//   +0x47 char text[0xff] (NUL terminated, <= 255)   +0x146 u8 cleared by the sender
// Median's own texts (D2Sigma string table, ids from the client code 0x5d150 / 0xf4bb0): docs/subsystems/ubers-events.md 3a.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace clientmsg {

constexpr size_t ChatRecordSize = 0x147;
constexpr size_t ChatNameMax = 0x3c;
constexpr size_t ChatTextMax = 0xff;
constexpr uint8_t ChatPacketId = 0x26;

// Chat type (byte +1), found in game with the harness verb `sysmsg` (docs/d2r-addresses.md section 12): 4 is a line in
// the chat log at the bottom left, coloured by the colour byte; 5 is an overhead bubble above the speaker; 1 is a log line
// too. 0, 2, 3 and 6-9 show nothing. Inline colour codes in the text are not interpreted.
constexpr uint8_t TypeSystem = 4;
constexpr uint8_t TypeOverhead = 5;

// Colours of the colour byte (+8), the index of D2's text colour list.
enum Colour : uint8_t { White = 0, Red = 1, Green = 2, Blue = 3, Tan = 4, Orange = 8, Yellow = 9 };

// Fills `rec` (ChatRecordSize bytes) with a system line. `text` is UTF-8, longer text is cut at ChatTextMax bytes.
inline void BuildChat(uint8_t* rec, uint8_t type, uint8_t colour, uint32_t speakerId, const char* text) noexcept {
	std::memset(rec, 0, ChatRecordSize);
	rec[0] = ChatPacketId;
	rec[1] = type;
	rec[3] = 0;  // unit type 0: a player
	std::memcpy(rec + 4, &speakerId, sizeof(speakerId));
	rec[8] = colour;
	if (text != nullptr) {
		size_t n = std::strlen(text);
		n = n > ChatTextMax ? ChatTextMax : n;
		std::memcpy(rec + 0x47, text, n);
	}
}

// A line sent while an NPC is speaking stops the speech: a chat line of type 1, 4 or 5 that reaches the client during an
// NPC's speech (the scrolling quest text) freezes it, the speech never ends and the NPC menu never opens (harness
// experiment 2026-10-22, Akara's Cain speech; type 0, which shows nothing, does not). The client opens the menu when the
// speech is over, so the notices of quest dialog handlers wait for the NPC menu to be up (the client's menu-open flag),
// then a few frames, and go out anyway when it never opens.
constexpr int NoticeSettleFrames = 2;
constexpr uint64_t NoticeExpireMs = 120000;
constexpr uint64_t NoticeDropMs = 150000;  // the client is gone or in another game: give up
constexpr auto NoticeDue(bool menuOpen, int framesOpen, uint64_t waitedMs) noexcept -> bool {
	return (menuOpen && framesOpen >= NoticeSettleFrames) || waitedMs >= NoticeExpireMs;
}

// "mm:ss" like D2Sigma's DungeonWidget timer (wsprintfW "%02d:%02d", Dungeon.cpp 0xf4fc0).
inline void FormatClock(uint32_t seconds, char* out, size_t outSize) noexcept {
	std::snprintf(out, outSize, "%02u:%02u", seconds / 60, seconds % 60);
}

// Median string 11192 ("Join another game to enter again\nThis area is locked") is the sealed level's tooltip.
inline constexpr const char* SealedLine1 = "This area is locked";
inline constexpr const char* SealedLine2 = "Join another game to enter again";
// Median string 11189 "(Required Level: %d)".
inline void RequiredLevelLine(int level, char* out, size_t outSize) noexcept {
	std::snprintf(out, outSize, "(Required Level: %d)", level);
}
// Median string 11193, shown with the clock while the level is open.
inline constexpr const char* ExitWarning = "ONCE YOU EXIT YOU CANNOT GO BACK";
// Median string 11191 "Closing in: %s".
inline void ClosingLine(uint32_t seconds, char* out, size_t outSize) noexcept {
	char clock[16];
	FormatClock(seconds, clock, sizeof(clock));
	std::snprintf(out, outSize, "Closing in: %s", clock);
}

// Quest reward notice (D2Sigma client messages 0x5f-0x61, text not found in the D2Sigma tables: neutral wording).
// `count` > 1 prefixes the number.
inline void RewardLine(const char* what, int count, char* out, size_t outSize) noexcept {
	if (count > 1) {
		std::snprintf(out, outSize, "You have received: %d x %s", count, what);
	} else {
		std::snprintf(out, outSize, "You have received: %s", what);
	}
}

// The goblin notice (D2Sigma packet 0x5a type 0x15, no text in the client): neutral wording.
inline constexpr const char* GoblinSighted = "A treasure goblin has been sighted nearby!";

// The lines of an entry refusal for requirements, in the order Median's popup shows them: the level, the difficulty, the
// class. Returns the number of lines written (0..3) into `lines` (each `lineSize` bytes). Only the failed requirements
// are listed. `classLock` is class + 1, 0 = any class.
inline auto RequirementLines(int levelReq, int minDifficulty, int classLock, int playerLevel, int difficulty, int cls,
                             char lines[3][64]) noexcept -> int {
	static constexpr const char* DifficultyNames[] = {"Normal", "Nightmare", "Hell"};
	static constexpr const char* ClassNames[] = {"Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin"};
	int n = 0;
	if (playerLevel < levelReq) {
		RequiredLevelLine(levelReq, lines[n++], 64);
	}
	if (difficulty < minDifficulty) {
		const int d = minDifficulty < 0 ? 0 : (minDifficulty > 2 ? 2 : minDifficulty);
		std::snprintf(lines[n++], 64, "Requires %s difficulty", DifficultyNames[d]);
	}
	if (classLock != 0 && cls + 1 != classLock) {
		const int c = classLock - 1;
		std::snprintf(lines[n++], 64, "Only for the %s", (c >= 0 && c < 7) ? ClassNames[c] : "other class");
	}
	return n;
}

}  // namespace clientmsg

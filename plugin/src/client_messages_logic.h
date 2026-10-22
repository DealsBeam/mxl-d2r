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

// Plugin messages: structured data from the server part of the plugin to the client part, carried in a chat packet 0x26 of
// type 0 (which D2R shows nowhere, and which does not freeze an NPC's speech like a visible line does). The text is the
// marker, one kind letter and the payload as hex digits; the client's packet-table entry for 0x26 (client_messages.cpp)
// takes the packets that start with the marker and hands the rest to the registered handler, every other chat packet goes to
// D2R's own reader.
constexpr uint8_t TypeHidden = 0;
constexpr char PluginMarker[] = "\x01MXL";  // 4 characters
constexpr size_t PluginMarkerSize = 4;
constexpr size_t PluginPayloadMax = (ChatTextMax - PluginMarkerSize - 1) / 2;  // 125 bytes

// Fills `rec` as BuildChat does with a hidden plugin message. False when the payload is too long.
inline auto BuildPluginMessage(uint8_t* rec, uint32_t speakerId, char kind, const uint8_t* data, size_t n) noexcept -> bool {
	if (n > PluginPayloadMax || (n != 0 && data == nullptr)) {
		return false;
	}
	char text[ChatTextMax + 1];
	std::memcpy(text, PluginMarker, PluginMarkerSize);
	text[PluginMarkerSize] = kind;
	static constexpr char Hex[] = "0123456789abcdef";
	for (size_t i = 0; i < n; ++i) {
		text[PluginMarkerSize + 1 + i * 2] = Hex[data[i] >> 4];
		text[PluginMarkerSize + 2 + i * 2] = Hex[data[i] & 15];
	}
	text[PluginMarkerSize + 1 + n * 2] = '\0';
	BuildChat(rec, TypeHidden, 0, speakerId, text);
	return true;
}

// Reads a chat text: true when it is a plugin message; `kind` and the payload bytes (at most `cap`) are filled, `n` is the
// payload length. A malformed payload (odd length, a non-hex digit, more than `cap` bytes) is not a message.
inline auto ParsePluginMessage(const char* text, char* kind, uint8_t* out, size_t cap, size_t* n) noexcept -> bool {
	if (text == nullptr || std::memcmp(text, PluginMarker, PluginMarkerSize) != 0 || text[PluginMarkerSize] == '\0') {
		return false;
	}
	*kind = text[PluginMarkerSize];
	const char* hex = text + PluginMarkerSize + 1;
	size_t len = 0;
	auto digit = [](char c) -> int {
		return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1);
	};
	while (hex[len] != '\0') {
		++len;
	}
	if (len % 2 != 0 || len / 2 > cap) {
		return false;
	}
	for (size_t i = 0; i < len / 2; ++i) {
		const int hi = digit(hex[i * 2]);
		const int lo = digit(hex[i * 2 + 1]);
		if (hi < 0 || lo < 0) {
			return false;
		}
		out[i] = static_cast<uint8_t>(hi * 16 + lo);
	}
	*n = len / 2;
	return true;
}

// Kinds in use.
constexpr char PluginKindNpcPage = 'N';  // the server answers a click on an NPC page line: which entries are open (npc_actions)
constexpr char PluginKindTimer = 'T';    // the dungeon clock of the level the player stands in (dungeon_lockout)
constexpr char PluginKindInfo = 'I';     // open a help panel (info_panels): one byte, the panel index

// Little endian field writers / readers for the payloads.
inline void Put16(uint8_t* p, uint16_t v) noexcept { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }
inline void Put32(uint8_t* p, uint32_t v) noexcept { Put16(p, static_cast<uint16_t>(v)); Put16(p + 2, static_cast<uint16_t>(v >> 16)); }
inline auto Get16(const uint8_t* p) noexcept -> uint16_t { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline auto Get32(const uint8_t* p) noexcept -> uint32_t { return Get16(p) | (static_cast<uint32_t>(Get16(p + 2)) << 16); }

// 'T' payload (8 bytes): u16 level, u16 0, u32 milliseconds left on that level's clock (0 = no running clock).
constexpr size_t TimerPayloadSize = 8;
inline void BuildTimerPayload(uint8_t* p, uint16_t level, uint32_t remainingMs) noexcept {
	Put16(p, level);
	Put16(p + 2, 0);
	Put32(p + 4, remainingMs);
}

// Whole seconds still shown for `ms` left (rounded up, like a countdown that reads 00:01 until it ends).
constexpr auto SecondsLeft(uint64_t ms) noexcept -> uint32_t {
	return static_cast<uint32_t>((ms + 999) / 1000);
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
// The quest-table condition of the warp test has no text of its own in Median (the unit message alone); this stands in.
inline constexpr const char* QuestLine = "You have not finished the quest for this area";
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

// Quest reward notice (D2Sigma client messages 0x5f-0x61). Median's replacement of the client's unit-message handler
// (D2Sigma 0xb13a0, jump table 0xb13f8..) plays a sound on the player's unit and shows nothing else: 0x5f item_gem (sounds
// row 217; the Arcane Crystal of Jerhyn / Cain), 0x60 shrine_boink (row 4715; Ormus's shrine items), 0x61 item_lightarmor
// (row 230; Griswold's Heart). The messages ride D2R's unit-message packet 0x2c (server 0x491960, client handler 0x12c870),
// so only the player they are sent to hears them. D2R knows 0x61 as the Ancients' reward voice (sound 4379) and nothing
// for 0x5f / 0x60; the client hook plays Median's sounds instead. shrine_boink is a custom sound file (the final art pass),
// D2R's holy shrine sound (row 2608) stands in. The text line below is only the fallback when the client hook is off.
inline constexpr uint32_t MsgRewardGem = 0x5f;
inline constexpr uint32_t MsgRewardShrine = 0x60;
inline constexpr uint32_t MsgRewardArmor = 0x61;
inline constexpr int SoundItemGem = 217;
inline constexpr int SoundShrineStandIn = 2608;
inline constexpr int SoundItemLightArmor = 230;
// The sound row of a reward message, 0 for any other message.
constexpr auto RewardSound(uint32_t message) noexcept -> int {
	return message == MsgRewardGem ? SoundItemGem
	                               : (message == MsgRewardShrine ? SoundShrineStandIn : (message == MsgRewardArmor ? SoundItemLightArmor : 0));
}
// `count` > 1 prefixes the number.
inline void RewardLine(const char* what, int count, char* out, size_t outSize) noexcept {
	if (count > 1) {
		std::snprintf(out, outSize, "You have received: %d x %s", count, what);
	} else {
		std::snprintf(out, outSize, "You have received: %s", what);
	}
}

// The goblin notice. Median's client keeps the goblin's unit id from packet 0x5a type 0x15 (handler 0x91a50) and its push
// notification code (D2Notifications.cpp 0x90f00) shows the unit's name followed by string 12320 StrPushNotifGoblinAppear,
// " appears!" (patchstring.tbl), so the line is "<monster name> appears!". The server has no client-side unit, it writes
// the variant's own monstats NameStr text (mxl_goblins.h, from the migrated monstats row: "Shiny Hoarder", "Rift
// Inspector", ...); the generic name only stands in when the row has none.
inline constexpr const char* GoblinName = "Treasure Goblin";
inline constexpr const char* GoblinAppearsSuffix = " appears!";
inline void GoblinAppears(const char* name, char* out, size_t outSize) noexcept {
	std::snprintf(out, outSize, "%s%s", (name != nullptr && name[0] != 0) ? name : GoblinName, GoblinAppearsSuffix);
}

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

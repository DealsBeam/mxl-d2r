// Pure parsing for the test harness gameplay commands (harness_cmds.cpp; unit test plugin/tests/harness_cmds_test.cpp).
// A command line is "h <verb> <args...>": whitespace separated tokens, numbers decimal or 0x hex.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace hcmd {

constexpr int MaxTokens = 24;  // a 17-byte server packet through `pkt` is 18 tokens
constexpr size_t MaxLine = 512;

struct Tokens {
	char text[MaxLine] = {};
	const char* tok[MaxTokens] = {};
	int n = 0;
};

// Splits `line` on spaces / tabs (at most MaxTokens, the rest of an over-long line is dropped).
inline void Split(const char* line, Tokens* t) noexcept {
	t->n = 0;
	size_t len = 0;
	while (line != nullptr && line[len] != '\0' && len < MaxLine - 1) {
		t->text[len] = line[len];
		++len;
	}
	t->text[len] = '\0';
	char* p = t->text;
	while (*p != '\0' && t->n < MaxTokens) {
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
			*p++ = '\0';
		}
		if (*p == '\0') {
			break;
		}
		t->tok[t->n++] = p;
		while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
			++p;
		}
	}
}

inline auto LowerEq(const char* a, const char* b) noexcept -> bool {
	if (a == nullptr || b == nullptr) {
		return false;
	}
	for (; *a != '\0' && *b != '\0'; ++a, ++b) {
		const char x = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
		const char y = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
		if (x != y) {
			return false;
		}
	}
	return *a == '\0' && *b == '\0';
}

// Decimal or 0x hex, optional leading '-'. The whole string must be a number.
inline auto ParseInt(const char* s, long long* out) noexcept -> bool {
	if (s == nullptr || *s == '\0') {
		return false;
	}
	bool neg = false;
	if (*s == '-') {
		neg = true;
		++s;
	}
	unsigned base = 10;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
	}
	if (*s == '\0') {
		return false;
	}
	unsigned long long v = 0;
	for (; *s != '\0'; ++s) {
		unsigned d = 0;
		if (*s >= '0' && *s <= '9') {
			d = static_cast<unsigned>(*s - '0');
		} else if (base == 16 && *s >= 'a' && *s <= 'f') {
			d = static_cast<unsigned>(*s - 'a' + 10);
		} else if (base == 16 && *s >= 'A' && *s <= 'F') {
			d = static_cast<unsigned>(*s - 'A' + 10);
		} else {
			return false;
		}
		v = v * base + d;
		if (v > 0x7fffffffffffffffULL) {
			return false;
		}
	}
	*out = neg ? -static_cast<long long>(v) : static_cast<long long>(v);
	return true;
}

// "532=3" or "532:1=3" (stat, optional layer, value). Names with a letter first are not stats.
inline auto ParseStatArg(const char* s, int* stat, int* layer, long long* value) noexcept -> bool {
	if (s == nullptr) {
		return false;
	}
	const char* eq = std::strchr(s, '=');
	if (eq == nullptr || eq == s) {
		return false;
	}
	char left[24] = {};
	const size_t n = static_cast<size_t>(eq - s);
	if (n >= sizeof(left)) {
		return false;
	}
	std::memcpy(left, s, n);
	char* colon = std::strchr(left, ':');
	long long st = 0, ly = 0, v = 0;
	if (colon != nullptr) {
		*colon = '\0';
		if (!ParseInt(colon + 1, &ly)) {
			return false;
		}
	}
	if (!ParseInt(left, &st) || !ParseInt(eq + 1, &v) || st < 0 || st > 0xffff || ly < 0 || ly > 0xffff) {
		return false;
	}
	*stat = static_cast<int>(st);
	*layer = static_cast<int>(ly);
	*value = v;
	return true;
}

// Item code of up to 4 characters ("tbk", "box") as the little-endian dword the game uses, space padded.
inline auto CodeToDword(const char* code) noexcept -> uint32_t {
	uint32_t v = 0;
	for (int i = 0; i < 4; ++i) {
		const unsigned char c = (code != nullptr && static_cast<size_t>(i) < std::strlen(code)) ? static_cast<unsigned char>(code[i]) : ' ';
		v |= static_cast<uint32_t>(c) << (8 * i);
	}
	return v;
}

inline void DwordToCode(uint32_t v, char out[5]) noexcept {
	for (int i = 0; i < 4; ++i) {
		const char c = static_cast<char>((v >> (8 * i)) & 0xff);
		out[i] = (c >= 32 && c < 127) ? c : '?';
	}
	out[4] = '\0';
	for (int i = 3; i > 0 && out[i] == ' '; --i) {
		out[i] = '\0';
	}
}

// --- body locations (bodylocs.txt order) ----------------------------------------------------------------------------
// "head", "amulet", "body", "rhand", "lhand", "rring", "lring", "belt", "boots", "gloves", "altr", "altl" or a number
// 1-12. Returns 0 for anything else.
inline auto ParseBodyLoc(const char* s) noexcept -> int {
	struct Name {
		const char* name;
		int loc;
	};
	static const Name names[] = {
	    {"head", 1},  {"helm", 1},   {"neck", 2},  {"amulet", 2}, {"torso", 3}, {"body", 3},  {"armor", 3}, {"rhand", 4},
	    {"rarm", 4},  {"weapon", 4}, {"lhand", 5}, {"larm", 5},   {"shield", 5}, {"rring", 6}, {"ring", 6},  {"lring", 7},
	    {"belt", 8},  {"feet", 9},   {"boots", 9}, {"gloves", 10}, {"altr", 11}, {"altl", 12},
	};
	long long v = 0;
	if (ParseInt(s, &v)) {
		return (v >= 1 && v <= 12) ? static_cast<int>(v) : 0;
	}
	for (const Name& n : names) {
		if (LowerEq(s, n.name)) {
			return n.loc;
		}
	}
	return 0;
}

// --- client packets built for the server handlers --------------------------------------------------------------------
// D2R's walk / run packets (ids 1 and 3, handlers 0x4ac050 / 0x4ace40) are 210 bytes: u8 id, u16 x, u16 y (where the
// client thinks it stands), u16 x, u16 y (the destination), u8 path length (0 = the server finds the path), then up to 50
// four-byte path nodes. Returns the packet size.
constexpr size_t WalkPacketSize = 210;
inline auto BuildWalkPacket(uint8_t* out, uint8_t id, int curX, int curY, int destX, int destY) noexcept -> size_t {
	std::memset(out, 0, WalkPacketSize);
	out[0] = id;
	const uint16_t v[4] = {static_cast<uint16_t>(curX), static_cast<uint16_t>(curY), static_cast<uint16_t>(destX),
	                       static_cast<uint16_t>(destY)};
	std::memcpy(out + 1, &v[0], 2);
	std::memcpy(out + 3, &v[1], 2);
	std::memcpy(out + 5, &v[2], 2);
	std::memcpy(out + 7, &v[3], 2);
	return WalkPacketSize;
}

// Packet 0x3c (select a skill, handler 0x4b4250): u8 id, u32 skill id (bit 31 = left button), u32 item id (-1 = none).
inline auto BuildSelectSkillPacket(uint8_t* out, int skillId, bool left) noexcept -> size_t {
	const uint32_t a = static_cast<uint32_t>(skillId) | (left ? 0x80000000U : 0U);
	const uint32_t b = 0xffffffffU;
	out[0] = 0x3c;
	std::memcpy(out + 1, &a, 4);
	std::memcpy(out + 5, &b, 4);
	return 9;
}

// --- cast target: "[x y] | unit <id> | nearest [class] | self" starting at token `from` -----------------------------------
struct CastTarget {
	enum Kind { Self, Point, Unit, Nearest } kind = Self;
	long long x = 0, y = 0, id = 0, cls = -1;
};
inline auto ParseCastTarget(const Tokens& t, int from, CastTarget* out) noexcept -> bool {
	*out = CastTarget();
	if (t.n <= from) {
		return true;
	}
	if (LowerEq(t.tok[from], "self")) {
		return t.n == from + 1;
	}
	if (LowerEq(t.tok[from], "unit")) {
		out->kind = CastTarget::Unit;
		return t.n == from + 2 && ParseInt(t.tok[from + 1], &out->id) && out->id >= 0;
	}
	if (LowerEq(t.tok[from], "nearest")) {
		out->kind = CastTarget::Nearest;
		if (t.n == from + 2) {
			return ParseInt(t.tok[from + 1], &out->cls) && out->cls >= 0;
		}
		return t.n == from + 1;
	}
	out->kind = CastTarget::Point;
	return t.n == from + 2 && ParseInt(t.tok[from], &out->x) && ParseInt(t.tok[from + 1], &out->y) && out->x >= 0 && out->y >= 0 &&
	       out->x < 0x10000 && out->y < 0x10000;
}

// --- units seen since a reset (missiles are short lived: a poll of the live list misses most of them) -----------------
struct SeenUnits {
	static constexpr int Max = 1024;
	uint32_t id[Max] = {};
	int32_t cls[Max] = {};
	int n = 0;
	int overflow = 0;
	void Reset() noexcept {
		n = 0;
		overflow = 0;
	}
	// true when the unit was new
	auto Add(uint32_t unitId, int32_t unitClass) noexcept -> bool {
		for (int i = 0; i < n; ++i) {
			if (id[i] == unitId && cls[i] == unitClass) {
				return false;
			}
		}
		if (n >= Max) {
			++overflow;
			return false;
		}
		id[n] = unitId;
		cls[n] = unitClass;
		++n;
		return true;
	}
	// "class 12 x3, class 80 x1" (classes in order of appearance), returns the number of classes
	auto Summary(char* out, size_t size) const noexcept -> int {
		int classes[64];
		int counts[64];
		int k = 0;
		for (int i = 0; i < n; ++i) {
			int j = 0;
			while (j < k && classes[j] != cls[i]) {
				++j;
			}
			if (j == k && k < 64) {
				classes[k] = cls[i];
				counts[k] = 0;
				++k;
			}
			if (j < k) {
				++counts[j];
			}
		}
		size_t len = 0;
		if (size > 0) {
			out[0] = '\0';
		}
		for (int j = 0; j < k && len + 24 < size; ++j) {
			len += static_cast<size_t>(std::snprintf(out + len, size - len, "%sclass %d x%d", j > 0 ? ", " : "", classes[j], counts[j]));
		}
		return k;
	}
};

// --- unit references of the verify verbs: "player", "merc" (the player's mercenary), "mon:<unit id>" ------------------------
struct UnitRef {
	enum Kind { None, Player, Merc, Monster } kind = None;
	long long id = 0;
};
inline auto ParseUnitRef(const char* s, UnitRef* out) noexcept -> bool {
	*out = UnitRef();
	if (s == nullptr) {
		return false;
	}
	if (LowerEq(s, "player")) {
		out->kind = UnitRef::Player;
		return true;
	}
	if (LowerEq(s, "merc")) {
		out->kind = UnitRef::Merc;
		return true;
	}
	if ((s[0] == 'm' || s[0] == 'M') && (s[1] == 'o' || s[1] == 'O') && (s[2] == 'n' || s[2] == 'N') && s[3] == ':') {
		out->kind = UnitRef::Monster;
		return ParseInt(s + 4, &out->id) && out->id >= 0;
	}
	return false;
}

// --- tallies of the verify verbs ---------------------------------------------------------------------------------------
// `itemroll`: items made in a loop and freed again; sockets 0..7+ and the ethereal flag counted.
struct RollTally {
	static constexpr int MaxSockets = 8;
	int n = 0;
	int sockets[MaxSockets + 1] = {};  // the last slot holds MaxSockets and more
	int ethereal = 0;
	static constexpr int MaxQuality = 10;
	int qualities[MaxQuality + 1] = {};  // the quality the generator really gave (the last slot holds MaxQuality and more)
	int automagic = 0;                   // items with an automagic affix id
	void Add(int socketCount, bool isEthereal) noexcept {
		++n;
		++sockets[socketCount < 0 ? 0 : (socketCount > MaxSockets ? MaxSockets : socketCount)];
		ethereal += isEthereal ? 1 : 0;
	}
	void AddQuality(int quality, bool hasAutomagic) noexcept {
		++qualities[quality < 0 ? 0 : (quality > MaxQuality ? MaxQuality : quality)];
		automagic += hasAutomagic ? 1 : 0;
	}
	// "2:1500 5:490" (only the qualities seen)
	auto Qualities(char* out, size_t size) const noexcept -> void {
		size_t len = 0;
		if (size > 0) {
			out[0] = '\0';
		}
		for (int i = 0; i <= MaxQuality && len + 12 < size; ++i) {
			if (qualities[i] != 0) {
				len += static_cast<size_t>(std::snprintf(out + len, size - len, "%s%d:%d", len > 0 ? " " : "", i, qualities[i]));
			}
		}
	}
	// "0:12 1:14 2:15 ..." (only the sockets seen)
	auto Sockets(char* out, size_t size) const noexcept -> void {
		size_t len = 0;
		if (size > 0) {
			out[0] = '\0';
		}
		for (int i = 0; i <= MaxSockets && len + 12 < size; ++i) {
			if (sockets[i] != 0) {
				len += static_cast<size_t>(std::snprintf(out + len, size - len, "%s%d:%d", len > 0 ? " " : "", i, sockets[i]));
			}
		}
	}
};

// `avoid`: results of D2R's avoid check 0x44ef70 (0 none, 2 avoid, 4 dodge, 8 evade, 0x10 block).
struct AvoidTally {
	int n = 0, none = 0, avoid = 0, dodge = 0, evade = 0, block = 0, other = 0;
	void Add(int result) noexcept {
		++n;
		switch (result) {
			case 0: ++none; break;
			case 2: ++avoid; break;
			case 4: ++dodge; break;
			case 8: ++evade; break;
			case 0x10: ++block; break;
			default: ++other; break;
		}
	}
	auto Successes() const noexcept -> int { return n - none; }
};

// The life a target lost to a hit, in whole life points (the stat is in 1/256), negative when the hit healed it.
constexpr auto LifeLost(int lifeBefore, int lifeAfter) noexcept -> int {
	return (lifeBefore - lifeAfter) / 256;
}

}  // namespace hcmd

// Pure parsing for the test harness gameplay commands (harness_cmds.cpp; unit test plugin/tests/harness_cmds_test.cpp).
// A command line is "h <verb> <args...>": whitespace separated tokens, numbers decimal or 0x hex.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hcmd {

constexpr int MaxTokens = 16;
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

}  // namespace hcmd

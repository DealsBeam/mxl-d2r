// Median's strings at run time, the pure part of strings.cpp (unit-tested in tests/strings_test.cpp). Contract:
// docs/strings-namespace.md ("Plugin uses").
//
// Median-only strings live in D2RLoader's `medianxl` namespace and have no numeric ID. The plugin reads their text by key
// through the PluginSDK localization service. Where D2R itself wants a number (the u16 string ids of an NPC menu record),
// the plugin hands it an id of its own, in a range it owns at the top of the 16-bit space, and answers D2R's string lookup
// for that id with the text of the key (strings.cpp hooks the lookup).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mxlstr {

// Full keys (namespace:key) of the Median strings the plugin shows itself. The key part is Median's key, the name the
// string had in tbl2json (work/mod/strings_idkeys.json: Median id -> key).
inline constexpr const char* KeyRequiredLevel = "medianxl:mxl_11189";    // "(Required Level: %d)"
inline constexpr const char* KeyClosingIn = "medianxl:mxl_11191";        // "Closing in: %s"
inline constexpr const char* KeySealed = "medianxl:mxl_11192";           // "Join another game to enter again\nThis area is locked"
inline constexpr const char* KeyExitWarning = "medianxl:mxl_11193";      // "ONCE YOU EXIT YOU CANNOT GO BACK"
inline constexpr const char* KeyGoblinAppears = "medianxl:StrPushNotifGoblinAppear";  // " appears!"
inline constexpr const char* KeyUniqueLimit = "medianxl:12109";  // "c1You can only carry one Relic" (unique carry limit)

// A string a generated table hands over: D2R's own numeric id for a native key, or the full key of a namespaced one
// (id 0). `none` is a table cell without a string.
struct Ref {
	uint16_t id = 0;
	const char* key = nullptr;

	constexpr auto IsNone() const noexcept -> bool { return id == 0 && (key == nullptr || key[0] == '\0'); }
	constexpr auto IsKey() const noexcept -> bool { return id == 0 && key != nullptr && key[0] != '\0'; }
};
inline auto operator==(const Ref& a, const Ref& b) noexcept -> bool {
	if (a.id != b.id) {
		return false;
	}
	if (a.key == b.key) {
		return true;
	}
	if (a.key == nullptr || b.key == nullptr) {
		return (a.key == nullptr || a.key[0] == '\0') && (b.key == nullptr || b.key[0] == '\0');
	}
	return std::strcmp(a.key, b.key) == 0;
}

// Copies the NUL terminated `from` into `out` (cut to fit, always terminated).
inline void CopyText(char* out, size_t size, const char* from) noexcept {
	if (size == 0) {
		return;
	}
	size_t n = from != nullptr ? std::strlen(from) : 0;
	n = n < size - 1 ? n : size - 1;
	if (n != 0) {
		std::memcpy(out, from, n);
	}
	out[n] = '\0';
}

// A Median text with one printf placeholder (%d, %s, %u, with an optional width such as %02d) written out with `arg`,
// and "%%" as "%". A placeholder after the first and any other '%' stay as they are. The text comes from a string file, so
// it never goes to printf itself.
inline void Substitute(char* out, size_t size, const char* tmpl, const char* arg) noexcept {
	if (size == 0) {
		return;
	}
	size_t n = 0;
	bool used = false;
	auto put = [&](char c) {
		if (n + 1 < size) {
			out[n++] = c;
		}
	};
	for (const char* p = tmpl != nullptr ? tmpl : ""; *p != '\0'; ++p) {
		if (*p != '%') {
			put(*p);
			continue;
		}
		if (p[1] == '%') {
			put('%');
			++p;
			continue;
		}
		const char* q = p + 1;
		while (*q >= '0' && *q <= '9') {
			++q;
		}
		if (!used && (*q == 'd' || *q == 's' || *q == 'u')) {
			for (const char* a = arg != nullptr ? arg : ""; *a != '\0'; ++a) {
				put(*a);
			}
			used = true;
			p = q;
		} else {
			put('%');
		}
	}
	out[n] = '\0';
}

// The text of a Median help string without its leading number line. The help texts of the trophy items start with a line that
// holds only a number (the string table's sort key, "^\s*\d+\s*\n"); the number is no part of the challenge.
inline auto SkipLeadingNumberLine(const char* text) noexcept -> const char* {
	if (text == nullptr) {
		return "";
	}
	const char* p = text;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
		++p;
	}
	const char* digits = p;
	while (*p >= '0' && *p <= '9') {
		++p;
	}
	if (p == digits) {
		return text;
	}
	const char* q = p;
	while (*q == ' ' || *q == '\t' || *q == '\r') {
		++q;
	}
	return *q == '\n' ? q + 1 : text;
}

// Calls out(line, index) for each text line of `text`: spaces at both ends dropped, blank lines left out, each cut to `max`
// bytes (a chat record's room). Returns the number of lines.
template <class Out>
auto ForEachLine(const char* text, size_t max, Out out) noexcept -> int {
	int lines = 0;
	char line[256];
	max = max < sizeof(line) - 1 ? max : sizeof(line) - 1;
	while (text != nullptr && *text != '\0') {
		const char* end = text;
		while (*end != '\0' && *end != '\n') {
			++end;
		}
		const char* from = text;
		const char* to = end;
		while (from < to && *from == ' ') {
			++from;
		}
		while (to > from && (to[-1] == ' ' || to[-1] == '\r')) {
			--to;
		}
		if (to > from) {
			size_t n = static_cast<size_t>(to - from);
			n = n > max ? max : n;
			std::memcpy(line, from, n);
			line[n] = '\0';
			out(static_cast<const char*>(line), lines);
			++lines;
		}
		text = *end == '\n' ? end + 1 : end;
	}
	return lines;
}

// The plugin's own string ids: Capacity ids counting down from FirstId. Id 0 is "no string" and the ids near 0xffff are
// markers in D2R's code, so the range stops well below them; strings.cpp checks that D2R has no text for an id before it
// takes it (the `isFree` callback).
class IdRegistry {
public:
	static constexpr int Capacity = 64;
	static constexpr size_t KeyMax = 96;
	static constexpr uint16_t FirstId = 0xfe00;

	// The id of `key`, registering it when it is new. 0 when the registry is full or the key is too long.
	template <class IsFree>
	auto Add(const char* key, IsFree&& isFree) noexcept -> uint16_t {
		const int found = IndexOfKey(key);
		if (found >= 0) {
			return IdAt(found);
		}
		if (key == nullptr || std::strlen(key) >= KeyMax || count_ >= Capacity) {
			return 0;
		}
		for (uint32_t tries = 0; tries < 4 * Capacity; ++tries) {
			const uint32_t id = FirstId - tries;
			if (IndexOf(id) < 0 && isFree(static_cast<uint16_t>(id))) {
				std::memcpy(keys_[count_], key, std::strlen(key) + 1);
				ids_[count_] = static_cast<uint16_t>(id);
				return ids_[count_++];
			}
		}
		return 0;
	}
	// The key registered under `id`, null for any other id.
	auto KeyOf(uint32_t id) const noexcept -> const char* {
		const int i = IndexOf(id);
		return i >= 0 ? keys_[i] : nullptr;
	}
	auto Count() const noexcept -> int { return count_; }
	// The registration number of `id` (0..Count()-1), -1 for any other id.
	auto IndexOf(uint32_t id) const noexcept -> int {
		for (int i = 0; i < count_; ++i) {
			if (ids_[i] == id) {
				return i;
			}
		}
		return -1;
	}

private:
	auto IdAt(int i) const noexcept -> uint16_t { return ids_[i]; }
	auto IndexOfKey(const char* key) const noexcept -> int {
		for (int i = 0; key != nullptr && i < count_; ++i) {
			if (std::strcmp(keys_[i], key) == 0) {
				return i;
			}
		}
		return -1;
	}
	char keys_[Capacity][KeyMax] = {};
	uint16_t ids_[Capacity] = {};
	int count_ = 0;
};

}  // namespace mxlstr

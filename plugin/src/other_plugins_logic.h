// Pure part of the `other plugins` line of `mxl` (mxl_core.cpp; no Windows headers, tested in plugin/tests/other_plugins_test.cpp).
// D2RLoader loads the plugins of the running mod (<game>\mods\<mod>\d2rloader\plugins) and then the game-wide ones
// (<game>\d2rloader\plugins). A player crash report then needs to say which other plugins ran next to mxl-core (bug #42).
#pragma once

#include <cstddef>
#include <cstdio>

namespace other_plugins {

inline auto Lower(char c) noexcept -> char {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : (c == '/' ? '\\' : c);
}

// Position of "\d2rloader\plugins\" in path (any case, / or \), or -1.
inline auto PluginDirAt(const char* path) noexcept -> long long {
	static constexpr char Needle[] = "\\d2rloader\\plugins\\";
	if (path == nullptr) {
		return -1;
	}
	for (long long i = 0; path[i] != '\0'; ++i) {
		size_t k = 0;
		while (Needle[k] != '\0' && path[i + static_cast<long long>(k)] != '\0' && Lower(path[i + static_cast<long long>(k)]) == Needle[k]) {
			++k;
		}
		if (Needle[k] == '\0') {
			return i;
		}
	}
	return -1;
}

// "<file> (mod <name>)" or "<file> (global)" for a module loaded from a D2RLoader plugin folder; false for any other module.
inline auto Label(const char* path, char* out, size_t n) noexcept -> bool {
	const long long at = PluginDirAt(path);
	if (at < 0 || out == nullptr || n == 0) {
		return false;
	}
	const char* file = path + at + 19;  // past "\d2rloader\plugins\"
	for (const char* p = file; *p != '\0'; ++p) {
		if (*p == '\\' || *p == '/') {
			file = p + 1;
		}
	}
	// The folder holding d2rloader\: <game> (global) or <game>\mods\<mod>.
	long long end = at, start = at;
	while (start > 0 && path[start - 1] != '\\' && path[start - 1] != '/') {
		--start;
	}
	long long pstart = start - 1;
	while (pstart > 0 && path[pstart - 1] != '\\' && path[pstart - 1] != '/') {
		--pstart;
	}
	const bool inMods = start >= 1 && pstart >= 0 && start - 1 - pstart == 4 && Lower(path[pstart]) == 'm' && Lower(path[pstart + 1]) == 'o' &&
	                    Lower(path[pstart + 2]) == 'd' && Lower(path[pstart + 3]) == 's';
	if (inMods) {
		std::snprintf(out, n, "%s (mod %.*s)", file, static_cast<int>(end - start), path + start);
	} else {
		std::snprintf(out, n, "%s (global)", file);
	}
	return true;
}

}  // namespace other_plugins

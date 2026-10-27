// Standalone test of the plugin's environment check (env_guard_logic.h, docs/bugs.md #53; not part of the plugin DLL).
#include "../src/env_guard_logic.h"

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
	using env_guard::Check;
	const char* game = "C:\\Program Files (x86)\\Diablo II Resurrected";
	const char* good = "C:\\Program Files (x86)\\Diablo II Resurrected\\mods\\D2R-XL\\d2rloader\\plugins\\mxl-core.dll";
	const char* global = "C:\\Program Files (x86)\\Diablo II Resurrected\\d2rloader\\plugins\\mxl-core.dll";
	const char* old = "C:\\Program Files (x86)\\Diablo II Resurrected\\mods\\MedianXL\\d2rloader\\plugins\\mxl-core.dll";

	::Check(Check(good, game, "D2R-XL", 0).Ok(), "installed plugin, mod D2R-XL: ok");
	::Check(Check("c:/program files (x86)/diablo ii resurrected/MODS/d2r-xl/d2rloader/plugins/mxl-core.dll", "C:\\Program Files (x86)\\Diablo II Resurrected\\", "d2r-xl", 0).Ok(),
	        "any case, forward slashes, trailing separator");
	env_guard::Verdict v = Check(global, game, "D2R-XL", 0);
	::Check(v.outsideMod && !v.otherMod && !v.duplicate, "game-wide plugins folder is outside the mod");
	v = Check(old, game, "MedianXL", 0);
	::Check(v.outsideMod && v.otherMod, "an old mod folder's copy, run as that mod");
	v = Check(good, game, "Other", 0);
	::Check(!v.outsideMod && v.otherMod, "our file, but another active mod");
	v = Check(good, game, "D2R-XL", 1);
	::Check(v.duplicate && !v.Ok(), "a second instance");
	::Check(Check(good, "", nullptr, 0).Ok() && Check(nullptr, game, "", 0).Ok(), "unknown path or mod: no warning");
	::Check(!Check("C:\\Program Files (x86)\\Diablo II Resurrected\\mods\\D2R-XL2\\d2rloader\\plugins\\mxl-core.dll", game, "D2R-XL", 0).Ok(),
	        "a similar folder name is not ours");
	::Check(!Check("C:\\Program Files (x86)\\Diablo II Resurrected\\mods\\D2R-XL\\..\\X\\mxl-core.dll", game, "D2R-XL", 0).Ok(), "no .. tricks");

	::Check(env_guard::IsOurModule("mxl-core.dll", nullptr) && env_guard::IsOurModule("MXL-Core-old.DLL", nullptr), "our module by file name");
	::Check(env_guard::IsOurModule("renamed.dll", "mxl-core"), "our module by plugin id");
	::Check(!env_guard::IsOurModule("loot.dll", "loot") && !env_guard::IsOurModule("mxl-core.txt", nullptr) && !env_guard::IsOurModule(nullptr, nullptr),
	        "other modules");

	char buf[512];
	env_guard::Detail(buf, sizeof(buf), Check(good, game, "D2R-XL", 0), good, "D2R-XL", nullptr);
	::Check(buf[0] == '\0', "no detail when ok");
	env_guard::Detail(buf, sizeof(buf), Check(global, game, "MedianXL", 1), global, "MedianXL", old);
	::Check(std::strncmp(buf, "WARNING:", 8) == 0 && std::strstr(buf, global) != nullptr && std::strstr(buf, "runs the mod MedianXL") != nullptr &&
	            std::strstr(buf, old) != nullptr && std::strstr(buf, "D2R-XL Downloader") != nullptr,
	        "detail names the path, the mod and the other copy");
	env_guard::Notice(buf, sizeof(buf), Check(global, game, "D2R-XL", 0), "0.69.9", "D2R-XL");
	::Check(std::strcmp(buf, "D2R-XL 0.69.9: the D2R-XL plugin is loaded from outside mods\\D2R-XL. Run the D2R-XL Downloader to fix it.") == 0,
	        "notice, outside the mod");
	env_guard::Notice(buf, sizeof(buf), Check(good, game, "MedianXL", 0), "0.69.9", "MedianXL");
	::Check(std::strcmp(buf, "D2R-XL 0.69.9: D2RLoader runs another mod (mod MedianXL). Run the D2R-XL Downloader to fix it.") == 0, "notice, other mod");
	env_guard::Notice(buf, sizeof(buf), Check(good, game, "D2R-XL", 2), "0.69.9", "D2R-XL");
	::Check(std::strstr(buf, "two copies") != nullptr, "notice, two copies");
	char tiny[8];
	env_guard::Detail(tiny, sizeof(tiny), Check(global, game, "MedianXL", 1), global, "MedianXL", old);
	::Check(std::strlen(tiny) < sizeof(tiny), "a small buffer is not overrun");
	if (failures == 0) {
		std::printf("env guard: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}

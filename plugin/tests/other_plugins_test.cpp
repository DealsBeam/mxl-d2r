// Standalone test of the `other plugins` labels (other_plugins_logic.h; not part of the plugin DLL).
#include "../src/other_plugins_logic.h"

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
auto LabelOf(const char* path) -> const char* {
	static char buf[128];
	return other_plugins::Label(path, buf, sizeof(buf)) ? buf : "-";
}
}  // namespace

int main() {
	Check(std::strcmp(LabelOf("D:\\D2R\\mods\\MedianXL\\d2rloader\\plugins\\mxl-core.dll"), "mxl-core.dll (mod MedianXL)") == 0, "mod plugin");
	Check(std::strcmp(LabelOf("C:\\Program Files (x86)\\Diablo II Resurrected\\d2rloader\\plugins\\loot.dll"), "loot.dll (global)") == 0, "global plugin");
	Check(std::strcmp(LabelOf("D:/D2R/MODS/Other/D2RLoader/Plugins/x.dll"), "x.dll (mod Other)") == 0, "any case, forward slashes");
	Check(std::strcmp(LabelOf("D:\\D2R\\mods\\A\\d2rloader\\plugins\\sub\\y.dll"), "y.dll (mod A)") == 0, "a sub folder of plugins");
	Check(std::strcmp(LabelOf("C:\\Windows\\System32\\kernel32.dll"), "-") == 0, "a system module is no plugin");
	Check(std::strcmp(LabelOf("D:\\D2R\\D2RCore.dll"), "-") == 0, "D2RLoader's own core is no plugin");
	Check(std::strcmp(LabelOf("D:\\d2rloader\\plugins\\z.dll"), "z.dll (global)") == 0, "game folder at the drive root");
	Check(!other_plugins::Label(nullptr, nullptr, 0), "no path");
	if (failures == 0) {
		std::printf("other plugins: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}

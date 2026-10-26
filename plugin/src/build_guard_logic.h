// Which D2R the plugin runs on (mxl_core.cpp; unit test plugin/tests/build_guard_test.cpp). Every address, hook and patch of
// the plugin belongs to one image: D2R 3.3.0 build 93847, PE timestamp 0x6ab3782c (docs/d2r-addresses.md). On any other
// image the plugin installs nothing and says so: in the log, on the screen (D2RLoader's overlay) and in the `mxl` command.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace build_guard {

constexpr uint32_t ExpectedImage = 0x6ab3782cU;  // TimeDateStamp of the dumped D2R.exe
constexpr const char* ExpectedBuild = "93847";  // D2RLoader's buildName for that image

// Hooks may go in only on the dumped image. The build name D2RLoader reports (null or "" when it has none) must agree too.
constexpr auto Matches(uint32_t imageStamp, const char* buildName) noexcept -> bool {
	if (imageStamp != ExpectedImage) {
		return false;
	}
	if (buildName == nullptr || buildName[0] == '\0') {
		return true;
	}
	const char* want = ExpectedBuild;
	for (; *buildName != '\0' && *want != '\0'; ++buildName, ++want) {
		if (*buildName != *want) {
			return false;
		}
	}
	return *buildName == '\0' && *want == '\0';
}

// "Median XL D2R 0.69.0 needs D2R build 93847; this is 94100. The plugin installs nothing: ..." for a mismatch.
inline void Message(char* out, size_t outSize, const char* pluginVersion, uint32_t imageStamp, const char* buildName) noexcept {
	if (out == nullptr || outSize == 0) {
		return;
	}
	const bool named = buildName != nullptr && buildName[0] != '\0';
	char found[64];
	if (!named) {
		std::snprintf(found, sizeof(found), "an unknown build (image 0x%08x)", imageStamp);
	} else if (std::strcmp(buildName, ExpectedBuild) == 0) {  // same build number, other D2R.exe (patched or another region)
		std::snprintf(found, sizeof(found), "%s with another D2R.exe (image 0x%08x)", buildName, imageStamp);
	} else {
		std::snprintf(found, sizeof(found), "%.40s", buildName);
	}
	std::snprintf(out, outSize, "Median XL D2R %s needs D2R build %s; this is %s. The mod's engine changes are off.",
	              pluginVersion != nullptr ? pluginVersion : "?", ExpectedBuild, found);
}

}  // namespace build_guard

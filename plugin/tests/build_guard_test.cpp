// Standalone test of the game build decision (build_guard_logic.h; not part of the plugin DLL).
#include "../src/build_guard_logic.h"

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
	using namespace build_guard;
	static_assert(Matches(ExpectedImage, "93847"), "the dumped image matches at compile time");

	Check(Matches(0x6ab3782cU, "93847"), "build 93847, dumped image: hooks go in");
	Check(Matches(0x6ab3782cU, nullptr), "dumped image, no build name from D2RLoader: hooks go in");
	Check(Matches(0x6ab3782cU, ""), "dumped image, empty build name: hooks go in");
	Check(!Matches(0x6ab3782dU, "93847"), "build name 93847 but another D2R.exe: nothing installed");
	Check(!Matches(0x6b000000U, "94100"), "another build: nothing installed");
	Check(!Matches(0x6ab3782cU, "94100"), "dumped image but D2RLoader names another build: nothing installed");
	Check(!Matches(0x6ab3782cU, "938470"), "longer build name: nothing installed");
	Check(!Matches(0x6ab3782cU, "9384"), "shorter build name: nothing installed");
	Check(!Matches(0, nullptr), "no image (exe base 0): nothing installed");

	char text[256];
	Message(text, sizeof(text), "0.69.0", 0x6b000000U, "94100");
	Check(std::strncmp(text, "Median XL D2R 0.69.0 needs D2R build 93847; this is 94100.", 58) == 0, "message names both builds");
	Message(text, sizeof(text), "0.69.0", 0x6b000000U, nullptr);
	Check(std::strstr(text, "this is an unknown build (image 0x6b000000)") != nullptr, "message without a build name gives the image");
	Message(text, sizeof(text), "0.69.0", 0x6ab3782dU, "93847");
	Check(std::strstr(text, "this is 93847 with another D2R.exe (image 0x6ab3782d)") != nullptr, "same number, other exe");
	char small[16];
	Message(small, sizeof(small), "0.69.0", 0, "1");
	Check(std::strlen(small) == sizeof(small) - 1, "message is cut to the buffer");
	Message(nullptr, 0, "0.69.0", 0, "1");  // must not write

	if (failures == 0) {
		std::printf("build guard tests: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}

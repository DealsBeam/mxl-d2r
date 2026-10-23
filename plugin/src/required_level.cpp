// Required level without the +6 oskill penalty (audit common1-013).
//
// Median: "ITEMS_GetRequiredLevel no longer adds 6 for class-only (oskill) bonuses" (D2Sigma patches D2Common 0x26667, the
// `add ecx, 6`). D2R's executable only holds a `jmp [rip+x]` thunk (0x376de0) for that function; the code is in D2RCore.dll
// (the thunk slot 0x143e2a3c0 is filled in at load time and points to a one-instruction trampoline that jumps to the
// function at D2RCore+0x3d4170). In it, the loop over the item's stat 97 entries (oskill: layer = skill id) takes the skill's
// reqlevel (skills.txt, record +0x214) and adds 6 when the skill is of another class than the player's (or has none, class
// byte above 7) before it raises the item's level to it. The function has the loop twice: with a player unit (class
// comparison, `add ecx, 6` at +0x3d49ee) and without one (`add eax, 6` at +0x3d49e2). Both adds are replaced by a 3-byte nop,
// so the oskill term of the required level is the skill's own reqlevel, as in Median.
//
// The two sites are checked against the expected bytes (the neighbouring instructions included) and against D2RCore.dll's
// build time stamp before they are written; nothing is written otherwise.
#include "required_level.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t CoreStamp = 0x6ab37824;  // IMAGE_FILE_HEADER.TimeDateStamp of the D2RCore.dll of build 93847
// With a player unit: jmp +0xc2 (the loop's next step) / add ecx, 6 / cmp r14d, ecx / cmovle r14d, ecx
constexpr uint64_t SiteWithUnit = 0x3d49ec;
constexpr uint8_t WithUnitOriginal[] = {0xeb, 0xc2, 0x83, 0xc1, 0x06, 0x41, 0x39, 0xce, 0x44, 0x0f, 0x4e, 0xf1};
constexpr uint8_t WithUnitPatched[] = {0xeb, 0xc2, 0x0f, 0x1f, 0x00, 0x41, 0x39, 0xce, 0x44, 0x0f, 0x4e, 0xf1};
// Without one: movsx eax, word [rax+0x214] / add eax, 6 / cmp r14d, eax / cmovle r14d, eax
constexpr uint64_t SiteNoUnit = 0x3d49db;
constexpr uint8_t NoUnitOriginal[] = {0x0f, 0xbf, 0x80, 0x14, 0x02, 0x00, 0x00, 0x83, 0xc0, 0x06, 0x41, 0x39, 0xc6, 0x44, 0x0f, 0x4e, 0xf0};
constexpr uint8_t NoUnitPatched[] = {0x0f, 0xbf, 0x80, 0x14, 0x02, 0x00, 0x00, 0x0f, 0x1f, 0x00, 0x41, 0x39, 0xc6, 0x44, 0x0f, 0x4e, 0xf0};

uint8_t* g_core = nullptr;

auto CoreStampOk(const uint8_t* base) noexcept -> bool {
	__try {
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		return dos->e_magic == IMAGE_DOS_SIGNATURE && nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp == CoreStamp;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

auto PatchSite(uint8_t* base, uint64_t rva, const uint8_t* original, const uint8_t* patched, size_t n) noexcept -> bool {
	uint8_t* at = base + rva;
	if (std::memcmp(at, original, n) != 0) {
		return false;
	}
	DWORD old = 0;
	if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) {
		return false;
	}
	std::memcpy(at, patched, n);
	FlushInstructionCache(GetCurrentProcess(), at, n);
	DWORD ignored = 0;
	VirtualProtect(at, n, old, &ignored);
	return true;
}

}  // namespace

auto RequiredLevelPatchedSites() noexcept -> int {
	if (g_core == nullptr) {
		return -1;
	}
	int sites = 0;
	sites += std::memcmp(g_core + SiteWithUnit, WithUnitPatched, sizeof(WithUnitPatched)) == 0 ? 1 : 0;
	sites += std::memcmp(g_core + SiteNoUnit, NoUnitPatched, sizeof(NoUnitPatched)) == 0 ? 1 : 0;
	return sites;
}

auto InstallRequiredLevel(const D2RL::PluginContext* /*context*/, char* report, size_t reportSize) noexcept -> bool {
	auto* core = reinterpret_cast<uint8_t*>(GetModuleHandleW(L"D2RCore.dll"));
	if (core == nullptr) {
		std::snprintf(report, reportSize, "off: D2RCore.dll is not loaded");
		return false;
	}
	if (!CoreStampOk(core)) {
		std::snprintf(report, reportSize, "off: D2RCore.dll is not the build the offsets are for (time stamp differs)");
		return false;
	}
	const bool withUnit = PatchSite(core, SiteWithUnit, WithUnitOriginal, WithUnitPatched, sizeof(WithUnitPatched));
	const bool noUnit = PatchSite(core, SiteNoUnit, NoUnitOriginal, NoUnitPatched, sizeof(NoUnitPatched));
	g_core = core;
	std::snprintf(report, reportSize,
	              withUnit && noUnit ? "on: oskill bonuses add no 6 to the required level (D2RCore.dll +0x3d49ee, +0x3d49e2 nopped)"
	                                 : "partly off: with player %s, without %s (expected bytes differ)",
	              withUnit ? "patched" : "unchanged", noUnit ? "patched" : "unchanged");
	return withUnit && noUnit;
}

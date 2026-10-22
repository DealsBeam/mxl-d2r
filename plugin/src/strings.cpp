// Median's strings at run time: key lookups through D2RLoader's localization service, and the plugin's own string ids for
// the places where D2R wants a number (strings_logic.h has the contract and the id registry).
#include "strings.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

constexpr uint64_t StringThunkRva = 0x3e2b730;  // D2R.exe: jmp [rip+disp] into D2RCore's lookup (u16 id -> UTF-8 text)
constexpr uint8_t StringThunkEntry[] = {0xff, 0x25};
constexpr size_t TextMax = 512;

using LookupFn = const char* (*)(uint32_t id);

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::LocalizationService* g_loc = nullptr;
LookupFn g_original = nullptr;
uint64_t* g_slot = nullptr;
bool g_hooked = false;

std::mutex g_lock;
mxlstr::IdRegistry g_ids;
char g_texts[mxlstr::IdRegistry::Capacity][TextMax];

// D2R's own text for `id` (null or empty when it has none). The lookup is D2R code: a bad id must not take the game down.
auto NativeText(uint32_t id) noexcept -> bool {
	if (g_original == nullptr) {
		return false;
	}
	const char* text = nullptr;
	__try {
		text = g_original(id);
		return text != nullptr && text[0] != '\0';
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

auto ServiceLookup(const char* key, char* out, size_t size) noexcept -> bool {
	if (g_loc == nullptr || g_ctx == nullptr || key == nullptr || size == 0) {
		return false;
	}
	uint32_t required = 0;
	out[0] = '\0';
	return g_loc->getStringByKey(g_ctx, key, out, static_cast<uint32_t>(size), &required) == D2RL::Localization::Result::Success;
}

// The replacement of D2R's lookup: the plugin's ids give the text of their key, everything else goes to D2R.
const char* LookupHook(uint32_t id) {
	if (id >= mxlstr::IdRegistry::FirstId - 4 * mxlstr::IdRegistry::Capacity && id <= mxlstr::IdRegistry::FirstId) {
		std::lock_guard<std::mutex> guard(g_lock);
		const int index = g_ids.IndexOf(id);
		if (index >= 0) {
			const char* key = g_ids.KeyOf(id);
			char* text = g_texts[index];
			if (!ServiceLookup(key, text, TextMax)) {
				mxlstr::CopyText(text, TextMax, key);  // the key shows what is missing
			}
			return text;
		}
	}
	return g_original(id);
}

// Replaces the import slot the thunk at StringThunkRva jumps through. False when the thunk is not what the plugin knows.
auto HookLookup(uint64_t base) noexcept -> bool {
	const auto* thunk = reinterpret_cast<const uint8_t*>(base + StringThunkRva);
	if (std::memcmp(thunk, StringThunkEntry, sizeof(StringThunkEntry)) != 0) {
		return false;
	}
	int32_t disp = 0;
	std::memcpy(&disp, thunk + 2, sizeof(disp));
	auto* slot = reinterpret_cast<uint64_t*>(base + StringThunkRva + 6 + disp);
	if (*slot == 0) {
		return false;
	}
	DWORD old = 0;
	if (!VirtualProtect(slot, sizeof(uint64_t), PAGE_READWRITE, &old)) {
		return false;
	}
	g_original = reinterpret_cast<LookupFn>(*slot);
	*slot = reinterpret_cast<uint64_t>(&LookupHook);
	g_slot = slot;
	VirtualProtect(slot, sizeof(uint64_t), old, &old);
	return true;
}

}  // namespace

auto InstallStrings(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_ctx = context;
	const bool service = context->QueryService(&g_loc) == D2RL::ServiceQueryResult::Success &&
	                     D2RL::HasLocalizationServiceField(g_loc, D2RL::LocalizationServiceRequiredSize);
	if (!service) {
		g_loc = nullptr;
	}
	g_hooked = HookLookup(context->exeBase);
	std::snprintf(report, reportSize, "%s; string ids %s", service ? "localization service on" : "off: no localization service",
	              g_hooked ? "on (lookup 0x3e2b730 answers the plugin's ids)" : "off (lookup thunk differs)");
	return service;
}

auto StringLookup(const char* fullKey, char* out, size_t outSize) noexcept -> bool {
	return ServiceLookup(fullKey, out, outSize);
}

auto StringText(const char* fullKey, const char* fallback, char* out, size_t outSize) noexcept -> const char* {
	if (!ServiceLookup(fullKey, out, outSize)) {
		mxlstr::CopyText(out, outSize, fallback);
	}
	return out;
}

auto StringId(const mxlstr::Ref& ref) noexcept -> uint16_t {
	if (ref.id != 0 || !ref.IsKey()) {
		return ref.id;
	}
	if (!g_hooked || g_loc == nullptr) {
		return 0;
	}
	std::lock_guard<std::mutex> guard(g_lock);
	return g_ids.Add(ref.key, [](uint16_t id) { return !NativeText(id); });
}

auto StringIdCount() noexcept -> int {
	std::lock_guard<std::mutex> guard(g_lock);
	return g_ids.Count();
}

auto StringKeyOfId(uint32_t id) noexcept -> const char* {
	std::lock_guard<std::mutex> guard(g_lock);
	return g_ids.KeyOf(id);
}

#include "strings_probe.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace {

const D2RL::LocalizationService* g_loc = nullptr;
const D2RL::PluginContext* g_ctx = nullptr;
uint64_t g_base = 0;
constexpr uint64_t StringRva = 0x3e2b730;  // client: D2RCore thunk (u16 string id) -> text

}  // namespace

auto StringsProbeInit(const D2RL::PluginContext* context, uint64_t imageBase) noexcept -> bool {
	g_ctx = context;
	g_base = imageBase;
	if (context == nullptr || context->QueryService(&g_loc) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLocalizationServiceField(g_loc, D2RL::LocalizationServiceRequiredSize)) {
		g_loc = nullptr;
		return false;
	}
	return true;
}

auto StringsProbeKey(const char* key, char* out, size_t outSize) noexcept -> int {
	if (g_loc == nullptr) {
		return -1;
	}
	uint32_t required = 0;
	out[0] = '\0';
	return static_cast<int>(g_loc->getStringByKey(g_ctx, key, out, static_cast<uint32_t>(outSize), &required));
}

auto StringsProbeId(uint32_t id, char* out, size_t outSize) noexcept -> int {
	if (g_loc == nullptr) {
		return -1;
	}
	uint32_t required = 0;
	out[0] = '\0';
	return static_cast<int>(g_loc->getStringById(g_ctx, id, out, static_cast<uint32_t>(outSize), &required));
}

auto StringsProbeNative(uint32_t id, char* out, size_t outSize) noexcept -> bool {
	out[0] = '\0';
	if (g_base == 0) {
		return false;
	}
	using StringFn = const char* (*)(uint32_t);
	const char* text = nullptr;
	__try {
		text = reinterpret_cast<StringFn>(g_base + StringRva)(id);
		if (text != nullptr) {
			std::snprintf(out, outSize, "%s", text);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		out[0] = '\0';
		return false;
	}
	return text != nullptr;
}

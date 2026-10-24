// Crash guard for D2R's unit translation layer (docs/bugs.md #34).
//
// Twice in seven full `group1.txt` runs the client died with an access violation at D2R.exe+0x7761DC while `morphdeath`
// switched to the Druid character (last-resort reports of 2026-10-24: RBX 0x8000003F0 and 0x46B000003FD). The function is the
// handler of event type 0x10 of the client's `UnitTranslationLayer` event loop (0x77a3b0 -> 0x775fc0(layer, event)): it reads
// RAX = *event, RBX = [RAX + 0x70] and then the byte [RBX + 0x870]. RBX held two 32-bit halves (a count and a capacity of 1000
// or so), not a pointer: the event's payload had been freed and reused by a small-vector buffer, a stale event of D2R's own
// queue. No plugin code is in the frame and none of our writes reaches it, so the cause stays D2R's; the hook only keeps the
// client alive: an event whose payload cannot be read (or whose +0x70 pointer cannot be read at its first bytes and at +0x870, the byte the
// handler reads) is skipped and logged once with the payload words, so the next report carries the data.
#include "translation_guard.h"

#include "d2r.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t HandlerRva = 0x775fc0;
// mov r11,rsp / push rbp / push rbx / push rsi / push rdi / lea rbp,[r11-0xe08] / sub rsp,0xee8
constexpr uint8_t HandlerEntry[] = {0x4c, 0x8b, 0xdc, 0x55, 0x53, 0x56, 0x57, 0x49, 0x8d, 0xab, 0xf8, 0xf1, 0xff, 0xff, 0x48, 0x81};
constexpr size_t PayloadField = 0x70;
constexpr size_t InnerByte = 0x870;  // the byte the handler reads first: [inner + 0x870]

using HandlerFn = void (*)(void* layer, void** event);

HandlerFn g_original = nullptr;
const D2RL::PluginContext* g_context = nullptr;
std::atomic<unsigned> g_seen{0}, g_skipped{0}, g_logged{0};

auto Readable(const void* p, size_t n) noexcept -> bool {
	MEMORY_BASIC_INFORMATION mbi{};
	if (p == nullptr || VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
		return false;
	}
	const uint8_t* end = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
	return static_cast<const uint8_t*>(p) + n <= end;
}

// True when the event can be handed to D2R's handler.
auto Sound(void** event, uint64_t* payloadWord, uint64_t* innerWord) noexcept -> bool {
	if (!Readable(event, sizeof(void*))) {
		return false;
	}
	const uint8_t* payload = static_cast<const uint8_t*>(*event);
	*payloadWord = reinterpret_cast<uint64_t>(payload);
	if (!Readable(payload + PayloadField, sizeof(void*))) {
		return false;
	}
	const uint8_t* inner = *reinterpret_cast<const uint8_t* const*>(payload + PayloadField);
	*innerWord = reinterpret_cast<uint64_t>(inner);
	return inner == nullptr || (Readable(inner, sizeof(void*)) && Readable(inner + InnerByte, 1));
}

void TranslationEvent(void* layer, void** event) noexcept {
	g_seen.fetch_add(1);
	uint64_t payload = 0, inner = 0;
	if (!Sound(event, &payload, &inner)) {
		g_skipped.fetch_add(1);
		if (g_context != nullptr && g_logged.fetch_add(1) < 8) {
			char line[200];
			std::snprintf(line, sizeof(line), "mxl-core: translation event skipped: event %p, payload %016llx, payload+0x70 %016llx (not readable memory)",
			              static_cast<void*>(event), static_cast<unsigned long long>(payload), static_cast<unsigned long long>(inner));
			g_context->LogWarn(line);
		}
		return;
	}
	g_original(layer, event);
}

}  // namespace

void TranslationGuardStats(unsigned* seen, unsigned* skipped) noexcept {
	*seen = g_seen.load();
	*skipped = g_skipped.load();
}

auto TranslationGuardProbe() noexcept -> bool {
	alignas(16) uint8_t payload[0x100] = {};
	*reinterpret_cast<uint64_t*>(payload + PayloadField) = 0x80000003F0ULL;  // the RBX of the first crash report
	void* event = payload;
	const unsigned before = g_skipped.load();
	TranslationEvent(nullptr, &event);
	return g_skipped.load() == before + 1;
}

auto InstallTranslationGuard(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	const bool ok = context->InstallInlineHook(HandlerRva, HandlerEntry, sizeof(HandlerEntry), reinterpret_cast<void*>(&TranslationEvent),
	                                           reinterpret_cast<void**>(&g_original));
	std::snprintf(report, reportSize, ok ? "on: the unit translation event handler 0x775fc0 skips an event with an unreadable payload (D2R.exe+0x7761DC crash, docs/bugs.md #34)"
	                                     : "off: entry bytes of 0x775fc0 differ");
	return ok;
}

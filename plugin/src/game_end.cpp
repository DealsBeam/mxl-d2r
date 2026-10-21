// Per-game state reset on the real end of a game (docs/unported.md "Per-game reset").
//
// The game destroy function 0x4057c0 starts with rip-relative instructions, so it cannot be detoured. It calls 0x486be0
// (Game*) first thing that matters here: the loop that frees every client record in the list at game + 0x138. That
// function has a clean prologue, no other caller, and its argument is the Game*, so the plugin detours it and tells its
// modules which game is gone, then lets the original free the clients. The idle sweep 0x406e40 is the only caller of
// 0x4057c0 (docs/d2r-addresses.md section 6), so this runs once per ended game.
#include "game_end.h"

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t TeardownRva = 0x486be0;
// 0x486be0: push rsi / sub rsp,0x20 / mov rsi,[rcx+0x138] (13 bytes, whole instructions; dump of build 93847,
// timestamp 0x6ab3782c)
constexpr uint8_t TeardownEntry[] = {0x40, 0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xb1, 0x38, 0x01, 0x00, 0x00};

using TeardownFn = void (*)(void* game);

TeardownFn g_original = nullptr;
GameEndFn g_callbacks[8] = {};
volatile long g_count = 0;

void TeardownHook(void* game) {
	if (game != nullptr) {
		++g_count;
		for (GameEndFn fn : g_callbacks) {
			if (fn != nullptr) {
				fn(game);
			}
		}
	}
	g_original(game);
}

}  // namespace

void RegisterGameEnd(GameEndFn fn) noexcept {
	for (GameEndFn& slot : g_callbacks) {
		if (slot == fn) {
			return;
		}
		if (slot == nullptr) {
			slot = fn;
			return;
		}
	}
}

auto GameEndCount() noexcept -> int {
	return static_cast<int>(g_count);
}

auto InstallGameEnd(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool ok = context->InstallInlineHook(TeardownRva, TeardownEntry, sizeof(TeardownEntry),
	                                           reinterpret_cast<void*>(&TeardownHook), reinterpret_cast<void**>(&g_original));
	std::snprintf(report, reportSize, ok ? "on: client teardown 0x486be0" : "off: hook failed");
	return ok;
}

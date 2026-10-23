// Median's buff / debuff icon bar on D2R's HD HUD (buff_icons.cpp, pure part buff_icons_logic.h, data mxl_state_icons.h).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Registers the overlay callback that draws the bar (client side). True when it is registered; the report line says what is on.
auto InstallBuffIcons(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Game (server) thread, once per game frame: the remaining time of the local player's states. Single player and the host
// of a game only (the end frame of a state lives in the server unit's state list); elsewhere the icons show without a timer.
void BuffIconsFrame(void* game) noexcept;

// For the test harness (UI thread): one line of counters, and the icons of the last frame ("state side slot row seconds",
// seconds -1 = no timer). Returns the number of icons written to `placed` (at most `capacity`).
struct BuffIconShown {
	int state;
	int side;
	int slot;
	int row;
	int seconds;
	bool hovered;
	float x, y, size;  // the icon's rectangle in overlay pixels
};
auto BuffIconsStatus(char* out, size_t outSize, BuffIconShown* placed, int capacity) noexcept -> int;

// Test harness: cursor override in screen pixels (the bar is hovered as if the cursor were there); off = the real cursor.
void BuffIconsHover(float x, float y, bool on) noexcept;
// Test harness: a magenta test rectangle with a line of text, to see whether the overlay is on the screen at all.
void BuffIconsProbe(bool on) noexcept;
// The probe's result codes: byte 0 first failing code of the rectangle, byte 1 failed requests, bytes 2-3 the frame flags.
auto BuffIconsProbeCodes() noexcept -> uint32_t;
// The centre of the last frame's icon of `state`, false when it is not shown.
auto BuffIconsCentre(int state, float* x, float* y) noexcept -> bool;
// Test harness: sets / clears a state bit on the client's copy of the local player (what the bar reads). False when there is no
// local player.
auto BuffIconsClientState(int state, bool on) noexcept -> bool;

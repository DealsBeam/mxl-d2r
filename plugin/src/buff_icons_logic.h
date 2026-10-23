// Median's buff / debuff icon bar: the pure part of buff_icons.cpp (unit-tested in tests/buff_icons_test.cpp).
//
// D2Sigma (ui/BottomPanel.cpp, 0x100d28e0 left group, 0x100d34b0 right group, 0x100d1230 one icon) walks its `stateicons`
// table in row order, once per group, and draws an icon for every row whose side matches the group and whose state the
// player has; the n-th such icon sits in column n % 10 and row n / 10 of the group, 28 x 28 pixels on a 32 pixel pitch. In
// Median's 1024 x 768 screen the left group starts at x = 177 and grows right, the right group's first icon starts at
// x = 817 and grows left, the first row's bottom edge is y = 698 and later rows go up. The D2R port draws the same bar
// through the OverlayService (rectangles and text only), laid out against D2R's HD HUD (hudpanelhd.json).
#pragma once

#include <cstddef>
#include <cstdint>

namespace bufficons {

// ---- D2Sigma's bar (classic 1024 x 768 coordinates) -----------------------------------------------------------
inline constexpr int IconPixels = 28;
inline constexpr int PitchPixels = 32;
inline constexpr int PerRow = 10;            // BottomPanel field +0x1c (constructor 0x100cfd20)
inline constexpr int LeftGroupX = 177;
inline constexpr int RightGroupX = 817;
inline constexpr int FirstRowBottomY = 698;
inline constexpr int Left = 0;
inline constexpr int Right = 1;

struct Rect {
	float x, y, w, h;
};

// ---- the HD layout ------------------------------------------------------------------------------------------
// D2R's HUD (hudpanelhd.json) is laid out in units that scale with the screen height: the health orb frame ends 680 units left of
// the screen centre, the mana orb frame starts 680 units right of it, the bar between them tops out 176 units above the bottom. One unit
// is HudUnitDivisor-th of the screen height. The icons sit just above the bar, beside the orbs (Median: right next to its
// orbs, on top of its panel).
inline constexpr float HudUnitDivisor = 1800.0F;
inline constexpr float HudInnerEdge = 680.0F;   // units from the centre to the inner edge of an orb frame
inline constexpr float HudBarTop = 176.0F;      // units from the bottom of the screen to the first row's bottom edge (just above the belt)
inline constexpr float IconScaleDivisor = 800.0F;  // an icon is 28 * height / 800 pixels (35 at 1080p)

struct Screen {
	float width;
	float height;
};

inline auto IconSize(const Screen& s) noexcept -> float {
	return static_cast<float>(IconPixels) * s.height / IconScaleDivisor;
}

// The rectangle of slot `index` (0, 1, 2, ... in drawing order) of a group, in screen pixels.
inline auto SlotRect(const Screen& s, int side, int index) noexcept -> Rect {
	const float size = IconSize(s);
	const float pitch = size * static_cast<float>(PitchPixels) / static_cast<float>(IconPixels);
	const float unit = s.height / HudUnitDivisor;
	const int column = index % PerRow;
	const int row = index / PerRow;
	const float y = s.height - HudBarTop * unit - size - static_cast<float>(row) * pitch;
	const float centre = s.width / 2.0F;
	const float x = side == Left ? centre - HudInnerEdge * unit + static_cast<float>(column) * pitch
	                             : centre + HudInnerEdge * unit - size - static_cast<float>(column) * pitch;
	return {x, y, size, size};
}

// ---- which icons, in which slot -----------------------------------------------------------------------------
struct Placed {
	int row;    // index into the table
	int side;   // Left or Right
	int slot;   // position within the group
};

// Walks `rows` (anything with .state and .side) in order, once per group exactly as D2Sigma does, and writes an entry for
// every row whose `active(rowIndex)` is true. Returns the number written (at most `capacity`).
template <typename Row, typename ActiveFn>
inline auto Place(const Row* rows, int rowCount, ActiveFn&& active, Placed* out, int capacity) noexcept -> int {
	int written = 0;
	for (int side = Left; side <= Right; ++side) {
		int slot = 0;
		for (int i = 0; i < rowCount; ++i) {
			if (static_cast<int>(rows[i].side) != side || rows[i].state < 0 || !active(i)) {
				continue;
			}
			if (written < capacity) {
				out[written] = {i, side, slot};
				++written;
			}
			++slot;
		}
	}
	return written;
}

// ---- the timer text -----------------------------------------------------------------------------------------
inline constexpr int FramesPerSecond = 25;
inline constexpr int MaxTimerSeconds = 100 * 3600;  // longer (or no end at all) shows no timer

// Seconds left from the state's end frame (the float of its stat list, `endFrame`) and the game frame now; -1 = no timer
// (no list, no end, the end already passed, or longer than MaxTimerSeconds).
inline auto SecondsLeft(float endFrame, uint32_t nowFrame) noexcept -> int {
	if (!(endFrame > static_cast<float>(nowFrame))) {
		return -1;
	}
	const double seconds = (static_cast<double>(endFrame) - static_cast<double>(nowFrame)) / FramesPerSecond;
	if (seconds > MaxTimerSeconds) {
		return -1;
	}
	return static_cast<int>(seconds + 0.999);  // a started second counts: 0.2 s left shows 1s, the way a countdown reads
}

// "45s" under a minute, "5:07" under an hour, "1:02:03" above; "" for no timer. Returns the characters written.
inline auto FormatRemaining(int seconds, char* out, size_t size) noexcept -> size_t {
	if (size == 0) {
		return 0;
	}
	out[0] = '\0';
	if (seconds < 0) {
		return 0;
	}
	char tmp[24];
	int n;
	if (seconds < 60) {
		n = 0;
		tmp[n++] = static_cast<char>('0' + seconds / 10 % 10);
		if (seconds < 10) {
			n = 0;
		}
		tmp[n++] = static_cast<char>('0' + seconds % 10);
		tmp[n++] = 's';
	} else if (seconds < 3600) {
		n = 0;
		const int m = seconds / 60;
		if (m >= 10) {
			tmp[n++] = static_cast<char>('0' + m / 10);
		}
		tmp[n++] = static_cast<char>('0' + m % 10);
		tmp[n++] = ':';
		tmp[n++] = static_cast<char>('0' + seconds % 60 / 10);
		tmp[n++] = static_cast<char>('0' + seconds % 10);
	} else {
		n = 0;
		const int h = seconds / 3600;
		if (h >= 100) {
			return 0;
		}
		if (h >= 10) {
			tmp[n++] = static_cast<char>('0' + h / 10);
		}
		tmp[n++] = static_cast<char>('0' + h % 10);
		tmp[n++] = ':';
		tmp[n++] = static_cast<char>('0' + seconds % 3600 / 60 / 10);
		tmp[n++] = static_cast<char>('0' + seconds % 3600 / 60 % 10);
		tmp[n++] = ':';
		tmp[n++] = static_cast<char>('0' + seconds % 60 / 10);
		tmp[n++] = static_cast<char>('0' + seconds % 10);
	}
	size_t w = 0;
	for (int i = 0; i < n && w + 1 < size; ++i) {
		out[w++] = tmp[i];
	}
	out[w] = '\0';
	return w;
}

// ---- hover ----------------------------------------------------------------------------------------------------
inline auto Contains(const Rect& r, float x, float y) noexcept -> bool {
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// The tooltip box of size w x h for a cursor at (cx, cy): up and to the left of the cursor's lower right when it fits (the bar is at
// the bottom of the screen), clamped inside the screen.
inline auto TooltipRect(const Screen& s, float cx, float cy, float w, float h) noexcept -> Rect {
	float x = cx + 14.0F;
	float y = cy - h - 8.0F;
	if (x + w > s.width) {
		x = cx - w - 14.0F;
	}
	if (x < 0.0F) {
		x = 0.0F;
	}
	if (y < 0.0F) {
		y = cy + 20.0F;
	}
	if (y + h > s.height) {
		y = s.height - h;
	}
	return {x, y, w, h};
}

// ---- the tint -------------------------------------------------------------------------------------------------
// D2Sigma hands the row's tint byte to the icon component (component +0x1c). The art is grey, the byte runs 0-12 and its
// values line up with Diablo II's text colours (curses 1 = red, buffs 4 = gold, 12 = green, ...), so it is applied as that
// colour: the icon's brightness times the colour. 0 leaves the pixel as drawn. (Approximation: the engine call that
// consumes the byte was not decoded.)
struct Rgb {
	uint8_t r, g, b;
};
inline constexpr int TintCount = 13;
inline constexpr Rgb TintColours[TintCount] = {
    {255, 255, 255},  // 0 white (no tint)
    {255, 77, 77},    // 1 red
    {0, 255, 0},      // 2 green
    {105, 105, 255},  // 3 blue
    {199, 179, 119},  // 4 gold
    {160, 160, 160},  // 5 grey
    {120, 120, 120},  // 6 black (kept visible)
    {213, 196, 148},  // 7 tan
    {255, 168, 0},    // 8 orange
    {255, 255, 100},  // 9 yellow
    {0, 160, 0},      // 10 dark green
    {174, 0, 255},    // 11 purple
    {40, 210, 40},    // 12 green
};

inline auto ApplyTint(int tint, Rgb in) noexcept -> Rgb {
	if (tint <= 0 || tint >= TintCount) {
		return in;
	}
	const float luma = (0.299F * in.r + 0.587F * in.g + 0.114F * in.b) / 255.0F;
	const float gain = luma * 1.45F;
	auto channel = [&](uint8_t c) {
		const float v = static_cast<float>(c) * gain;
		return static_cast<uint8_t>(v > 255.0F ? 255.0F : v);
	};
	const Rgb c = TintColours[tint];
	return {channel(c.r), channel(c.g), channel(c.b)};
}

// ---- pixels to rectangles -------------------------------------------------------------------------------------
// A 28 x 28 icon as the fewest rectangles the greedy walk finds: from each opaque pixel not yet covered, the widest run of
// the same colour to the right, then as many rows down as are the same run. `argb` holds one value per pixel (0 =
// transparent, else the colour with a non-zero top byte). Returns the number of rectangles written (at most `capacity`).
struct PixelRect {
	uint8_t x, y, w, h;
	uint32_t argb;
};
inline auto BuildRects(const uint32_t* argb, PixelRect* out, int capacity) noexcept -> int {
	constexpr int N = IconPixels;
	bool covered[N * N] = {};
	int count = 0;
	for (int y = 0; y < N; ++y) {
		for (int x = 0; x < N; ++x) {
			const uint32_t c = argb[y * N + x];
			if (c == 0 || covered[y * N + x]) {
				continue;
			}
			int w = 1;
			while (x + w < N && argb[y * N + x + w] == c && !covered[y * N + x + w]) {
				++w;
			}
			int h = 1;
			for (; y + h < N; ++h) {
				bool same = true;
				for (int i = 0; i < w && same; ++i) {
					same = argb[(y + h) * N + x + i] == c && !covered[(y + h) * N + x + i];
				}
				if (!same) {
					break;
				}
			}
			for (int j = 0; j < h; ++j) {
				for (int i = 0; i < w; ++i) {
					covered[(y + j) * N + x + i] = true;
				}
			}
			if (count < capacity) {
				out[count] = {static_cast<uint8_t>(x), static_cast<uint8_t>(y), static_cast<uint8_t>(w), static_cast<uint8_t>(h), c};
			}
			++count;
		}
	}
	return count < capacity ? count : capacity;
}

}  // namespace bufficons

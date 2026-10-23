// Median's buff / debuff icon bar (D2Sigma ui/BottomPanel.cpp: 0x100d28e0 left group, 0x100d34b0 right group, 0x100d1230 one
// icon, 0x100befc0 "does the player have the state"), drawn on D2R's HD HUD with D2RLoader's OverlayService.
//
// What D2Sigma does, and what is kept: its `stateicons` table (tools/state_icons.py -> mxl_state_icons.h) is walked in row
// order once per group; every row of the group whose state the player has gets the next slot (10 per row, rows grow upward,
// the left group grows right from the left orb, the right group grows left from the right orb). Each icon is a 28 x 28
// frame of state-icons.dc6, tinted by the row's tint byte. A hover shows the row's tooltip string. D2Sigma shows no timer;
// the remaining time under each icon is the port's addition (the format is buff_icons_logic.h FormatRemaining).
//
// How it is drawn: D2R has no buff bar of its own, and the SDK's OverlayService draws lines, rectangles and text only (no
// sprites; WidgetService / PanelService cannot place or switch a frame at run time). So the frame art (Median's own, decoded
// at build time into the committed mxl_state_icon_art.h) is turned into filled rectangles once at install (same-colour
// runs merged), upscaled nearest-neighbour to the screen's icon size every frame.
//
// States are read from the client's copy of the local player (the same bits D2Sigma reads). The remaining time comes from the
// server unit's state list (the end frame at list +0x24, game frame at game+0x170, 25 per second), sampled on the game frame
// hook (BuffIconsFrame); a client that is not the host has no server unit, its icons show without a timer.
#include "buff_icons.h"

#include "buff_icons_logic.h"
#include "d2r.h"
#include "mxl_state_icon_art.h"
#include "mxl_state_icons.h"
#include "strings.h"
#include "strings_logic.h"

#define NOMINMAX
#include <windows.h>

#include <D2RLPlugin/overlay.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

namespace si = mxl_stateicons;
using d2r::Game;
using d2r::Unit;

constexpr uint64_t LocalPlayerSlotRva = 0x2a23704;  // client: u32, the local player's slot
constexpr uint64_t UnitBySlotRva = 0x9a480;         // client: (slot < 8) -> that player's unit or null
constexpr uint64_t HasStateRva = 0x3351b0;          // (unit, state) -> non-zero when set
constexpr uint64_t SetStateRva = 0x3355a0;          // (unit, state, on)
constexpr uint64_t StateListRva = 0x2f5940;         // (server unit, state) -> its stat list for that state or null
constexpr uint64_t FindUnitRva = 0x48fe80;          // (game, type, id) -> server unit
constexpr int UnitId = 0x08;
constexpr int GameFrame = 0x170;
constexpr int ListEndFrame = 0x24;  // float in a state's stat list: the frame it ends on
constexpr uint32_t SnapshotEvery = 5;

using UnitBySlotFn = Unit* (*)(uint32_t slot);
using HasStateFn = uint64_t (*)(Unit* unit, int32_t state);
using SetStateFn = void (*)(Unit* unit, int32_t state, int32_t on);
using StateListFn = void* (*)(Unit* unit, int32_t state);
using FindUnitFn = Unit* (*)(Game* game, int32_t type, uint32_t id);

const D2RL::PluginContext* g_context = nullptr;
const D2RL::OverlayService* g_overlay = nullptr;
UnitBySlotFn g_unitBySlot = nullptr;
HasStateFn g_hasState = nullptr;
SetStateFn g_setState = nullptr;
StateListFn g_stateList = nullptr;
FindUnitFn g_findUnit = nullptr;
bool g_on = false;

// One entry per table row: the row's icon as rectangles (tint applied).
struct RowArt {
	std::vector<bufficons::PixelRect> rects;
};
std::vector<RowArt> g_art;

// Timer snapshot (game thread writes, overlay callback reads).
std::atomic<float> g_end[si::RowCount];
std::atomic<uint32_t> g_nowFrame{0};
std::atomic<uint64_t> g_nowTick{0};
std::atomic<uint32_t> g_lastSnap{0};
std::atomic<uint32_t> g_snapshots{0};

// Cursor override of the test harness.
std::atomic<bool> g_hoverOn{false};
std::atomic<float> g_hoverX{0.0F};
std::atomic<float> g_hoverY{0.0F};
std::atomic<uint32_t> g_probeCodes{0};  // result codes of the probe's rectangle, outline, text (one byte each) and the frame's flags
std::atomic<bool> g_probe{false};  // harness: a test rectangle and line of text, to see whether the overlay reaches the screen

// What the last overlay frame drew (overlay thread writes, harness reads).
struct Last {
	int count = 0;
	BuffIconShown shown[si::RowCount];
	float centreX[si::RowCount];
	float centreY[si::RowCount];
	float screenW = 0.0F;
	float screenH = 0.0F;
	int left = 0;
	int right = 0;
	int tooltipState = -1;
	int tooltipLines = 0;
	uint64_t frames = 0;       // overlay frames that found a local player
	uint64_t iconsDrawn = 0;   // icons drawn, all frames
	uint64_t rects = 0;        // filled rectangles requested, all frames
	uint64_t failed = 0;       // draw requests the service refused
	uint32_t firstFail = 0;    // the first refusal's result code
	uint64_t tooltips = 0;     // frames that drew a tooltip
	int lastRects = 0;         // rectangles of the last frame
};
std::mutex g_mutex;
Last g_last;

auto Colour(uint32_t argb) noexcept -> D2RL::Overlay::Color {
	return {static_cast<float>((argb >> 16) & 0xff) / 255.0F, static_cast<float>((argb >> 8) & 0xff) / 255.0F,
	        static_cast<float>(argb & 0xff) / 255.0F, 1.0F};
}

// The client's copy of the local player; null when there is none (no game, between games).
auto ClientPlayer() noexcept -> Unit* {
	__try {
		const uint32_t slot = *reinterpret_cast<const uint32_t*>(g_context->exeBase + LocalPlayerSlotRva);
		return slot < 8 ? g_unitBySlot(slot) : nullptr;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

// Which table rows' states the client unit has. False when the unit cannot be read.
auto ReadActive(Unit* unit, bool* active) noexcept -> bool {
	__try {
		for (int i = 0; i < si::RowCount; ++i) {
			active[i] = g_hasState(unit, si::Rows[i].state) != 0;
		}
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// The end frame of every row's state on the server copy of the local player (0 = none / no list). False when there is no
// server unit.
auto SampleEnds(Game* game, float* ends) noexcept -> bool {
	__try {
		Unit* client = ClientPlayer();
		if (client == nullptr) {
			return false;
		}
		Unit* unit = g_findUnit(game, 0, d2r::At<uint32_t>(client, UnitId));
		if (unit == nullptr) {
			return false;
		}
		for (int i = 0; i < si::RowCount; ++i) {
			void* list = g_stateList(unit, si::Rows[i].state);
			ends[i] = list != nullptr ? d2r::At<float>(list, ListEndFrame) : 0.0F;
		}
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// The cursor in overlay pixels: the harness override, else the real cursor when it is over this process's window.
auto CursorPoint(const D2RL::Overlay::Frame* frame, float* x, float* y) noexcept -> bool {
	if (g_hoverOn.load()) {
		*x = g_hoverX.load();
		*y = g_hoverY.load();
		return true;
	}
	POINT p;
	if (!GetCursorPos(&p)) {
		return false;
	}
	HWND under = WindowFromPoint(p);
	HWND root = under != nullptr ? GetAncestor(under, GA_ROOT) : nullptr;
	DWORD pid = 0;
	if (root == nullptr || GetWindowThreadProcessId(root, &pid) == 0 || pid != GetCurrentProcessId()) {
		return false;
	}
	RECT client;
	if (!ScreenToClient(root, &p) || !GetClientRect(root, &client) || client.right <= 0 || client.bottom <= 0) {
		return false;
	}
	*x = static_cast<float>(p.x) * frame->screenWidth / static_cast<float>(client.right);
	*y = static_cast<float>(p.y) * frame->screenHeight / static_cast<float>(client.bottom);
	return true;
}

struct Pen {
	const D2RL::PluginContext* context;
	const D2RL::Overlay::Frame* frame;
	uint64_t rects = 0;
	uint64_t failed = 0;
	uint32_t firstFail = 0;

	void Note(D2RL::Overlay::Result r) noexcept {
		if (r != D2RL::Overlay::Result::Success) {
			++failed;
			if (firstFail == 0) {
				firstFail = static_cast<uint32_t>(r);
			}
		}
	}
	void Fill(float l, float t, float r, float b, D2RL::Overlay::Color c) noexcept {
		D2RL::Overlay::FilledRectangleRequest req{};
		req.structSize = D2RL::Overlay::FilledRectangleRequestSize;
		req.canvas = frame->canvas;
		req.rect = {l, t, r, b};
		req.color = c;
		++rects;
		Note(g_overlay->drawFilledRectangle(context, &req));
	}
	void Outline(float l, float t, float r, float b, D2RL::Overlay::Color c) noexcept {
		D2RL::Overlay::RectangleRequest req{};
		req.structSize = D2RL::Overlay::RectangleRequestSize;
		req.canvas = frame->canvas;
		req.rect = {l, t, r, b};
		req.color = c;
		req.thickness = 1.0F;
		Note(g_overlay->drawRectangle(context, &req));
	}
	auto Width(const char* text, float size) noexcept -> float {
		D2RL::Overlay::MeasureTextRequest measure{};
		measure.structSize = D2RL::Overlay::MeasureTextRequestSize;
		measure.canvas = frame->canvas;
		measure.textSize = size;
		measure.text = text;
		measure.textLength = static_cast<uint32_t>(std::strlen(text));
		D2RL::Overlay::TextMetrics metrics{};
		metrics.structSize = D2RL::Overlay::TextMetricsSize;
		return g_overlay->measureText(context, &measure, &metrics) == D2RL::Overlay::Result::Success ? metrics.width : 0.0F;
	}
	void Text(const char* text, float x, float y, float size, D2RL::Overlay::Color c, bool shadow) noexcept {
		for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
			D2RL::Overlay::TextRequest req{};
			req.structSize = D2RL::Overlay::TextRequestSize;
			req.canvas = frame->canvas;
			const float s = pass == 0 ? 1.5F : 0.0F;
			req.position = {x + s, y + s};
			req.color = pass == 0 ? D2RL::Overlay::Color{0.0F, 0.0F, 0.0F, 0.9F} : c;
			req.textSize = size;
			req.text = text;
			req.textLength = static_cast<uint32_t>(std::strlen(text));
			Note(g_overlay->drawText(context, &req));
		}
	}
};

// The game frame now, estimated from the last snapshot (frames run 25 per second; at most 30 frames ahead of the snapshot
// so a paused game does not run its timers on).
auto FrameNow() noexcept -> double {
	const uint64_t tick = GetTickCount64();
	const uint64_t then = g_nowTick.load();
	double ahead = then != 0 && tick > then ? static_cast<double>(tick - then) * bufficons::FramesPerSecond / 1000.0 : 0.0;
	ahead = ahead > 30.0 ? 30.0 : ahead;
	return static_cast<double>(g_nowFrame.load()) + ahead;
}

void __cdecl DrawBar(const D2RL::PluginContext* context, const D2RL::Overlay::Frame* frame, void*) noexcept {
	if (frame == nullptr || !g_on) {
		return;
	}
	if (g_probe.load()) {
		Pen probe{context, frame};
		probe.Fill(frame->screenWidth * 0.3F, frame->screenHeight * 0.3F, frame->screenWidth * 0.7F, frame->screenHeight * 0.45F,
		           {0.9F, 0.1F, 0.9F, 1.0F});
		const uint32_t fillCode = probe.firstFail;
		probe.Outline(frame->screenWidth * 0.3F, frame->screenHeight * 0.3F, frame->screenWidth * 0.7F, frame->screenHeight * 0.45F,
		              {1.0F, 1.0F, 0.0F, 1.0F});
		probe.Text("BUFF ICONS OVERLAY PROBE", frame->screenWidth * 0.32F, frame->screenHeight * 0.33F, frame->defaultTextSize * 1.5F,
		           {1.0F, 1.0F, 1.0F, 1.0F}, true);
		g_probeCodes.store(fillCode | (static_cast<uint32_t>(probe.failed) << 8) | (frame->flags << 16));
	}
	Unit* unit = ClientPlayer();
	bool active[si::RowCount];
	if (unit == nullptr || !ReadActive(unit, active)) {
		std::lock_guard<std::mutex> lock(g_mutex);
		g_last.count = 0;
		g_last.left = g_last.right = 0;
		g_last.tooltipState = -1;
		return;
	}
	static bufficons::Placed placed[si::RowCount];
	const int count = bufficons::Place(si::Rows, si::RowCount, [&](int row) { return active[row]; }, placed, si::RowCount);

	const bufficons::Screen screen{frame->screenWidth, frame->screenHeight};
	const float size = bufficons::IconSize(screen);
	const float scale = size / static_cast<float>(bufficons::IconPixels);
	const double now = FrameNow();
	float cx = 0.0F, cy = 0.0F;
	const bool haveCursor = CursorPoint(frame, &cx, &cy);

	Pen pen{context, frame};
	static BuffIconShown shown[si::RowCount];
	static float centreX[si::RowCount];
	static float centreY[si::RowCount];
	int left = 0, right = 0, hovered = -1;
	for (int n = 0; n < count; ++n) {
		const bufficons::Placed& p = placed[n];
		const bufficons::Rect r = bufficons::SlotRect(screen, p.side, p.slot);
		for (const bufficons::PixelRect& px : g_art[p.row].rects) {
			// Edges snapped to whole pixels so the scaled runs meet without seams.
			const float l = std::floor(r.x + static_cast<float>(px.x) * scale + 0.5F);
			const float t = std::floor(r.y + static_cast<float>(px.y) * scale + 0.5F);
			const float rr = std::floor(r.x + static_cast<float>(px.x + px.w) * scale + 0.5F);
			const float bb = std::floor(r.y + static_cast<float>(px.y + px.h) * scale + 0.5F);
			pen.Fill(l, t, rr, bb, Colour(px.argb));
		}
		p.side == bufficons::Left ? ++left : ++right;
		const int seconds = bufficons::SecondsLeft(g_end[p.row].load(), static_cast<uint32_t>(now));
		const bool over = haveCursor && bufficons::Contains(r, cx, cy);
		if (over) {
			hovered = n;
		}
		shown[n] = {si::Rows[p.row].state, p.side, p.slot, p.row, seconds, over, r.x, r.y, r.w};
		centreX[n] = r.x + r.w / 2.0F;
		centreY[n] = r.y + r.h / 2.0F;
		if (seconds >= 0) {
			char text[24];
			bufficons::FormatRemaining(seconds, text, sizeof(text));
			const float textSize = size * 0.42F;
			const float w = pen.Width(text, textSize);
			pen.Text(text, r.x + (r.w - w) / 2.0F, r.y + r.h - textSize * 1.15F, textSize, {1.0F, 0.95F, 0.6F, 1.0F}, true);
		}
	}

	// The hovered icon's tooltip (the row's string; rows without one have none).
	int tooltipLines = 0;
	int tooltipState = -1;
	if (hovered >= 0) {
		const bufficons::Placed& p = placed[hovered];
		const char* key = si::Rows[p.row].tooltip;
		char text[512] = "";
		if (key[0] != '\0') {
			StringText(key, "", text, sizeof(text));
		}
		char lines[6][160];
		int nLines = 0;
		if (text[0] != '\0') {
			mxlstr::ForEachLine(text, 150, [&](const char* line, int i) {
				if (i < 6) {
					std::snprintf(lines[i], sizeof(lines[i]), "%s", line);
					nLines = i + 1;
				}
			});
		}
		if (nLines > 0) {
			const float textSize = frame->defaultTextSize;
			float w = 0.0F;
			for (int i = 0; i < nLines; ++i) {
				const float lw = pen.Width(lines[i], textSize);
				w = lw > w ? lw : w;
			}
			const float lineH = textSize * 1.25F;
			const float h = static_cast<float>(nLines) * lineH + 10.0F;
			const bufficons::Rect box = bufficons::TooltipRect(screen, cx, cy, w + 16.0F, h);
			pen.Fill(box.x, box.y, box.x + box.w, box.y + box.h, {0.04F, 0.04F, 0.06F, 0.92F});
			pen.Outline(box.x, box.y, box.x + box.w, box.y + box.h, {0.6F, 0.55F, 0.4F, 1.0F});
			for (int i = 0; i < nLines; ++i) {
				pen.Text(lines[i], box.x + 8.0F, box.y + 5.0F + static_cast<float>(i) * lineH, textSize,
				         i == 0 ? D2RL::Overlay::Color{1.0F, 0.9F, 0.55F, 1.0F} : D2RL::Overlay::Color{0.9F, 0.9F, 0.9F, 1.0F}, true);
			}
			tooltipLines = nLines;
			tooltipState = si::Rows[p.row].state;
		}
	}

	std::lock_guard<std::mutex> lock(g_mutex);
	g_last.count = count;
	std::memcpy(g_last.shown, shown, sizeof(BuffIconShown) * static_cast<size_t>(count));
	std::memcpy(g_last.centreX, centreX, sizeof(float) * static_cast<size_t>(count));
	std::memcpy(g_last.centreY, centreY, sizeof(float) * static_cast<size_t>(count));
	g_last.screenW = frame->screenWidth;
	g_last.screenH = frame->screenHeight;
	g_last.left = left;
	g_last.right = right;
	g_last.tooltipState = tooltipState;
	g_last.tooltipLines = tooltipLines;
	++g_last.frames;
	g_last.iconsDrawn += static_cast<uint64_t>(count);
	g_last.rects += pen.rects;
	g_last.lastRects = static_cast<int>(pen.rects);
	g_last.failed += pen.failed;
	g_last.firstFail = g_last.firstFail != 0 ? g_last.firstFail : pen.firstFail;
	if (tooltipLines > 0) {
		++g_last.tooltips;
	}
}

// Every row's frame as rectangles, the row's tint applied.
void BuildArt() {
	g_art.assign(si::RowCount, RowArt{});
	for (int i = 0; i < si::RowCount; ++i) {
		uint32_t argb[si::FrameSize * si::FrameSize];
		const uint8_t* idx = si::Pixels[si::Rows[i].frame];
		for (int k = 0; k < si::FrameSize * si::FrameSize; ++k) {
			if (idx[k] == 0) {
				argb[k] = 0;
				continue;
			}
			const bufficons::Rgb c = bufficons::ApplyTint(si::Rows[i].tint, {si::Palette[idx[k]][0], si::Palette[idx[k]][1], si::Palette[idx[k]][2]});
			argb[k] = 0xff000000u | (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | c.b;
		}
		bufficons::PixelRect rects[si::FrameSize * si::FrameSize];
		const int n = bufficons::BuildRects(argb, rects, si::FrameSize * si::FrameSize);
		g_art[i].rects.assign(rects, rects + n);
	}
}

}  // namespace

auto InstallBuffIcons(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	static_assert(si::FrameSize == bufficons::IconPixels);
	if (context->QueryService(&g_overlay) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasOverlayServiceField(g_overlay, D2RL::OverlayServiceRequiredSize)) {
		g_overlay = nullptr;
		std::snprintf(report, reportSize, "off: no overlay service");
		return false;
	}
	const uint64_t base = context->exeBase;
	g_unitBySlot = reinterpret_cast<UnitBySlotFn>(base + UnitBySlotRva);
	g_hasState = reinterpret_cast<HasStateFn>(base + HasStateRva);
	g_setState = reinterpret_cast<SetStateFn>(base + SetStateRva);
	g_stateList = reinterpret_cast<StateListFn>(base + StateListRva);
	g_findUnit = reinterpret_cast<FindUnitFn>(base + FindUnitRva);
	BuildArt();
	size_t totalRects = 0;
	for (const RowArt& a : g_art) {
		totalRects += a.rects.size();
	}
	D2RL::Overlay::CallbackRegistration reg{};
	reg.structSize = D2RL::Overlay::CallbackRegistrationSize;
	reg.phase = D2RL::Overlay::Phase::AfterGameUi;
	reg.callback = &DrawBar;
	D2RL::Overlay::CallbackHandle handle = D2RL::Overlay::InvalidCallbackHandle;
	g_on = g_overlay->registerFrameCallback(context, &reg, &handle) == D2RL::Overlay::Result::Success;
	std::snprintf(report, reportSize, "%s: %d state icons, %d frames as %u rectangles, overlay rectangles at the HUD, timers from the server unit",
	              g_on ? "on" : "off (callback refused)", static_cast<int>(si::RowCount), static_cast<int>(si::FrameCount),
	              static_cast<unsigned>(totalRects));
	return g_on;
}

void BuffIconsFrame(void* gameArg) noexcept {
	if (!g_on) {
		return;
	}
	auto* game = static_cast<Game*>(gameArg);
	const uint32_t frame = d2r::At<uint32_t>(game, GameFrame);
	const uint32_t last = g_lastSnap.load();
	if (frame >= last && frame - last < SnapshotEvery) {
		return;
	}
	float ends[si::RowCount];
	if (!SampleEnds(game, ends)) {
		return;
	}
	g_lastSnap.store(frame);
	for (int i = 0; i < si::RowCount; ++i) {
		g_end[i].store(ends[i]);
	}
	g_nowFrame.store(frame);
	g_nowTick.store(GetTickCount64());
	g_snapshots.fetch_add(1);
}

auto BuffIconsStatus(char* out, size_t outSize, BuffIconShown* placed, int capacity) noexcept -> int {
	std::lock_guard<std::mutex> lock(g_mutex);
	std::snprintf(out, outSize,
	              "on=%d frames=%llu icons=%d left=%d right=%d screen=%.0fx%.0f rectsLast=%d rects=%llu failed=%llu firstFail=%u "
	              "tooltip=%d lines=%d tooltips=%llu snapshots=%u rows=%d",
	              g_on ? 1 : 0, static_cast<unsigned long long>(g_last.frames), g_last.count, g_last.left, g_last.right,
	              static_cast<double>(g_last.screenW), static_cast<double>(g_last.screenH), g_last.lastRects,
	              static_cast<unsigned long long>(g_last.rects), static_cast<unsigned long long>(g_last.failed), g_last.firstFail,
	              g_last.tooltipState, g_last.tooltipLines, static_cast<unsigned long long>(g_last.tooltips), g_snapshots.load(),
	              static_cast<int>(si::RowCount));
	const int n = g_last.count < capacity ? g_last.count : capacity;
	for (int i = 0; i < n; ++i) {
		placed[i] = g_last.shown[i];
	}
	return n;
}

void BuffIconsProbe(bool on) noexcept {
	g_probe.store(on);
}

auto BuffIconsProbeCodes() noexcept -> uint32_t {
	return g_probeCodes.load();
}

void BuffIconsHover(float x, float y, bool on) noexcept {
	g_hoverX.store(x);
	g_hoverY.store(y);
	g_hoverOn.store(on);
}

auto BuffIconsCentre(int state, float* x, float* y) noexcept -> bool {
	std::lock_guard<std::mutex> lock(g_mutex);
	for (int i = 0; i < g_last.count; ++i) {
		if (g_last.shown[i].state == state) {
			*x = g_last.centreX[i];
			*y = g_last.centreY[i];
			return true;
		}
	}
	return false;
}

auto BuffIconsClientState(int state, bool on) noexcept -> bool {
	if (!g_on) {
		return false;
	}
	Unit* unit = ClientPlayer();
	if (unit == nullptr) {
		return false;
	}
	__try {
		g_setState(unit, state, on ? 1 : 0);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

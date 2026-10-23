// Tests of the buff icon bar's pure part (plugin/src/buff_icons_logic.h) and its generated table (mxl_state_icons.h).
#include "../src/buff_icons_logic.h"
#include "../src/mxl_state_icons.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: buff icons: %s\n", what);
	}
}

struct TestRow {
	int16_t state;
	uint8_t side;
};

auto Fmt(int seconds) -> const char* {
	static char out[24];
	bufficons::FormatRemaining(seconds, out, sizeof(out));
	return out;
}

}  // namespace

int RunBuffIconsTests() {
	using namespace bufficons;

	// ---- the table
	namespace si = mxl_stateicons;
	Check(si::RowCount > 100 && si::FrameCount > 50, "the generated table is not empty");
	int left = 0, right = 0;
	for (const si::Row& r : si::Rows) {
		Check(r.side <= 1, "a row's side is a group");
		Check(r.state > 0, "a row has a migrated state id");
		Check(r.tint < TintCount, "a tint is a text colour index");
		Check(r.frame < si::FrameCount, "a frame is in the art");
		Check(r.tooltip != nullptr && (r.tooltip[0] == '\0' || std::strchr(r.tooltip, ':') != nullptr), "a tooltip is a full key or none");
		(r.side == 0 ? left : right)++;
	}
	Check(left > 0 && right > 0, "both groups have icons");

	// ---- layout (1080p: the icon is 28 * 1080 / 800 = 37.8 px)
	const Screen hd{1920.0F, 1080.0F};
	Check(IconSize(hd) > 37.7F && IconSize(hd) < 37.9F, "icon size scales with the screen height");
	const Rect l0 = SlotRect(hd, Left, 0);
	const Rect l1 = SlotRect(hd, Left, 1);
	const Rect l10 = SlotRect(hd, Left, 10);
	const Rect r0 = SlotRect(hd, Right, 0);
	const Rect r1 = SlotRect(hd, Right, 1);
	Check(l1.x > l0.x && l1.y == l0.y, "the left group grows right");
	Check(r1.x < r0.x && r1.y == r0.y, "the right group grows left");
	Check(l10.x == l0.x && l10.y < l0.y, "the 11th icon starts a new row above the first");
	Check(l0.x < 960.0F && r0.x > 960.0F, "the groups sit left and right of the centre");
	const float pitch = l1.x - l0.x;
	Check(pitch > l0.w && pitch < l0.w * 1.2F, "icons are spaced a little wider than they are (28 on a pitch of 32)");
	Check(l0.y + l0.h < 1080.0F && l0.y > 700.0F, "the first row sits above the bottom bar");
	Check(Contains(l0, l0.x + 1.0F, l0.y + 1.0F) && !Contains(l0, l0.x + l0.w, l0.y + 1.0F) && !Contains(l0, l0.x - 1.0F, l0.y), "Contains is half-open");

	// ---- which icons, in which slot: table order within a group, left group first
	const TestRow rows[] = {{10, Left}, {20, Right}, {30, Left}, {40, Right}, {50, Left}, {-1, Left}};
	bool on[6] = {true, true, false, true, true, true};
	Placed placed[8];
	int n = Place(rows, 6, [&](int i) { return on[i]; }, placed, 8);
	Check(n == 4, "only active rows with a state are placed");
	Check(placed[0].row == 0 && placed[0].side == Left && placed[0].slot == 0, "first left icon, slot 0");
	Check(placed[1].row == 4 && placed[1].side == Left && placed[1].slot == 1, "second left icon takes slot 1 (the inactive row leaves no gap)");
	Check(placed[2].row == 1 && placed[2].side == Right && placed[2].slot == 0, "right group restarts at slot 0");
	Check(placed[3].row == 3 && placed[3].side == Right && placed[3].slot == 1, "right group in table order");
	n = Place(rows, 6, [&](int i) { return on[i]; }, placed, 2);
	Check(n == 2, "capacity limits the output");

	// ---- timer
	Check(SecondsLeft(0.0F, 100) == -1, "no end frame: no timer");
	Check(SecondsLeft(100.0F, 100) == -1 && SecondsLeft(90.0F, 100) == -1, "an ended state shows no timer");
	Check(SecondsLeft(100.0F + 25.0F * 45, 100) == 45, "45 s left");
	Check(SecondsLeft(100.0F + 5.0F, 100) == 1, "a started second counts");
	Check(SecondsLeft(100.0F + 25.0F * 3600.0F * 200.0F, 100) == -1, "an endless state (huge end frame) shows no timer");
	Check(std::strcmp(Fmt(45), "45s") == 0 && std::strcmp(Fmt(7), "7s") == 0 && std::strcmp(Fmt(0), "0s") == 0, "under a minute: seconds");
	Check(std::strcmp(Fmt(60), "1:00") == 0 && std::strcmp(Fmt(307), "5:07") == 0 && std::strcmp(Fmt(3599), "59:59") == 0, "under an hour: m:ss");
	Check(std::strcmp(Fmt(600), "10:00") == 0, "two-digit minutes");
	Check(std::strcmp(Fmt(3723), "1:02:03") == 0 && std::strcmp(Fmt(36000), "10:00:00") == 0, "an hour or more: h:mm:ss");
	Check(Fmt(-1)[0] == '\0', "no timer: empty text");
	char tiny[3];
	Check(FormatRemaining(307, tiny, sizeof(tiny)) == 2 && tiny[2] == '\0', "the text is cut to the buffer");

	// ---- hover tooltip box stays on the screen
	const Rect a = TooltipRect(hd, 500.0F, 900.0F, 200.0F, 60.0F);
	Check(a.y + a.h <= 900.0F && a.x >= 500.0F, "above and right of the cursor when it fits");
	const Rect b = TooltipRect(hd, 1900.0F, 900.0F, 200.0F, 60.0F);
	Check(b.x + b.w <= 1920.0F, "flips left at the right edge");
	const Rect c = TooltipRect(hd, 500.0F, 10.0F, 200.0F, 60.0F);
	Check(c.y >= 0.0F, "below the cursor at the top edge");

	// ---- tint
	const Rgb grey{128, 128, 128};
	const Rgb same = ApplyTint(0, grey);
	Check(same.r == 128 && same.g == 128 && same.b == 128, "tint 0 leaves the pixel");
	const Rgb red = ApplyTint(1, grey);
	Check(red.r > red.g && red.r > red.b, "tint 1 is red");
	const Rgb green = ApplyTint(2, grey);
	Check(green.g > green.r && green.g > green.b, "tint 2 is green");
	const Rgb out = ApplyTint(200, grey);
	Check(out.r == 128, "an unknown tint leaves the pixel");

	// ---- pixels to rectangles
	uint32_t px[IconPixels * IconPixels] = {};
	for (int y = 2; y < 6; ++y) {
		for (int x = 3; x < 9; ++x) {
			px[y * IconPixels + x] = 0xff112233;
		}
	}
	px[10 * IconPixels + 10] = 0xff445566;
	PixelRect rects[16];
	n = BuildRects(px, rects, 16);
	Check(n == 2, "a solid block and a dot are two rectangles");
	Check(rects[0].x == 3 && rects[0].y == 2 && rects[0].w == 6 && rects[0].h == 4 && rects[0].argb == 0xff112233, "the block merges into one rectangle");
	Check(rects[1].x == 10 && rects[1].y == 10 && rects[1].w == 1 && rects[1].h == 1, "the dot");
	px[3 * IconPixels + 5] = 0xff778899;  // a hole in the block's colour
	n = BuildRects(px, rects, 16);
	long covered = 0;
	for (int i = 0; i < n; ++i) {
		covered += static_cast<long>(rects[i].w) * rects[i].h;
	}
	Check(covered == 4 * 6 + 1, "the rectangles cover exactly the opaque pixels (the odd pixel splits the block)");
	Check(BuildRects(px, rects, 1) == 1, "capacity limits the rectangle count");
	return failures;
}

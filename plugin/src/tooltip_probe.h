// Item tooltip text readback for the test harness (tooltip_probe.cpp; docs/item-tooltips.md). Only installed while the harness
// is on (mxl-testharness.txt); never in normal play.
//
// D2R builds an item's property lines in D2Client\src\UI\mods.cpp: 0x2dc4b0 builds the whole text of an item (one line per
// property, "\n" between them) and calls 0x2d6520 for each property: (viewer unit, itemstatcost record, value, layer / skill,
// flag, char out[256], mode) -> line. The probe hooks both, keeps the lines the game formatted and the arguments of the last
// build, and can build the text again for any item of the client's player with those arguments (`h tip`).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace tip {

struct Line {
	uint32_t seq;      // 1-based count of formatter calls since the probe was installed
	int32_t stat;      // itemstatcost row of the record
	int32_t value;
	uint32_t layer;    // the fourth argument: the skill / class / layer of the property
	int32_t mode;      // 4 = item tooltip and Advanced Stats panel, 2 / 3 = Chronicle texts
	int32_t flag;      // 1 = the stat's group line (the dgrp columns: "+N to all Attributes")
	int32_t viewerType;  // unit type of the first argument (0 player, 4 item), -1 when unreadable
	uint32_t viewerId;
	int32_t ret;       // the formatter's return value (al)
	char text[232];
};

struct Build {
	uint32_t seq;           // 1-based count of builds the game itself asked for
	uint64_t arg[10];       // item, out, size, p4 .. p10 as the game passed them
	uint8_t optional[16];   // the contents of p10 (an optional<u64>: value, then has-value) at the time of the call
	char text[1024];        // what the game's build wrote
	bool valid;
};

}  // namespace tip

auto InstallTooltipProbe(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// The formatter calls recorded so far: how many (the last sequence number) and one by sequence number (false when it has
// been overwritten: the ring keeps the last 512).
auto TipLineCount() noexcept -> uint32_t;
auto TipLineAt(uint32_t seq, tip::Line* out) noexcept -> bool;
// Forgets the recorded lines (the build record stays).
void TipReset() noexcept;
// The last build the game asked for itself (a hover of an item).
auto TipLastBuild(tip::Build* out) noexcept -> bool;
// Builds the text of `item` (a unit of the client) with the arguments of the last real build. False when there is none yet
// or the probe is not installed. The formatter calls of the build are recorded as usual.
auto TipRender(void* item, char* out, int size) noexcept -> bool;
// One formatter call for a viewer unit (the client's player), as the property loop makes it. Fills `out` (char[256]), returns
// the formatter's return value, or -1 when not installed.
auto TipFormat(void* viewer, int stat, int value, uint32_t layer, int mode, int flag, char* out) noexcept -> int;

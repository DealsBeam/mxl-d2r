// Median's item property lines in D2R's item tooltip (item_text.cpp; rules in item_text_logic.h, docs/item-tooltips.md).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Hooks D2R's property line formatter (0x2d6520) for the descfuncs 31-38 that D2R writes no text for (or a wrong one) and for
// the class lock stat 492. `lines` false hooks the function but changes nothing (the test harness readback needs the hook).
// True when hooked.
auto InstallItemText(const D2RL::PluginContext* context, bool lines, char* report, size_t reportSize) noexcept -> bool;

// The formatter exactly as the item text builder calls it (with the plugin's lines); false return = no text. Used by the
// harness to format one property. False in the first member when the hook is not installed.
auto ItemTextFormat(void* viewer, const int16_t* record, int32_t value, uint32_t layer, char flag, char* out, int32_t mode) noexcept -> char;
auto ItemTextHooked() noexcept -> bool;

// The harness readback: called after every formatter call with what the game asked for and what it got.
using FormatObserver = void (*)(void* viewer, const int16_t* record, int32_t value, uint32_t layer, char flag, int32_t mode, char ret,
                                const char* text);
void SetFormatObserver(FormatObserver observer) noexcept;

// How many lines the plugin wrote since the game started, for the `mxl` command.
auto ItemTextLinesWritten() noexcept -> uint32_t;

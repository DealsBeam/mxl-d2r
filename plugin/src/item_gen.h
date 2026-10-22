// Median's item generation and item rules on D2R's functions: class lock, ethereal roll and effect, socket roll, dye colour
// (item_gen.cpp; docs/subsystems/items-xp.md section 5).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks, each switchable in mxl-core-off.txt: "classlock" (the item requirement check 0x36bc50 denies class-locked items
// to other classes), "ethereal" (Median's ethereal rate and effect in the item generator 0x442d60 / 0x3d3510), "sockets"
// (Median's socket roll, 0x441fc0), "dyes" (the item colour function 0x3710d0 paints dyed items), "autoid" (magic, set, rare and
// unique items are generated identified, audit IT3; shares the generator hook 0x442d60), "allquals" (sockets for
// qualities 4-9 and the automagic roll for sets and uniques: patches of 0x442d60, see item_gen.cpp). True when every hook
// asked for installed.
auto InstallItemGen(const D2RL::PluginContext* context, bool classLock, bool ethereal, bool sockets, bool dyes, bool identify, bool allQuals, char* report,
                    size_t reportSize) noexcept -> bool;

// Counters for the "mxl" console line.
void ItemGenSummary(char* text, size_t size) noexcept;

// The counts of items that pass each part of D2R's ethereal gate (harness verb itemroll).
void ItemGenGateText(char* text, size_t size) noexcept;
void ItemGenSocketText(char* text, size_t size) noexcept;

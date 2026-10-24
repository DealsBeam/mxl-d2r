// Guard for a crash inside D2R's own UnitTranslationLayer event handler (translation_guard.cpp; docs/bugs.md #34).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks the handler of event type 0x10 of the client's unit translation layer (0x775fc0) and skips an event whose payload pointer
// is not readable memory. Off word `transguard`.
auto InstallTranslationGuard(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Events seen by the hook and events skipped so far (the `mxl` line and the harness).
void TranslationGuardStats(unsigned* seen, unsigned* skipped) noexcept;

// Calls the hook with an event whose payload word is `0x8000003F0` (the value of the crash reports) and says whether it was skipped.
auto TranslationGuardProbe() noexcept -> bool;

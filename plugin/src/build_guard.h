// Wrong game build (build_guard.cpp, decision in build_guard_logic.h).
#pragma once

#include <D2RLPlugin/api.h>

// Draws `text` at the top of the screen for the whole session; false when D2RLoader has no overlay service.
auto ShowWrongBuildNotice(const D2RL::PluginContext* context, const char* text) noexcept -> bool;
// The same line for any other notice (the environment warning, docs/bugs.md #53); a later call replaces the text.
auto ShowNotice(const D2RL::PluginContext* context, const char* text) noexcept -> bool;

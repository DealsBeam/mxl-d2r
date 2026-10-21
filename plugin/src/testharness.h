// Test harness (testharness.cpp): input for unattended test runs, so the real mouse, keyboard and focus stay free.
// Off unless mxl-testharness.txt exists next to the plugin DLL (or env MXL_TESTHARNESS is set). Never active in normal play.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallTestHarness(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;
void StopTestHarness() noexcept;
void HarnessEcho(const char* line) noexcept;  // appends a console line to mxl-testharness-console.txt while the harness is on

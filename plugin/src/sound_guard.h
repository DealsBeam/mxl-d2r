// Guard for the client's unit sound wrapper 0x107dd0: a sound id outside sounds.txt is skipped instead of crashing in
// the record lookup 0xd8c70 (bugs #42 / #46). sound_guard.cpp.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallSoundGuard(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// "sounds.txt <rows> rows (D2R-XL <ModSoundRows>), <n> bad sound ids skipped (last id <id> from +0x<caller>)" for the mxl
// console command.
void SoundGuardStatus(char* out, size_t outSize) noexcept;

// Rows of D2R-XL's sounds.txt (both banks). releasetables (tools/game_checks.py) checks it against the installed mod's file.
constexpr int ModSoundRows = 12449;

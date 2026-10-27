// Guard for the client's unit sound wrapper 0x107dd0: a sound id outside sounds.txt is skipped instead of crashing in
// the record lookup 0xd8c70 (bugs #42 / #46). sound_guard.cpp.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

auto InstallSoundGuard(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// "<n> bad sound ids skipped (last id <id> from +0x<caller>)" for the mxl console command.
void SoundGuardStatus(char* out, size_t outSize) noexcept;

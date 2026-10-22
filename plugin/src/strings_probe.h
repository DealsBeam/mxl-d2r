// Test-harness probe of D2RLoader's localization service and D2R's numeric string lookup (docs/strings-namespace-research.md).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Queries the localization service once (LoadPlugin). False when the service is missing.
auto StringsProbeInit(const D2RL::PluginContext* context, uint64_t imageBase) noexcept -> bool;

// getStringByKey (full key such as "medianxl:Key" or "d2r:strCancel"; "mxl-core"-local when unqualified) into `out`.
// Returns the service Result as an integer (0 = Success); -1 when the service is not available.
auto StringsProbeKey(const char* key, char* out, size_t outSize) noexcept -> int;
// getStringById (original game IDs only per the SDK).
auto StringsProbeId(uint32_t id, char* out, size_t outSize) noexcept -> int;
// D2R's own numeric lookup, the D2RCore thunk 0x3e2b730 (u16 id -> UTF-8 text); null text when unreadable.
auto StringsProbeNative(uint32_t id, char* out, size_t outSize) noexcept -> bool;

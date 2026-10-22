// Median's strings at run time (strings.cpp; contract docs/strings-namespace.md "Plugin uses").
#pragma once

#include <D2RLPlugin/api.h>

#include "strings_logic.h"

#include <cstddef>
#include <cstdint>

// LoadPlugin: finds the localization service and replaces the import slot of D2R's numeric string lookup (0x143e2b730) with
// the plugin's lookup, which answers the plugin's own ids (IdFor) and passes every other id on. The service alone is enough
// for Lookup / Text; the id hook is the client's part. True when the service is there; `report` says what is on.
auto InstallStrings(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// The text of a full key ("medianxl:Key", "d2r:strCancel") in the active language, UTF-8. False when it is not there.
auto StringLookup(const char* fullKey, char* out, size_t outSize) noexcept -> bool;

// The text of `fullKey` in `out`, else `fallback` (the English the plugin used before Median's string was found; may be
// null). Returns `out`.
auto StringText(const char* fullKey, const char* fallback, char* out, size_t outSize) noexcept -> const char*;

// The id D2R's menu records / string lookups use for `ref`: the native id, or the plugin's own id for a namespaced key
// (registered on first use, once D2R's string tables are loaded). 0 when there is none.
auto StringId(const mxlstr::Ref& ref) noexcept -> uint16_t;

// Test harness: how many ids the plugin has handed out, and the key of one (null: not ours).
auto StringIdCount() noexcept -> int;
auto StringKeyOfId(uint32_t id) noexcept -> const char*;

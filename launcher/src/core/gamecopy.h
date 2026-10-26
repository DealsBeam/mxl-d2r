// The private game copy (docs/launcher.md "Private game copy"): which files of the player's D2R are copied, and the
// D2RLoader settings the launcher writes into the copy.
#pragma once

#include <string>

namespace mxl {

// `rel` is relative to the D2R folder, '\' or '/' separators, any case. Excluded: mods (other mods stay where they
// are), every D2RLoader file (the copy gets the pinned 1.3.1), logs and crash reports, and our own bookkeeping.
bool ExcludedFromCopy(const std::string& rel);

// Set `key = value` in [section] of a TOML text: an uncommented key is replaced in place (its comment kept), a
// commented template line (`# key = ...`) is replaced, otherwise the key is added under the section header (the
// section is appended when missing). `value` is TOML text (`"D2R-XL"`, `false`).
std::string SetTomlKey(const std::string& text, const std::string& section, const std::string& key, const std::string& value);

// Value text of `key` in [section] ("" when missing), without a trailing comment.
std::string GetTomlKey(const std::string& text, const std::string& section, const std::string& key);

// Where D2R-XL lives, next to the official Median XL (docs/launcher.md "Coexisting with the official Median XL").
// Windows paths, UTF-8.
constexpr const char* ProductFolder = "D2R-XL";
// <localAppData>\D2R-XL: state, cache, log
std::string DefaultDataDir(const std::string& localAppData);
// <dataDir>\game when the data folder is on D2R's drive, else <D2R's drive root>\D2R-XL\game (the drive rule)
std::string DefaultGameDir(const std::string& dataDir, bool dataDirOnD2RDrive, const std::string& d2rDriveRoot);
// True for a path the official Median XL owns or this launcher must never use: <localAppData>\mxl-launcher (its
// launcher), <roamingAppData>\MedianXL (its saves), <localAppData>\MedianXL, and a classic Diablo II folder (any path
// segment named "Diablo II"). Case-insensitive, '\' or '/'.
bool OfficialMedianXLPath(const std::string& path, const std::string& localAppData, const std::string& roamingAppData);

}  // namespace mxl

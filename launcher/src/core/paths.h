// Where the downloader writes (docs/launcher.md "Where it writes"): the mod folder <D2R>\mods\<modFolder> and nothing
// else in the D2R folder, its own data in %LOCALAPPDATA%\D2R-XL, never the official Median XL's folders. The marker that
// proves a mod folder is ours, the D2RLoader version check, the TOML edits for D2RLoader's config. Testable without
// Windows (Windows paths as UTF-8 strings).
#pragma once

#include <string>

namespace mxl {

constexpr const char* ProductFolder = "D2R-XL";
// The marker file at the top of the mod folder: {"made_by": MarkerMadeBy, "id": ..., "version": ...}.
constexpr const char* InstallMarkerName = "d2r-xl-install.json";
constexpr const char* MarkerMadeBy = "D2R-XL Downloader";
// Downloads in progress, inside the mod folder (same volume: a finished object is renamed into place).
constexpr const char* DownloadFolderName = "d2r-xl-download";

// <localAppData>\D2R-XL: state, log
std::string DefaultDataDir(const std::string& localAppData);

// A mod folder name from a release manifest: one plain path segment ("D2R-XL"), nothing that could leave <D2R>\mods.
bool SafeModFolderName(const std::string& name);
// <d2rDir>\mods\<modFolder> ("" when the name is not safe). The only folder of the D2R install the downloader writes.
std::string ModTargetDir(const std::string& d2rDir, const std::string& modFolder);
// True when `path` is `dir` itself or inside it (case-insensitive, '\' or '/', no ".." tricks: a path with a ".."
// segment is never inside).
bool PathIsOrUnder(const std::string& path, const std::string& dir);

// What a mod folder's marker says about who made it.
enum class MarkerVerdict {
	None,      // no marker: not made by the downloader (a manual or tools/install_mod.py install): ask before replacing
	Ours,      // made by the downloader, and its id is the one in the state (or the state has none)
	OtherId,   // made by the downloader, another id (e.g. the state was deleted, or another Windows user installed it)
	Unreadable // a marker that is not ours (damaged, or another made_by)
};
// `text`: the marker file ("" when missing), `stateId`: the id the state recorded ("" when none).
MarkerVerdict CheckMarker(const std::string& text, const std::string& stateId);
std::string MakeMarker(const std::string& id, const std::string& version);

// D2RLoader's file version ("1.3.1-beta") against the release's ("1.3.1-beta"; the manifest's d2rloader.version). Equal
// numbers are enough: "1.3.1" matches "1.3.1-beta", "1.3.1.0" too; "1.3.0" or "1.4.0" do not. "" never matches.
bool LoaderVersionMatches(const std::string& found, const std::string& required);

// Set `key = value` in [section] of a TOML text: an uncommented key is replaced in place (its comment kept), a
// commented template line (`# key = ...`) is replaced, otherwise the key is added under the section header (the
// section is appended when missing). `value` is TOML text (`"D2R-XL"`, `false`).
std::string SetTomlKey(const std::string& text, const std::string& section, const std::string& key, const std::string& value);

// Value text of `key` in [section] ("" when missing), without a trailing comment.
std::string GetTomlKey(const std::string& text, const std::string& section, const std::string& key);

// True for a path the official Median XL owns or this downloader must never use: <localAppData>\mxl-launcher (its
// launcher), <roamingAppData>\MedianXL (its saves), <localAppData>\MedianXL, and a classic Diablo II folder (any path
// segment named "Diablo II"). Case-insensitive, '\' or '/'.
bool OfficialMedianXLPath(const std::string& path, const std::string& localAppData, const std::string& roamingAppData);

// True for the game's own processes, which hold the mod's files: D2R.exe and D2RLoader*.exe (case-insensitive, file name
// or full path). Not the downloader, Battle.net's "Diablo II Resurrected Launcher.exe" or BlizzardError.exe.
bool IsGameProcessName(const std::string& exe);

}  // namespace mxl

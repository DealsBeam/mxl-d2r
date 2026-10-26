// The release manifest (docs/launcher.md "Release feed") and the install plan: which files of a release to fetch,
// which to leave alone and which to remove, given what is installed now.
#pragma once

#include "json.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mxl {

struct ManifestFile {
	std::string path;  // relative to the mod folder (<game>\mods\<modFolder>), '/' separators
	uint64_t size = 0;
	std::string sha256;
};

// release-manifest.json, schemaVersion 1, as lane pub-release defines it (docs/release.md "Release manifest").
// Unknown fields are ignored; an unknown schemaVersion is refused.
struct Manifest {
	int schemaVersion = 0;
	std::string name;           // "MedianXL"
	std::string version;        // the release, shown to the player
	std::string pluginVersion;
	bool complete = false;      // false: a CI package without data, never installed
	int d2rBuild = 0;           // d2r.build: the build the game copy must be at (93847)
	uint32_t imageTimestamp = 0;  // d2r.imageTimestamp: of the unpacked image in memory, not the file (0 = not given)
	struct Loader {
		std::string version, url, sha256, signerThumbprint;
		uint64_t size = 0;
	} d2rloader;
	std::string launchDefaultMod;  // launch.default_mod, for the copy's d2rloader.toml
	std::string launchArguments;   // launch.launch_arguments ("-txt")
	std::string modFolder;         // the files live in <game>\mods\<modFolder>; the zip's top folder
	std::string saveFolder;        // modinfo savepath ("MedianXL/"); the launcher never writes there
	std::set<std::string> preserve;  // player-editable: written only when missing
	std::string zipName;           // the release zip, relative to the manifest URL (or absolute)
	uint64_t zipSize = 0;
	std::string zipSha256;
	std::string launcherMin;       // optional (launcher lane extension): oldest launcher that can install it
	std::vector<ManifestFile> files;
};

// Parse and check a manifest. Every path must pass SafeRelativePath, sha256 must be 64 hex digits, fileCount /
// totalSize (when given) must match the list.
bool ParseManifest(const std::string& text, Manifest& out, std::string& error);
Json ManifestToJson(const Manifest& m);

// A relative path that cannot leave its root: no empty / "." / ".." segment, no drive, no backslash, no characters
// Windows refuses (<>:"|?* and control characters), no trailing dot or space in a segment, no device names.
bool SafeRelativePath(const std::string& path);

// Files under the mod folder that an update never deletes although the release does not list them: D2RLoader's own
// output (d2rloader/logs/, d2rloader/crashes/).
bool RuntimeOutput(const std::string& path);

struct InstallPlan {
	std::vector<ManifestFile> fetch;      // missing or different: download and install
	std::vector<std::string> remove;      // on disk under the mod folder, not in the release (docs/release.md "files")
	std::vector<std::string> preserved;   // preserve entries that exist: left as they are
	uint64_t fetchBytes = 0;
};

// current(path) gives the sha256 of the installed file, or "" when it is missing; `local` lists every file now under
// the mod folder (relative, '/' separators).
InstallPlan PlanInstall(const Manifest& next, const std::vector<std::string>& local,
                        const std::function<std::string(const std::string&)>& current);

// "1.2.10" > "1.2.9"; a "-beta" style suffix sorts before the plain version.
int CompareVersions(const std::string& a, const std::string& b);

// Resolve `ref` against `base` (a manifest URL): absolute http(s):// or file:// stays, "x.zip" replaces the last
// segment, "/x.zip" keeps scheme and host.
std::string ResolveUrl(const std::string& base, const std::string& ref);

}  // namespace mxl

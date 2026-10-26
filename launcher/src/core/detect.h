// Finding D2R and checking its build: the parsers (testable without Windows). The registry / file system side is in
// src/win/game.cpp.
#pragma once

#include <string>
#include <vector>

namespace mxl {

constexpr int RequiredBuild = 93847;  // D2R 3.3 build the port is made for (docs/d2r-baseline.md)

// .build.info of a Blizzard (CASC) install: '|' separated, first line "Name!TYPE:n|...". Returns the "Version" of
// the active row ("3.3.93847"), or "" when there is none.
std::string BuildInfoVersion(const std::string& text);

// Last number of a version string: "3.3.93847" -> 93847, "1.0.0.93847" -> 93847; 0 when there is none.
int BuildNumber(const std::string& version);

// Every "path" value of Steam's libraryfolders.vdf (KeyValues text), unescaped ("C:\\Steam" -> "C:\Steam").
std::vector<std::string> SteamLibraryPaths(const std::string& vdf);

// Value of a top-level key of an appmanifest_*.acf ("installdir", "name", "appid"), "" when missing.
std::string AcfValue(const std::string& acf, const std::string& key);

// True when an appmanifest names D2R (its name or install folder).
bool AcfIsD2R(const std::string& acf);

enum class BuildVerdict { Ok, Older, Newer, Unknown };
struct BuildCheck {
	BuildVerdict verdict = BuildVerdict::Unknown;
	int build = 0;
	std::string message;  // one line for the status bar
};
// `infoVersion` from .build.info, `exeVersion` the file version of D2R.exe ("" when unreadable). Both must name the
// required build when present; .build.info is the one Battle.net updates first.
BuildCheck CheckBuild(const std::string& infoVersion, const std::string& exeVersion, int required = RequiredBuild);

}  // namespace mxl

// What the launcher does (docs/launcher.md): find D2R, make the private game copy, put the pinned D2RLoader and the
// Median XL release into it, start the game; Repair and Uninstall. Used by the window (main.cpp) and the CLI.
#pragma once

#include "../core/json.h"
#include "../core/manifest.h"
#include "game.h"

#include <atomic>
#include <functional>
#include <string>

namespace mxl {

constexpr const char* LauncherVersion = "0.1.0";
// the download host (docs/release.md "Hosting layout"): channels/<channel>.json, releases/<v>/, objects/
constexpr const char* DefaultSite = "https://dl.werdegames.com/mxl/";
constexpr const char* DefaultChannel = "stable";
constexpr const char* IssuesUrl = "https://github.com/Nugwa628/mxl-d2r/issues";

// D2RLoader 1.3.1-beta, pinned (never "latest"): the zip as published on d2rloader.net and the files inside it.
constexpr const char* LoaderVersion = "1.3.1-beta";
constexpr const char* LoaderUrl = "https://d2rloader.net/download/D2RLoader-1.3.1-beta.zip";
constexpr const char* LoaderZipSha256 = "9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7";
constexpr unsigned long long LoaderZipSize = 47652455;
constexpr const char* LoaderSigner = "8001082E968FADAA58BF2F73EFA1810A11D0C28F";  // D2RLoader.exe / D2RCore.dll signer

struct Options {
	std::wstring dataDir;    // state, cache, log (default %LOCALAPPDATA%\MedianXL)
	std::string site;        // download site root: https://, http://, file:// or a local folder (default DefaultSite, or the saved one)
	std::string channel;     // "stable" (default) or "test"
	std::wstring source;     // the player's D2R folder (default: detected)
	std::wstring gameDir;    // the private copy (default: <data>\game when on the D2R drive, else <D2R drive>\MedianXL\game)
	bool anyDrive = false;   // allow a copy on another drive than the D2R install (docs/launcher.md "Drive rule")
	std::wstring extraArgs;  // more game arguments ("-w")
	bool background = false; // tests: start the game without taking the foreground (as tools/game_ctl.py does)
	std::string loaderUrl;   // tests: another source of the same pinned zip (file://); the sha256 still has to match
};

struct Progress {
	std::string text;
	double fraction = -1;  // 0..1, or -1 for "busy"
};

class Launcher {
public:
	explicit Launcher(Options o);

	// One-line summary per part; also fills `ready` (Play possible) and `updateKnown` after CheckFeed.
	std::string StatusText();
	bool CanPlay(std::string* why = nullptr);

	// The actions. Each returns false with a message for the player in `error`.
	bool InstallOrUpdate(std::string& error);  // copy (once), loader, release: only what is missing or changed
	bool Repair(std::string& error);           // the same with every file hashed again
	bool Play(std::string& error);
	bool Uninstall(std::string& error);        // the copy, the cache and our state; never saves, never the real D2R
	bool CheckFeed(std::string& error);        // read the manifest only

	std::wstring SavesDir();
	std::wstring GameDir() const { return gameDir_; }
	const Options& options() const { return opt_; }

	std::function<void(const Progress&)> onProgress;
	std::atomic<bool> cancel{false};

private:
	bool Prepare(std::string& error);  // source + game dir + state
	bool EnsureCopy(bool verify, std::string& error);
	bool EnsureLoader(bool verify, std::string& error);
	bool EnsureMod(bool verify, std::string& error);
	bool LoadState();
	bool SaveState();
	std::wstring DefaultGameDir(const std::wstring& source);
	bool LocationOk(const std::wstring& source, const std::wstring& dir, std::string& error);
	void Report(const std::string& text, double fraction = -1);
	std::wstring ModDir() const;
	std::string ModFolder() const;
	bool WriteLaunchConfig(std::string& error);
	bool CheckLoaderSignatures(std::string& error);
	bool InstallObjects(const std::wstring& mod, std::vector<ManifestFile>& fetch, Json& files, std::string& error);
	bool InstallFromZip(const std::wstring& mod, const std::vector<ManifestFile>& fetch, Json& files, std::string& error);
	std::string NewLauncherUrl() const;

	Options opt_;
	std::wstring statePath_;
	Json state_;
	D2RInstall source_;
	std::wstring gameDir_;
	bool feedChecked_ = false;
	Manifest feed_;            // the channel's release manifest
	std::string siteRoot_;     // SiteRoot(opt_.site)
	std::string manifestUrl_;
};

}  // namespace mxl

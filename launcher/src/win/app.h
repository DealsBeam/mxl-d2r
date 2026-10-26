// What the launcher does (docs/launcher.md): find D2R, make the private game copy, put the pinned D2RLoader and the
// D2R-XL release into it, start the game; Repair and Uninstall. Used by the window (main.cpp) and the CLI.
#pragma once

#include "../core/json.h"
#include "../core/manifest.h"
#include "game.h"

#include <atomic>
#include <functional>
#include <string>

namespace mxl {

#ifndef MXL_LAUNCHER_VERSION
#define MXL_LAUNCHER_VERSION "0.1.1"  // cmake -DMXL_LAUNCHER_VERSION=x.y.z (tests of the self-update)
#endif
constexpr const char* LauncherVersion = MXL_LAUNCHER_VERSION;
// the download host (docs/release.md "Hosting layout"): channels/<channel>.json, releases/<v>/, objects/
constexpr const char* DefaultSite = "https://dl.werdegames.com/mxl/";
constexpr const char* DefaultChannel = "stable";
constexpr int ObjectAttempts = 5;  // tries per object (resumed each time) before an install stops with "try again"
constexpr const char* IssuesUrl ="https://github.com/Nugwa628/mxl-d2r/issues";

// D2RLoader 1.3.1-beta, pinned (never "latest"): the zip as published on d2rloader.net and the files inside it.
constexpr const char* LoaderVersion = "1.3.1-beta";
constexpr const char* LoaderUrl = "https://d2rloader.net/download/D2RLoader-1.3.1-beta.zip";
constexpr const char* LoaderZipSha256 = "9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7";
constexpr unsigned long long LoaderZipSize = 47652455;
constexpr const char* LoaderSigner = "8001082E968FADAA58BF2F73EFA1810A11D0C28F";  // D2RLoader.exe / D2RCore.dll signer

struct Options {
	std::wstring dataDir;    // state, cache, log (default %LOCALAPPDATA%\D2R-XL)
	std::string site;        // download site root: https://, http://, file:// or a local folder (default DefaultSite, or the saved one)
	std::string channel;     // "stable" (default) or "test"
	std::wstring source;     // the player's D2R folder (default: detected)
	std::wstring gameDir;    // the private copy (default: <data>\game when on the D2R drive, else <D2R drive>\D2R-XL\game)
	bool anyDrive = false;   // test builds only (MXL_LAUNCHER_TEST_OPTIONS): a copy on another drive than D2R (docs/launcher.md "Drive rule")
	std::wstring extraArgs;  // more game arguments ("-w")
	bool background = false; // tests: start the game without taking the foreground (as tools/game_ctl.py does)
	std::string loaderUrl;   // tests: another source of the same pinned zip (file://); the sha256 still has to match
};

struct Progress {
	std::string text;
	double fraction = -1;  // 0..1, or -1 for "busy"
};

// docs/launcher.md "Coexisting with the official Median XL": its folders and a classic Diablo II folder are never used.
bool OfficialMedianXLFolder(const std::wstring& dir);

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

	// Self-update (docs/launcher.md "Self-update"): the manifest's `launcher` entry, newer than this one, after a
	// CheckFeed (also one that refused the release because it needs a newer launcher).
	bool LauncherUpdateAvailable() const;
	std::string LauncherUpdateVersion() const { return offer_.version; }
	// Downloads it next to this exe, checks size and sha256 against the manifest (whether or not it is signed), then
	// swaps the files: this exe becomes <exe>.old, the new one takes its name. The caller starts it.
	bool SelfUpdate(std::string& error);
	static std::wstring ExePath();
	static void RemoveOldExe();  // <exe>.old of an earlier update, once that process has ended

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
	std::string NewLauncherUrl() const;

	Options opt_;
	std::wstring statePath_;
	Json state_;
	D2RInstall source_;
	std::wstring gameDir_;
	bool feedChecked_ = false;
	Manifest feed_;            // the channel's release manifest
	decltype(Manifest::launcher) offer_;  // its launcher entry (set even when the release itself is refused)
	std::string siteRoot_;     // SiteRoot(opt_.site)
	std::string manifestUrl_;
};

}  // namespace mxl

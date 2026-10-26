#include "app.h"

#include "../core/gamecopy.h"
#include "../core/inflate.h"
#include "../core/sha256.h"
#include "../core/zip.h"
#include "http.h"
#include "util.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <thread>

namespace mxl {
namespace {

struct LoaderFile {
	const char* path;  // inside the zip and inside the game folder
	const char* sha256;
	bool config;  // the player may edit it: written once, then only our keys are set
};
const LoaderFile LoaderFiles[] = {
	{"D2RLoader.exe", "93021dad48533bcfca8a95c69a0cc00cdb9008a3ce6ceba1d04c7aca71985a10", false},
	{"D2RCore.dll", "2a868d013d2e0830bd2d9e04b918b19e46a73cf726c833e70d089b948fdeb5a2", false},
	{"d2rloader.mpq", "1f02aa4e3179cb8a8417125160aa3ef18003ea81ee1a4f54e0a76fd3f22d933b", false},
	{"d2rloader/config/d2rloader.toml", "86be762edb678386ef109589df85cb234cf13d992c548607a44f072d77655e42", true},
};
const char* LoaderDirs[] = {"mods", "d2rloader/config", "d2rloader/data", "d2rloader/plugins", "d2rloader/logs", "d2rloader/crashes"};

constexpr uint64_t SpareBytes = 3ull << 30;  // the release (about 1.5 GB) plus room for logs and saves
constexpr const char* MarkerName = "d2r-xl-copy.json";

std::string Mb(uint64_t b) { return std::to_string((b + (1 << 19)) >> 20) + " MB"; }
std::string Gb(uint64_t b) {
	char s[32];
	std::snprintf(s, sizeof(s), "%.1f GB", double(b) / double(1ull << 30));
	return s;
}

std::string RandomId() {
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	char s[40];
	std::snprintf(s, sizeof(s), "%08lx%016llx", GetCurrentProcessId(), static_cast<unsigned long long>(c.QuadPart));
	return s;
}

// Read a zip entry out of a whole-file buffer (the D2RLoader zip is read from the cache in one piece).
bool ZipEntryData(const std::string& zip, const ZipEntry& e, std::vector<uint8_t>& out, std::string& error) {
	auto p = reinterpret_cast<const uint8_t*>(zip.data());
	if (e.offset + 30 > zip.size()) {
		error = "zip entry outside the file";
		return false;
	}
	size_t hs = LocalHeaderSize(p + e.offset, zip.size() - size_t(e.offset));
	if (!hs || e.offset + hs + e.compSize > zip.size()) {
		error = "bad zip entry " + e.name;
		return false;
	}
	const uint8_t* d = p + e.offset + hs;
	if (e.method == 0)
		out.assign(d, d + e.compSize);
	else if (e.method != 8 || !Inflate(d, size_t(e.compSize), out, size_t(e.size))) {
		error = "cannot unpack " + e.name;
		return false;
	}
	if (Crc32(out.data(), out.size()) != e.crc) {
		error = "CRC mismatch in " + e.name;
		return false;
	}
	return true;
}

bool ReadZipDirectory(const std::string& zip, std::vector<ZipEntry>& entries, std::string& error) {
	size_t tailSize = zip.size() < 70000 ? zip.size() : 70000;
	std::vector<uint8_t> tail(zip.end() - std::ptrdiff_t(tailSize), zip.end());
	ZipTail t;
	if (!FindZipTail(tail, zip.size(), t, error))
		return false;
	return ParseCentralDirectory(reinterpret_cast<const uint8_t*>(zip.data()) + t.cdOffset, size_t(t.cdSize), t.entries, entries, error);
}

std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

}  // namespace

Launcher::Launcher(Options o) : opt_(std::move(o)) {
	if (opt_.dataDir.empty())
		opt_.dataDir = Wide(DefaultDataDir(Utf8(KnownFolder(0))));
	statePath_ = JoinPath(opt_.dataDir, L"launcher.json");
	LoadState();
	if (opt_.site.empty())
		opt_.site = state_["site"].Str(DefaultSite);
	if (opt_.channel.empty())
		opt_.channel = state_["channel"].Str(DefaultChannel);
	siteRoot_ = SiteRoot(opt_.site);
}

void Launcher::Report(const std::string& text, double fraction) {
	Log(text);
	if (onProgress)
		onProgress({text, fraction});
}

bool Launcher::LoadState() {
	std::string text, err;
	state_ = Json::Object();
	if (!ReadWholeFile(statePath_, text))
		return false;
	Json j;
	if (!Json::Parse(text, j, err) || !j.IsObject()) {
		Log("state file unreadable (" + err + "), starting over: " + Utf8(statePath_));
		return false;
	}
	state_ = j;
	return true;
}

bool Launcher::SaveState() {
	state_.Set("launcher", LauncherVersion);
	state_.Set("site", opt_.site);
	state_.Set("channel", opt_.channel);
	return WriteFileAtomic(statePath_, state_.Dump());
}

std::string Launcher::ModFolder() const {
	return !feed_.modFolder.empty() ? feed_.modFolder : state_["mod"]["modFolder"].Str("D2R-XL");
}
std::wstring Launcher::ModDir() const { return JoinPath(gameDir_, L"mods\\" + Wide(ModFolder())); }

// The copy's own d2rloader.toml gets the release's launch keys (docs/release.md "launch"); D2RLoader started without
// arguments builds the D2R command line from them ("<launch_arguments> -mod <default_mod>").
bool Launcher::WriteLaunchConfig(std::string& error) {
	std::wstring toml = JoinPath(gameDir_, L"d2rloader\\config\\d2rloader.toml");
	std::string text;
	if (!ReadWholeFile(toml, text)) {
		error = "Cannot read " + Utf8(toml) + ". Press Repair.";
		return false;
	}
	const Json& launch = state_["mod"]["launch"];
	std::string mod = !feed_.launchDefaultMod.empty() ? feed_.launchDefaultMod : launch["default_mod"].Str(ModFolder());
	std::string args = feed_.version.empty() ? launch["launch_arguments"].Str("-txt") : feed_.launchArguments;
	if (!opt_.extraArgs.empty())
		args += (args.empty() ? "" : " ") + Utf8(opt_.extraArgs);
	std::string quoted;
	for (char c : args)
		if (c != '"' && uint8_t(c) >= 0x20)
			quoted += c;
	std::string fixed = SetTomlKey(text, "d2rloader", "default_mod", "\"" + mod + "\"");
	fixed = SetTomlKey(fixed, "d2rloader", "launch_arguments", "\"" + quoted + "\"");
	fixed = SetTomlKey(fixed, "d2rloader", "check_for_updates", "false");
	if (fixed != text && !WriteFileAtomic(toml, fixed)) {
		error = "Cannot write " + Utf8(toml);
		return false;
	}
	return true;
}

bool OfficialMedianXLFolder(const std::wstring& dir) {
	return OfficialMedianXLPath(Utf8(dir), Utf8(KnownFolder(0)), Utf8(KnownFolder(3)));
}

std::wstring Launcher::DefaultGameDir(const std::wstring& source) {
	std::wstring root;
	uint32_t src = VolumeSerial(source, &root);  // same drive as D2R (docs/launcher.md "Drive rule")
	return Wide(mxl::DefaultGameDir(Utf8(opt_.dataDir), !src || VolumeSerial(opt_.dataDir) == src, Utf8(root)));
}

bool Launcher::LocationOk(const std::wstring& source, const std::wstring& dir, std::string& error) {
	if (OfficialMedianXLFolder(dir)) {
		error = "The D2R-XL folder cannot be inside the official Median XL's folders or a Diablo II folder (" + Utf8(dir) +
		        "). Pick another folder.";
		return false;
	}
	if (PathUnder(dir, source) || PathUnder(source, dir)) {
		error = "The D2R-XL folder must be outside the D2R folder (" + Utf8(source) + ").";
		return false;
	}
	if (dir.size() <= 3) {
		error = "Pick a folder, not a whole drive.";
		return false;
	}
	if (!opt_.anyDrive && VolumeSerial(dir) != VolumeSerial(source)) {
		std::wstring root;
		VolumeSerial(source, &root);
		error = "The D2R-XL folder must be on the same drive as D2R (" + Utf8(root) +
		        "): D2R's Battle.net login file only opens from that drive. Pick a folder on " + Utf8(root) + ".";
		return false;
	}
	return true;
}

bool Launcher::Prepare(std::string& error) {
	// the source: from the command line, the state, or detection
	std::wstring src = opt_.source.empty() ? Wide(state_["source"].Str()) : opt_.source;
	if (src.empty() || !LooksLikeD2R(src)) {
		auto found = FindD2RInstalls();
		if (found.empty()) {
			error = "D2R was not found. Install Diablo II: Resurrected from Battle.net or Steam, or choose its folder.";
			return false;
		}
		source_ = found.front();
	} else {
		source_ = InspectD2R(src, opt_.source.empty() ? state_["source_kind"].Str("saved") : "chosen");
	}
	if (FileExists(JoinPath(source_.dir, Wide(MarkerName)))) {
		error = "That folder is a D2R-XL game copy, not the D2R install. Choose the D2R folder from Battle.net or Steam.";
		return false;
	}
	gameDir_ = !opt_.gameDir.empty() ? opt_.gameDir : !state_["game_dir"].Str().empty() ? Wide(state_["game_dir"].Str()) : DefaultGameDir(source_.dir);
	if (OfficialMedianXLFolder(opt_.dataDir) || OfficialMedianXLFolder(gameDir_)) {  // docs/launcher.md "Coexisting with the official Median XL"
		error = "D2R-XL never uses the official Median XL's folders or a Diablo II folder (" +
		        Utf8(OfficialMedianXLFolder(opt_.dataDir) ? opt_.dataDir : gameDir_) + "). Choose other folders.";
		return false;
	}
	state_.Set("source", Utf8(source_.dir));
	state_.Set("source_kind", source_.source);
	state_.Set("game_dir", Utf8(gameDir_));
	return true;
}

std::wstring Launcher::SavesDir() {
	std::string savepath = "D2R-XL/", text, err;
	Json j;
	if (!gameDir_.empty() && ReadWholeFile(JoinPath(ModDir(), Wide(ModFolder()) + L".mpq\\modinfo.json"), text) && Json::Parse(text, j, err))
		savepath = j["savepath"].Str(savepath);
	std::wstring w = Wide(savepath);
	while (!w.empty() && (w.back() == L'/' || w.back() == L'\\'))
		w.pop_back();
	return JoinPath(KnownFolder(1), L"Diablo II Resurrected\\mods\\" + w);
}

// ---------------------------------------------------------------- the copy

bool Launcher::EnsureCopy(bool verify, std::string& error) {
	bool complete = state_["copy_complete"].Bool() && FileExists(JoinPath(gameDir_, Wide(MarkerName))) && LooksLikeD2R(gameDir_);
	if (complete && !verify) {
		D2RInstall copy = InspectD2R(gameDir_, "copy");
		if (copy.build.verdict != BuildVerdict::Ok) {
			error = "The D2R-XL game copy is not build " + std::to_string(RequiredBuild) + " (" + copy.build.message + "). Use Repair.";
			return false;
		}
		return true;
	}
	if (source_.build.verdict != BuildVerdict::Ok) {
		if (complete) {  // a newer D2R cannot repair the frozen copy, but the copy itself still works
			Report("D2R is now build " + std::to_string(source_.build.build) + "; the D2R-XL copy stays at build " +
			       std::to_string(RequiredBuild) + " and is not compared with it.");
			return true;
		}
		error = source_.build.message;
		return false;
	}
	if (!complete && !LocationOk(source_.dir, gameDir_, error))
		return false;
	std::wstring marker = JoinPath(gameDir_, Wide(MarkerName));
	if (DirExists(gameDir_) && !FileExists(marker)) {
		WIN32_FIND_DATAW fd;  // an existing folder must be empty: we only ever delete what we made
		HANDLE h = FindFirstFileW(JoinPath(gameDir_, L"*").c_str(), &fd);
		bool empty = true;
		if (h != INVALID_HANDLE_VALUE) {
			do {
				std::wstring n = fd.cFileName;
				empty = empty && (n == L"." || n == L"..");
			} while (empty && FindNextFileW(h, &fd));
			FindClose(h);
		}
		if (!empty) {
			error = "The folder " + Utf8(gameDir_) + " is not empty and was not made by this launcher. Choose an empty or new folder.";
			return false;
		}
	}
	Report("Listing the files of D2R in " + Utf8(source_.dir) + " ...");
	CopyList list;
	if (!ListGameFiles(source_.dir, list, error))
		return false;
	uint64_t present = 0;
	for (const auto& f : list.files) {
		uint64_t size = 0, mtime = 0;
		if (FileSize(JoinPath(gameDir_, f.rel), size, &mtime) && size == f.size && mtime == f.mtime)
			present += f.size;
	}
	uint64_t free = 0;
	if (FreeBytes(gameDir_, free) && free < list.bytes - present + SpareBytes) {
		error = "Not enough disk space for the D2R-XL game copy: it needs " + Gb(list.bytes - present + SpareBytes) + ", " + Gb(free) +
		        " are free on that drive.";
		return false;
	}
	if (!FileExists(marker)) {
		MakeDirs(gameDir_);
		Json m = Json::Object();
		m.Set("made_by", "D2R-XL launcher").Set("id", RandomId()).Set("source", Utf8(source_.dir)).Set("build", RequiredBuild);
		if (!WriteFileAtomic(marker, m.Dump())) {
			error = "Cannot write into " + Utf8(gameDir_) + ": " + Utf8(LastErrorText(GetLastError()));
			return false;
		}
	}
	std::string marktext, err;
	Json mj;
	if (ReadWholeFile(marker, marktext) && Json::Parse(marktext, mj, err))
		state_.Set("copy_id", mj["id"].Str());
	state_.Set("copy_complete", false);
	SaveState();
	Report("Copying D2R (" + Gb(list.bytes) + (present ? ", " + Gb(present) + " already copied" : "") + ") to " + Utf8(gameDir_) +
	       " (" + FileSystemName(gameDir_) + ")");
	ULONGLONG t0 = GetTickCount64();
	bool ok = CopyGameFiles(source_.dir, gameDir_, list, cancel, [&](const CopyProgress& p) {
		double secs = double(GetTickCount64() - t0) / 1000.0;
		double rate = secs > 1 ? double(p.doneBytes - p.skippedBytes) / secs : 0;
		std::string eta = rate > 0 && p.totalBytes > p.doneBytes ? ", " + std::to_string(int(double(p.totalBytes - p.doneBytes) / rate / 60.0 + 0.5)) + " min left" : "";
		if (onProgress)
			onProgress({"Copying D2R: " + Gb(p.doneBytes) + " of " + Gb(p.totalBytes) + eta,
			             p.totalBytes ? double(p.doneBytes) / double(p.totalBytes) : -1});
	}, error);
	if (!ok)
		return false;
	D2RInstall copy = InspectD2R(gameDir_, "copy");
	if (copy.build.verdict != BuildVerdict::Ok) {
		error = "The copy does not read as build " + std::to_string(RequiredBuild) + ": " + copy.build.message;
		return false;
	}
	state_.Set("copy_complete", true);
	state_.Set("copy_build", copy.build.build);
	state_.Set("copy_bytes", list.bytes);
	SaveState();
	Report("Game copy ready: " + Utf8(gameDir_) + " (" + Gb(list.bytes) + ", " + std::to_string(list.files.size()) + " files, " +
	       std::to_string(int(double(GetTickCount64() - t0) / 1000.0)) + " s)");
	return true;
}

// ---------------------------------------------------------------- D2RLoader

// On top of the pinned sha256: the Authenticode signer of the two binaries (docs/release.md "d2rloader").
bool Launcher::CheckLoaderSignatures(std::string& error) {
	for (const wchar_t* name : {L"D2RLoader.exe", L"D2RCore.dll"}) {
		std::string thumb;
		bool intact = false;
		if (!SignerThumbprint(JoinPath(gameDir_, name), thumb, intact) || !intact || thumb != LoaderSigner) {
			error = "The D2RLoader file " + Utf8(name) + " does not carry the expected signature (signer " + (thumb.empty() ? "none" : thumb) +
			        (intact ? "" : ", signature not intact") + "). Press Repair.";
			return false;
		}
	}
	return true;
}

bool Launcher::EnsureLoader(bool verify, std::string& error) {
	bool allOk = true;
	for (const auto& f : LoaderFiles) {
		std::wstring p = JoinPath(gameDir_, Wide(f.path));
		if (f.config) {
			allOk = allOk && FileExists(p);
			continue;
		}
		uint64_t size = 0, mtime = 0;
		auto rec = state_["loader_files"][f.path];
		bool quick = !verify && FileSize(p, size, &mtime) && rec["mtime"].Str() == std::to_string(mtime) && rec["size"].Int() == int64_t(size);
		allOk = allOk && (quick || FileSha256(p) == f.sha256);
	}
	if (allOk && (!verify || CheckLoaderSignatures(error))) {
		state_.Set("loader", LoaderVersion);
		return true;
	}
	error.clear();
	std::wstring cache = JoinPath(opt_.dataDir, L"cache\\D2RLoader-" + Wide(LoaderVersion) + L".zip");
	MakeDirs(ParentDir(cache));
	std::string zip;
	if (!ReadWholeFile(cache, zip) || Sha256Hex(zip.data(), zip.size()) != LoaderZipSha256) {
		std::string url = opt_.loaderUrl.empty() ? LoaderUrl : opt_.loaderUrl;
		Report("Downloading D2RLoader " + std::string(LoaderVersion) + " from " + url);
		zip.clear();
		FetchResult r = Fetch(url, 0, 0, [&](const uint8_t* p, size_t n) {
			if (zip.size() + n > LoaderZipSize + 1024)
				return false;
			zip.append(reinterpret_cast<const char*>(p), n);
			if (onProgress)
				onProgress({"Downloading D2RLoader: " + Mb(zip.size()) + " of " + Mb(LoaderZipSize), double(zip.size()) / double(LoaderZipSize)});
			return !cancel.load();
		});
		if (!r.ok) {
			error = "D2RLoader download failed: " + r.error;
			return false;
		}
		std::string got = Sha256Hex(zip.data(), zip.size());
		if (got != LoaderZipSha256) {
			error = "The D2RLoader download does not match the pinned " + std::string(LoaderVersion) + " (sha256 " + got +
			        "). Nothing was installed.";
			return false;
		}
		WriteFileAtomic(cache, zip);
	}
	std::vector<ZipEntry> entries;
	if (!ReadZipDirectory(zip, entries, error))
		return false;
	for (const char* d : LoaderDirs)
		MakeDirs(JoinPath(gameDir_, Wide(d)));
	Json rec = Json::Object();
	for (const auto& f : LoaderFiles) {
		auto it = std::find_if(entries.begin(), entries.end(), [&](const ZipEntry& e) { return e.name == f.path; });
		if (it == entries.end()) {
			error = std::string("D2RLoader zip lacks ") + f.path;
			return false;
		}
		std::wstring p = JoinPath(gameDir_, Wide(f.path));
		std::vector<uint8_t> data;
		if (!ZipEntryData(zip, *it, data, error))
			return false;
		if (Sha256Hex(data.data(), data.size()) != f.sha256) {
			error = std::string("D2RLoader file ") + f.path + " does not match the pinned version";
			return false;
		}
		if (f.config) {  // the template, only when missing; the launch keys are set by WriteLaunchConfig
			if (!FileExists(p) && !WriteFileAtomic(p, data.data(), data.size())) {
				error = "Cannot write " + Utf8(p);
				return false;
			}
			continue;
		}
		if (FileSha256(p) != f.sha256) {
			Report(std::string("Installing D2RLoader file ") + f.path);
			if (!WriteFileAtomic(p, data.data(), data.size())) {
				error = "Cannot write " + Utf8(p) + ": " + Utf8(LastErrorText(GetLastError()));
				return false;
			}
		}
		uint64_t size = 0, mtime = 0;
		FileSize(p, size, &mtime);
		Json r = Json::Object();
		r.Set("size", size).Set("mtime", std::to_string(mtime));
		rec.Set(f.path, r);
	}
	if (!CheckLoaderSignatures(error))
		return false;
	state_.Set("loader_files", rec);
	state_.Set("loader", LoaderVersion);
	SaveState();
	Report(std::string("D2RLoader ") + LoaderVersion + " ready in the game copy");
	return true;
}

// ---------------------------------------------------------------- the release

std::string Launcher::NewLauncherUrl() const {
	return feed_.launcher.path.empty() ? siteRoot_ : ResolveUrl(siteRoot_, feed_.launcher.path);
}

// docs/release.md "Hosting layout": channels/<channel>.json names the release manifest and its sha256.
bool Launcher::CheckFeed(std::string& error) {
	for (char c : opt_.channel)
		if (!std::isalnum(uint8_t(c)) && c != '-') {
			error = "Bad channel name \"" + opt_.channel + "\".";
			return false;
		}
	std::string chUrl = siteRoot_ + "channels/" + opt_.channel + ".json";
	Report("Reading the release channel " + chUrl);
	std::string text;
	int status = 0;
	if (!FetchText(chUrl, text, error, 1u << 20, &status)) {
		Log("channel " + chUrl + ": " + (status ? "HTTP " + std::to_string(status) : std::string("no answer")) + ": " + error);
		error = ChannelReadError(opt_.channel, chUrl, status, status ? "" : error);
		return false;
	}
	Log("channel " + chUrl + ": HTTP " + std::to_string(status) + ", " + std::to_string(text.size()) + " bytes");
	Channel ch;
	if (!ParseChannel(text, ch, error)) {
		error = "The download site's channel file " + chUrl + " is not usable: " + error;
		return false;
	}
	std::string manUrl = ResolveUrl(siteRoot_, ch.manifest);
	if (!FetchText(manUrl, text, error)) {
		error = "Cannot read the release manifest " + manUrl + ": " + error;
		return false;
	}
	if (Sha256Hex(text.data(), text.size()) != ch.manifestSha256) {
		error = "The release manifest " + manUrl + " does not match the channel file (sha256). The site may be in the middle of an "
		        "update; try again in a few minutes.";
		return false;
	}
	Manifest m;
	if (!ParseManifest(text, m, error)) {
		if (m.schemaVersion > ManifestSchema) {
			offer_ = m.launcher;  // the release's own launcher can read it: self-update
			feed_.launcher = m.launcher;
			error = "This launcher is too old for D2R-XL " + m.version + " (its release manifest has format " +
			        std::to_string(m.schemaVersion) + ", this launcher reads " + std::to_string(ManifestSchema) +
			        "). Update the launcher: " + NewLauncherUrl();
		} else if (m.schemaVersion > 0 && m.schemaVersion < ManifestSchema) {
			error = "This launcher is too new for the release on the download site (D2R-XL " + m.version +
			        ", release manifest format " + std::to_string(m.schemaVersion) + "; this launcher reads " +
			        std::to_string(ManifestSchema) + "). Try again later, or report it: " + IssuesUrl;
		} else {
			error = "The release manifest is damaged: " + error;
		}
		return false;
	}
	if (m.version != ch.version) {
		error = "The channel names D2R-XL " + ch.version + " but its manifest is " + m.version + ".";
		return false;
	}
	offer_ = m.launcher;  // a launcher update is possible even when this launcher cannot install the release
	if (m.d2rBuild != RequiredBuild) {
		feed_ = m;
		error = "D2R-XL " + m.version + " needs D2R build " + std::to_string(m.d2rBuild) + "; this launcher knows build " +
		        std::to_string(RequiredBuild) + ". Get the new launcher: " + NewLauncherUrl();
		return false;
	}
	if (!m.complete) {
		error = "The release manifest names D2R-XL " + m.version + " as incomplete (a test package without game data); it cannot be installed.";
		return false;
	}
	// the launcher installs only its own pinned D2RLoader; a release that names another one needs a new launcher
	if ((!m.d2rloader.version.empty() && m.d2rloader.version != LoaderVersion) ||
	    (!m.d2rloader.sha256.empty() && m.d2rloader.sha256 != LoaderZipSha256) ||
	    (!m.d2rloader.signerThumbprint.empty() && m.d2rloader.signerThumbprint != LoaderSigner)) {
		feed_ = m;
		error = "D2R-XL " + m.version + " needs D2RLoader " + m.d2rloader.version + "; this launcher installs " + LoaderVersion +
		        ". Get the new launcher: " + NewLauncherUrl();
		return false;
	}
	if (!m.launcherMin.empty() && CompareVersions(LauncherVersion, m.launcherMin) < 0) {
		feed_ = m;
		error = "D2R-XL " + m.version + " needs launcher " + m.launcherMin + " or newer (this is " + LauncherVersion +
		        "). Get it: " + NewLauncherUrl();
		return false;
	}
	feed_ = std::move(m);
	manifestUrl_ = manUrl;
	feedChecked_ = true;
	if (LauncherUpdateAvailable())
		Report("A newer launcher (" + offer_.version + ") is available: " + NewLauncherUrl());
	return true;
}

bool Launcher::LauncherUpdateAvailable() const {
	if (offer_.version.empty() || offer_.path.empty() || offer_.size <= 0 || offer_.sha256.size() != 64 ||
	    CompareVersions(offer_.version, LauncherVersion) <= 0)
		return false;
	// The published exe is this one (a release whose launcher entry carries a wrong version): nothing to update, and
	// offering it would loop, since the replacement reports the same version again.
	if (selfSha256_.empty())
		selfSha256_ = FileSha256(ExePath());
	return selfSha256_ != offer_.sha256;
}

std::wstring Launcher::ExePath() {
	std::wstring buf(32768, L'\0');
	buf.resize(GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size())));
	return buf;
}

void Launcher::RemoveOldExe() {
	std::wstring old = ExePath() + L".old";
	if (!FileExists(old))
		return;
	std::thread([old] {  // the old process may still be ending
		for (int i = 0; i < 20 && !DeleteFileW(LongPath(old).c_str()) && FileExists(old); ++i)
			Sleep(500);
	}).detach();
}

// The sha256 of the published launcher is what makes it trusted: it comes from the release manifest, which is checked
// against the channel file's manifestSha256 and read over HTTPS. A signature is logged, never required, so a new
// signing certificate (or none) does not block an update.
bool Launcher::SelfUpdate(std::string& error) {
	if (!LauncherUpdateAvailable()) {
		error = "No newer launcher is published.";
		return false;
	}
	std::wstring exe = ExePath(), part = exe + L".download", old = exe + L".old";
	std::string url = ResolveUrl(siteRoot_, offer_.path);
	std::string manual = " Get it yourself: " + url;
	DeleteFileW(LongPath(part).c_str());
	HANDLE h = CreateFileW(LongPath(part).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		error = "Cannot write next to the launcher (" + Utf8(ParentDir(exe)) + "): " + Utf8(LastErrorText(GetLastError())) + "." + manual;
		return false;
	}
	Report("Downloading launcher " + offer_.version + " from " + url);
	uint64_t got = 0;
	bool writeOk = true;
	FetchResult r = Fetch(url, 0, 0, [&](const uint8_t* p, size_t n) {
		DWORD w = 0;
		if (got + n > offer_.size || !WriteFile(h, p, DWORD(n), &w, nullptr) || w != n) {
			writeOk = false;
			return false;
		}
		got += n;
		if (onProgress)
			onProgress({"Downloading launcher " + offer_.version + ": " + Mb(got) + " of " + Mb(offer_.size), double(got) / double(offer_.size)});
		return !cancel.load();
	});
	CloseHandle(h);
	std::string sha = r.ok && writeOk && got == offer_.size ? FileSha256(part) : "";
	if (sha != offer_.sha256) {
		DeleteFileW(LongPath(part).c_str());
		if (!r.ok)
			error = "Launcher download failed: " + r.error + "." + manual;
		else
			error = "The downloaded launcher does not match the release manifest (" + std::to_string(got) + " bytes, sha256 " +
			        (sha.empty() ? std::string("not checked") : sha) + "; expected " + std::to_string(offer_.size) + " bytes, " +
			        offer_.sha256 + "). Nothing was changed.";
		return false;
	}
	std::string thumb;
	bool intact = false;
	if (SignerThumbprint(part, thumb, intact))
		Log("launcher " + offer_.version + ": signed by " + thumb + (intact ? "" : " (signature not intact)"));
	else
		Log("launcher " + offer_.version + ": not signed");
	DeleteFileW(LongPath(old).c_str());
	if (!MoveFileExW(LongPath(exe).c_str(), LongPath(old).c_str(), MOVEFILE_REPLACE_EXISTING)) {  // a running exe can be renamed
		error = "Cannot replace the launcher: " + Utf8(LastErrorText(GetLastError())) + "." + manual;
		DeleteFileW(LongPath(part).c_str());
		return false;
	}
	if (!MoveFileExW(LongPath(part).c_str(), LongPath(exe).c_str(), MOVEFILE_WRITE_THROUGH)) {
		error = "Cannot replace the launcher: " + Utf8(LastErrorText(GetLastError())) + "." + manual;
		MoveFileExW(LongPath(old).c_str(), LongPath(exe).c_str(), 0);
		DeleteFileW(LongPath(part).c_str());
		return false;
	}
	Report("Launcher updated to " + offer_.version + " (sha256 checked against the release manifest)");
	return true;
}

namespace {

void RecordFile(Json& files, const std::wstring& dst, const std::string& path, const std::string& sha) {
	uint64_t size = 0, mtime = 0;
	FileSize(dst, size, &mtime);
	Json rec = Json::Object();
	rec.Set("size", size).Set("mtime", std::to_string(mtime)).Set("sha256", sha);
	files.Set(path, rec);
}

// Move `from` onto `to` (same volume): the old file stays in place until the new one is complete.
bool MoveInto(const std::wstring& from, const std::wstring& to) {
	MakeDirs(ParentDir(to));
	return MoveFileExW(LongPath(from).c_str(), LongPath(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

}  // namespace

// docs/release.md "Hosting layout": every changed file is one object, <objectsBase><sha[0:2]>/<sha>, the only way a
// release is installed. Downloads run in parallel into <game>\d2r-xl-download\<sha>.part, resume with a Range request
// (ObjectAttempts tries per object, with a growing pause; 404/403/410 end it at once), are checked (size, sha256) and
// then renamed into place. On return `fetch` holds what could not be installed; the rest stays installed.
bool Launcher::InstallObjects(const std::wstring& mod, std::vector<ManifestFile>& fetch, Json& files, std::string& error) {
	std::vector<ObjectFetch> objs = GroupObjects(fetch);
	std::wstring tmp = JoinPath(gameDir_, L"d2r-xl-download");
	if (!MakeDirs(tmp)) {
		error = "cannot create " + Utf8(tmp);
		return false;
	}
	uint64_t totalBytes = 0;
	for (const auto& o : objs)
		totalBytes += o.size;
	std::atomic<uint64_t> doneBytes{0};
	std::atomic<size_t> next{0}, doneFiles{0}, running{0};
	std::mutex mu;  // guards files, failed, firstError
	std::set<std::string> failed;
	std::string firstError;
	Report(std::to_string(fetch.size()) + " files to download (" + Mb(totalBytes) + ", " + std::to_string(objs.size()) +
	       " objects) from " + ResolveUrl(siteRoot_, feed_.objectsBase));

	auto one = [&](HttpSession& http, const ObjectFetch& o, std::string& err) -> bool {
		std::string url = ResolveUrl(siteRoot_, ObjectPath(feed_.objectsBase, o.sha256));
		std::wstring part = JoinPath(tmp, Wide(o.sha256) + L".part");
		for (int attempt = 0; attempt < ObjectAttempts; ++attempt) {
			for (int waited = 0; attempt && waited < (1000 << attempt) && !cancel.load(); waited += 100)
				Sleep(100);  // 2, 4, 8, 16 s between attempts: a dropped line often comes back
			if (cancel.load()) {
				err = "cancelled";
				return false;
			}
			uint64_t have = 0;
			if (!FileSize(part, have) || have > o.size) {
				DeleteFileW(LongPath(part).c_str());
				have = 0;
			}
			doneBytes += have;  // a resumed part counts as done
			if (have < o.size || !FileExists(part)) {
				HANDLE h = CreateFileW(LongPath(part).c_str(), FILE_APPEND_DATA, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (h == INVALID_HANDLE_VALUE) {
					err = "cannot write " + Utf8(part) + ": " + Utf8(LastErrorText(GetLastError()));
					return false;
				}
				bool writeOk = true;
				FetchResult r;
				r.ok = true;  // nothing to fetch for an empty file
				if (o.size > have)
					r = http.Fetch(url, have, o.size - have, [&](const uint8_t* p, size_t n) {
						DWORD w = 0;
						if (!WriteFile(h, p, DWORD(n), &w, nullptr) || w != n) {
							writeOk = false;
							return false;
						}
						doneBytes += n;
						return !cancel.load();
					});
				CloseHandle(h);
				if (!writeOk) {
					err = "cannot write " + Utf8(part) + " (disk full?)";
					return false;
				}
				if (!r.ok) {
					err = r.error;
					uint64_t now = 0;
					FileSize(part, now);
					doneBytes -= now;  // counted again on the retry
					if (r.status == 404 || r.status == 403 || r.status == 410 || cancel.load())
						return false;
					continue;
				}
			}
			uint64_t size = 0;
			FileSize(part, size);
			if (size != o.size || FileSha256(part) != o.sha256) {
				err = "object " + o.sha256 + " does not match the release manifest (size or sha256)";
				DeleteFileW(LongPath(part).c_str());
				doneBytes -= size;
				continue;
			}
			// one object, several paths: copy to all but the last, then move the part file itself
			for (size_t i = 0; i < o.paths.size(); ++i) {
				std::wstring dst = JoinPath(mod, Wide(o.paths[i]));
				bool ok;
				if (i + 1 < o.paths.size()) {
					std::wstring t = dst + L".mxlpart";
					MakeDirs(ParentDir(dst));
					ok = CopyFileW(LongPath(part).c_str(), LongPath(t).c_str(), FALSE) && MoveInto(t, dst);
				} else {
					ok = MoveInto(part, dst);
				}
				if (!ok) {
					err = "cannot write " + Utf8(dst) + ": " + Utf8(LastErrorText(GetLastError()));
					return false;
				}
				std::lock_guard<std::mutex> lock(mu);
				RecordFile(files, dst, o.paths[i], o.sha256);
			}
			doneFiles += o.paths.size();
			return true;
		}
		return false;
	};

	size_t nWorkers = std::min<size_t>(8, objs.size());
	running = nWorkers;
	std::vector<std::thread> workers;
	for (size_t w = 0; w < nWorkers; ++w)
		workers.emplace_back([&] {
			HttpSession http;
			for (;;) {
				size_t i = next++;
				if (i >= objs.size() || cancel.load())
					break;
				std::string err;
				if (!one(http, objs[i], err)) {
					std::lock_guard<std::mutex> lock(mu);
					failed.insert(objs[i].sha256);
					if (firstError.empty())
						firstError = err;
				}
			}
			--running;
		});
	ULONGLONG lastSave = GetTickCount64(), lastShow = 0;
	while (running.load()) {
		Sleep(100);
		ULONGLONG now = GetTickCount64();
		if (onProgress && now - lastShow >= 500) {
			lastShow = now;
			onProgress({"Downloading D2R-XL " + feed_.version + ": " + std::to_string(doneFiles.load()) + " of " +
			                std::to_string(fetch.size()) + " files (" + Mb(doneBytes.load()) + " of " + Mb(totalBytes) + ")",
			            totalBytes ? double(doneBytes.load()) / double(totalBytes) : -1});
		}
		if (now - lastSave > 5000) {  // resume point: what is installed so far
			lastSave = now;
			std::lock_guard<std::mutex> lock(mu);
			state_.Set("mod_files", files);
			SaveState();
		}
	}
	for (auto& t : workers)
		t.join();
	std::vector<ManifestFile> left;
	for (const auto& f : fetch)
		if (failed.count(f.sha256) || files[f.path]["sha256"].Str() != f.sha256)
			left.push_back(f);
	size_t installed = fetch.size() - left.size();
	fetch.swap(left);
	if (fetch.empty()) {
		std::string ignore;
		RemoveTree(tmp, ignore);  // leftover parts of objects no longer needed
		Report("Downloaded " + std::to_string(objs.size()) + " objects (" + Mb(totalBytes) + "), installed " + std::to_string(installed) +
		       " files of D2R-XL " + feed_.version);
		return true;
	}
	error = cancel.load() ? std::string("cancelled") : std::to_string(failed.size()) + " downloads failed, first: " + firstError;
	return false;
}

bool Launcher::EnsureMod(bool verify, std::string& error) {
	if (!feedChecked_ && !CheckFeed(error))
		return false;
	std::wstring mod = ModDir();
	// d2r.imageTimestamp is not compared here: it is the TimeDateStamp of the unpacked image in memory (what the
	// plugin's build guard reads); the D2R.exe file of build 93847 carries another one (0x6a6b96a4)
	Json files = state_["mod_files"].IsObject() ? state_["mod_files"] : Json::Object();
	uint64_t hashed = 0;
	Report(verify ? "Checking every installed D2R-XL file ..." : "Comparing the installed files with release " + feed_.version + " ...");
	auto current = [&](const std::string& path) -> std::string {
		std::wstring p = JoinPath(mod, Wide(path));
		uint64_t size = 0, mtime = 0;
		if (!FileSize(p, size, &mtime))
			return "";
		const Json& r = files[path];
		if (!verify && r["size"].Int(-1) == int64_t(size) && r["mtime"].Str() == std::to_string(mtime) && r["sha256"].IsString())
			return r["sha256"].Str();
		hashed += size;
		std::string sha = FileSha256(p);
		Json rec = Json::Object();
		rec.Set("size", size).Set("mtime", std::to_string(mtime)).Set("sha256", sha);
		files.Set(path, rec);
		return sha;
	};
	InstallPlan plan = PlanInstall(feed_, ListFilesUnder(mod), current);
	for (const auto& k : plan.preserved)
		Report("Kept your " + k);
	for (const auto& r : plan.remove) {  // not part of the release (docs/release.md "files")
		DeleteFileW(LongPath(JoinPath(mod, Wide(r))).c_str());
		files.Set(r, Json());
	}
	if (!plan.remove.empty())
		Report("Removed " + std::to_string(plan.remove.size()) + " files that are not part of D2R-XL " + feed_.version);
	if (!plan.fetch.empty()) {
		std::vector<ManifestFile> left = plan.fetch;
		if (!InstallObjects(mod, left, files, error)) {  // the objects are the only way in (docs/release.md)
			state_.Set("mod_files", files);
			SaveState();
			if (cancel.load())
				error = "Stopped. Update again to continue where it stopped.";
			else
				error = "Download failed: " + std::to_string(left.size()) + " of " + std::to_string(plan.fetch.size()) +
				        " files could not be downloaded (" + error + "). Check your connection and try again: the " +
				        std::to_string(plan.fetch.size() - left.size()) + " files already downloaded are kept.";
			return false;
		}
	} else {
		Report("D2R-XL " + feed_.version + " is installed and up to date" + (hashed ? " (" + Mb(hashed) + " checked)" : ""));
	}
	for (const auto& d : feed_.emptyDirs)  // folders the release has without files (d2rloader/patches)
		if (!MakeDirs(JoinPath(mod, Wide(d)))) {
			error = "Cannot create " + Utf8(JoinPath(mod, Wide(d)));
			return false;
		}
	Json clean = Json::Object();  // drop records of files that are gone
	for (const auto& [k, v] : files.members())
		if (v.IsObject())
			clean.Set(k, v);
	state_.Set("mod_files", clean);
	state_.Set("mod", ManifestToJson(feed_));
	SaveState();
	return WriteLaunchConfig(error);
}

// ---------------------------------------------------------------- actions

bool Launcher::InstallOrUpdate(std::string& error) {
	cancel = false;
	if (!Prepare(error) || !CheckFeed(error))
		return false;
	if (!ProcessesUnder(gameDir_).empty()) {
		error = "D2R-XL is running. Close the game first.";
		return false;
	}
	return EnsureCopy(false, error) && EnsureLoader(false, error) && EnsureMod(false, error);
}

bool Launcher::Repair(std::string& error) {
	cancel = false;
	if (!Prepare(error) || !CheckFeed(error))
		return false;
	if (!ProcessesUnder(gameDir_).empty()) {
		error = "D2R-XL is running. Close the game first.";
		return false;
	}
	return EnsureCopy(true, error) && EnsureLoader(true, error) && EnsureMod(true, error);
}

bool Launcher::CanPlay(std::string* why) {
	std::string e;
	if (gameDir_.empty() && !Prepare(e)) {
		if (why) *why = e;
		return false;
	}
	if (!state_["copy_complete"].Bool() || !LooksLikeD2R(gameDir_)) {
		if (why) *why = "D2R-XL is not installed yet. Press Update.";
		return false;
	}
	if (!FileExists(JoinPath(gameDir_, L"D2RLoader.exe")) || !state_["mod"].IsObject()) {
		if (why) *why = "The installation is incomplete. Press Update or Repair.";
		return false;
	}
	return true;
}

namespace {
struct FindCtx {
	std::wstring dir;
	HWND found = nullptr;
};
BOOL CALLBACK FindGameWindow(HWND h, LPARAM lp) {
	auto ctx = reinterpret_cast<FindCtx*>(lp);
	if (!IsWindowVisible(h))
		return TRUE;
	wchar_t title[128];
	GetWindowTextW(h, title, int(std::size(title)));
	if (std::wstring(title).rfind(L"Diablo II", 0) != 0)
		return TRUE;
	DWORD pid = 0;
	GetWindowThreadProcessId(h, &pid);
	HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!p)
		return TRUE;
	wchar_t img[MAX_PATH * 2];
	DWORD n = DWORD(std::size(img));
	bool ours = QueryFullProcessImageNameW(p, 0, img, &n) && PathUnder(std::wstring(img, n), ctx->dir);
	CloseHandle(p);
	if (ours) {
		ctx->found = h;
		return FALSE;
	}
	return TRUE;
}
}  // namespace

bool Launcher::Play(std::string& error) {
	cancel = false;
	if (!Prepare(error))
		return false;
	std::string why;
	if (!CanPlay(&why)) {
		error = why;
		return false;
	}
	if (!ProcessesUnder(gameDir_).empty()) {
		error = "D2R-XL is already running.";
		return false;
	}
	// D2R's options (graphics, sound) live in one Settings.json for every install; keep the player's own once
	std::wstring settings = JoinPath(KnownFolder(1), L"Diablo II Resurrected\\Settings.json");
	std::wstring backup = JoinPath(opt_.dataDir, L"backup\\Settings.json");
	if (FileExists(settings) && !FileExists(backup)) {
		MakeDirs(ParentDir(backup));
		if (CopyFileW(settings.c_str(), backup.c_str(), TRUE))
			Report("Saved a copy of your D2R settings to " + Utf8(backup));
	}
	std::wstring cookie = JoinPath(KnownFolder(0), L"Blizzard Entertainment\\ClientSdk\\cookie.bin");
	uint64_t csize = 0, ctime = 0;
	if (!FileSize(cookie, csize, &ctime)) {
		Report("Note: no Battle.net login record found. If D2R says it has not been online for 30 days, start D2R once from Battle.net while online.");
	} else {
		FILETIME now;
		GetSystemTimeAsFileTime(&now);
		uint64_t n = (uint64_t(now.dwHighDateTime) << 32) | now.dwLowDateTime;
		double days = double(n - ctime) / (10000000.0 * 86400.0);
		if (days > 25)
			Report("Note: D2R was last started online " + std::to_string(int(days)) +
			       " days ago. D2R asks for an online start every 30 days: start it once from Battle.net.");
	}
	std::wstring exe = JoinPath(gameDir_, L"D2RLoader.exe");
	if (!WriteLaunchConfig(error))  // the mod and its arguments come from the copy's d2rloader.toml
		return false;
	std::wstring cmd = Quote(exe);
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	if (opt_.background) {
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_SHOWNOACTIVATE;
	}
	PROCESS_INFORMATION pi{};
	HWND prevFg = GetForegroundWindow();
	Report("Starting " + Utf8(cmd));
	if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, gameDir_.c_str(), &si, &pi)) {
		error = "Cannot start D2RLoader: " + Utf8(LastErrorText(GetLastError()));
		return false;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	// The loader restarts itself and D2R takes the foreground when its window appears. In background mode (tests)
	// the window goes to the back once and the foreground is handed back, exactly as tools/game_ctl.py does.
	ULONGLONG t0 = GetTickCount64();
	bool sentBack = !opt_.background;
	while (GetTickCount64() - t0 < 30000) {
		if (!sentBack) {
			FindCtx ctx{gameDir_};
			EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&ctx));
			if (ctx.found) {
				SetWindowPos(ctx.found, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
				if (prevFg && GetForegroundWindow() == ctx.found)
					SetForegroundWindow(prevFg);
				sentBack = true;
			}
		}
		Sleep(50);
	}
	if (ProcessesUnder(gameDir_).empty()) {
		error = "The game closed right after starting. Logs: " + Utf8(JoinPath(gameDir_, L"d2rloader\\logs")) +
		        ". If D2R is installed on another drive than the D2R-XL folder, move the D2R-XL folder to that drive.";
		return false;
	}
	Report("D2R-XL is running.");
	return true;
}

bool Launcher::Uninstall(std::string& error) {
	cancel = false;
	std::wstring dir = !opt_.gameDir.empty() ? opt_.gameDir : Wide(state_["game_dir"].Str());
	gameDir_ = dir;
	std::wstring saves = SavesDir();  // from the mod's modinfo.json, before it goes
	if (!dir.empty() && !ProcessesUnder(dir).empty()) {
		error = "D2R-XL is running. Close the game first.";
		return false;
	}
	if (!dir.empty() && DirExists(dir)) {
		std::wstring marker = JoinPath(dir, Wide(MarkerName));
		std::string text, err;
		Json m;
		if (!ReadWholeFile(marker, text) || !Json::Parse(text, m, err) || m["made_by"].Str() != "D2R-XL launcher") {
			error = "Not removing " + Utf8(dir) + ": it has no D2R-XL launcher marker, so this launcher did not make it.";
			return false;
		}
		if (!state_["copy_id"].Str().empty() && m["id"].Str() != state_["copy_id"].Str()) {
			error = "Not removing " + Utf8(dir) + ": its marker belongs to another installation.";
			return false;
		}
		if (!state_["source"].Str().empty() && (PathUnder(dir, Wide(state_["source"].Str())) || PathUnder(Wide(state_["source"].Str()), dir))) {
			error = "Not removing " + Utf8(dir) + ": it overlaps the D2R install.";
			return false;
		}
		Report("Removing the D2R-XL game copy " + Utf8(dir) + " ...");
		if (!RemoveTree(dir, error))
			return false;
		std::wstring parent = ParentDir(dir);
		if (!SamePath(parent, opt_.dataDir) && JoinPath(ParentDir(parent), L"D2R-XL") == parent)
			RemoveDirectoryW(parent.c_str());  // <drive>\D2R-XL when empty
	}
	std::string e2;
	std::wstring cache = JoinPath(opt_.dataDir, L"cache");
	if (DirExists(cache))
		RemoveTree(cache, e2);
	DeleteFileW(statePath_.c_str());
	Report("D2R-XL is uninstalled. Your characters stay in " + Utf8(saves) + "; your D2R settings backup (if any) is in " +
	       Utf8(JoinPath(opt_.dataDir, L"backup")) + ".");
	state_ = Json::Object();
	return true;
}

std::string Launcher::StatusText() {
	std::string e, out;
	if (!Prepare(e))
		return e;
	out = "D2R: " + Utf8(source_.dir) + " (" + source_.source + ", " + source_.build.message + ")\n";
	if (state_["copy_complete"].Bool())
		out += "D2R-XL folder: " + Utf8(gameDir_) + " (build " + std::to_string(state_["copy_build"].Int()) + ")\n";
	else
		out += "D2R-XL folder: " + Utf8(gameDir_) + " (not made yet: needs about " +
		       Gb(state_["copy_bytes"].Int(30ll << 30) + SpareBytes) + ")\n";
	std::string installed = state_["mod"]["version"].Str();
	out += "D2R-XL: " + (installed.empty() ? std::string("not installed") : installed);
	if (feedChecked_ && !installed.empty() && feed_.version != installed)
		out += " (update " + feed_.version + " available)";
	else if (feedChecked_ && !installed.empty())
		out += " (latest)";
	out += "\nD2RLoader: " + state_["loader"].Str("not installed");
	return out;
}

}  // namespace mxl

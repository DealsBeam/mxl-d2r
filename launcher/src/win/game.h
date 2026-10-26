// D2R on this PC: finding installs (Battle.net uninstall entry, Steam libraries, default folders), the build check,
// volumes, the private copy and the game processes.
#pragma once

#include "../core/detect.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mxl {

struct D2RInstall {
	std::wstring dir;
	std::string source;  // "Battle.net", "Steam", "default folder", "chosen"
	std::string infoVersion, exeVersion;
	BuildCheck build;
};

bool LooksLikeD2R(const std::wstring& dir);
D2RInstall InspectD2R(const std::wstring& dir, const std::string& source);
// Every install found (duplicates removed), best first: a build-93847 Battle.net install, then other matches.
std::vector<D2RInstall> FindD2RInstalls();

std::string ExeFileVersion(const std::wstring& exe);
// SHA-1 thumbprint (upper-case hex) of the Authenticode signer of a file. `intact` is false when the signature is
// missing or the file was changed after signing; a self-signed certificate (D2RLoader's) counts as intact.
bool SignerThumbprint(const std::wstring& file, std::string& thumbprint, bool& intact);
// Every file under `dir`, relative with '/' separators (no reparse points followed).
std::vector<std::string> ListFilesUnder(const std::wstring& dir);

// Serial of the volume holding `path` (0 when unknown) and its root ("C:\").
uint32_t VolumeSerial(const std::wstring& path, std::wstring* root = nullptr);
bool FreeBytes(const std::wstring& path, uint64_t& free);
std::string FileSystemName(const std::wstring& path);  // "NTFS", "ReFS"

struct CopyProgress {
	uint64_t doneBytes = 0, totalBytes = 0;
	uint64_t doneFiles = 0, totalFiles = 0;
	uint64_t skippedBytes = 0;  // already there from an earlier run (resume)
	std::wstring current;
};

struct CopyList {
	struct Item {
		std::wstring rel;
		uint64_t size, mtime;
	};
	std::vector<Item> files;
	std::vector<std::wstring> dirs;
	uint64_t bytes = 0;
};
// Files of `src` to copy (ExcludedFromCopy filtered out; reparse points are not followed).
bool ListGameFiles(const std::wstring& src, CopyList& out, std::string& error);

// Copy `list` from src to dst. A file whose size and write time already match is skipped (resume); others go to
// "<name>.mxlpart" first and are renamed when complete. CopyFile2 block-clones on ReFS / Dev Drive by itself.
bool CopyGameFiles(const std::wstring& src, const std::wstring& dst, const CopyList& list, std::atomic<bool>& cancel,
                   const std::function<void(const CopyProgress&)>& progress, std::string& error);

// Remove a folder tree. Junctions and symlinks inside are unlinked, never followed.
bool RemoveTree(const std::wstring& dir, std::string& error);

struct GameProcess {
	unsigned long pid;
	std::wstring exe;
};
// Processes whose image lies inside `dir` (the copy): D2RLoader.exe and the D2R it starts.
std::vector<GameProcess> ProcessesUnder(const std::wstring& dir);

}  // namespace mxl

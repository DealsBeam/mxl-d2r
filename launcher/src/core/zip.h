// Zip directory reader. The launcher reads the central directory of a release zip (local file or HTTP ranges) and
// then fetches only the byte ranges of the entries it needs (docs/launcher.md "Updates").
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mxl {

struct ZipEntry {
	std::string name;  // as stored, '/' separators
	uint16_t method = 0;  // 0 stored, 8 deflate
	uint16_t flags = 0;
	uint32_t crc = 0;
	uint64_t compSize = 0;
	uint64_t size = 0;
	uint64_t offset = 0;  // local header offset
	bool dir = false;
};

struct ZipTail {
	uint64_t cdOffset = 0;
	uint64_t cdSize = 0;
	uint64_t entries = 0;
};

// Find the end-of-central-directory record (zip64 too) in the last bytes of the file. `tail` holds the file's last
// tail.size() bytes, `fileSize` the whole size. Needs the zip64 locator inside `tail` when there is one.
bool FindZipTail(const std::vector<uint8_t>& tail, uint64_t fileSize, ZipTail& out, std::string& error);
bool ParseCentralDirectory(const uint8_t* cd, size_t size, uint64_t expectEntries, std::vector<ZipEntry>& out, std::string& error);

// Bytes of the local header (30 + name + extra) at the start of `p`; 0 when it is no local header.
size_t LocalHeaderSize(const uint8_t* p, size_t avail);

// Ranges to download for `wanted` entries (indices into `all`, which must be sorted by offset): entries next to each
// other in the zip are merged, and so are runs with less than `gap` bytes between them.
struct ByteRange {
	uint64_t begin = 0, end = 0;  // [begin, end)
	std::vector<size_t> entries;  // indices into `all`, in offset order
};
std::vector<ByteRange> PlanRanges(const std::vector<ZipEntry>& all, const std::vector<size_t>& wanted, uint64_t cdOffset,
                                  uint64_t gap);

}  // namespace mxl

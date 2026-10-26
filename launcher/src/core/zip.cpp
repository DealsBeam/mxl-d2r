#include "zip.h"

#include <algorithm>

namespace mxl {
namespace {
uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
uint64_t U64(const uint8_t* p) { return uint64_t(U32(p)) | (uint64_t(U32(p + 4)) << 32); }
}  // namespace

bool FindZipTail(const std::vector<uint8_t>& tail, uint64_t fileSize, ZipTail& out, std::string& error) {
	if (tail.size() < 22) {
		error = "file too small for a zip";
		return false;
	}
	for (size_t i = tail.size() - 22 + 1; i-- > 0;) {
		const uint8_t* p = tail.data() + i;
		if (U32(p) != 0x06054b50)
			continue;
		if (i + 22 + U16(p + 20) != tail.size())
			continue;  // the comment must run to the end of the file
		out.entries = U16(p + 10);
		out.cdSize = U32(p + 12);
		out.cdOffset = U32(p + 16);
		if ((out.entries == 0xffff || out.cdSize == 0xffffffff || out.cdOffset == 0xffffffff) && i >= 20 &&
		    U32(p - 20) == 0x07064b50) {
			uint64_t z64 = U64(p - 20 + 8);  // offset of the zip64 end record
			uint64_t tailStart = fileSize - tail.size();
			if (z64 < tailStart || z64 - tailStart + 56 > tail.size()) {
				error = "zip64 end record outside the read tail";
				return false;
			}
			const uint8_t* r = tail.data() + (z64 - tailStart);
			if (U32(r) != 0x06064b50) {
				error = "bad zip64 end record";
				return false;
			}
			out.entries = U64(r + 32);
			out.cdSize = U64(r + 40);
			out.cdOffset = U64(r + 48);
		}
		if (out.cdOffset + out.cdSize > fileSize) {
			error = "central directory outside the file";
			return false;
		}
		return true;
	}
	error = "no zip end record (not a zip, or a comment longer than the read tail)";
	return false;
}

bool ParseCentralDirectory(const uint8_t* cd, size_t size, uint64_t expectEntries, std::vector<ZipEntry>& out, std::string& error) {
	out.clear();
	size_t i = 0;
	while (i + 46 <= size && U32(cd + i) == 0x02014b50) {
		const uint8_t* p = cd + i;
		ZipEntry e;
		e.flags = U16(p + 8);
		e.method = U16(p + 10);
		e.crc = U32(p + 16);
		e.compSize = U32(p + 20);
		e.size = U32(p + 24);
		size_t nameLen = U16(p + 28), extraLen = U16(p + 30), commentLen = U16(p + 32);
		e.offset = U32(p + 42);
		if (i + 46 + nameLen + extraLen + commentLen > size) {
			error = "truncated central directory";
			return false;
		}
		e.name.assign(reinterpret_cast<const char*>(p + 46), nameLen);
		const uint8_t* x = p + 46 + nameLen;
		for (size_t k = 0; k + 4 <= extraLen;) {
			uint16_t id = U16(x + k), len = U16(x + k + 2);
			if (k + 4 + len > extraLen)
				break;
			if (id == 0x0001) {  // zip64: only the fields that are 0xffffffff in the record, in this order
				const uint8_t* z = x + k + 4;
				size_t left = len;
				if (e.size == 0xffffffff && left >= 8) { e.size = U64(z); z += 8; left -= 8; }
				if (e.compSize == 0xffffffff && left >= 8) { e.compSize = U64(z); z += 8; left -= 8; }
				if (e.offset == 0xffffffff && left >= 8) { e.offset = U64(z); }
			}
			k += 4 + len;
		}
		e.dir = !e.name.empty() && e.name.back() == '/';
		out.push_back(std::move(e));
		i += 46 + nameLen + extraLen + commentLen;
	}
	if (out.size() != expectEntries) {
		error = "central directory holds " + std::to_string(out.size()) + " entries, the end record says " + std::to_string(expectEntries);
		return false;
	}
	return true;
}

size_t LocalHeaderSize(const uint8_t* p, size_t avail) {
	if (avail < 30 || U32(p) != 0x04034b50)
		return 0;
	return 30 + size_t(U16(p + 26)) + U16(p + 28);
}

}  // namespace mxl

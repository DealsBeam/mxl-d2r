// Raw DEFLATE decoder (RFC 1951) for zip entries: whole input in, whole output out (the size is known from the zip
// directory). The largest file of a release is about 40 MB, so no streaming is needed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mxl {

// Decode `in` into exactly `outSize` bytes. False on corrupt data or a size mismatch.
bool Inflate(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t outSize);

uint32_t Crc32(const void* data, size_t size, uint32_t crc = 0);

}  // namespace mxl

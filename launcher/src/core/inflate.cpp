#include "inflate.h"

#include <cstring>

namespace mxl {
namespace {

constexpr int MaxBits = 15;

struct Huffman {
	uint16_t count[MaxBits + 1];
	uint16_t symbol[320];
};

struct Bits {
	const uint8_t* in;
	size_t size;
	size_t pos = 0;
	uint32_t buf = 0;
	int have = 0;
	bool bad = false;

	uint32_t Get(int n) {
		while (have < n) {
			if (pos >= size) {
				bad = true;
				return 0;
			}
			buf |= uint32_t(in[pos++]) << have;
			have += 8;
		}
		uint32_t v = buf & ((1u << n) - 1);
		buf >>= n;
		have -= n;
		return v;
	}
};

// Canonical code from code lengths; false when the set is over-subscribed. An incomplete set is allowed (a single
// distance code is legal), decoding then fails only if an unused code shows up.
bool Build(Huffman& h, const uint8_t* lengths, int n) {
	std::memset(h.count, 0, sizeof(h.count));
	for (int s = 0; s < n; ++s)
		h.count[lengths[s]]++;
	if (h.count[0] == n)
		return true;
	int left = 1;
	for (int len = 1; len <= MaxBits; ++len) {
		left <<= 1;
		left -= h.count[len];
		if (left < 0)
			return false;
	}
	uint16_t offs[MaxBits + 1];
	offs[1] = 0;
	for (int len = 1; len < MaxBits; ++len)
		offs[len + 1] = uint16_t(offs[len] + h.count[len]);
	for (int s = 0; s < n; ++s)
		if (lengths[s])
			h.symbol[offs[lengths[s]]++] = uint16_t(s);
	return true;
}

int Decode(Bits& b, const Huffman& h) {
	int code = 0, first = 0, index = 0;
	for (int len = 1; len <= MaxBits; ++len) {
		code |= int(b.Get(1));
		if (b.bad)
			return -1;
		int count = h.count[len];
		if (code - count < first)
			return h.symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

const uint16_t LenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t LenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t DistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t DistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool Codes(Bits& b, std::vector<uint8_t>& out, size_t limit, const Huffman& lit, const Huffman& dist) {
	for (;;) {
		int sym = Decode(b, lit);
		if (sym < 0)
			return false;
		if (sym < 256) {
			if (out.size() >= limit)
				return false;
			out.push_back(uint8_t(sym));
		} else if (sym == 256) {
			return true;
		} else {
			sym -= 257;
			if (sym >= 29)
				return false;
			size_t len = LenBase[sym] + b.Get(LenExtra[sym]);
			int ds = Decode(b, dist);
			if (ds < 0 || ds >= 30)
				return false;
			size_t d = DistBase[ds] + b.Get(DistExtra[ds]);
			if (b.bad || d > out.size() || out.size() + len > limit)
				return false;
			size_t from = out.size() - d;
			for (size_t k = 0; k < len; ++k)
				out.push_back(out[from + k]);
		}
	}
}

bool FixedTables(Huffman& lit, Huffman& dist) {
	uint8_t l[288];
	int s = 0;
	for (; s < 144; ++s) l[s] = 8;
	for (; s < 256; ++s) l[s] = 9;
	for (; s < 280; ++s) l[s] = 7;
	for (; s < 288; ++s) l[s] = 8;
	uint8_t d[30];
	for (auto& x : d) x = 5;
	return Build(lit, l, 288) && Build(dist, d, 30);
}

bool DynamicTables(Bits& b, Huffman& lit, Huffman& dist) {
	int nlen = int(b.Get(5)) + 257, ndist = int(b.Get(5)) + 1, ncode = int(b.Get(4)) + 4;
	if (b.bad || nlen > 286 || ndist > 30)
		return false;
	static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
	uint8_t lengths[320] = {};
	for (int i = 0; i < ncode; ++i)
		lengths[order[i]] = uint8_t(b.Get(3));
	Huffman lencode;
	if (b.bad || !Build(lencode, lengths, 19))
		return false;
	int idx = 0;
	while (idx < nlen + ndist) {
		int sym = Decode(b, lencode);
		if (sym < 0)
			return false;
		if (sym < 16) {
			lengths[idx++] = uint8_t(sym);
			continue;
		}
		uint8_t len = 0;
		int rep;
		if (sym == 16) {
			if (idx == 0)
				return false;
			len = lengths[idx - 1];
			rep = 3 + int(b.Get(2));
		} else if (sym == 17) {
			rep = 3 + int(b.Get(3));
		} else {
			rep = 11 + int(b.Get(7));
		}
		if (b.bad || idx + rep > nlen + ndist)
			return false;
		while (rep--)
			lengths[idx++] = len;
	}
	if (lengths[256] == 0)
		return false;  // no end-of-block code
	return Build(lit, lengths, nlen) && Build(dist, lengths + nlen, ndist);
}

}  // namespace

bool Inflate(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t outSize) {
	out.clear();
	out.reserve(outSize);
	Bits b{in, inSize};
	int last;
	do {
		last = int(b.Get(1));
		int type = int(b.Get(2));
		if (b.bad)
			return false;
		if (type == 0) {
			b.buf = 0;
			b.have = 0;  // stored block: skip to the byte boundary
			if (b.pos + 4 > inSize)
				return false;
			unsigned len = in[b.pos] | (in[b.pos + 1] << 8);
			unsigned nlen = in[b.pos + 2] | (in[b.pos + 3] << 8);
			b.pos += 4;
			if (len != (~nlen & 0xffff) || b.pos + len > inSize || out.size() + len > outSize)
				return false;
			out.insert(out.end(), in + b.pos, in + b.pos + len);
			b.pos += len;
		} else if (type == 1 || type == 2) {
			Huffman lit, dist;
			if (!(type == 1 ? FixedTables(lit, dist) : DynamicTables(b, lit, dist)))
				return false;
			if (!Codes(b, out, outSize, lit, dist))
				return false;
		} else {
			return false;
		}
	} while (!last);
	return out.size() == outSize;
}

uint32_t Crc32(const void* data, size_t size, uint32_t crc) {
	struct Table {
		uint32_t t[256];
		Table() {
			for (uint32_t i = 0; i < 256; ++i) {
				uint32_t c = i;
				for (int k = 0; k < 8; ++k)
					c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
				t[i] = c;
			}
		}
	};
	static const Table table;  // thread-safe static init
	auto p = static_cast<const uint8_t*>(data);
	crc = ~crc;
	for (size_t i = 0; i < size; ++i)
		crc = table.t[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
	return ~crc;
}

}  // namespace mxl

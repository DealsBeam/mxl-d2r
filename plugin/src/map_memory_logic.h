// Pure part of the per-level map memory (map_memory.cpp, docs/subsystems/areas-drlg.md section 6; no game or Windows headers,
// unit-tested by tests/map_memory_test.cpp).
//
// D2R keeps the explored rooms of a character in `<char>.ma0..3` (one file per game, `.map` is the index). A file starts with
// a table of 100 offsets (400 bytes), one chain head per automap layer (Levels.txt `Layer`), then the nodes, appended at the
// end of the file every time a level is left:
//
//   +0   next node of the chain (0 = last)
//   +4   layer
//   +8   word 1 (the layer record's second word)       +12  word 2 (a tag of the game; the third blob is only read when it matches)
//   +16  size of blob 1   +20 size of blob 2   +24 size of blob 3   +28 size of blob 4
//   +32  the four blobs, back to back (rooms as 16-bit triples)
//
// D2R indexes the table by the layer itself, on the stack (`local_1d8[layer]`, 0xd5710 / 0xd5fe0): Median's layers go up to
// 279, so a layer of 100 or more read and wrote past the array. Layers keep their file format here: the table index is the
// layer modulo 100, and since every node names its layer, a chain can hold nodes of several layers (layer 144 shares the chain
// of layer 44). The loader walks a chain and cuts it at the first node of another layer (a corrupt file in D2R's eyes), so it
// is shown a view of the file in which the chain holds only the nodes of the layer it loads, renamed to the table index.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace mapmem {

constexpr int Buckets = 100;
constexpr uint32_t TableBytes = Buckets * 4;
constexpr uint32_t NodeHeader = 32;
constexpr uint32_t NodeLayerAt = 4;
constexpr uint32_t NodeSizesAt = 16;
constexpr int Blobs = 4;
constexpr uint32_t MaxFile = 256u << 20;  // a file of this size or more is not read (corrupt or foreign)

// The table index of a layer; -1 for a layer that cannot be stored.
inline auto Bucket(int32_t layer) noexcept -> int {
	return layer < 0 ? -1 : static_cast<int>(layer % Buckets);
}

inline auto Get32(const uint8_t* p) noexcept -> uint32_t {
	uint32_t v;
	std::memcpy(&v, p, 4);
	return v;
}

inline void Put32(uint8_t* p, uint32_t v) noexcept {
	std::memcpy(p, &v, 4);
}

struct Node {
	uint32_t off = 0;
	uint32_t next = 0;
	int32_t layer = 0;
	uint32_t size[Blobs] = {};
	uint32_t end = 0;
};

// A node whose header and blobs lie inside the file (offset past the table).
inline auto ParseNode(const uint8_t* img, uint32_t size, uint32_t off, Node* node) noexcept -> bool {
	if (off < TableBytes || off > size || size - off < NodeHeader) {
		return false;
	}
	node->off = off;
	node->next = Get32(img + off);
	node->layer = static_cast<int32_t>(Get32(img + off + NodeLayerAt));
	uint64_t total = NodeHeader;
	for (int k = 0; k < Blobs; ++k) {
		node->size[k] = Get32(img + off + NodeSizesAt + 4 * k);
		total += node->size[k];
	}
	if (off + total > size) {
		return false;
	}
	node->end = static_cast<uint32_t>(off + total);
	return true;
}

struct Chain {
	std::vector<Node> nodes;
	bool clean = true;  // ends at a next of 0; false: a node outside the file, or a loop (D2R's own cut writes the node's offset into itself)
};

// The chain of one table entry, up to the first node that is outside the file or was seen before (a loop).
inline auto Walk(const uint8_t* img, uint32_t size, int bucket) noexcept -> Chain {
	Chain chain;
	if (bucket < 0 || bucket >= Buckets || size < TableBytes) {
		chain.clean = false;
		return chain;
	}
	std::unordered_set<uint32_t> seen;
	uint32_t cur = Get32(img + 4 * bucket);
	while (cur != 0) {
		Node node;
		if (!seen.insert(cur).second || !ParseNode(img, size, cur, &node)) {
			chain.clean = false;
			break;
		}
		chain.nodes.push_back(node);
		cur = node.next;
	}
	return chain;
}

// What the loader should see when it loads `layer`. Returns false when the file itself will do (a layer below 100 whose chain is
// clean and holds only that layer, which is every file D2R wrote); else `view` is a copy in which the table entry and the next
// links go through the nodes of `layer` only, and those nodes name the table index as their layer (what the loader compares
// with, the caller sets the layer record to the table index while it runs). Offsets are unchanged.
inline auto LoadView(const std::vector<uint8_t>& img, int32_t layer, std::vector<uint8_t>* view) noexcept -> bool {
	const int bucket = Bucket(layer);
	if (bucket < 0 || img.size() < TableBytes || img.size() >= MaxFile) {
		return false;
	}
	const uint32_t size = static_cast<uint32_t>(img.size());
	const Chain chain = Walk(img.data(), size, bucket);
	bool foreign = false;
	for (const Node& n : chain.nodes) {
		foreign = foreign || n.layer != layer;
	}
	if (layer < Buckets && chain.clean && !foreign) {
		return false;
	}
	*view = img;
	uint8_t* v = view->data();
	std::vector<const Node*> kept;
	for (const Node& n : chain.nodes) {
		if (n.layer == layer) {
			kept.push_back(&n);
		}
	}
	Put32(v + 4 * bucket, kept.empty() ? 0 : kept.front()->off);
	for (size_t i = 0; i < kept.size(); ++i) {
		Put32(v + kept[i]->off, i + 1 < kept.size() ? kept[i + 1]->off : 0);
		Put32(v + kept[i]->off + NodeLayerAt, static_cast<uint32_t>(bucket));
	}
	return true;
}

// One node to append (what D2R's save routine 0xd5fe0 gets as its record: seven words, then the four blobs).
struct Record {
	int32_t word[7] = {};              // [0] layer, [1], [2], [3..6] the blob sizes
	const uint8_t* blob[Blobs] = {};   // blob k has word[3 + k] bytes; read only when that is not 0
};

enum class SaveResult { Appended, Skipped, ChainReset };

// Appends the node to the file through `f`:
//   uint32_t Size()                                                  the file's size
//   uint32_t Read(void* dst, uint32_t len, uint32_t off)             bytes read (0 past the end)
//   void Write(const void* src, uint32_t len, uint32_t off)          writes at off, the file grows to fit
//   void Replace(const void* src, uint32_t len)                      the file becomes these bytes
// Same steps as the original (a missing table is created, the node goes to the end of the file, the previous last node of
// the chain links to it, the table is rewritten), except that the table index is the layer modulo 100, and that a chain which
// loops or leaves the file (D2R follows it for ever then, bug #24) is dropped and started again.
template <class File>
auto AppendNode(File& f, const Record& rec) -> SaveResult {
	const int bucket = Bucket(rec.word[0]);
	if (bucket < 0) {
		return SaveResult::Skipped;
	}
	uint32_t table[Buckets];
	std::memset(table, 0, sizeof(table));
	if (f.Read(table, TableBytes, 0) != TableBytes) {
		std::memset(table, 0, sizeof(table));
		f.Replace(table, TableBytes);
	}
	const uint32_t newOff = f.Size();
	SaveResult result = SaveResult::Appended;
	if (table[bucket] == 0) {
		table[bucket] = newOff;
	} else {
		const uint32_t limit = newOff / NodeHeader + 1;
		uint32_t cur = table[bucket];
		uint32_t last = 0;
		bool ok = true;
		for (uint32_t steps = 0; cur != 0; ++steps) {
			if (steps > limit || cur < TableBytes || cur >= newOff) {
				ok = false;
				break;
			}
			last = cur;
			uint32_t next = 0;
			if (f.Read(&next, 4, cur) != 4) {
				ok = false;
				break;
			}
			cur = next;
		}
		if (ok) {
			f.Write(&newOff, 4, last);
		} else {
			table[bucket] = newOff;
			result = SaveResult::ChainReset;
		}
	}
	f.Write(table, TableBytes, 0);
	int32_t word[7];
	std::memcpy(word, rec.word, sizeof(word));
	for (int k = 0; k < Blobs; ++k) {
		if (rec.blob[k] == nullptr || word[3 + k] < 0) {
			word[3 + k] = 0;  // a size without data would make a node the loader cannot read
		}
	}
	uint32_t off = newOff;
	const uint32_t zero = 0;
	f.Write(&zero, 4, off);
	off += 4;
	for (int i = 0; i < 7; ++i) {
		f.Write(&word[i], 4, off);
		off += 4;
	}
	for (int k = 0; k < Blobs; ++k) {
		const uint32_t n = static_cast<uint32_t>(word[3 + k]);
		if (n != 0) {
			f.Write(rec.blob[k], n, off);
			off += n;
		}
	}
	return result;
}

}  // namespace mapmem

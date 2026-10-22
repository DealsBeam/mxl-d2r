// Standalone test of the map memory file logic (map_memory_logic.h; not part of the plugin DLL).
#include "../src/map_memory_logic.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: %s\n", what);
	}
}

// A SaveFile as the logic sees it.
struct FakeFile {
	std::vector<uint8_t> bytes;
	auto Size() const -> uint32_t { return static_cast<uint32_t>(bytes.size()); }
	auto Read(void* dst, uint32_t len, uint32_t off) const -> uint32_t {
		if (bytes.empty() || bytes.size() <= off) {
			return 0;
		}
		const uint32_t n = bytes.size() < static_cast<size_t>(off) + len ? static_cast<uint32_t>(bytes.size() - off) : len;
		std::memcpy(dst, bytes.data() + off, n);
		return n;
	}
	void Write(const void* src, uint32_t len, uint32_t off) {
		if (bytes.size() < static_cast<size_t>(off) + len) {
			bytes.resize(static_cast<size_t>(off) + len, 0);
		}
		std::memcpy(bytes.data() + off, src, len);
	}
	void Replace(const void* src, uint32_t len) {
		bytes.assign(static_cast<const uint8_t*>(src), static_cast<const uint8_t*>(src) + len);
	}
};

// A node for `layer` with one blob of `n` bytes filled with `fill`.
auto Save(FakeFile& f, int32_t layer, uint32_t n, uint8_t fill) -> mapmem::SaveResult {
	std::vector<uint8_t> blob(n, fill);
	mapmem::Record rec;
	rec.word[0] = layer;
	rec.word[1] = 7;
	rec.word[2] = 9;
	rec.word[3] = static_cast<int32_t>(n);
	rec.blob[0] = blob.data();
	return mapmem::AppendNode(f, rec);
}

auto Nodes(const std::vector<uint8_t>& img, int bucket) -> mapmem::Chain {
	return mapmem::Walk(img.data(), static_cast<uint32_t>(img.size()), bucket);
}
}  // namespace

int main() {
	using namespace mapmem;

	// the table index
	Check(Bucket(0) == 0 && Bucket(99) == 99 && Bucket(100) == 0 && Bucket(144) == 44 && Bucket(279) == 79, "bucket is the layer modulo 100");
	Check(Bucket(-1) == -1, "a negative layer has no bucket");

	// the first node of a new file lands at 400, as in D2R's files
	FakeFile f;
	Check(Save(f, 5, 6, 0xaa) == SaveResult::Appended, "first save");
	Check(f.bytes.size() == TableBytes + NodeHeader + 6, "table, header and blob");
	Check(Get32(f.bytes.data() + 5 * 4) == TableBytes, "table entry of layer 5 points at 400");
	Check(Get32(f.bytes.data() + TableBytes) == 0 && Get32(f.bytes.data() + TableBytes + 4) == 5, "node: next 0, layer 5");
	Check(Get32(f.bytes.data() + TableBytes + 8) == 7 && Get32(f.bytes.data() + TableBytes + 12) == 9, "node: words 1 and 2");
	Check(Get32(f.bytes.data() + TableBytes + 16) == 6 && Get32(f.bytes.data() + TableBytes + 32) == 0xaaaaaaaa, "node: size, blob");

	// saves of one layer chain up
	Save(f, 5, 4, 0xbb);
	Save(f, 5, 8, 0xcc);
	Chain c = Nodes(f.bytes, 5);
	Check(c.clean && c.nodes.size() == 3, "three nodes in the chain of layer 5");
	Check(c.nodes[1].off == c.nodes[0].end && c.nodes[2].off == c.nodes[1].end, "nodes are appended one after the other");
	Check(c.nodes[0].size[0] == 6 && c.nodes[1].size[0] == 4 && c.nodes[2].size[0] == 8, "in save order");

	// a clean file of a layer below 100 is used as it is
	std::vector<uint8_t> view;
	Check(!LoadView(f.bytes, 5, &view), "legacy file, layer 5: no view");
	Check(!LoadView(f.bytes, 6, &view), "an empty chain of a layer below 100: no view");

	// layers 44, 144, 244 share table entry 44
	Save(f, 44, 2, 0x44);
	Save(f, 144, 3, 0x14);
	Save(f, 44, 2, 0x45);
	Save(f, 144, 5, 0x15);
	Save(f, 244, 1, 0x24);
	c = Nodes(f.bytes, 44);
	Check(c.clean && c.nodes.size() == 5, "five nodes share the chain of entry 44");
	Check(LoadView(f.bytes, 44, &view), "layer 44 needs a view once 144 shares its chain");
	Chain v = Nodes(view, 44);
	Check(v.clean && v.nodes.size() == 2, "view of layer 44: its two nodes");
	Check(v.nodes[0].size[0] == 2 && v.nodes[1].size[0] == 2 && v.nodes[0].layer == 44 && v.nodes[1].layer == 44, "view of layer 44: right nodes");
	Check(view[v.nodes[0].off + NodeHeader] == 0x44 && view[v.nodes[1].off + NodeHeader] == 0x45, "view of layer 44: blobs are the file's");
	Check(view.size() == f.bytes.size(), "offsets unchanged");
	Check(LoadView(f.bytes, 144, &view), "layer 144 gets a view");
	v = Nodes(view, 44);
	Check(v.clean && v.nodes.size() == 2 && v.nodes[0].size[0] == 3 && v.nodes[1].size[0] == 5, "view of layer 144: its two nodes in order");
	Check(v.nodes[0].layer == 44 && v.nodes[1].layer == 44, "view of layer 144 names the table index as the layer");
	Check(LoadView(f.bytes, 244, &view), "layer 244 gets a view");
	v = Nodes(view, 44);
	Check(v.nodes.size() == 1 && v.nodes[0].size[0] == 1, "view of layer 244: its one node");
	Check(Get32(f.bytes.data() + 144 % 100 * 4) == c.nodes[0].off, "the file itself is not changed by the views");

	// a layer of 100 or more with nothing stored still gets a (empty) view, so the loader never sees a layer past its table
	Check(LoadView(f.bytes, 279, &view), "layer 279, nothing stored: view");
	Check(Get32(view.data() + 79 * 4) == 0, "nothing stored: empty chain");

	// every Median layer keeps its own nodes
	FakeFile g;
	for (int round = 0; round < 3; ++round) {
		for (int layer = 0; layer < 280; ++layer) {
			Save(g, layer, 1 + static_cast<uint32_t>(layer % 7), static_cast<uint8_t>(layer + round));
		}
	}
	bool all = true;
	for (int layer = 0; layer < 280 && all; ++layer) {
		const bool needsView = LoadView(g.bytes, layer, &view);
		const std::vector<uint8_t>& use = needsView ? view : g.bytes;
		const Chain k = Nodes(use, Bucket(layer));
		bool ok = k.clean && k.nodes.size() == 3;
		for (size_t i = 0; ok && i < k.nodes.size(); ++i) {
			ok = k.nodes[i].size[0] == 1 + static_cast<uint32_t>(layer % 7) &&
			     use[k.nodes[i].off + NodeHeader] == static_cast<uint8_t>(layer + static_cast<int>(i));
		}
		all = all && ok;
	}
	Check(all, "280 layers x 3 saves: each layer sees its own nodes in order");

	// a chain that loops (D2R's cut of a non-first node writes the node's offset into itself, bug #24)
	FakeFile h;
	Save(h, 7, 2, 1);
	Save(h, 7, 2, 2);
	c = Nodes(h.bytes, 7);
	Put32(h.bytes.data() + c.nodes[1].off, c.nodes[1].off);
	c = Nodes(h.bytes, 7);
	Check(!c.clean && c.nodes.size() == 2, "a loop is found, each node once");
	Check(LoadView(h.bytes, 7, &view), "a loop gets a view");
	v = Nodes(view, 7);
	Check(v.clean && v.nodes.size() == 2, "the view ends the chain after the two nodes");
	Check(Save(h, 7, 2, 3) == SaveResult::ChainReset, "a save over a loop starts the chain again");
	c = Nodes(h.bytes, 7);
	Check(c.clean && c.nodes.size() == 1 && c.nodes[0].size[0] == 2, "the chain has the new node only");
	Check(Save(h, 7, 2, 4) == SaveResult::Appended, "and goes on");
	Check(Nodes(h.bytes, 7).nodes.size() == 2, "two nodes now");

	// a link past the end of the file
	FakeFile m;
	Save(m, 3, 2, 1);
	Put32(m.bytes.data() + TableBytes, 99999);
	c = Nodes(m.bytes, 3);
	Check(!c.clean && c.nodes.size() == 1, "a link outside the file stops the chain");
	Check(Save(m, 3, 2, 9) == SaveResult::ChainReset, "the save drops it");

	// a table entry below the table, a node cut short by the end of the file
	FakeFile q;
	Save(q, 2, 10, 5);
	Put32(q.bytes.data() + 2 * 4, 8);
	Check(!Nodes(q.bytes, 2).clean && Nodes(q.bytes, 2).nodes.empty(), "an entry inside the table is no node");
	FakeFile r;
	Save(r, 2, 10, 5);
	r.bytes.resize(r.bytes.size() - 3);
	Check(!Nodes(r.bytes, 2).clean, "blob past the end of the file");
	Node scratch;
	Check(!ParseNode(r.bytes.data(), static_cast<uint32_t>(r.bytes.size()), 0, &scratch), "offset 0 is not a node");

	// a file shorter than its table is not read
	std::vector<uint8_t> tiny(100, 0);
	Check(!LoadView(tiny, 5, &view), "short file: nothing to show");

	// records: a negative layer is not saved, a size without data is written as 0
	FakeFile s;
	Check(Save(s, -1, 2, 1) == SaveResult::Skipped && s.bytes.empty(), "negative layer is skipped");
	mapmem::Record rec;
	rec.word[0] = 12;
	rec.word[3] = 40;
	AppendNode(s, rec);
	c = Nodes(s.bytes, 12);
	Check(c.clean && c.nodes.size() == 1 && c.nodes[0].size[0] == 0 && s.bytes.size() == TableBytes + NodeHeader, "size without data: written as 0");

	// a short table (a file that got cut) is made again
	FakeFile t;
	t.bytes.assign(120, 0xff);
	Save(t, 9, 2, 1);
	c = Nodes(t.bytes, 9);
	Check(c.clean && c.nodes.size() == 1 && c.nodes[0].off == TableBytes, "short table: file rebuilt, node at 400");

	if (failures == 0) {
		std::printf("map memory tests: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}

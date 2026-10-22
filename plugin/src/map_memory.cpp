// Per-level map memory for Levels.txt layers above 99 (docs/subsystems/areas-drlg.md section 6, docs/bugs.md #24).
//
// D2R remembers the explored rooms of each automap layer in the character's map file (map_memory_logic.h has the layout). The
// file's table holds 100 chains and both routines index it by the layer on their stack (`local_1d8[100]`): load 0xd5710 (called
// by the level switch 0xd1710 for the new level) and save 0xd5fe0 (called by 0xd6230 for the level being left, and on exit by
// 0xd2e40). Median's layers go up to 279. The file format stays: the table index is the layer modulo 100 and the nodes name
// their layer.
//   save  0xd5fe0   replaced: the same steps in AppendNode (table index by the bucket, the real layer in the node, a chain that
//                   loops or leaves the file started again instead of followed for ever).
//   load  0xd5710   wrapped: when the layer's chain holds other layers' nodes, or the layer is 100 or more, the loader runs with
//                   the layer record's layer set to the table index and sees a view of the file (LoadView) in which the chain
//                   holds only this layer's nodes. The view is served by hooks on the two file functions below for that one file
//                   and thread; the loader's own writes (the chain cut after an invalid room, which for a node that is not the
//                   first writes the node's offset into itself) are dropped.
//   file reads 0xa1e110 / writes 0xa1e380: pass through, except during that load.
// Entry bytes of all four are the first whole instructions of the dump (build 93847, timestamp 0x6ab3782c), none rip-relative.
#include "map_memory.h"

#include "map_memory_logic.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr uint64_t LoadRva = 0xd5710;
constexpr uint64_t SaveRva = 0xd5fe0;
constexpr uint64_t ReadRva = 0xa1e110;
constexpr uint64_t WriteRva = 0xa1e380;
constexpr uint64_t ReplaceRva = 0xa1e220;  // not hooked: (file, src, len) makes the file these bytes
constexpr uint64_t ChooseRva = 0xd5450;    // (save system, index file) -> this game's map file; repeating it changes nothing
constexpr uint64_t RecordRva = 0x2a2cf68;  // pointer to the current automap layer record (word 0 = layer)

// 40 55 | lea rbp,[rsp-0x210]
constexpr uint8_t LoadEntry[] = {0x40, 0x55, 0x48, 0x8d, 0xac, 0x24, 0xf0, 0xfd, 0xff, 0xff};
// 40 53 | 55 | 56 | 57 | 41 54 | 41 56 | 41 57
constexpr uint8_t SaveEntry[] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57};
// mov [rsp+0x10],rbx / mov [rsp+0x18],rbp / mov [rsp+0x20],rsi
constexpr uint8_t ReadEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};
// mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x20],rsi
constexpr uint8_t WriteEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20};

// Offsets in the record: four ordered sets (a 0x28-byte tree head each) at +8, +0x30, +0x58, +0x80, the size at +0x20 of each;
// the blobs the save routine writes are the pointers at +0x20, +0x38, +0x50, +0x68 of its own (stack) record.
constexpr int SetSizeAt[4] = {0x28, 0x50, 0x78, 0xa0};
constexpr int BlobPtrAt[mapmem::Blobs] = {0x20, 0x38, 0x50, 0x68};

using LoadFn = void (*)(void* saveSystem, void* indexFile);
using SaveFn = void (*)(int32_t* record, void* saveSystem, void* indexFile);
using ReadFn = uint64_t (*)(void* file, void* dst, uint64_t len, uint64_t off);
using WriteFn = uint64_t (*)(void* file, const void* src, uint64_t len, uint64_t off);
using ReplaceFn = uint64_t (*)(void* file, const void* src, uint64_t len);
using ChooseFn = void* (*)(void* saveSystem, void* indexFile);

uint64_t g_base = 0;
LoadFn g_origLoad = nullptr;
SaveFn g_origSave = nullptr;
ReadFn g_origRead = nullptr;
WriteFn g_origWrite = nullptr;
ReplaceFn g_replace = nullptr;
ChooseFn g_choose = nullptr;
volatile LONG g_ready = 0;

void* volatile g_activeFile = nullptr;  // the file the running load sees as g_view
volatile DWORD g_activeThread = 0;
const std::vector<uint8_t>* g_view = nullptr;

void* g_lastSystem = nullptr;  // the last arguments of a load or save, for the harness
void* g_lastIndex = nullptr;
volatile LONG g_loads = 0, g_saves = 0, g_views = 0, g_resets = 0;

auto Record() noexcept -> int32_t* {
	return *reinterpret_cast<int32_t* const*>(g_base + RecordRva);
}

auto FileSize(void* file) noexcept -> uint32_t {
	const uint64_t n = *reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(file) + 0x10);
	return n > mapmem::MaxFile ? mapmem::MaxFile : static_cast<uint32_t>(n);
}

struct GameFile {
	void* file;
	auto Size() const -> uint32_t { return FileSize(file); }
	auto Read(void* dst, uint32_t len, uint32_t off) const -> uint32_t { return static_cast<uint32_t>(g_origRead(file, dst, len, off)); }
	void Write(const void* src, uint32_t len, uint32_t off) const { g_origWrite(file, src, len, off); }
	void Replace(const void* src, uint32_t len) const { g_replace(file, src, len); }
};

auto ReadHook(void* file, void* dst, uint64_t len, uint64_t off) -> uint64_t {
	if (file != nullptr && file == g_activeFile && g_activeThread == GetCurrentThreadId() && g_view != nullptr) {
		const uint64_t size = g_view->size();
		uint32_t n = static_cast<uint32_t>(len);
		if (size == 0 || size <= off) {
			return 0;
		}
		if (size < off + n) {
			n = static_cast<uint32_t>(size - off);
		}
		std::memcpy(dst, g_view->data() + off, n);
		return n;
	}
	return g_origRead(file, dst, len, off);
}

auto WriteHook(void* file, const void* src, uint64_t len, uint64_t off) -> uint64_t {
	if (file != nullptr && file == g_activeFile && g_activeThread == GetCurrentThreadId()) {
		return static_cast<uint32_t>(len);  // the loader's cut of a chain: the file is not touched
	}
	return g_origWrite(file, src, len, off);
}

void SaveHook(int32_t* record, void* saveSystem, void* indexFile) {
	if (g_ready == 0 || record == nullptr) {
		g_origSave(record, saveSystem, indexFile);
		return;
	}
	g_lastSystem = saveSystem;
	g_lastIndex = indexFile;
	void* file = g_choose(saveSystem, indexFile);
	if (file == nullptr) {
		return;
	}
	mapmem::Record rec;
	std::memcpy(rec.word, record, sizeof(rec.word));
	for (int k = 0; k < mapmem::Blobs; ++k) {
		rec.blob[k] = *reinterpret_cast<const uint8_t* const*>(reinterpret_cast<const uint8_t*>(record) + BlobPtrAt[k]);
	}
	InterlockedIncrement(&g_saves);
	GameFile gameFile{file};
	if (mapmem::AppendNode(gameFile, rec) == mapmem::SaveResult::ChainReset) {
		InterlockedIncrement(&g_resets);
	}
}

void LoadHook(void* saveSystem, void* indexFile) {
	int32_t* record = g_ready != 0 ? Record() : nullptr;
	if (record == nullptr) {
		g_origLoad(saveSystem, indexFile);
		return;
	}
	g_lastSystem = saveSystem;
	g_lastIndex = indexFile;
	InterlockedIncrement(&g_loads);
	const int32_t layer = record[0];
	void* file = g_choose(saveSystem, indexFile);
	const uint32_t size = file != nullptr ? FileSize(file) : 0;
	std::vector<uint8_t> image;
	std::vector<uint8_t> view;
	if (file == nullptr || size < mapmem::TableBytes || size >= mapmem::MaxFile) {
		g_origLoad(saveSystem, indexFile);
		return;
	}
	image.resize(size);
	if (g_origRead(file, image.data(), size, 0) != size || !mapmem::LoadView(image, layer, &view)) {
		g_origLoad(saveSystem, indexFile);
		return;
	}
	InterlockedIncrement(&g_views);
	g_view = &view;
	g_activeThread = GetCurrentThreadId();
	g_activeFile = file;
	record[0] = mapmem::Bucket(layer);
	g_origLoad(saveSystem, indexFile);
	if (Record() == record) {
		record[0] = layer;
	}
	g_activeFile = nullptr;
	g_activeThread = 0;
	g_view = nullptr;
}

}  // namespace

auto InstallMapMemory(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_base = context->exeBase;
	g_replace = reinterpret_cast<ReplaceFn>(g_base + ReplaceRva);
	g_choose = reinterpret_cast<ChooseFn>(g_base + ChooseRva);
	const bool ok =
		context->InstallInlineHook(ReadRva, ReadEntry, sizeof(ReadEntry), reinterpret_cast<void*>(&ReadHook), reinterpret_cast<void**>(&g_origRead)) &&
		context->InstallInlineHook(WriteRva, WriteEntry, sizeof(WriteEntry), reinterpret_cast<void*>(&WriteHook), reinterpret_cast<void**>(&g_origWrite)) &&
		context->InstallInlineHook(SaveRva, SaveEntry, sizeof(SaveEntry), reinterpret_cast<void*>(&SaveHook), reinterpret_cast<void**>(&g_origSave)) &&
		context->InstallInlineHook(LoadRva, LoadEntry, sizeof(LoadEntry), reinterpret_cast<void*>(&LoadHook), reinterpret_cast<void**>(&g_origLoad));
	if (ok) {
		InterlockedExchange(&g_ready, 1);
	}
	std::snprintf(report, reportSize, ok ? "on: map memory by layer modulo 100 (load 0xd5710, save 0xd5fe0, file 0xa1e110 / 0xa1e380)"
	                                     : "off: map memory hook failed (the game keeps its 100-layer table)");
	return ok;
}

auto MapMemoryProbe(MapMemoryInfo* info) noexcept -> bool {
	info->loads = g_loads;
	info->saves = g_saves;
	info->views = g_views;
	info->resets = g_resets;
	const int32_t* record = g_base != 0 ? Record() : nullptr;
	if (record == nullptr) {
		return false;
	}
	info->haveLayer = true;
	info->layer = record[0];
	info->bucket = mapmem::Bucket(record[0]);
	for (int k = 0; k < 4; ++k) {
		info->cells[k] = *reinterpret_cast<const uint64_t*>(reinterpret_cast<const uint8_t*>(record) + SetSizeAt[k]);
	}
	if (g_ready != 0 && g_lastSystem != nullptr && g_lastIndex != nullptr) {
		void* file = g_choose(g_lastSystem, g_lastIndex);
		const uint32_t size = file != nullptr ? FileSize(file) : 0;
		if (size >= mapmem::TableBytes) {
			std::vector<uint8_t> image(size);
			if (g_origRead(file, image.data(), size, 0) == size) {
				const mapmem::Chain chain = mapmem::Walk(image.data(), size, info->bucket);
				info->haveFile = true;
				info->fileSize = size;
				info->clean = chain.clean;
				for (const mapmem::Node& n : chain.nodes) {
					(n.layer == info->layer ? info->nodes : info->foreign) += 1;
				}
			}
		}
	}
	return true;
}

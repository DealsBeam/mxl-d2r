#include "item_events.h"

#include "d2r.h"
#include "item_events_logic.h"

#define NOMINMAX
#include <windows.h>
#include <intrin.h>

#include <tlhelp32.h>

#include <cstdio>
#include <cstring>

namespace item_events {
namespace {

// Slots of the D2RLoader thunks (exe RVA of the qword the thunk jumps through), in slot order.
constexpr uint64_t SlotRva[] = {0x3e2a400, 0x3e2a408, 0x3e2a410, 0x3e2a418, 0x3e2a420, 0x3e2a428};
constexpr const char* SlotName[] = {"register", "unregister", "has", "clear", "dispatch", "registerFn"};
constexpr int SlotCount = 6;

struct Entry {
	int slot;
	uint64_t a[12];
	uint64_t ret;
};
constexpr int RingSize = 4096;
Entry g_ring[RingSize];
volatile LONG g_next = 0;
uint64_t g_exeBase = 0;
uint64_t* g_slotPtr[SlotCount] = {};
uint64_t g_orig[SlotCount] = {};
bool g_on = false;

using Fn12 = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                          uint64_t, uint64_t);

template <int N>
uint64_t Detour(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8,
                uint64_t a9, uint64_t a10, uint64_t a11, uint64_t a12) {
	const LONG i = InterlockedIncrement(&g_next) - 1;
	Entry& e = g_ring[i % RingSize];
	e.slot = N;
	e.a[0] = a1; e.a[1] = a2; e.a[2] = a3; e.a[3] = a4; e.a[4] = a5; e.a[5] = a6;
	e.a[6] = a7; e.a[7] = a8; e.a[8] = a9; e.a[9] = a10; e.a[10] = a11; e.a[11] = a12;
	e.ret = reinterpret_cast<uint64_t>(_ReturnAddress());
	return reinterpret_cast<Fn12>(g_orig[N])(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
}

constexpr Fn12 Detours[SlotCount] = {&Detour<0>, &Detour<1>, &Detour<2>, &Detour<3>, &Detour<4>, &Detour<5>};

}  // namespace

bool TraceStart(uint64_t exeBase) noexcept {
	if (g_on) {
		return true;
	}
	g_exeBase = exeBase;
	for (int i = 0; i < SlotCount; ++i) {
		g_slotPtr[i] = reinterpret_cast<uint64_t*>(exeBase + SlotRva[i]);
		g_orig[i] = *g_slotPtr[i];
		if (g_orig[i] == 0 || IsBadReadPtr(reinterpret_cast<void*>(g_orig[i]), 16)) {
			return false;
		}
	}
	for (int i = 0; i < SlotCount; ++i) {
		DWORD old = 0;
		VirtualProtect(g_slotPtr[i], 8, PAGE_READWRITE, &old);
		InterlockedExchange64(reinterpret_cast<volatile LONG64*>(g_slotPtr[i]), static_cast<LONG64>(reinterpret_cast<uint64_t>(Detours[i])));
		VirtualProtect(g_slotPtr[i], 8, old, &old);
	}
	g_on = true;
	return true;
}

void TraceStop() noexcept {
	if (!g_on) {
		return;
	}
	for (int i = 0; i < SlotCount; ++i) {
		InterlockedExchange64(reinterpret_cast<volatile LONG64*>(g_slotPtr[i]), static_cast<LONG64>(g_orig[i]));
	}
	g_on = false;
}

void TraceClear() noexcept { InterlockedExchange(&g_next, 0); }

void TraceDump(int slot, int max, void (*emit)(const char* line)) noexcept {
	const LONG total = g_next;
	const LONG oldest = total > RingSize ? total - RingSize : 0;
	LONG first = oldest;
	// the newest `max` entries of the wanted slot
	int wanted = 0;
	for (LONG i = total - 1; i >= oldest && wanted < max; --i) {
		if (slot < 0 || g_ring[i % RingSize].slot == slot) {
			++wanted;
			first = i;
		}
	}
	char line[400];
	for (LONG i = first; i < total; ++i) {
		const Entry& e = g_ring[i % RingSize];
		if (slot >= 0 && e.slot != slot) {
			continue;
		}
		const uint64_t ret = e.ret >= g_exeBase && e.ret < g_exeBase + 0x4000000 ? e.ret - g_exeBase : e.ret;
		std::snprintf(line, sizeof(line),
		              "ev#%ld %s ret=%llx a=%llx %llx %llx %llx s=%llx %llx %llx %llx %llx %llx %llx %llx", static_cast<long>(i),
		              SlotName[e.slot], static_cast<unsigned long long>(ret), static_cast<unsigned long long>(e.a[0]),
		              static_cast<unsigned long long>(e.a[1]), static_cast<unsigned long long>(e.a[2]),
		              static_cast<unsigned long long>(e.a[3]), static_cast<unsigned long long>(e.a[4]),
		              static_cast<unsigned long long>(e.a[5]), static_cast<unsigned long long>(e.a[6]),
		              static_cast<unsigned long long>(e.a[7]), static_cast<unsigned long long>(e.a[8]),
		              static_cast<unsigned long long>(e.a[9]), static_cast<unsigned long long>(e.a[10]),
		              static_cast<unsigned long long>(e.a[11]));
		emit(line);
	}
}

// --- write watch ---------------------------------------------------------------------------------------------------

namespace {

struct WatchHit {
	uint64_t rip;
	uint64_t chain[8];
	uint32_t tid;
	int slot;
};
constexpr int WatchRing = 256;
WatchHit g_hits[WatchRing];
volatile LONG g_hitCount = 0;
uint64_t g_watchAddr[4] = {};
int g_watchLen[4] = {};
bool g_watchRead[4] = {};
void* g_vectored = nullptr;

LONG CALLBACK WatchHandler(PEXCEPTION_POINTERS info) {
	if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	CONTEXT* ctx = info->ContextRecord;
	const uint64_t dr6 = ctx->Dr6;
	if ((dr6 & 0xf) == 0) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	const LONG i = InterlockedIncrement(&g_hitCount) - 1;
	WatchHit& h = g_hits[i % WatchRing];
	h.rip = ctx->Rip;
	h.tid = GetCurrentThreadId();
	h.slot = (dr6 & 1) ? 0 : (dr6 & 2) ? 1 : (dr6 & 4) ? 2 : 3;
	void* frames[10] = {};
	const USHORT n = RtlCaptureStackBackTrace(0, 10, frames, nullptr);
	for (int k = 0; k < 8; ++k) {
		h.chain[k] = 2 + k < n ? reinterpret_cast<uint64_t>(frames[2 + k]) : 0;
	}
	ctx->Dr6 = 0;
	ctx->EFlags |= 0x10000;  // resume flag: continue past the breakpoint instruction
	return EXCEPTION_CONTINUE_EXECUTION;
}

bool ApplyToThreads(bool set) {
	bool ok = true;
	const DWORD self = GetCurrentThreadId();
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE) {
		return false;
	}
	THREADENTRY32 te{};
	te.dwSize = sizeof(te);
	for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te)) {
		if (te.th32OwnerProcessID != GetCurrentProcessId()) {
			continue;
		}
		HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
		if (th == nullptr) {
			continue;
		}
		if (te.th32ThreadID != self) {
			SuspendThread(th);
		}
		CONTEXT ctx{};
		ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
		if (GetThreadContext(th, &ctx)) {
			uint64_t dr7 = 0;
			uint64_t* regs[4] = {&ctx.Dr0, &ctx.Dr1, &ctx.Dr2, &ctx.Dr3};
			for (int s = 0; s < 4; ++s) {
				if (set && g_watchAddr[s] != 0) {
					*regs[s] = g_watchAddr[s];
					const uint64_t lenBits = g_watchLen[s] == 1 ? 0 : g_watchLen[s] == 2 ? 1 : g_watchLen[s] == 8 ? 2 : 3;
					dr7 |= (1ULL << (s * 2)) | ((g_watchRead[s] ? 3ULL : 1ULL) << (16 + s * 4)) | (lenBits << (18 + s * 4));  // write only
				} else {
					*regs[s] = 0;
				}
			}
			ctx.Dr7 = dr7;
			ctx.Dr6 = 0;
			ok = SetThreadContext(th, &ctx) && ok;
		} else {
			ok = false;
		}
		if (te.th32ThreadID != self) {
			ResumeThread(th);
		}
		CloseHandle(th);
	}
	CloseHandle(snap);
	return ok;
}

}  // namespace

bool WatchSet(int slot, uint64_t address, int len, uint64_t exeBase, bool readToo) noexcept {
	if (slot < 0 || slot > 3 || (len != 1 && len != 2 && len != 4 && len != 8)) {
		return false;
	}
	g_exeBase = exeBase;
	if (g_vectored == nullptr) {
		g_vectored = AddVectoredExceptionHandler(1, WatchHandler);
	}
	g_watchAddr[slot] = address;
	g_watchLen[slot] = len;
	g_watchRead[slot] = readToo;
	return ApplyToThreads(true);
}

bool WatchSelfTest() noexcept {
	static volatile uint64_t cell = 0;
	WatchSet(3, reinterpret_cast<uint64_t>(&cell), 8, g_exeBase);
	const LONG before = g_hitCount;
	cell = 1;
	const bool hit = g_hitCount != before;
	for (int s = 3; s < 4; ++s) {
		g_watchAddr[s] = 0;
	}
	ApplyToThreads(true);
	return hit;
}

void WatchClear() noexcept {
	for (int s = 0; s < 4; ++s) {
		g_watchAddr[s] = 0;
	}
	ApplyToThreads(false);
	InterlockedExchange(&g_hitCount, 0);
}

void WatchDump(void (*emit)(const char* line)) noexcept {
	const LONG total = g_hitCount;
	char line[400];
	auto rva = [](uint64_t a) { return a >= g_exeBase && a < g_exeBase + 0x4000000 ? a - g_exeBase : a; };
	for (LONG i = total > WatchRing ? total - WatchRing : 0; i < total; ++i) {
		const WatchHit& h = g_hits[i % WatchRing];
		std::snprintf(line, sizeof(line), "watch#%ld slot=%d tid=%u rip=%llx chain %llx %llx %llx %llx %llx %llx %llx %llx", static_cast<long>(i), h.slot,
		              h.tid, static_cast<unsigned long long>(rva(h.rip)), static_cast<unsigned long long>(rva(h.chain[0])),
		              static_cast<unsigned long long>(rva(h.chain[1])), static_cast<unsigned long long>(rva(h.chain[2])),
		              static_cast<unsigned long long>(rva(h.chain[3])), static_cast<unsigned long long>(rva(h.chain[4])),
		              static_cast<unsigned long long>(rva(h.chain[5])), static_cast<unsigned long long>(rva(h.chain[6])),
		              static_cast<unsigned long long>(rva(h.chain[7])));
		emit(line);
	}
}

// --- the port ------------------------------------------------------------------------------------------------------

namespace {

using d2r::Unit;
using Handler = uint64_t (*)(void* game, int32_t event, Unit* unit, Unit* other, void* arg5, uint64_t code, uint64_t a7, uint64_t a8,
                             uint64_t a9);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using GetRecordFn = const uint8_t* (*)(uint32_t dataFlag, int32_t skillId);

constexpr uint64_t GetStatRva = 0x2f5020;        // (unit, stat, layer)
constexpr uint64_t RvaSkillRecord = 0x97790;     // (game + 0x106, skill id) -> skills record
constexpr uint64_t RegisterSlotRva = 0x3e2a400;  // the `register` thunk's slot; its target is inside D2RCore.dll
// D2RCore.dll as shipped with D2RLoader 1.3.1: the handlers its table holds for the functions this file replaces or wraps.
struct CoreHandler {
	int func;
	uint64_t rva;
	uint8_t entry[7];
	size_t entryLen;
};
constexpr CoreHandler CoreHandlers[] = {
    {15, 0x3d12b0, {0x48, 0x83, 0xec, 0x38, 0x48, 0x8b, 0x05}, 7},  // open wounds
    {16, 0x3d1310, {0xe9, 0xbb, 0xd7, 0xff, 0xff, 0, 0}, 5},        // crushing blow (a jmp thunk)
    {33, 0x3d19b0, {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x50}, 7},  // forwarders to D2R's own functions 33 / 34, which return at once
    {34, 0x3d1a20, {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x50}, 7},  // for these stats
};
// D2R's functions 20 (chance to cast a skill) and 28 (flat life) are the bodies Median's 34 and 33 are.
constexpr uint64_t MainFunc20 = 0x583b30, MainFunc28 = 0x583a70;

bool Readable(const void* p, size_t n) noexcept { return !IsBadReadPtr(p, n); }

GetStatFn g_getStat = nullptr;
Handler g_original15 = nullptr;
Handler g_original16 = nullptr;
Handler g_main20 = nullptr;
Handler g_main28 = nullptr;
volatile LONG g_calls33 = 0, g_calls34 = 0;
uint64_t g_last33[9] = {};
uint64_t g_ret33 = 0, g_ret34 = 0;
int64_t g_lastCastSkill = -1, g_lastCastLevel = -1, g_lastCastWarp = -1;
uint64_t g_exeBaseForRecords = 0;

bool UnitBlocksWoundsOrCrush(Unit* attacker, Unit* victim) noexcept {
	const int aType = attacker != nullptr ? d2r::At<int32_t>(attacker, d2r::UnitType) : -1;
	const int vType = victim != nullptr ? d2r::At<int32_t>(victim, d2r::UnitType) : -1;
	return mxl_itemevents::BlocksWoundsOrCrush(
	    aType, aType == mxl_itemevents::UnitMonster ? g_getStat(attacker, mxl_itemevents::ImmunityStat, 0) : 0, vType,
	    vType == mxl_itemevents::UnitMonster ? g_getStat(victim, mxl_itemevents::ImmunityStat, 0) : 0);
}

uint64_t Gate15(void* game, int32_t event, Unit* unit, Unit* other, void* arg5, uint64_t code, uint64_t a7, uint64_t a8, uint64_t a9) {
	if (UnitBlocksWoundsOrCrush(unit, other)) {
		return 0;
	}
	return g_original15(game, event, unit, other, arg5, code, a7, a8, a9);
}

uint64_t Gate16(void* game, int32_t event, Unit* unit, Unit* other, void* arg5, uint64_t code, uint64_t a7, uint64_t a8, uint64_t a9) {
	if (UnitBlocksWoundsOrCrush(unit, other)) {
		return 0;
	}
	return g_original16(game, event, unit, other, arg5, code, a7, a8, a9);
}

uint64_t HealOnKill(void* game, int32_t event, Unit* unit, Unit* other, void* arg5, uint64_t code, uint64_t a7, uint64_t a8, uint64_t a9) {
	InterlockedIncrement(&g_calls33);
	g_last33[0] = reinterpret_cast<uint64_t>(game);
	g_last33[1] = static_cast<uint64_t>(event);
	g_last33[2] = reinterpret_cast<uint64_t>(unit);
	g_last33[3] = reinterpret_cast<uint64_t>(other);
	g_last33[4] = reinterpret_cast<uint64_t>(arg5);
	g_last33[5] = code;
	g_last33[6] = a7;
	g_last33[7] = a8;
	g_last33[8] = a9;
	g_ret33 = g_main28(game, event, unit, other, arg5, mxl_itemevents::ToD2rCode(code), a7, a8, a9);
	return g_ret33;
}

uint64_t CastOnKill(void* game, int32_t event, Unit* unit, Unit* other, void* arg5, uint64_t code, uint64_t a7, uint64_t a8, uint64_t a9) {
	InterlockedIncrement(&g_calls34);
	const uint32_t d2rCode = mxl_itemevents::ToD2rCode(code);
	g_lastCastSkill = mxl_itemevents::CastSkill(d2rCode);
	g_lastCastLevel = mxl_itemevents::CastLevel(d2rCode);
	g_lastCastWarp = -1;
	if (game != nullptr && Readable(static_cast<uint8_t*>(game) + 0x106, 1)) {
		const uint8_t* rec = reinterpret_cast<GetRecordFn>(g_exeBaseForRecords + RvaSkillRecord)(*(static_cast<uint8_t*>(game) + 0x106), static_cast<int32_t>(g_lastCastSkill));
		if (rec != nullptr && Readable(rec + mxl_itemevents::RecordWarpOffset, 1)) {
			g_lastCastWarp = mxl_itemevents::IsWarpSkill(rec[mxl_itemevents::RecordWarpOffset]) ? 1 : 0;
		}
	}
	g_ret34 = g_main20(game, event, unit, other, arg5, d2rCode, a7, a8, a9);
	return g_ret34;
}

// Writes `value` over an 8-byte table slot in memory the loader mapped (no PatchWrite: the slot may be outside the exe).
bool WriteSlot(uint64_t* slot, uint64_t value) noexcept {
	DWORD old = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) {
		return false;
	}
	*slot = value;
	DWORD ignore = 0;
	VirtualProtect(slot, sizeof(*slot), old, &ignore);
	return true;
}

}  // namespace

void Counters(long* heal, long* cast, uint64_t* lastArgs9) noexcept {
	lastArgs9[11] = static_cast<uint64_t>(g_lastCastSkill);
	lastArgs9[12] = static_cast<uint64_t>(g_lastCastLevel);
	lastArgs9[13] = static_cast<uint64_t>(g_lastCastWarp);
	*heal = g_calls33;
	*cast = g_calls34;
	for (int i = 0; i < 9; ++i) {
		lastArgs9[i] = g_last33[i];
	}
	lastArgs9[9] = g_ret33;
	lastArgs9[10] = g_ret34;
}

bool Install(uint64_t exeBase, char* report, size_t reportSize) noexcept {
	using namespace mxl_itemevents;
	g_exeBaseForRecords = exeBase;
	g_getStat = reinterpret_cast<GetStatFn>(exeBase + GetStatRva);
	g_main20 = reinterpret_cast<Handler>(exeBase + MainFunc20);
	g_main28 = reinterpret_cast<Handler>(exeBase + MainFunc28);
	// D2RCore.dll is mapped by hand; its base is the allocation the `register` thunk's target lives in.
	const char* note = "no D2RCore.dll";
	bool ok = false;
	const auto* thunkSlot = reinterpret_cast<const uint64_t*>(exeBase + RegisterSlotRva);
	if (Readable(thunkSlot, 8) && *thunkSlot != 0) {
		MEMORY_BASIC_INFORMATION mbi{};
		if (VirtualQuery(reinterpret_cast<const void*>(*thunkSlot), &mbi, sizeof(mbi)) != 0 && mbi.AllocationBase != nullptr) {
			const uint64_t core = reinterpret_cast<uint64_t>(mbi.AllocationBase);
			auto* table = reinterpret_cast<uint64_t*>(core + CoreHandlerTableRva);
			bool match = Readable(table, 41 * 8) && Readable(g_main20, 8) && Readable(g_main28, 8);
			for (const CoreHandler& h : CoreHandlers) {
				match = match && Readable(reinterpret_cast<void*>(core + h.rva), 8) && table[h.func] == core + h.rva &&
				        std::memcmp(reinterpret_cast<void*>(core + h.rva), h.entry, h.entryLen) == 0;
			}
			if (!match) {
				note = "D2RCore.dll handlers 15 / 16 / 33 / 34 are not the expected ones";
			} else {
				g_original15 = reinterpret_cast<Handler>(table[FuncOpenWounds]);
				g_original16 = reinterpret_cast<Handler>(table[FuncCrushingBlow]);
				ok = WriteSlot(&table[FuncOpenWounds], reinterpret_cast<uint64_t>(&Gate15)) &&
				     WriteSlot(&table[FuncCrushingBlow], reinterpret_cast<uint64_t>(&Gate16)) &&
				     WriteSlot(&table[FuncHealOnKill], reinterpret_cast<uint64_t>(&HealOnKill)) &&
				     WriteSlot(&table[FuncCastOnKill], reinterpret_cast<uint64_t>(&CastOnKill));
				note = ok ? "ok" : "write failed";
			}
		}
	}
	std::snprintf(report, reportSize,
	              "%s: D2RCore handlers 33 (stat 265) -> D2R function 28, 34 (stat 376) -> D2R function 20, 15 / 16 skip monsters with stat %d (%s); "
	              "10 / 11 / 12 are D2RCore's",
	              ok ? "on" : "off", mxl_itemevents::ImmunityStat, note);
	return ok;
}

}  // namespace item_events

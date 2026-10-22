// D2Sigma's skill start functions that D2R lacks (skill_starts_logic.h has the rules).
//
// D2Sigma's server start table (0x101df948, indexed by skills.txt srvstfunc) has own functions in slots 6, 10, 28, 30, 39, 56,
// 58, 66 and 67. D2R's table (RVA 0x238e710, 91 slots) has the same function in 6, 10, 28, 39 and 56 (checked on the hit
// fields and calc offsets of each pair), a different one in 30, 58, 66 and 67, and uses 64-70 for its own skills. Median's
// skills with srvstfunc 30 (Flash, Move Only) and 66 (sk851) would run D2R's functions, so tools/txt_migrate.py moves them
// to the free slots 71 and 72 and these two functions do what D2Sigma's 0x100b00d0 / 0x100b01f0 do:
//   evaluate auralencalc (skills.txt +0x80 in D2R) for the caster; with a state (aurastate, +0xa0) in the states table and a
//   positive length, give the caster the state until frame + length, as D2R's own start functions do (0x569670): a stat list
//   of flag 2 for the state with the generic state expiry (0x436240), attached to the unit, the unit's state bit, a timed
//   event 0xc at the expiry frame. Slot 72 first refuses when the caster has the state already.
// Not ported: D2Sigma's second state-bit call (1.13c D2Common 11039, a flag word only the 1.13c unit save reads) and the
// client message D2R's buffs send afterwards (the state itself reaches the client with the next state update).
#include "skill_starts.h"

#include "d2r.h"
#include "skill_starts_logic.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr uint64_t StartTable = 0x238e710;       // 91 x 8-byte function pointers, indexed by srvstfunc
constexpr uint64_t RvaStateList = 0x2f5940;      // (unit, state) -> the unit's stat list for that state
constexpr uint64_t RvaHasState = 0x3351b0;       // (unit, state) -> non-zero when the state is on
constexpr uint64_t RvaSetState = 0x3354c0;       // (unit, state, on)
constexpr uint64_t RvaUnitType = 0x34b9d0;       // (unit) -> unit type
constexpr uint64_t RvaUnitId = 0x34a330;         // (unit) -> unit id
constexpr uint64_t RvaAllocList = 0x2f7300;      // (flags, expiry frame, unit type, unit id) -> stat list
constexpr uint64_t RvaListState = 0x2f72f0;      // (list, state)
constexpr uint64_t RvaListExpiry = 0x2f7ab0;     // (list, expiry frame)
constexpr uint64_t RvaListCallback = 0x2f7a50;   // (list, function): run when the list ends
constexpr uint64_t RvaAttachList = 0x2f2cb0;     // (unit, list, 1)
constexpr uint64_t RvaStateExpired = 0x436240;   // the generic state expiry (clears the state, tells the client)
constexpr uint64_t RvaAddTimedEvent = 0x48b720;  // (game, unit, type, frame, 0, 0, 0, 0)
constexpr uint64_t RvaDataTables = 0x300a90;     // (dataFlag) -> table block; +0x298 = rows of states.txt (int64)
constexpr int DataStateCount = 0x298;
constexpr int GameFrame = 0x170;
constexpr int TimedEventStateEnd = 0xc;
constexpr int ListFlagState = 2;

constexpr int SkAuraLenCalc = 0x80;              // skills.txt auralencalc (1.13c +0x60)
constexpr int SkAuraState = 0xa0;                // int16 aurastate (1.13c +0x80)

using StateListFn = void* (*)(Unit* unit, int32_t state);
using HasStateFn = int32_t (*)(Unit* unit, int32_t state);
using SetStateFn = void (*)(Unit* unit, int32_t state, int32_t on);
using UnitTypeFn = int32_t (*)(Unit* unit);
using UnitIdFn = uint32_t (*)(Unit* unit);
using AllocListFn = void* (*)(int32_t flags, int32_t expiry, int32_t unitType, uint32_t unitId);
using ListStateFn = void (*)(void* list, int32_t state);
using ListExpiryFn = void (*)(void* list, int32_t expiry);
using ListCallbackFn = void (*)(void* list, void* function);
using AttachListFn = void (*)(Unit* unit, void* list, int32_t one);
using AddTimedEventFn = void (*)(Game* game, Unit* unit, int32_t type, int32_t frame, int64_t a, int32_t b, int32_t c, int32_t d);
using DataTablesFn = uint8_t* (*)(uint32_t dataFlag);

uint64_t g_base = 0;
StateListFn g_stateList = nullptr;
HasStateFn g_hasState = nullptr;
SetStateFn g_setState = nullptr;
UnitTypeFn g_unitType = nullptr;
UnitIdFn g_unitId = nullptr;
AllocListFn g_allocList = nullptr;
ListStateFn g_listState = nullptr;
ListExpiryFn g_listExpiry = nullptr;
ListCallbackFn g_listCallback = nullptr;
AttachListFn g_attachList = nullptr;
AddTimedEventFn g_addEvent = nullptr;
DataTablesFn g_dataTables = nullptr;

volatile long g_casts[2] = {0, 0};
volatile long g_refused = 0;
volatile long g_lastState = 0;
volatile long g_lastExpiry = 0;

// The shared body: 0 = refused (the cast stops), 1 = the state is on.
auto StartTimedState(Game* game, Unit* unit, int32_t skillId, int32_t level, bool refuseIfPresent) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	skillstart::Input in{};
	in.state = At<int16_t>(skill, SkAuraState);
	in.duration = d2r::EvalCalc(game, unit, skill, SkAuraLenCalc, skillId, level);
	in.frame = At<int32_t>(game, GameFrame);
	in.refuseIfPresent = refuseIfPresent;
	in.present = in.state > 0 && g_hasState(unit, in.state) != 0;
	const uint8_t* tables = g_dataTables(dataFlag);
	in.stateCount = tables != nullptr ? static_cast<int32_t>(At<int64_t>(tables, DataStateCount)) : 0;
	const skillstart::Decision d = skillstart::Decide(in);
	if (!d.start) {
		InterlockedIncrement(&g_refused);
		return 0;
	}
	void* list = g_stateList(unit, in.state);
	if (list == nullptr) {
		list = g_allocList(ListFlagState, d.expiry, g_unitType(unit), g_unitId(unit));
		if (list == nullptr) {
			return 0;
		}
		g_listState(list, in.state);
		g_listCallback(list, reinterpret_cast<void*>(g_base + RvaStateExpired));
		g_attachList(unit, list, 1);
		g_setState(unit, in.state, 1);
	}
	g_listExpiry(list, d.expiry);
	g_addEvent(game, unit, TimedEventStateEnd, d.expiry, 0, 0, 0, 0);
	InterlockedExchange(&g_lastState, in.state);
	InterlockedExchange(&g_lastExpiry, d.expiry);
	return 1;
}

// D2Sigma 30 (0x100b00d0): Flash, Move Only.
auto StartTimedStateFn(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	const int64_t r = StartTimedState(game, unit, skillId, level, false);
	if (r != 0) {
		InterlockedIncrement(&g_casts[0]);
	}
	return r;
}

// D2Sigma 66 (0x100b01f0): sk851.
auto StartTimedStateOnceFn(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	const int64_t r = StartTimedState(game, unit, skillId, level, true);
	if (r != 0) {
		InterlockedIncrement(&g_casts[1]);
	}
	return r;
}

struct Slot {
	int slot;
	int64_t (*function)(Game*, Unit*, int32_t, int32_t) noexcept;
	const char* name;
};
constexpr Slot Slots[] = {
    {skillstart::SlotTimedState, StartTimedStateFn, "Median 30 timed state"},
    {skillstart::SlotTimedStateOnce, StartTimedStateOnceFn, "Median 66 timed state once"},
};

}  // namespace

auto InstallSkillStarts(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	d2r::Bind(context->exeBase);
	g_base = context->exeBase;
	g_stateList = reinterpret_cast<StateListFn>(g_base + RvaStateList);
	g_hasState = reinterpret_cast<HasStateFn>(g_base + RvaHasState);
	g_setState = reinterpret_cast<SetStateFn>(g_base + RvaSetState);
	g_unitType = reinterpret_cast<UnitTypeFn>(g_base + RvaUnitType);
	g_unitId = reinterpret_cast<UnitIdFn>(g_base + RvaUnitId);
	g_allocList = reinterpret_cast<AllocListFn>(g_base + RvaAllocList);
	g_listState = reinterpret_cast<ListStateFn>(g_base + RvaListState);
	g_listExpiry = reinterpret_cast<ListExpiryFn>(g_base + RvaListExpiry);
	g_listCallback = reinterpret_cast<ListCallbackFn>(g_base + RvaListCallback);
	g_attachList = reinterpret_cast<AttachListFn>(g_base + RvaAttachList);
	g_addEvent = reinterpret_cast<AddTimedEventFn>(g_base + RvaAddTimedEvent);
	g_dataTables = reinterpret_cast<DataTablesFn>(g_base + RvaDataTables);
	static constexpr uint8_t Empty[8] = {};
	int installed = 0;
	size_t used = 0;
	for (const Slot& s : Slots) {
		const uint64_t rva = StartTable + static_cast<uint64_t>(s.slot) * 8;
		const bool ok = context->PatchWriteU64(rva, Empty, sizeof(Empty), reinterpret_cast<uint64_t>(s.function));
		installed += ok ? 1 : 0;
		if (used < reportSize) {
			const int n = std::snprintf(report + used, reportSize - used, "%s srvstfunc %d %s%s", used ? ";" : "", s.slot, s.name,
			                            ok ? "" : " FAILED");
			used += n > 0 ? static_cast<size_t>(n) : 0;
		}
	}
	return installed;
}

auto SkillStartStats(char* out, size_t outSize) noexcept -> void {
	std::snprintf(out, outSize, "slot %d %ld starts, slot %d %ld starts, %ld refused, last state %ld until frame %ld",
	              skillstart::SlotTimedState, g_casts[0], skillstart::SlotTimedStateOnce, g_casts[1], g_refused, g_lastState, g_lastExpiry);
}

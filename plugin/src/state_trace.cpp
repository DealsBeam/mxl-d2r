// Diagnosis (bug #22, monsters cannot be hovered with the states fix on): logs which states D2R sets on which
// units, and which state-mask checks answer "yes" for which units, each distinct case once. Off unless
// mxl-core-off.txt lists the word "trace".
#include "state_trace.h"

#include <cstdint>
#include <cstdio>
#include <intrin.h>

namespace {

constexpr uint64_t SetStateRva = 0x2f7ea0;  // (unit, state, on)
// PUSH RBX (REX) / PUSH RSI / PUSH R15 / SUB RSP,0x50 / MOVSXD RBX,EDX / MOV R15D,R8D
constexpr uint8_t SetStateEntry[] = {0x40, 0x53, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x50, 0x48, 0x63, 0xda, 0x45, 0x8b, 0xf8};
constexpr uint64_t MaskCheckRva = 0x336440;  // (unit, mask) -> 1 when the unit has any state of the mask
// MOV [RSP+0x8],RBX / MOV [RSP+0x10],RBP / PUSH RSI / PUSH RDI / PUSH R14
constexpr uint8_t MaskCheckEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x56, 0x57, 0x41, 0x56};
constexpr uint64_t SkillCheckRva = 0x33f360;  // (unit, skill entry) -> 0 usable, else a reason code (D2Common Skills.cpp)
// PUSH RSI / PUSH RDI / SUB RSP,0x28 / MOV RDI,RDX / MOV RSI,RCX / TEST RDX,RDX
constexpr uint8_t SkillCheckEntry[] = {0x40, 0x56, 0x57, 0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xf1, 0x48, 0x85, 0xd2};
constexpr uint64_t WhirlStartRva = 0x56a420;  // srvstfunc 38, Whirlwind's start (game, unit, skill, level) -> 1 started
// MOV [RSP+0x10],RBX / MOV [RSP+0x20],R9D / PUSH RBP / PUSH RSI / PUSH RDI / PUSH R12
constexpr uint8_t WhirlStartEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x44, 0x89, 0x4c, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54};
constexpr uint64_t SkillStartRva = 0x43b3e0;  // server: (game, unit, skill, level, force) -> start a skill (calls srvstfunc)
// MOV [RSP+0x10],RBX / MOV [RSP+0x18],RBP / MOV [RSP+0x20],RSI
constexpr uint8_t SkillStartEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};
constexpr uint64_t UnitRoomRva = 0x34b440;    // (unit) -> room
constexpr uint64_t RoomInTownRva = 0x2f0750;  // (room) -> 1 in a town
constexpr uint64_t HasStateRva = 0x3351b0;   // (unit, state)
constexpr uint64_t UnitClassRva = 0x349860;  // (unit, file, line) -> class id
constexpr uint64_t DataFlagRva = 0x34a0e0;   // (unit) -> data flag byte

using SetStateFn = void (*)(void* unit, int32_t state, int32_t on) noexcept;
using MaskCheckFn = int32_t (*)(void* unit, const uint32_t* mask) noexcept;
using SkillCheckFn = int32_t (*)(void* unit, int64_t* entry) noexcept;
using WhirlStartFn = int64_t (*)(void* game, void* unit, int32_t skill, int32_t level) noexcept;
using SkillStartFn = int32_t (*)(void* game, void* unit, int32_t skill, int32_t level, int32_t force) noexcept;
using UnitRoomFn = void* (*)(void* unit) noexcept;
using RoomInTownFn = int32_t (*)(void* room) noexcept;
using HasStateFn = int32_t (*)(void* unit, int32_t state) noexcept;
using UnitClassFn = int32_t (*)(void* unit, const char* file, int32_t line) noexcept;
using DataFlagFn = uint8_t (*)(void* unit) noexcept;

const D2RL::PluginContext* g_context = nullptr;
uint64_t g_base = 0;
SetStateFn g_setState = nullptr;
MaskCheckFn g_maskCheck = nullptr;
UnitClassFn g_unitClass = nullptr;
DataFlagFn g_dataFlag = nullptr;
SkillCheckFn g_skillCheck = nullptr;
WhirlStartFn g_whirlStart = nullptr;
HasStateFn g_hasState = nullptr;
SkillStartFn g_skillStart = nullptr;
UnitRoomFn g_unitRoom = nullptr;
RoomInTownFn g_roomInTown = nullptr;
volatile long g_startLines = 0;
volatile long g_whirlLines = 0;

constexpr int MaxSeen = 1500;
uint64_t g_seen[MaxSeen];
int g_seenCount = 0;
volatile long g_lock = 0;

// True the first time a key is seen (a plain spin lock: the server and client threads both set states).
auto FirstTime(uint64_t key) noexcept -> bool {
	while (_InterlockedCompareExchange(&g_lock, 1, 0) != 0) {
	}
	bool first = g_seenCount < MaxSeen;
	for (int i = 0; i < g_seenCount && first; ++i) {
		first = g_seen[i] != key;
	}
	if (first) {
		g_seen[g_seenCount++] = key;
	}
	_InterlockedExchange(&g_lock, 0);
	return first;
}

void Describe(void* unit, uint32_t* type, int32_t* cls, uint32_t* flag) noexcept {
	*type = unit ? *static_cast<const uint32_t*>(unit) : 99;
	*cls = unit ? g_unitClass(unit, __FILE__, __LINE__) : -1;
	*flag = unit ? g_dataFlag(unit) : 0;
}

void SetState(void* unit, int32_t state, int32_t on) noexcept {
	if (unit != nullptr) {
		uint32_t type, flag;
		int32_t cls;
		Describe(unit, &type, &cls, &flag);
		const uint64_t key = (1ull << 63) | (uint64_t(flag & 0xff) << 48) | (uint64_t(type & 0xff) << 40) |
		                     (uint64_t(cls & 0xffff) << 24) | (uint64_t(on != 0) << 16) | uint64_t(state & 0xffff);
		if (FirstTime(key)) {
			char line[160];
			std::snprintf(line, sizeof(line), "mxl-core trace: state %d %s on type %u class %d (flag %u)", state,
			              on ? "set" : "cleared", type, cls, flag);
			g_context->LogInfo(line);
		}
	}
	g_setState(unit, state, on);
}

auto MaskCheck(void* unit, const uint32_t* mask) noexcept -> int32_t {
	const int32_t result = g_maskCheck(unit, mask);
	if (unit != nullptr && result != 0) {
		const uint64_t caller = reinterpret_cast<uint64_t>(_ReturnAddress()) - g_base;
		uint32_t type, flag;
		int32_t cls;
		Describe(unit, &type, &cls, &flag);
		const uint64_t key = (uint64_t(type & 0xff) << 56) | (uint64_t(cls & 0xffff) << 40) | (caller & 0xffffffffff);
		if (FirstTime(key)) {
			char line[160];
			std::snprintf(line, sizeof(line), "mxl-core trace: mask check yes from 0x%llx on type %u class %d (flag %u)",
			              static_cast<unsigned long long>(caller), type, cls, flag);
			g_context->LogInfo(line);
		}
	}
	return result;
}

// Why a skill cannot be used: each distinct (skill, unit kind, answer) once.
auto SkillCheck(void* unit, int64_t* entry) noexcept -> int32_t {
	const int32_t result = g_skillCheck(unit, entry);
	if (unit != nullptr && entry != nullptr && *entry != 0) {
		const int32_t skill = *reinterpret_cast<const int16_t*>(*entry);
		const uint32_t type = *static_cast<const uint32_t*>(unit);
		const uint64_t key = (2ull << 62) | (uint64_t(type & 0xff) << 40) | (uint64_t(result & 0xff) << 24) | uint64_t(skill & 0xffff);
		if (type == 0 && FirstTime(key)) {
			char line[160];
			std::snprintf(line, sizeof(line), "mxl-core trace: skill %d usable check on a player -> %d (0 = usable)", skill, result);
			g_context->LogInfo(line);
		}
	}
	return result;
}

// Server skill starts by players (the code that calls srvstfunc): skill, result, in town; the first 60.
auto SkillStart(void* game, void* unit, int32_t skill, int32_t level, int32_t force) noexcept -> int32_t {
	const int32_t result = g_skillStart(game, unit, skill, level, force);
	if (unit != nullptr && *static_cast<const uint32_t*>(unit) == 0 && _InterlockedIncrement(&g_startLines) <= 60) {
		void* room = g_unitRoom(unit);
		char line[160];
		std::snprintf(line, sizeof(line), "mxl-core trace: server skill start %d level %d force %d -> %d (in town %d)", skill,
		              level, force, result, room ? g_roomInTown(room) : -1);
		g_context->LogInfo(line);
	}
	return result;
}

// Whirlwind's server start: what it returns, the first 40 times.
auto WhirlStart(void* game, void* unit, int32_t skill, int32_t level) noexcept -> int64_t {
	const int32_t blocked = unit ? g_hasState(unit, 12) : -1;
	const int64_t result = g_whirlStart(game, unit, skill, level);
	if (_InterlockedIncrement(&g_whirlLines) <= 40) {
		char line[160];
		std::snprintf(line, sizeof(line), "mxl-core trace: whirlwind start skill %d level %d -> %lld (state 12 before: %d)", skill,
		              level, static_cast<long long>(result), blocked);
		g_context->LogInfo(line);
	}
	return result;
}

}  // namespace

auto InstallStateTrace(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	g_base = context->exeBase;
	g_unitClass = reinterpret_cast<UnitClassFn>(context->exeBase + UnitClassRva);
	g_dataFlag = reinterpret_cast<DataFlagFn>(context->exeBase + DataFlagRva);
	g_hasState = reinterpret_cast<HasStateFn>(context->exeBase + HasStateRva);
	g_unitRoom = reinterpret_cast<UnitRoomFn>(context->exeBase + UnitRoomRva);
	g_roomInTown = reinterpret_cast<RoomInTownFn>(context->exeBase + RoomInTownRva);
	const bool ok =
		context->InstallInlineHook(SetStateRva, SetStateEntry, sizeof(SetStateEntry), SetState, &g_setState) &&
		context->InstallInlineHook(MaskCheckRva, MaskCheckEntry, sizeof(MaskCheckEntry), MaskCheck, &g_maskCheck) &&
		context->InstallInlineHook(SkillCheckRva, SkillCheckEntry, sizeof(SkillCheckEntry), SkillCheck, &g_skillCheck) &&
		context->InstallInlineHook(SkillStartRva, SkillStartEntry, sizeof(SkillStartEntry), SkillStart, &g_skillStart) &&
		context->InstallInlineHook(WhirlStartRva, WhirlStartEntry, sizeof(WhirlStartEntry), WhirlStart, &g_whirlStart);
	std::snprintf(report, reportSize, ok ? "on: logging state changes, state-mask hits, player skill checks, server skill starts, Whirlwind starts" : "off: hook failed");
	return ok;
}

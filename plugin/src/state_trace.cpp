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
constexpr uint64_t UnitClassRva = 0x349860;  // (unit, file, line) -> class id
constexpr uint64_t DataFlagRva = 0x34a0e0;   // (unit) -> data flag byte

using SetStateFn = void (*)(void* unit, int32_t state, int32_t on) noexcept;
using MaskCheckFn = int32_t (*)(void* unit, const uint32_t* mask) noexcept;
using UnitClassFn = int32_t (*)(void* unit, const char* file, int32_t line) noexcept;
using DataFlagFn = uint8_t (*)(void* unit) noexcept;

const D2RL::PluginContext* g_context = nullptr;
uint64_t g_base = 0;
SetStateFn g_setState = nullptr;
MaskCheckFn g_maskCheck = nullptr;
UnitClassFn g_unitClass = nullptr;
DataFlagFn g_dataFlag = nullptr;

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

}  // namespace

auto InstallStateTrace(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	g_base = context->exeBase;
	g_unitClass = reinterpret_cast<UnitClassFn>(context->exeBase + UnitClassRva);
	g_dataFlag = reinterpret_cast<DataFlagFn>(context->exeBase + DataFlagRva);
	const bool ok =
		context->InstallInlineHook(SetStateRva, SetStateEntry, sizeof(SetStateEntry), SetState, &g_setState) &&
		context->InstallInlineHook(MaskCheckRva, MaskCheckEntry, sizeof(MaskCheckEntry), MaskCheck, &g_maskCheck);
	std::snprintf(report, reportSize, ok ? "on: logging state changes and state-mask hits" : "off: hook failed");
	return ok;
}

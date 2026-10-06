// Mana costs as D2Sigma computes them (docs/plugin-spec.md, "Mana costs").
//
// D2Sigma's cost check (0x100b0af0) is ((level - 1) * lvlmana + mana) << manashift, where mana and lvlmana are
// skills2 formulas evaluated for the caster and lvlmana is scaled by (100 + stat 228) / 100 (Median's mana cost %,
// D2Resource.cpp 0x100a1dc0). D2R's cost function `0x33aa00` (dataFlag, skillId, level) has the same shape but reads
// the 16-bit mana/lvlmana columns and gets no unit. tools/txt_migrate.py writes the constant formulas into those
// columns and the 17 others (16 summons' petmax-based lvlmana, titan_s_fortitude's) into calc7 (lvlmana) and calc8
// (mana), which no Median skill uses. The hook evaluates them for the caster and applies stat 228.
//
// The unit: D2R's two charge sites call `0x33aa00` with it in a callee-saved register (server `0x436830` body: RDI,
// client `0x2188b0`: RBX). A stub passes that register as a fourth argument, picked by the return address; other
// callers (tooltips, AI checks) get D2R's own value.
#include "mana_costs.h"

#include "d2r.h"

#define NOMINMAX
#include <windows.h>

#include <intrin.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Unit;

constexpr uint64_t ManaCostRva = 0x33aa00;
constexpr uint64_t ServerChargeReturn = 0x436a00;  // after CALL 0x33aa00 at 0x4369fb, unit in RDI
constexpr uint64_t ClientChargeReturn = 0x21893c;  // after CALL 0x33aa00 at 0x218937, unit in RBX
constexpr uint64_t GetStatRva = 0x2f5020;          // (unit, stat, layer)
// MOV [RSP+8],RBX / PUSH RDI / SUB RSP,0x20 / MOV EDI,R8D / XOR EBX,EBX
constexpr uint8_t ManaCostEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
                                     0xec, 0x20, 0x41, 0x8b, 0xf8, 0x33, 0xdb};

constexpr int SkCalc7 = 0x1a8;      // lvlmana formula (txt_migrate.py skill_costs)
constexpr int SkCalc8 = 0x1ac;      // mana formula
constexpr int SkMinMana = 0x226;    // int16
constexpr int SkManaShift = 0x228;  // uint8
constexpr int SkMana = 0x22a;       // int16
constexpr int SkLvlMana = 0x22c;    // int16
constexpr int32_t StatManaCostPercent = 228;

using ManaCostFn = int32_t (*)(uint32_t dataFlag, int32_t skillId, int32_t level);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);

ManaCostFn g_original = nullptr;
GetStatFn g_getStat = nullptr;
uint64_t g_exeBase = 0;

auto ManaCost(uint32_t dataFlag, int32_t skillId, int32_t level, Unit* unit) noexcept -> int32_t {
	const uint64_t from = reinterpret_cast<uint64_t>(_ReturnAddress()) - g_exeBase;
	if (unit == nullptr || (from != ServerChargeReturn && from != ClientChargeReturn)) {
		return g_original(dataFlag, skillId, level);
	}
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag & 0xff, skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t manaCalc = At<int32_t>(skill, SkCalc8);
	const int32_t lvlManaCalc = At<int32_t>(skill, SkCalc7);
	const int32_t mana = manaCalc >= 0 ? d2r::api.evalSkillCalc(dataFlag & 0xff, unit, manaCalc, skillId, level)
	                                   : At<int16_t>(skill, SkMana);
	int32_t lvlMana = lvlManaCalc >= 0 ? d2r::api.evalSkillCalc(dataFlag & 0xff, unit, lvlManaCalc, skillId, level)
	                                   : At<int16_t>(skill, SkLvlMana);
	if (mana == 0 && lvlMana == 0) {
		return 0;
	}
	lvlMana = static_cast<int32_t>(static_cast<int64_t>(lvlMana) * (100 + g_getStat(unit, StatManaCostPercent, 0)) / 100);
	const int64_t cost = (static_cast<int64_t>(std::max(level - 1, 0)) * lvlMana + mana) << (At<uint8_t>(skill, SkManaShift) & 31);
	const int64_t minimum = static_cast<int64_t>(At<int16_t>(skill, SkMinMana)) << 8;
	return static_cast<int32_t>(std::clamp<int64_t>(std::max(cost, minimum), 0, INT32_MAX));
}

// mov rax,[rsp] / mov r9,rdi / mov r10,<client return> / cmp rax,r10 / jne +3 / mov r9,rbx / jmp [rip] / <ManaCost>
auto BuildStub(uint64_t clientReturn) noexcept -> void* {
	uint8_t code[48] = {0x48, 0x8b, 0x04, 0x24, 0x49, 0x89, 0xf9, 0x49, 0xba};
	size_t n = 9;
	std::memcpy(code + n, &clientReturn, 8);
	n += 8;
	const uint8_t tail[] = {0x4c, 0x39, 0xd0, 0x75, 0x03, 0x49, 0x89, 0xd9, 0xff, 0x25, 0x00, 0x00, 0x00, 0x00};
	std::memcpy(code + n, tail, sizeof(tail));
	n += sizeof(tail);
	const uint64_t target = reinterpret_cast<uint64_t>(&ManaCost);
	std::memcpy(code + n, &target, 8);
	n += 8;
	void* stub = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (stub == nullptr) {
		return nullptr;
	}
	std::memcpy(stub, code, n);
	DWORD old = 0;
	if (!VirtualProtect(stub, 4096, PAGE_EXECUTE_READ, &old)) {
		VirtualFree(stub, 0, MEM_RELEASE);
		return nullptr;
	}
	FlushInstructionCache(GetCurrentProcess(), stub, n);
	return stub;
}

}  // namespace

auto InstallManaCosts(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_exeBase = context->exeBase;
	g_getStat = reinterpret_cast<GetStatFn>(g_exeBase + GetStatRva);
	void* stub = BuildStub(g_exeBase + ClientChargeReturn);
	const bool ok = stub != nullptr &&
	                context->InstallInlineHook(ManaCostRva, ManaCostEntry, sizeof(ManaCostEntry), stub,
	                                           reinterpret_cast<void**>(&g_original));
	std::snprintf(report, reportSize, ok ? "on: calc7/calc8 cost formulas, stat 228 mana cost %%"
	                                     : (stub == nullptr ? "off: no stub memory" : "off: mana cost hook failed"));
	return ok;
}

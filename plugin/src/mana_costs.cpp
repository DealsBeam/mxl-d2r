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
// callers (tooltips, AI checks) get D2R's own value. D2RLoader runs a relocated copy of the server function, so the site
// is told by its return address or, there, by the instructions after the call (ChargeSite).
#include "mana_costs.h"

#include "d2r.h"
#include "item_rules_logic.h"

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
constexpr uint64_t SkillUseRva = 0x43acb0;         // server skill use (game, unit, skill id, ...): the caster is in RDX
// mov [rsp+0x10],rbx / mov [rsp+0x20],r9d / mov [rsp+0x18],r8d / push rbp / push rsi / push rdi / push r12
constexpr uint8_t SkillUseEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x44, 0x89, 0x4c, 0x24, 0x20,
                                     0x44, 0x89, 0x44, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54};
constexpr uint64_t GetStatRva = 0x2f5020;          // (unit, stat, layer)
constexpr uint64_t UnitRoomRva = 0x34b440;         // (unit) -> room
constexpr uint64_t RoomIsTownRva = 0x2f0750;       // (room) -> non-zero in a town
constexpr uint8_t ServerAfterCall[] = {0x8b, 0xd8, 0x85, 0xc0, 0x7f, 0x04};  // mov ebx,eax / test eax,eax / jg +4
// D2RLoader's copy of the server function (a re-assembled body, registers reallocated): mov ebx,eax / test eax,eax / mov eax,imm
constexpr uint8_t RelocatedServerAfterCall[] = {0x89, 0xc3, 0x85, 0xc0, 0xb8};
constexpr uint8_t ClientAfterCall[] = {0x8b, 0xf8, 0x85, 0xc0, 0x7e};        // mov edi,eax / test eax,eax / jle
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
using UnitRoomFn = void* (*)(Unit* unit);
using RoomIsTownFn = int64_t (*)(void* room);

ManaCostFn g_original = nullptr;
GetStatFn g_getStat = nullptr;
UnitRoomFn g_unitRoom = nullptr;
RoomIsTownFn g_roomIsTown = nullptr;
bool g_townFree = false;
Unit* volatile g_caster = nullptr;  // the unit of the last server skill use (SkillUseEntry stub)
void* g_skillUseNext = nullptr;      // the stub's way on: D2R's skill use behind the hook
ManaChargeLog g_log;
uint64_t g_exeBase = 0;

// D2Sigma's cost at a charge site: the formulas, stat 228, the minimum. The affordability test (SkillAfford) has no minimum.
auto SigmaCost(uint32_t dataFlag, int32_t skillId, int32_t level, Unit* unit, bool minimum = true) noexcept -> int32_t {
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
	const int64_t floorCost = minimum ? static_cast<int64_t>(At<int16_t>(skill, SkMinMana)) << 8 : 0;
	return static_cast<int32_t>(std::clamp<int64_t>(std::max(cost, floorCost), 0, INT32_MAX));
}

// Which charge site the call comes from: 1 server, 2 client, 0 any other caller. D2RLoader's hook of the server function
// (0x436830) runs a relocated copy of its body, so the return address is not 0x436a00 there: the instructions after the
// CALL tell (`mov ebx,eax; test eax,eax; jg` on the server, `mov edi,eax; test eax,eax; jle` on the client).
auto ChargeSite(const uint8_t* ret) noexcept -> int {
	const uint64_t rva = reinterpret_cast<uint64_t>(ret) - g_exeBase;
	if (rva == ServerChargeReturn || std::memcmp(ret, ServerAfterCall, sizeof(ServerAfterCall)) == 0) {
		return 1;
	}
	if (rva == ClientChargeReturn || std::memcmp(ret, ClientAfterCall, sizeof(ClientAfterCall)) == 0) {
		return 2;
	}
	if (std::memcmp(ret, RelocatedServerAfterCall, sizeof(RelocatedServerAfterCall)) == 0) {
		return 3;
	}
	return 0;
}

// The caster of the relocated server copy: its unit register is not known there, so the skill use function leaves the unit
// in g_caster. Only a player counts (type 0); anything unreadable is no caster.
auto Caster() noexcept -> Unit* {
	__try {
		Unit* unit = g_caster;
		return unit != nullptr && At<int32_t>(unit, d2r::UnitType) == 0 ? unit : nullptr;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

auto ManaCost(uint32_t dataFlag, int32_t skillId, int32_t level, Unit* unit) noexcept -> int32_t {
	const uint8_t* ret = static_cast<const uint8_t*>(_ReturnAddress());
	int site = ChargeSite(ret);
	if (site == 3) {
		unit = Caster();
	}
	if (site == 0 || unit == nullptr) {
		g_log.otherCalls++;
		g_log.otherFrom = reinterpret_cast<uint64_t>(ret);
		g_log.otherSkill = skillId;
		return g_original(dataFlag, skillId, level);
	}
	// Audit SK3 (P2.1.0): no mana in a town. Only the two charge sites get here, so tooltips and AI checks keep the cost.
	const int32_t cost = SigmaCost(dataFlag, skillId, level, unit);
	void* room = g_unitRoom(unit);
	const bool town = room != nullptr && g_roomIsTown(room) != 0;
	const int32_t charged = itemrules::ManaCharge(cost, g_townFree && town, g_townFree);
	g_log.calls++;
	g_log.room = room;
	g_log.roomIsTown = town ? 1 : 0;
	g_log.cost = cost >> 8;
	g_log.charged = charged >> 8;
	g_log.site = site;
	return charged;
}

// mov rax,[rsp] / mov r9,rdi / cmp byte [rax+1],0xf8 / jne +3 / mov r9,rbx / jmp [rip] / <ManaCost>
// The unit is in RDI at the server site and in RBX at the client site (`mov edi,eax` there: second byte 0xf8).
auto BuildStub() noexcept -> void* {
	uint8_t code[48] = {0x48, 0x8b, 0x04, 0x24, 0x49, 0x89, 0xf9, 0x80, 0x78, 0x01, 0xf8, 0x75, 0x03, 0x49, 0x89, 0xd9,
	                    0xff, 0x25, 0x00, 0x00, 0x00, 0x00};
	size_t n = 22;
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

// mov r11,&g_caster / mov [r11],rdx / mov r11,&g_skillUseNext / jmp [r11]
auto BuildSkillUseStub() noexcept -> void* {
	uint8_t code[40] = {0x49, 0xbb};
	size_t n = 2;
	const uint64_t caster = reinterpret_cast<uint64_t>(&g_caster);
	std::memcpy(code + n, &caster, 8);
	n += 8;
	const uint8_t store[] = {0x49, 0x89, 0x13, 0x49, 0xbb};
	std::memcpy(code + n, store, sizeof(store));
	n += sizeof(store);
	const uint64_t next = reinterpret_cast<uint64_t>(&g_skillUseNext);
	std::memcpy(code + n, &next, 8);
	n += 8;
	const uint8_t jump[] = {0x41, 0xff, 0x23};
	std::memcpy(code + n, jump, sizeof(jump));
	n += sizeof(jump);
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

// --- Affordability (audit common2-016 (a), D2Sigma D2SkillValidation.cpp 0x100b0b60) ---------------------------------------
// D2R's afford test 0x340900(unit, skill entry) reads the 16-bit mana / lvlmana columns: the 17 skills whose cost is a
// formula (calc7 / calc8) read 0 there and are always "affordable", and stat 228 (mana cost %) never scales the lvlmana
// part. D2Sigma uses the same formulas as the charge: ((level - 1) * lvlmana * (100 + stat 228) / 100 + mana) << shift
// against the mana stat (the life stat for the blood-cost state). The hook keeps the rest of D2R's test as it is (the 0x74
// special case) and replaces only the cost, and only for a player and only when the record has a formula or the unit has
// a mana cost %; any other call gets D2R's answer.
constexpr uint64_t AffordRva = 0x340900;
// push rbp / push rsi / push r12 / push r15 / sub rsp,0x28 / xor r8d,r8d / mov rsi,rdx / mov
constexpr uint8_t AffordEntry[] = {0x40, 0x55, 0x56, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83, 0xec, 0x28, 0x45, 0x33, 0xc0, 0x48, 0x8b};
constexpr uint64_t SkillLevelBonusRva = 0x3400a0;  // (unit, entry, 0) -> levels added by items
constexpr uint64_t UnitDataFlagRva = 0x34a0e0;     // (unit) -> the data table flag byte
constexpr uint64_t MaxSkillLevelRva = 0x300c70;    // (dataFlag, 0) -> the highest skill level the tables allow
constexpr uint64_t HasStateRva = 0x3351b0;         // (unit, state)
constexpr uint64_t SpecialUnitRva = 0x336090;      // (unit): the 0x74 special case of the test
constexpr int EntryLevel = 0x40;                   // int32: the skill's own level
constexpr int EntryKind = 0x4c;                    // int32: -1 for an ordinary skill
constexpr int SkSpecial = 0x4e;                    // int16 of the record
constexpr int32_t StateLifeCost = 0x72;
constexpr int32_t StatMana = 8;
constexpr int32_t StatLife = 6;

using AffordFn = uint64_t (*)(Unit* unit, int64_t* entry);
using BonusFn = int32_t (*)(Unit* unit, int64_t* entry, int32_t zero);
using DataFlagByteFn = uint8_t (*)(Unit* unit);
using MaxLevelFn = int32_t (*)(uint8_t dataFlag, int32_t zero);
using HasStateFn = int64_t (*)(Unit* unit, int32_t state);
using SpecialFn = int64_t (*)(Unit* unit);

AffordFn g_affordOriginal = nullptr;
BonusFn g_levelBonus = nullptr;
DataFlagByteFn g_dataFlagByte = nullptr;
MaxLevelFn g_maxLevel = nullptr;
HasStateFn g_hasState = nullptr;
SpecialFn g_special = nullptr;
volatile LONG g_affordCalls = 0, g_affordChanged = 0;

// The skill level D2R's test uses: own level plus the item bonus, at least 0, at most the table's top level.
auto AffordLevel(Unit* unit, int64_t* entry) noexcept -> int32_t {
	const int32_t sum = At<int32_t>(entry, EntryLevel) + g_levelBonus(unit, entry, 0);
	const int32_t level = sum > 0 ? sum : 0;
	const int32_t top = g_maxLevel(g_dataFlagByte(unit), 0);
	return top <= level ? top : level;
}

auto SkillAfford(Unit* unit, int64_t* entry) noexcept -> uint64_t {
	const uint64_t original = g_affordOriginal(unit, entry);
	if (unit == nullptr || entry == nullptr || *entry == 0 || At<int32_t>(entry, EntryKind) != -1 ||
	    At<int32_t>(unit, d2r::UnitType) != 0) {
		return original;
	}
	const uint8_t* skill = reinterpret_cast<const uint8_t*>(*entry);
	const int32_t percent = g_getStat(unit, StatManaCostPercent, 0);
	if (At<int32_t>(skill, SkCalc7) < 0 && At<int32_t>(skill, SkCalc8) < 0 && percent == 0) {
		return original;  // the 16-bit columns are the whole cost: D2R's test is right
	}
	const bool lifeCost = g_hasState(unit, StateLifeCost) != 0;
	if (!lifeCost && At<int16_t>(skill, SkSpecial) == 0x74 && g_special(unit) != 0) {
		return original;
	}
	const int32_t level = AffordLevel(unit, entry);
	const int32_t cost = SigmaCost(g_dataFlagByte(unit), At<int16_t>(skill, 0), level, unit, false);
	const int32_t have = g_getStat(unit, lifeCost ? StatLife : StatMana, 0);
	const uint64_t verdict = cost <= have ? 1 : 0;
	InterlockedIncrement(&g_affordCalls);
	if (verdict != original) {
		InterlockedIncrement(&g_affordChanged);
	}
	return verdict;
}

}  // namespace

auto LastManaCharge() noexcept -> ManaChargeLog {
	return g_log;
}

auto SkillAffordLog() noexcept -> AffordLog {
	return {static_cast<unsigned>(g_affordCalls), static_cast<unsigned>(g_affordChanged)};
}

auto SigmaSkillCost(Unit* unit, int64_t* entry) noexcept -> int32_t {
	if (unit == nullptr || entry == nullptr || *entry == 0 || g_dataFlagByte == nullptr) {
		return -1;
	}
	const uint8_t* skill = reinterpret_cast<const uint8_t*>(*entry);
	return SigmaCost(g_dataFlagByte(unit), At<int16_t>(skill, 0), AffordLevel(unit, entry), unit, false);
}

void ResolveAffordFunctions(const D2RL::PluginContext* context) noexcept {
	g_exeBase = context->exeBase;
	g_getStat = reinterpret_cast<GetStatFn>(g_exeBase + GetStatRva);
	g_levelBonus = reinterpret_cast<BonusFn>(g_exeBase + SkillLevelBonusRva);
	g_dataFlagByte = reinterpret_cast<DataFlagByteFn>(g_exeBase + UnitDataFlagRva);
	g_maxLevel = reinterpret_cast<MaxLevelFn>(g_exeBase + MaxSkillLevelRva);
	g_hasState = reinterpret_cast<HasStateFn>(g_exeBase + HasStateRva);
	g_special = reinterpret_cast<SpecialFn>(g_exeBase + SpecialUnitRva);
}

auto InstallSkillAfford(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	ResolveAffordFunctions(context);
	const bool ok = context->InstallInlineHook(AffordRva, AffordEntry, sizeof(AffordEntry), SkillAfford,
	                                           reinterpret_cast<void**>(&g_affordOriginal));
	std::snprintf(report, reportSize, ok ? "on: affordability from the calc7/calc8 formulas and stat 228 (0x340900)"
	                                     : "off: afford test hook failed (0x340900 entry bytes differ?)");
	return ok;
}

auto InstallManaCosts(const D2RL::PluginContext* context, bool townFree, char* report, size_t reportSize) noexcept -> bool {
	g_exeBase = context->exeBase;
	g_getStat = reinterpret_cast<GetStatFn>(g_exeBase + GetStatRva);
	g_unitRoom = reinterpret_cast<UnitRoomFn>(g_exeBase + UnitRoomRva);
	g_roomIsTown = reinterpret_cast<RoomIsTownFn>(g_exeBase + RoomIsTownRva);
	g_townFree = townFree;
	void* stub = BuildStub();
	void* skillUseStub = townFree ? BuildSkillUseStub() : nullptr;
	// The caster hook only serves the town rule (the relocated server copy); without it the server charge keeps D2R's value.
	const bool casterOk = !townFree || (skillUseStub != nullptr &&
	                                    context->InstallInlineHook(SkillUseRva, SkillUseEntry, sizeof(SkillUseEntry), skillUseStub,
	                                                               reinterpret_cast<void**>(&g_skillUseNext)));
	const bool ok = stub != nullptr &&
	                context->InstallInlineHook(ManaCostRva, ManaCostEntry, sizeof(ManaCostEntry), stub,
	                                           reinterpret_cast<void**>(&g_original));
	std::snprintf(report, reportSize, ok ? (!townFree ? "on: calc7/calc8 cost formulas, stat 228 mana cost %%, town mana off"
	                                          : casterOk ? "on: calc7/calc8 cost formulas, stat 228 mana cost %%, free in town (caster hook 0x43acb0)"
	                                                     : "on: calc7/calc8 cost formulas, stat 228 mana cost %%, free in town on the client only (caster hook failed)")
	                                     : (stub == nullptr ? "off: no stub memory" : "off: mana cost hook failed"));
	return ok;
}

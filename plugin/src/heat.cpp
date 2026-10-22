// The Fire Sorceress's Heat (docs/plugin-spec.md, "Heat").
//
// Median's Fire Sorceress has no mana: the mana bar is heat. Her fire tree skills cost nothing (skills2 formulas of 0,
// so the data already carries that), Molten Core's passive puts the heat loss rate in stat 297 (passivecalc1:
// 100 + level / 3 - mana regeneration % / 10) and Emberstep, Flamefront and Flamestrike put a negative rate on her for a
// while (aura and missile hit state), which builds heat. Only D2Sigma's mana regeneration reads stat 297
// (D2Resource.cpp 0x100a1b50): with it set the regeneration becomes a drain, none in a town.
//
// D2R's per-tick regeneration is 0x42e350 (game, unit) -> 1 when the mana stat changed. The hook lets D2R run it for every
// unit without stat 297 and runs D2Sigma's drain (heat_logic.h) for the others. The charstats record (regeneration
// seconds at +0x36) comes from 0xfcfc0 (dataFlag, class), as in D2R's own function.
#include "heat.h"

#include "d2r.h"
#include "heat_logic.h"

#include <cstdint>
#include <cstdio>

namespace {

using d2r::Unit;

constexpr uint64_t RegenRva = 0x42e350;       // (game, unit) -> 1 when mana changed
constexpr uint64_t GetStatRva = 0x2f5020;     // (unit, stat, layer)
constexpr uint64_t MaxManaRva = 0x2f4e20;     // (unit) -> maximum mana (x256)
constexpr uint64_t AddStatRva = 0x2f34f0;     // (unit, stat, value, layer)
constexpr uint64_t HasStateRva = 0x3351b0;    // (unit, state) -> non-zero when set
constexpr uint64_t UnitClassRva = 0x349860;   // (unit, file, line) -> class id
constexpr uint64_t CharStatsRva = 0xfcfc0;    // (dataFlag, class) -> charstats record
constexpr uint64_t UnitRoomRva = 0x34b440;    // (unit) -> room
constexpr uint64_t RoomInTownRva = 0x2f0750;  // (room) -> 1 in a town
// mov [rsp+0x10],rbx / mov [rsp+0x18],rbp / push rsi / push rdi / push r15 / sub rsp,0x20
constexpr uint8_t RegenEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x57, 0x48, 0x83, 0xec, 0x20};
constexpr int CharStatsRegenSeconds = 0x36;  // uint8
constexpr int32_t StatMana = 8;
constexpr int32_t StatManaRecovery = 26;
constexpr int32_t StatManaRecoveryBonus = 27;
constexpr int32_t StatHeatRate = 297;
constexpr int32_t StateNoRegen = 0x55;

using RegenFn = uint64_t (*)(d2r::Game* game, Unit* unit) noexcept;
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using MaxManaFn = int32_t (*)(Unit* unit);
using AddStatFn = void (*)(Unit* unit, int32_t stat, int32_t value, int32_t layer);
using HasStateFn = uint64_t (*)(Unit* unit, int32_t state);
using UnitClassFn = int32_t (*)(Unit* unit, const char* file, int32_t line);
using CharStatsFn = const uint8_t* (*)(uint32_t dataFlag, int32_t classId);
using UnitRoomFn = void* (*)(Unit* unit);
using RoomInTownFn = int32_t (*)(void* room);

RegenFn g_original = nullptr;
GetStatFn g_getStat = nullptr;
MaxManaFn g_maxMana = nullptr;
AddStatFn g_addStat = nullptr;
HasStateFn g_hasState = nullptr;
UnitClassFn g_unitClass = nullptr;
CharStatsFn g_charStats = nullptr;
UnitRoomFn g_unitRoom = nullptr;
RoomInTownFn g_roomInTown = nullptr;

auto Regen(d2r::Game* game, Unit* unit) noexcept -> uint64_t {
	if (unit == nullptr || game == nullptr) {
		return g_original(game, unit);
	}
	const int32_t rate = g_getStat(unit, StatHeatRate, 0);
	if (rate == 0) {
		return g_original(game, unit);
	}
	void* room = g_unitRoom(unit);
	if (room != nullptr && g_roomInTown(room) != 0) {
		return 0;  // no heat loss in a town (and no regeneration)
	}
	heat::Regen r;
	r.mana = g_getStat(unit, StatMana, 0);
	r.maxMana = g_maxMana(unit);
	r.recovery = g_getStat(unit, StatManaRecovery, 0);
	r.heat = rate;
	if (g_hasState(unit, StateNoRegen) == 0) {
		const uint8_t* chars = g_charStats(d2r::DataFlag(game), g_unitClass(unit, "plugin", 0));
		if (chars == nullptr) {
			return 0;
		}
		r.base = r.maxMana / heat::RegenFrames(d2r::At<uint8_t>(chars, CharStatsRegenSeconds));
		r.bonus = g_getStat(unit, StatManaRecoveryBonus, 0);
	}
	const int32_t delta = heat::HeatDelta(r);
	if (delta == 0) {
		return 0;
	}
	g_addStat(unit, StatMana, delta, 0);
	return 1;
}

}  // namespace

auto InstallHeat(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	g_maxMana = reinterpret_cast<MaxManaFn>(base + MaxManaRva);
	g_addStat = reinterpret_cast<AddStatFn>(base + AddStatRva);
	g_hasState = reinterpret_cast<HasStateFn>(base + HasStateRva);
	g_unitClass = reinterpret_cast<UnitClassFn>(base + UnitClassRva);
	g_charStats = reinterpret_cast<CharStatsFn>(base + CharStatsRva);
	g_unitRoom = reinterpret_cast<UnitRoomFn>(base + UnitRoomRva);
	g_roomInTown = reinterpret_cast<RoomInTownFn>(base + RoomInTownRva);
	const bool ok = context->InstallInlineHook(RegenRva, RegenEntry, sizeof(RegenEntry), Regen,
	                                           reinterpret_cast<void**>(&g_original));
	std::snprintf(report, reportSize, ok ? "on: stat 297 turns mana regeneration into a heat drain (none in town)"
	                                     : "off: mana regeneration hook failed");
	return ok;
}

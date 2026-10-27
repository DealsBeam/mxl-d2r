// D2Sigma's item event 14: "the unit dies" (docs/plugin-spec.md "Death event 14").
//
// Median adds two rows to events.txt (ev14, ev15). Event 14 is raised by D2Sigma's wrapper 0x10057930 of the 1.13c unit death
// function 0x6fcfeee0 (game, victim, killer, flag): `mov edx, 0xe` and the victim / killer as the event's units, before the
// death itself runs. Every call of 0x6fcfeee0 in D2Game goes through the wrapper (17 call sites redirected, the pet removal
// 0x6fcb77e0 among them), so the event comes with every death, a pet that its owner unsummons too. Stat 271 (D2R name
// item_mana_bytime, event `events_14`, function 30 = cast the skill of the layer) is Median's "cast on death": 248 monprop rows
// carry it, e.g. the Night Hawks' explosion (skill 445) and the death novas of many monsters.
//
// txt_migrate keeps Median's event as row 18 (`events_14`) of D2R's events.txt; D2R never raises it. The hook on D2R's unit
// death function 0x44db20 (same arguments, D2Game\src\Unit\SUnitDmg.cpp) raises it through the dispatch wrapper 0x44d570 (game,
// event, unit, other unit, 0) on the victim, with the killer (or none) as the other unit, then runs the death. The event id is
// read from stat 271's itemstatcost record (+0x44), so a renumbered events.txt keeps working.
//
// Not as in D2Sigma: a unit that is already dying or dead (the modes the death function itself returns on: monster 0 / 12,
// player 0 / 17) gets no second event; D2Sigma raised it for every call, also one the death function then ignores.
#include "death_event.h"

#include "d2r.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t DeathRva = 0x44db20;      // (game, victim, killer or null, flag): the unit death function
constexpr uint64_t DispatchRva = 0x44d570;   // (game, event, unit, other unit, arg): the unit event dispatch wrapper
constexpr uint64_t StatManagerRva = 0x2a9a580;  // data managers, 16 bytes apart; itemstatcost array at +0x1258, count at +0x1260
constexpr int StatRecordSize = 0x144, StatEvent1 = 0x44;
constexpr int CastOnDeathStat = 271;
constexpr int UnitMode = 0x0c;
constexpr int PlayerType = 0, MonsterType = 1;
// 0x44db20: mov [rsp+8],rbx / [rsp+0x10],rbp / [rsp+0x18],rsi, push rdi / r14 / r15, sub rsp,0x50 (24 bytes, whole instructions,
// no rip-relative operand; dump of build 93847)
constexpr uint8_t DeathEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89,
                                  0x74, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x50};

using DeathFn = void (*)(void* game, void* victim, void* killer, int32_t flag);
using DispatchFn = void (*)(void* game, int32_t event, void* unit, void* other, void* arg);

DeathFn g_original = nullptr;
uint64_t g_exe = 0;
bool g_installed = false;
volatile long g_deaths = 0, g_raised = 0, g_eventId = -1;

// Stat 271's first event word, once the tables are loaded; -1 while they are not (or the stat has no event).
auto EventId() noexcept -> long {
	if (g_eventId >= 0) {
		return g_eventId;
	}
	for (int idx = 0; idx < 4; ++idx) {
		const auto* manager = *reinterpret_cast<const uint8_t* const*>(g_exe + StatManagerRva + idx * 16);
		const auto* array = manager != nullptr ? d2r::At<const uint8_t*>(manager, 0x1258) : nullptr;
		if (array != nullptr && CastOnDeathStat < d2r::At<int64_t>(manager, 0x1260)) {
			const int16_t event = d2r::At<int16_t>(array + static_cast<size_t>(CastOnDeathStat) * StatRecordSize, StatEvent1);
			if (event > 0 && event < 64) {
				InterlockedExchange(&g_eventId, event);
			}
			break;
		}
	}
	return g_eventId;
}

auto AlreadyDead(void* unit) noexcept -> bool {
	const int32_t type = d2r::At<int32_t>(unit, d2r::UnitType);
	const int32_t mode = d2r::At<int32_t>(unit, UnitMode);
	return (type == MonsterType && (mode == 0 || mode == 12)) || (type == PlayerType && (mode == 0 || mode == 17));
}

void DeathHook(void* game, void* victim, void* killer, int32_t flag) {
	InterlockedIncrement(&g_deaths);
	if (game != nullptr && victim != nullptr) {
		__try {
			const long event = EventId();
			if (event > 0 && !AlreadyDead(victim)) {
				reinterpret_cast<DispatchFn>(g_exe + DispatchRva)(game, static_cast<int32_t>(event), victim, killer, nullptr);
				InterlockedIncrement(&g_raised);
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
	g_original(game, victim, killer, flag);
}

}  // namespace

auto InstallDeathEvent(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_exe = context->exeBase;
	g_installed = context->InstallInlineHook(DeathRva, DeathEntry, sizeof(DeathEntry), reinterpret_cast<void*>(&DeathHook),
	                                         reinterpret_cast<void**>(&g_original));
	std::snprintf(report, reportSize, "%s", g_installed ? "on (stat 271 cast on death: event raised on the victim of every death)" : "off: hook failed");
	return g_installed;
}

auto DeathEventCounts(long* deaths, long* raised, long* eventId) noexcept -> bool {
	*deaths = g_deaths;
	*raised = g_raised;
	*eventId = g_eventId;
	return g_installed;
}

// Median XL's skill cooldowns (D2Sigma D2SkillValidation.cpp, audit section 4) on D2R.
//
// Median: the delay of a skill is its delay formula minus stat 309 (layer = the skill id; items "reduces cooldown of <skill>"),
// in the client skill use (D2Client 0xa2e06), the D2Common ready test (0x52279) and the D2Game skill finish (0xa09d5, 0xa1aed);
// the cooldown start (0x100b1140) writes one timer per skill, or ONE timer per cooldown group (skills2 byte +0x2c) for the
// skills that have one, into the player's own table, and the ready test (0x100b0ab0) reads the same entries. Non-players have
// no cooldown at all.
//
// D2R already has a per-skill cooldown (docs/d2r-addresses.md section 17): the skill finish evaluates the record's globaldelay
// (+0x234) and localdelay (+0x238) and, when above zero, calls
//   0x439470 (game, unit, delay)         the unit-wide state 0x79 (skilldelay), D2R's own: Werewolf / Werebear
//   0x439500 (game, unit, delay, skill)  state 0xb9 (cooldown) with stat 359 at layer = skill id = the end frame
// (ready test 0x340540: stat 359 at the skill's layer > 0 means not ready). The client twin of the local start is 0x217b90
// (unit, delay, skill), called by its skill use (0x2170d4) and 0x215fc0 (0x21604c), which sets the same state on the client's
// copy of the unit. Both are detoured here: the delay loses stat 309 and the start is repeated for every skill of the group
// with the same delay, which is exactly Median's single group timer (cooldowns_logic.h proves it against a model of Median's
// tables). Median's start and ready test skip every other unit type (0x100b0a40+0x70: `if (*unit != 0) return 1`; the start
// stores into the player's own table only), so a non-player's start is not passed on: a monster, pet or hireling has no cooldown
// and can use a skill with a delay again at once. That also leaves the monster's state 0xb9 / stat 359 unset, which D2R's ready
// test 0x340540 reads.
// Both starts are called only with a delay above zero; a reduced delay of zero or less starts nothing, as Median's does.
//
// Entry bytes of build 93847 (timestamp 0x6ab3782c), whole instructions, no rip-relative operand in the first 20 bytes:
//   0x439500: mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x18],rsi / push rdi / sub rsp,0x40
//   0x217b90: the same with sub rsp,0x20
#include "cooldowns.h"

#include "cooldowns_logic.h"
#include "d2r.h"
#include "mxl_cooldowns.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t ServerStartRva = 0x439500;
constexpr uint64_t ClientStartRva = 0x217b90;
constexpr uint64_t GetUnitStatRva = 0x2f5020;  // (unit, stat, layer) -> total value
constexpr uint8_t ServerEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10,
                                   0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x40};
constexpr uint8_t ClientEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10,
                                   0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20};
constexpr int UnitTypeOffset = 0x00;
constexpr size_t MaxGroup = 64;

using ServerStartFn = void (*)(void* game, void* unit, int32_t delay, int32_t skill);
using ClientStartFn = void (*)(void* unit, int32_t delay, int32_t skill);
using GetUnitStatFn = int32_t (*)(void* unit, int32_t stat, int32_t layer);

ServerStartFn g_serverOriginal = nullptr;
ClientStartFn g_clientOriginal = nullptr;
GetUnitStatFn g_getUnitStat = nullptr;
cooldowns::Counters g_counts;
bool g_installed = false;

constexpr size_t MemberCount = sizeof(mxl_cooldowns::Members) / sizeof(mxl_cooldowns::Members[0]);

auto IsPlayer(void* unit) noexcept -> bool {
	return unit != nullptr && cooldowns::HasCooldowns(d2r::At<int32_t>(unit, UnitTypeOffset));
}

// The reduced delay for a used skill and the list of skills to start; false when nothing starts.
template <typename Start>
void StartAll(void* unit, int32_t delay, int32_t skill, Start&& start) noexcept {
	const int32_t reduction = g_getUnitStat(unit, cooldowns::StatCooldownReduction, skill);
	const int32_t reduced = cooldowns::ReducedDelay(delay, reduction);
	InterlockedIncrement(&g_counts.starts);
	if (reduced != delay) {
		InterlockedIncrement(&g_counts.reduced);
	}
	if (!cooldowns::Starts(reduced)) {
		InterlockedIncrement(&g_counts.dropped);
		return;
	}
	int32_t list[MaxGroup];
	const size_t n = cooldowns::StartList(mxl_cooldowns::Members, MemberCount, skill, list, MaxGroup);
	for (size_t i = 0; i < n; ++i) {
		if (list[i] != skill) {
			InterlockedIncrement(&g_counts.shared);
		}
		start(reduced, list[i]);
	}
}

void ServerHook(void* game, void* unit, int32_t delay, int32_t skill) {
	if (!IsPlayer(unit)) {
		InterlockedIncrement(&g_counts.monsters);  // Median: a non-player is always ready, nothing is stored
		return;
	}
	StartAll(unit, delay, skill, [&](int32_t d, int32_t s) { g_serverOriginal(game, unit, d, s); });
}

void ClientHook(void* unit, int32_t delay, int32_t skill) {
	if (!IsPlayer(unit)) {
		InterlockedIncrement(&g_counts.monsters);
		return;
	}
	StartAll(unit, delay, skill, [&](int32_t d, int32_t s) { g_clientOriginal(unit, d, s); });
}

}  // namespace

auto CooldownCounts(long* starts, long* reduced, long* shared, long* dropped, long* monsters) noexcept -> bool {
	*starts = g_counts.starts;
	*reduced = g_counts.reduced;
	*shared = g_counts.shared;
	*dropped = g_counts.dropped;
	*monsters = g_counts.monsters;
	return g_installed;
}

auto CooldownMemberCount() noexcept -> size_t {
	return MemberCount;
}

auto CooldownGroupCount() noexcept -> size_t {
	return cooldowns::GroupCount(mxl_cooldowns::Members, MemberCount);
}

auto InstallCooldowns(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_getUnitStat = reinterpret_cast<GetUnitStatFn>(context->exeBase + GetUnitStatRva);
	const bool server = context->InstallInlineHook(ServerStartRva, ServerEntry, sizeof(ServerEntry), reinterpret_cast<void*>(&ServerHook),
	                                               reinterpret_cast<void**>(&g_serverOriginal));
	const bool client = context->InstallInlineHook(ClientStartRva, ClientEntry, sizeof(ClientEntry), reinterpret_cast<void*>(&ClientHook),
	                                               reinterpret_cast<void**>(&g_clientOriginal));
	g_installed = server && client;
	if (g_installed) {
		cooldowns::FormatReport(report, reportSize, MemberCount, CooldownGroupCount(), g_counts);
	} else {
		std::snprintf(report, reportSize, "off: hook failed (server %s, client %s)", server ? "ok" : "no", client ? "ok" : "no");
	}
	return g_installed;
}

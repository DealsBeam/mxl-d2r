// Median's skill restriction modes 3 to 7 (docs/plugin-spec.md, "Skill restrictions").
//
// skills.txt `restrict` + State1-3 say when a skill may be used. D2Sigma's check (D2SkillValidation.cpp 0x100b0c30) has
// modes 0 to 7; D2R's usability check 0x33f360 knows 0 to 2 and lets every other value through, so Median's 35 skills
// with 3 to 7 ("Requires Steady Aim", the form / devotion exclusions) were usable at any time. The hook runs D2R's
// check and, when it says usable (0) for a skill whose mode is 3 to 7, applies the rule of skill_restrict_logic.h to the
// caster's states and answers 4 (restricted, the code D2R's own modes give).
//
// The skill entry D2R passes is a pointer to a struct whose first field is the skills.txt record (+0x2d0 the mode
// byte, +0x2d2 the three states as int16, -1 none). Restrict 7 also refuses while Median's melee devotion or
// progressive cold is set: tools/txt_migrate.py writes their D2R state ids to mxl_ids.h.
//
// Item-triggered skills are left alone on purpose. D2R has a second restrict test, 0x33f6a0 (modes 0 to 2 only), called by the
// event-skill caster 0x589930 (item procs, charges, oskills). D2Sigma has no counterpart: in 1.13c the restrict test is called
// from D2Common 10787 only, and the event-skill caster 0x6fd114f0 never reaches it, so Median's item procs ignore `restrict`.
// 0x33f6a0 lets modes 3 to 7 through, which is the same (docs/sigma-patch-audit.md item 7; game check `restrictitems`).
#include "skill_restrict.h"

#include "d2r.h"
#include "mxl_ids.h"
#include "skill_restrict_logic.h"

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t SkillCheckRva = 0x33f360;  // (unit, skill entry) -> 0 usable, else a reason code
constexpr uint64_t HasStateRva = 0x3351b0;    // (unit, state) -> non-zero when set
// push rsi / push rdi / sub rsp,0x28 / mov rdi,rdx / mov rsi,rcx / test rdx,rdx
constexpr uint8_t SkillCheckEntry[] = {0x40, 0x56, 0x57, 0x48, 0x83, 0xec, 0x28, 0x48,
                                       0x8b, 0xfa, 0x48, 0x8b, 0xf1, 0x48, 0x85, 0xd2};
constexpr int SkRestrict = 0x2d0;  // uint8
constexpr int SkStates = 0x2d2;    // 3 x int16
constexpr int32_t Restricted = 4;

using SkillCheckFn = int32_t (*)(void* unit, int64_t* entry) noexcept;
using HasStateFn = uint64_t (*)(void* unit, int32_t state);

SkillCheckFn g_original = nullptr;
HasStateFn g_hasState = nullptr;

auto SkillCheck(void* unit, int64_t* entry) noexcept -> int32_t {
	const int32_t result = g_original(unit, entry);
	if (result != 0 || unit == nullptr || entry == nullptr || *entry == 0) {
		return result;
	}
	const auto* record = reinterpret_cast<const uint8_t*>(*entry);
	const int mode = d2r::At<uint8_t>(record, SkRestrict);
	if (mode < skill_restrict::RestrictFirst || mode > skill_restrict::RestrictLast) {
		return result;
	}
	const int16_t states[3] = {d2r::At<int16_t>(record, SkStates), d2r::At<int16_t>(record, SkStates + 2),
	                           d2r::At<int16_t>(record, SkStates + 4)};
	const bool allowed = skill_restrict::Allowed(mode, states, mxl_ids::StateMeleeDevotion, mxl_ids::StateProgressiveCold,
	                                             [&](int32_t state) { return g_hasState(unit, state) != 0; });
	return allowed ? result : Restricted;
}

}  // namespace

auto InstallSkillRestrict(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_hasState = reinterpret_cast<HasStateFn>(context->exeBase + HasStateRva);
	const bool ok = context->InstallInlineHook(SkillCheckRva, SkillCheckEntry, sizeof(SkillCheckEntry), SkillCheck,
	                                           reinterpret_cast<void**>(&g_original));
	std::snprintf(report, reportSize, ok ? "on: restrict 3-7 on the cast path (no state, all states, first-not-others, first or second, devotion/cold); item procs/charges unrestricted as in Median"
	                                     : "off: skill check hook failed (the state trace has it?)");
	return ok;
}

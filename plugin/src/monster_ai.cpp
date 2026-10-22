// Median's monster AI on D2R (D2Sigma D2AI.cpp). docs/subsystems/monsters-ai.md, docs/d2r-addresses.md section 12.
//
// 1. andariel_ai (47 monsters). D2R's AI table is 155 fixed entries read only through the getter 0x4a36c0(unit, mode), which
//    maps any AI id past the table to entry 0 ("None", a bare ret). The getter is detoured: whatever it returns for a
//    monster whose monstats AI id (+0x52) is 155 is replaced by an entry of the plugin's {kind 1, init 0, think, alt 0}.
//    monai.txt carries the extra row `andariel_ai` as id 155 (tools/monster_ai_data.py). The think is monster_ai_logic.h's
//    Andariel() driven by D2R's own primitives (the twins of Median's helpers, all verified against the dump):
//      wait 0x4a6d10 (game, unit, frames)            mode action 0x4a78e0 (game, unit, mode, target)
//      use skill 0x4a7bc0 (game, unit, mode, skill, target, 0, 0, 0)    walk 0x4a7270 (unit, 1, 0, 0) + 0x4a8740 (game, unit, target, 7)
//      nearest enemy 0x595f80 (game, unit, &distance, 0) -> unit or null (distance 0x7fffffff when none)
//    With "medianai" off the entry's think rewrites the monster row's aip2 / aip3 (D2R's Andariel reads them as "approach
//    chance" and "distance chance") and calls D2R's own Andariel think 0x5aba70, which is what the data did before.
// 2. Summoner 0x5b4d50: D2R takes the cold or fire branch first by the target's resistances; Median's rule is `roll < aip3`.
//    Two code patches: the resistance comparison (`setge cl` at 0x5b4e49) becomes `mov cl, 1` and the roll test (`cmovle` at
//    0x5b4feb) becomes `cmovl`, which leaves first = roll < aip3 exactly.
// 3. MosquitoNest (AI 83): an init at table entry 83 (+8) that delays the first spawn: ctl+0x44 (the frame D2R's think waits
//    for) = frame + the nest skill's delay (skill 1, compiled localdelay at skill record +0x238), or the frame without one.
//
// Entry bytes verified against the dump of build 93847 (docs/d2r-addresses.md); whole instructions, no rip-relative.
#include "monster_ai.h"

#include "d2r.h"
#include "monster_ai_logic.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t GetterRva = 0x4a36c0;       // (unit, mode) -> table entry
constexpr uint64_t TableRva = 0x2396e90;       // 155 entries of 0x20: {kind, init, think, alt}
constexpr uint64_t DrAndarielRva = 0x5aba70;   // D2R's Andariel think
constexpr uint64_t WaitRva = 0x4a6d10;
constexpr uint64_t ModeActionRva = 0x4a78e0;
constexpr uint64_t UseSkillRva = 0x4a7bc0;
constexpr uint64_t WalkSetRva = 0x4a7270;
constexpr uint64_t WalkRva = 0x4a8740;
constexpr uint64_t NearestRva = 0x595f80;
constexpr uint64_t MosquitoInitRva = TableRva + 83 * 0x20 + 8;
constexpr uint64_t SummonerRes = 0x5b4e49;     // 0f 9d c1: setge cl
constexpr uint64_t SummonerRoll = 0x5b4fed;    // 4e in `44 0f 4e 4c 24 44`: cmovle r9d, [rsp+0x44]

constexpr int32_t AndarielAi = 155;
constexpr int RowAi = 0x52;               // monstats row: AI id (int16)
constexpr int RowAip = 0x90;              // aip1 per difficulty (int16 x3), the next aip every 6 bytes
constexpr int RowSkill = 0x1c4;           // Skill1..5 (int16), the modes at RowSkillMode + n (byte)
constexpr int RowSkillMode = 0x1d4;
constexpr int ArgCtl = 0x00, ArgTarget = 0x10, ArgDist = 0x20, ArgReach = 0x24, ArgRow = 0x28, ArgDifficulty = 0x38;
constexpr int CtlPhase = 0x44, CtlMark = 0x48, CtlCooldown = 0x4c;
constexpr int MonsterHitMode = 0x68;      // monster data (unit+0x10): the last mode left, GetHit (3 / 0x13) = Median's "controlled" gate
constexpr int GameFrame = 0x170;
constexpr int SkLocalDelay = 0x238;       // skills record: compiled localdelay calc

// 0x4a36c0: mov [rsp+0x20],rsi / push rdi / sub rsp,0x30 / mov [rsp+0x40],rbx   (15 bytes; the next instruction is xor edi,edi)
constexpr uint8_t GetterEntry[] = {0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0x89, 0x5c, 0x24, 0x40};
constexpr uint8_t SetgeCl[] = {0x0f, 0x9d, 0xc1};
constexpr uint8_t MovCl1[] = {0xb1, 0x01, 0x90};
constexpr uint8_t Cmovle = 0x4e;
constexpr uint8_t Cmovl = 0x4c;

struct Entry {
	uint64_t kind, init, think, alt;
};

using GetterFn = Entry* (*)(Unit* unit, int32_t mode);
using ThinkFn = void (*)(Game* game, Unit* unit, void* args);
using WaitFn = int64_t (*)(Game* game, Unit* unit, int32_t frames);
using ModeActionFn = int64_t (*)(Game* game, Unit* unit, int32_t mode, Unit* target);
using UseSkillFn = int64_t (*)(Game* game, Unit* unit, uint32_t mode, int32_t skill, Unit* target, int32_t x, int32_t y, uint8_t flag);
using WalkSetFn = void (*)(Unit* unit, int32_t a, int32_t b, int32_t c);
using WalkFn = int64_t (*)(Game* game, Unit* unit, Unit* target, uint32_t bits);
using NearestFn = Unit* (*)(Game* game, Unit* unit, int32_t* distance, uint32_t* aux);

GetterFn g_getterOriginal = nullptr;
ThinkFn g_drAndariel = nullptr;
WaitFn g_wait = nullptr;
ModeActionFn g_modeAction = nullptr;
UseSkillFn g_useSkill = nullptr;
WalkSetFn g_walkSet = nullptr;
WalkFn g_walk = nullptr;
NearestFn g_nearest = nullptr;
const Entry* g_table0 = nullptr;
Entry g_entry = {};
bool g_native = false;

std::atomic<uint32_t> g_thinks{0}, g_waits{0}, g_attacks{0}, g_enemyAttacks{0}, g_charges{0}, g_walks{0}, g_fallback{0}, g_controlled{0};
std::atomic<uint32_t> g_skills[5] = {};

template <typename T>
auto Load(const void* p, int offset) noexcept -> T {
	return d2r::At<T>(p, offset);
}

auto AipOf(const uint8_t* row, int n, int difficulty) noexcept -> int32_t {  // n = 1..8
	return Load<int16_t>(row, RowAip + 6 * (n - 1) + 2 * difficulty);
}

auto Chance(uint32_t* seed, int32_t percent) noexcept -> bool {
	return d2r::RandomBelow(seed, 100) < percent;
}

// Sigma 0xc03f0: lo = lo * K + hi, hi = 0x29a (not the carry): the reseed step the think takes before the in-reach rolls.
void AdvanceSeed(uint32_t* seed) noexcept {
	seed[0] = seed[0] * 0x6ac690c5U + seed[1];
	seed[1] = 0x29a;
}

void NativeThink(Game* game, Unit* unit, void* args) noexcept {
	auto* ctl = Load<uint8_t*>(args, ArgCtl);
	auto* target = Load<Unit*>(args, ArgTarget);
	const auto* row = Load<const uint8_t*>(args, ArgRow);
	if (game == nullptr || unit == nullptr || ctl == nullptr || row == nullptr) {
		return;
	}
	const int difficulty = Load<uint8_t>(args, ArgDifficulty) % 3;
	uint32_t* seed = d2r::api.unitSeed(unit);
	if (seed == nullptr) {
		return;
	}

	monster_ai::Input in;
	in.dist = Load<int32_t>(args, ArgDist);
	in.inReach = Load<int32_t>(args, ArgReach) != 0;
	in.p = {AipOf(row, 1, difficulty), AipOf(row, 2, difficulty), AipOf(row, 3, difficulty), AipOf(row, 4, difficulty),
	        AipOf(row, 5, difficulty)};
	for (int n = 1; n <= 4; ++n) {
		in.skillOk[n] = Load<int16_t>(row, RowSkill + 2 * (n - 1)) >= 0;
	}
	int32_t enemyDistance = 0x7fffffff;
	Unit* enemy = g_nearest(game, unit, &enemyDistance, nullptr);
	in.hasEnemy = enemy != nullptr;
	in.enemyDist = enemyDistance;
	if (const auto* data = Load<const uint8_t*>(unit, 0x10)) {
		in.controlled = monster_ai::IsControlledMode(Load<int32_t>(data, MonsterHitMode));
	}

	monster_ai::Ctl state;
	state.phase = Load<int32_t>(ctl, CtlPhase);
	state.mark = Load<int32_t>(ctl, CtlMark);
	state.cd = Load<int32_t>(ctl, CtlCooldown);

	if (in.inReach) {
		AdvanceSeed(seed);
	}
	const monster_ai::Decision d = monster_ai::Andariel(in, state, [&](int32_t percent) { return Chance(seed, percent); });

	*reinterpret_cast<int32_t*>(ctl + CtlPhase) = state.phase;
	*reinterpret_cast<int32_t*>(ctl + CtlMark) = state.mark;
	*reinterpret_cast<int32_t*>(ctl + CtlCooldown) = state.cd;

	g_thinks.fetch_add(1, std::memory_order_relaxed);
	if (in.controlled) {
		g_controlled.fetch_add(1, std::memory_order_relaxed);
	}
	Unit* aim = (d.onEnemy && enemy != nullptr) ? enemy : target;
	switch (d.kind) {
	case monster_ai::Kind::Wait:
		g_waits.fetch_add(1, std::memory_order_relaxed);
		g_wait(game, unit, d.frames);
		break;
	case monster_ai::Kind::Attack:
		(d.onEnemy ? g_enemyAttacks : g_attacks).fetch_add(1, std::memory_order_relaxed);
		g_modeAction(game, unit, 4, aim);
		break;
	case monster_ai::Kind::Charge:
		g_charges.fetch_add(1, std::memory_order_relaxed);
		g_modeAction(game, unit, 8, target);
		break;
	case monster_ai::Kind::Skill: {
		g_skills[d.skill].fetch_add(1, std::memory_order_relaxed);
		const int32_t skill = Load<int16_t>(row, RowSkill + 2 * (d.skill - 1));
		const uint32_t mode = Load<uint8_t>(row, RowSkillMode + (d.skill - 1));
		g_useSkill(game, unit, mode, skill, aim, 0, 0, 0);
		break;
	}
	case monster_ai::Kind::Walk:
		g_walks.fetch_add(1, std::memory_order_relaxed);
		g_walkSet(unit, 1, 0, 0);
		g_walk(game, unit, target, 7);
		break;
	}
}

// "medianai" off: D2R's Andariel think with the row read the way the data used to ask for (tools/monster_ai_data.py before
// the port): aip1 and aip4 as Median has them, aip2 = 3, aip3 = 25. Rewriting the shared row is idempotent.
void FallbackThink(Game* game, Unit* unit, void* args) noexcept {
	auto* row = Load<uint8_t*>(args, ArgRow);
	if (row != nullptr) {
		const int difficulty = Load<uint8_t>(args, ArgDifficulty) % 3;
		const monster_ai::DrAips a = monster_ai::FallbackAips(AipOf(row, 1, difficulty), AipOf(row, 4, difficulty));
		auto* aip2 = reinterpret_cast<int16_t*>(row + RowAip + 6 + 2 * difficulty);
		auto* aip3 = reinterpret_cast<int16_t*>(row + RowAip + 12 + 2 * difficulty);
		if (*aip2 != a.aip2) {
			*aip2 = static_cast<int16_t>(a.aip2);
		}
		if (*aip3 != a.aip3) {
			*aip3 = static_cast<int16_t>(a.aip3);
		}
	}
	g_fallback.fetch_add(1, std::memory_order_relaxed);
	g_drAndariel(game, unit, args);
}

// Median's MosquitoNest init (0x1004a110).
void MosquitoInit(Game* game, Unit* unit, void* args) noexcept {
	auto* ctl = Load<uint8_t*>(args, ArgCtl);
	const auto* row = Load<const uint8_t*>(args, ArgRow);
	if (game == nullptr || ctl == nullptr || row == nullptr) {
		return;
	}
	const int32_t frame = Load<int32_t>(game, GameFrame);
	const int32_t skillId = Load<int16_t>(row, RowSkill);
	bool has = false;
	int32_t delay = 0;
	if (skillId >= 0) {
		if (const uint8_t* record = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId)) {
			has = true;
			delay = d2r::EvalCalc(game, unit, record, SkLocalDelay, skillId, 1);
		}
	}
	*reinterpret_cast<int32_t*>(ctl + CtlPhase) = monster_ai::MosquitoFirstSpawn(frame, has, delay);
}

auto AiOf(const Unit* unit) noexcept -> int32_t {
	if (unit == nullptr || d2r::At<int32_t>(unit, d2r::UnitType) != 1) {
		return -1;
	}
	const auto* data = d2r::At<const uint8_t*>(unit, 0x10);
	const auto* row = data != nullptr ? d2r::At<const uint8_t*>(data, 0) : nullptr;
	return row != nullptr ? d2r::At<int16_t>(row, RowAi) : -1;
}

auto GetterHook(Unit* unit, int32_t mode) -> Entry* {
	Entry* entry = g_getterOriginal(unit, mode);
	if (entry == g_table0 && AiOf(unit) == AndarielAi) {
		return &g_entry;
	}
	return entry;
}

}  // namespace

auto InstallMonsterAi(const D2RL::PluginContext* context, bool native, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	g_native = native;
	g_table0 = reinterpret_cast<const Entry*>(base + TableRva);
	g_drAndariel = reinterpret_cast<ThinkFn>(base + DrAndarielRva);
	g_wait = reinterpret_cast<WaitFn>(base + WaitRva);
	g_modeAction = reinterpret_cast<ModeActionFn>(base + ModeActionRva);
	g_useSkill = reinterpret_cast<UseSkillFn>(base + UseSkillRva);
	g_walkSet = reinterpret_cast<WalkSetFn>(base + WalkSetRva);
	g_walk = reinterpret_cast<WalkFn>(base + WalkRva);
	g_nearest = reinterpret_cast<NearestFn>(base + NearestRva);
	g_entry = {1, 0, reinterpret_cast<uint64_t>(native ? &NativeThink : &FallbackThink), 0};

	if (!context->InstallInlineHook(GetterRva, GetterEntry, sizeof(GetterEntry), reinterpret_cast<void*>(&GetterHook),
	                                reinterpret_cast<void**>(&g_getterOriginal))) {
		std::snprintf(report, reportSize, "off: AI table getter hook failed");
		return false;
	}
	if (!native) {
		std::snprintf(report, reportSize, "fallback: AI 155 runs D2R's Andariel think (left out by mxl-core-off.txt)");
		return true;
	}
	bool summoner = context->PatchBytes(SummonerRes, SetgeCl, sizeof(SetgeCl), MovCl1, sizeof(MovCl1));
	summoner = summoner && context->PatchWriteU8(SummonerRoll, &Cmovle, 1, Cmovl);
	static constexpr uint8_t Empty[8] = {};
	const bool nest = context->PatchWriteU64(MosquitoInitRva, Empty, sizeof(Empty), reinterpret_cast<uint64_t>(&MosquitoInit));
	std::snprintf(report, reportSize, "on: andariel_ai (AI 155)%s%s", summoner ? ", Summoner roll < aip3" : ", Summoner patch FAILED",
	              nest ? ", MosquitoNest init" : ", MosquitoNest init FAILED");
	return true;
}

void MonsterAiStatus(char* out, size_t outSize) noexcept {
	std::snprintf(out, outSize,
	              "%u thinks (wait %u, attack %u + %u on the enemy, charge %u, walk %u, skills 1-4: %u %u %u %u), fallback thinks %u, "
	              "controlled thinks %u",
	              g_thinks.load(), g_waits.load(), g_attacks.load(), g_enemyAttacks.load(), g_charges.load(), g_walks.load(),
	              g_skills[1].load(), g_skills[2].load(), g_skills[3].load(), g_skills[4].load(), g_fallback.load(),
	              g_controlled.load());
}

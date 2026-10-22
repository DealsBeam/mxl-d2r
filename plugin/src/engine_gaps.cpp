// Small Median engine rules (engine_gaps_logic.h): the hit-20 damage floor, the recast guard of srvdofunc 47 and the player's
// slow floor by difficulty. The first two are byte patches (the expected bytes are checked), the third replaces the entry of
// the floor function 0x449f40 for players and calls the original for everything else.
#include "engine_gaps.h"

#include "d2r.h"
#include "engine_gaps_logic.h"

#include <cstdint>

namespace {

constexpr int GameDifficulty = 0x104;  // game +0x104: difficulty byte (0 Normal, 1 Nightmare, 2 Hell)

using SlowFloorFn = int (*)(void* game, void* unit) noexcept;
SlowFloorFn g_slowOriginal = nullptr;
SlowFloorFn g_slowEntry = nullptr;  // the patched entry, as the harness reaches it

int SlowFloorHook(void* game, void* unit) noexcept {
	if (game != nullptr && unit != nullptr && d2r::At<uint32_t>(unit, d2r::UnitType) == gaps::UnitTypePlayer) {
		return gaps::PlayerSlowFloor(d2r::At<uint8_t>(game, GameDifficulty));
	}
	return g_slowOriginal(game, unit);
}

}  // namespace

auto EngineGapsSlowFloor(void* game, void* unit) noexcept -> int {
	return g_slowEntry != nullptr ? g_slowEntry(game, unit) : 0;
}

auto InstallEngineGaps(const D2RL::PluginContext* context, bool hitFloor, bool recast, bool slowFloor, char* report, size_t reportSize) noexcept -> int {
	bool hit = false, rec = false, slow = false;
	if (hitFloor) {
		hit = context->PatchBytes(gaps::Hit20FloorRva, gaps::Hit20FloorOriginal, sizeof(gaps::Hit20FloorOriginal), gaps::Hit20FloorPatched,
		                          sizeof(gaps::Hit20FloorPatched));
	}
	if (recast) {
		rec = context->PatchBytes(gaps::RecastGuardRva, gaps::RecastGuardOriginal, sizeof(gaps::RecastGuardOriginal), gaps::RecastGuardPatched,
		                          sizeof(gaps::RecastGuardPatched));
	}
	if (slowFloor) {
		slow = context->InstallInlineHook(gaps::SlowFloorRva, gaps::SlowFloorEntry, sizeof(gaps::SlowFloorEntry),
		                                  reinterpret_cast<void*>(&SlowFloorHook), reinterpret_cast<void**>(&g_slowOriginal));
		if (slow) {
			g_slowEntry = reinterpret_cast<SlowFloorFn>(context->exeBase + gaps::SlowFloorRva);
		}
	}
	gaps::FormatReport(report, reportSize, hit, rec, slow, !hitFloor, !recast, !slowFloor);
	return (hit ? 1 : 0) + (rec ? 1 : 0) + (slow ? 1 : 0);
}

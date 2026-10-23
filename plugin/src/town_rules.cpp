// Median's NPC healing and town portal lock (town_rules_logic.h).
//
// NPC healing: D2R's server handler for the NPC talk packet (0x4c2410) always calls 0x53fc50(game, player, npc), which heals
// life, mana and stamina when byte +6 of the NPC's entry in the static NPC table (0x237ea60, 43 x 0x1c bytes; docs/d2r-addresses.md
// section 6) is set. D2R sets it for the act healers only; the part sets it for every entry.
//
// Town portal lock: the town portal skill (srvdofunc 109) is 0x5822b0(game, player, skill) -> 1 done / 0 failed: it checks the
// skill (0x97790), asks 0x46fa70 whether a portal fits and calls 0x470ae0 to create it. The detour answers 0 with the "failed" unit
// message (0x14, the function's own failure reaction) when the player stands in a level whose Tplock flag is set.
#include "town_rules.h"

#include "d2r.h"
#include "town_rules_logic.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t NpcTableRva = 0x237ea60;
constexpr int NpcTableCount = 43;
constexpr int NpcEntrySize = 0x1c;
constexpr int16_t FirstClass = 148;  // Akara
constexpr int16_t LastClass = 520;   // Cain of act 5

constexpr uint64_t TownPortalRva = 0x5822b0;   // (game, player, skill) -> 1 done, 0 failed
constexpr uint64_t RoomLevelRva = 0x2efc10;    // (room) -> level id
constexpr uint64_t UnitRoomRva = 0x34b440;     // (unit) -> room
constexpr uint64_t UnitMessageRva = 0x491960;  // (player, message, subject)
constexpr int32_t MessageFailed = 0x14;

// 0x5822b0: mov [rsp+8],rbx / push rdi / sub rsp,0x20 / mov rdi,rdx / mov rbx,rcx (16 bytes; dump of build 93847)
constexpr uint8_t TownPortalEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xd9};

using TownPortalFn = int64_t (*)(Game* game, Unit* player, int32_t skill);
using RoomLevelFn = int32_t (*)(void* room);
using UnitRoomFn = void* (*)(Unit* unit);
using UnitMessageFn = void (*)(Unit* player, int32_t message, Unit* subject);

TownPortalFn g_original = nullptr;
RoomLevelFn g_roomLevel = nullptr;
UnitRoomFn g_unitRoom = nullptr;
UnitMessageFn g_unitMessage = nullptr;
std::atomic<int> g_refusals{-1};

auto ReadClass(const uint8_t* entry) noexcept -> int16_t {
	int16_t cls = 0;
	std::memcpy(&cls, entry, sizeof(cls));
	return cls;
}

int64_t TownPortalHook(Game* game, Unit* player, int32_t skill) noexcept {
	if (player != nullptr) {
		void* room = g_unitRoom(player);
		if (room != nullptr && town::TownPortalLocked(g_roomLevel(room))) {
			g_refusals.fetch_add(1);
			g_unitMessage(player, MessageFailed, player);
			return 0;
		}
	}
	return g_original(game, player, skill);
}

auto HealAll(const D2RL::PluginContext* context, int* set, int* already) noexcept -> bool {
	const auto* table = reinterpret_cast<const uint8_t*>(context->exeBase + NpcTableRva);
	if (ReadClass(table) != FirstClass || ReadClass(table + (NpcTableCount - 1) * NpcEntrySize) != LastClass) {
		return false;
	}
	bool ok = true;
	for (int i = 0; i < NpcTableCount; ++i) {
		const uint8_t current = table[i * NpcEntrySize + town::HealFlagOffset];
		if (!town::NeedsHealFlag(current)) {
			++*already;
			continue;
		}
		static constexpr uint8_t Zero[1] = {0};
		const bool wrote = context->PatchWriteU8(NpcTableRva + static_cast<uint64_t>(i) * NpcEntrySize + town::HealFlagOffset, Zero, sizeof(Zero), 1);
		*set += wrote ? 1 : 0;
		ok = ok && wrote;
	}
	return ok;
}

}  // namespace

auto TownPortalRefusals() noexcept -> int {
	return g_refusals.load();
}

auto InstallTownRules(const D2RL::PluginContext* context, bool healAll, bool townPortalLock, char* report, size_t reportSize) noexcept -> int {
	int installed = 0;
	char heal[96] = "off";
	char lock[96] = "off";
	if (healAll) {
		int set = 0, already = 0;
		if (HealAll(context, &set, &already)) {
			++installed;
			std::snprintf(heal, sizeof(heal), "on (%d NPC entries set, %d already healed)", set, already);
		} else {
			std::snprintf(heal, sizeof(heal), "FAILED (NPC table not recognised or a write failed)");
		}
	}
	if (townPortalLock) {
		g_roomLevel = reinterpret_cast<RoomLevelFn>(context->exeBase + RoomLevelRva);
		g_unitRoom = reinterpret_cast<UnitRoomFn>(context->exeBase + UnitRoomRva);
		g_unitMessage = reinterpret_cast<UnitMessageFn>(context->exeBase + UnitMessageRva);
		if (context->InstallInlineHook(TownPortalRva, TownPortalEntry, sizeof(TownPortalEntry), reinterpret_cast<void*>(&TownPortalHook),
		                               reinterpret_cast<void**>(&g_original))) {
			g_refusals.store(0);
			++installed;
			std::snprintf(lock, sizeof(lock), "on (hook 0x%llx)", static_cast<unsigned long long>(TownPortalRva));
		} else {
			std::snprintf(lock, sizeof(lock), "FAILED (hook not installed)");
		}
	}
	std::snprintf(report, reportSize, "npc heal %s; town portal lock %s", heal, lock);
	return installed;
}

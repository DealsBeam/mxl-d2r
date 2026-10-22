// Trophy challenge windows (docs/plugin-spec.md, "Trophy timer"; logic in trophy_timer_logic.h).
//
// Median's trophy recipes match a player's stat 202 (D2R 378) against the item's challenge code. The boss kills put the
// code there through timed states, which is the time limit; Median's patch notes (P2.10.0) also void a started challenge
// when the player dies or enters another zone, which no table says. This part samples every player in the game every
// PollFrames server frames (the goblins.cpp frame hook calls TrophyTimerFrame): a window opens when the stat takes a value,
// is voided on death or an area change, and the cube condition (cube_conditions.cpp, op 6 on stat 378) refuses a recipe whose
// window is voided or past the trophy's limit. The player gets a chat line when a window is voided.
#include "trophy_timer.h"

#include "client_messages.h"
#include "client_messages_logic.h"
#include "d2r.h"
#include "trophy_timer_logic.h"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t UnitRoomRva = 0x34b440;    // (unit) -> room
constexpr uint64_t RoomLevelRva = 0x2efc10;   // (room) -> level id
constexpr uint64_t ClientUnitRva = 0x484c90;  // (client, 0) -> the client's player unit
constexpr uint64_t GetStatRva = 0x2f5020;     // (unit, stat, layer): total value

constexpr int GameClients = 0x138;  // first client; client+0x628 = next
constexpr int GameFrame = 0x170;
constexpr int ClientNext = 0x628;
constexpr int ClientState = 0x04;  // 4 = in game
constexpr int UnitType = 0x00;
constexpr int UnitId = 0x08;
constexpr int UnitMode = 0x0c;
constexpr int ModeDeath = 0, ModeDead = 17;  // player modes of a dying or dead character

using UnitRoomFn = void* (*)(Unit* unit);
using RoomLevelFn = int32_t (*)(void* room);
using ClientUnitFn = Unit* (*)(void* client, int32_t zero);
using StatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);

UnitRoomFn g_unitRoom = nullptr;
RoomLevelFn g_roomLevel = nullptr;
ClientUnitFn g_clientUnit = nullptr;
StatFn g_getStat = nullptr;
bool g_on = false;

trophy::Tracker g_tracker;
uint32_t g_lastPoll = 0;
std::atomic<uint32_t> g_opened{0}, g_voided{0}, g_refused{0};

void Sample(Unit* player, uint32_t frame) noexcept {
	const int32_t code = g_getStat(player, trophy::StatChallenge, 0);
	const int32_t mode = d2r::At<int32_t>(player, UnitMode);
	void* room = g_unitRoom(player);
	const int32_t area = room != nullptr ? g_roomLevel(room) : -1;
	if (area < 0 && code != 0) {
		return;  // between areas: the room is gone for a moment, do not read it as a zone change
	}
	const auto event = g_tracker.Observe(d2r::At<uint32_t>(player, UnitId), code, area, mode == ModeDeath || mode == ModeDead, frame);
	switch (event) {
	case trophy::Tracker::Event::Opened:
		g_opened.fetch_add(1);
		break;
	case trophy::Tracker::Event::VoidedZone:
		g_voided.fetch_add(1);
		SendClientMessage(player, trophy::VoidedByZone, clientmsg::Red);
		break;
	case trophy::Tracker::Event::VoidedDeath:
		g_voided.fetch_add(1);
		SendClientMessage(player, trophy::VoidedByDeath, clientmsg::Red);
		break;
	default:
		break;
	}
}

}  // namespace

void TrophyTimerFrame(Game* game) noexcept {
	if (!g_on || game == nullptr) {
		return;
	}
	const uint32_t frame = d2r::At<uint32_t>(game, GameFrame);
	g_tracker.Touch(frame);
	if (frame - g_lastPoll < trophy::PollFrames && frame >= g_lastPoll) {
		return;
	}
	g_lastPoll = frame;
	int guard = 0;
	for (auto* client = d2r::At<uint8_t*>(game, GameClients); client != nullptr && guard < 64;
	     client = d2r::At<uint8_t*>(client, ClientNext), ++guard) {
		if (d2r::At<int32_t>(client, ClientState) != 4) {
			continue;
		}
		Unit* unit = g_clientUnit(client, 0);
		if (unit != nullptr && d2r::At<int32_t>(unit, UnitType) == 0) {
			Sample(unit, frame);
		}
	}
}

auto TrophyWindowOpen(Game* game, Unit* player, int32_t code) noexcept -> bool {
	if (!g_on || game == nullptr || player == nullptr) {
		return true;
	}
	const bool open = g_tracker.Open(d2r::At<uint32_t>(player, UnitId), code, d2r::At<uint32_t>(game, GameFrame));
	if (!open) {
		g_refused.fetch_add(1);
	}
	return open;
}

void TrophyTimerStatus(char* out, size_t outSize) noexcept {
	std::snprintf(out, outSize, "%u windows opened, %u voided, %u cube checks refused", g_opened.load(), g_voided.load(),
	              g_refused.load());
}

auto InstallTrophyTimer(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	g_unitRoom = reinterpret_cast<UnitRoomFn>(base + UnitRoomRva);
	g_roomLevel = reinterpret_cast<RoomLevelFn>(base + RoomLevelRva);
	g_clientUnit = reinterpret_cast<ClientUnitFn>(base + ClientUnitRva);
	g_getStat = reinterpret_cast<StatFn>(base + GetStatRva);
	g_on = true;
	std::snprintf(report, reportSize, "on: challenge stat %d, windows by trophy, voided by death or a zone change (%d trophies timed)",
	              trophy::StatChallenge, static_cast<int>(sizeof(trophy::Limits) / sizeof(trophy::Limits[0])));
	return true;
}

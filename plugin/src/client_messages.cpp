// Server -> player text with D2R's chat packet 0x26 (docs/d2r-addresses.md section 12).
//
//   0x48fde0(player unit) -> the player's client (client+0 is the connection id), 0 for a non-player
//   0x477a30(client, record) -> trims the 0x147-byte chat record (name <= 60 bytes, text <= 255) and writes packet 0x26
//
// Both are called, not hooked; their entry bytes are compared with the dump of build 93847 first. The D2R client shows the
// text in its message log with the colour byte; D2Sigma's own client widgets (DungeonWidget, notices) are not part of D2R.
#include "client_messages.h"

#include "client_messages_logic.h"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Unit;

constexpr uint64_t ClientOfRva = 0x48fde0;  // (player unit) -> client or null
constexpr uint64_t SendChatRva = 0x477a30;  // (client, record)
constexpr uint64_t ClientUnitRva = 0x484c90;  // (client, 0) -> the client's player unit (used by goblins.cpp too)
constexpr int GameClients = 0x138;  // game: first client; client+0x628 = next
constexpr int ClientNext = 0x628;
constexpr int ClientState = 0x04;  // 4 = in game

// 0x48fde0: push rbx / sub rsp,0x20 / mov rbx,rcx / test rcx,rcx / je +0x1e / call (15 bytes)
constexpr uint8_t ClientOfEntry[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x74, 0x1e, 0xe8};
// 0x477a30: mov [rsp+0x18],rbx / push rbp rsi rdi r14 r15 / sub rsp,0x190 (15 bytes)
constexpr uint8_t SendChatEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec};

using ClientOfFn = void* (*)(Unit* player);
using SendChatFn = void (*)(void* client, const uint8_t* record);
using ClientUnitFn = Unit* (*)(void* client, int32_t zero);

ClientOfFn g_clientOf = nullptr;
SendChatFn g_sendChat = nullptr;
ClientUnitFn g_clientUnit = nullptr;
std::atomic<int> g_sent{0}, g_failed{0};

auto Matches(uint64_t address, const uint8_t* expected, size_t n) noexcept -> bool {
	return std::memcmp(reinterpret_cast<const void*>(address), expected, n) == 0;
}

auto Send(Unit* player, uint8_t type, uint8_t colour, const char* text) noexcept -> bool {
	if (g_clientOf == nullptr || g_sendChat == nullptr || player == nullptr || text == nullptr) {
		return false;
	}
	__try {
		if (d2r::At<int32_t>(player, d2r::UnitType) != 0) {
			return false;
		}
		void* client = g_clientOf(player);
		if (client == nullptr) {
			g_failed.fetch_add(1);
			return false;
		}
		uint8_t record[clientmsg::ChatRecordSize];
		clientmsg::BuildChat(record, type, colour, d2r::At<uint32_t>(player, 0x08), text);
		g_sendChat(client, record);
		g_sent.fetch_add(1);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed.fetch_add(1);
		return false;
	}
}

}  // namespace

auto InstallClientMessages(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	const bool ok = Matches(base + ClientOfRva, ClientOfEntry, sizeof(ClientOfEntry)) &&
	                Matches(base + SendChatRva, SendChatEntry, sizeof(SendChatEntry));
	if (ok) {
		g_clientOf = reinterpret_cast<ClientOfFn>(base + ClientOfRva);
		g_sendChat = reinterpret_cast<SendChatFn>(base + SendChatRva);
		g_clientUnit = reinterpret_cast<ClientUnitFn>(base + ClientUnitRva);
	}
	std::snprintf(report, reportSize, "%s", ok ? "on: chat packet 0x26 to one player" : "off: entry bytes of 0x48fde0 / 0x477a30 differ");
	return ok;
}

auto SendClientMessage(Unit* player, const char* text, uint8_t colour) noexcept -> bool {
	return Send(player, clientmsg::TypeSystem, colour, text);
}

auto SendGameMessage(d2r::Game* game, const char* text, uint8_t colour) noexcept -> int {
	if (game == nullptr || g_clientUnit == nullptr) {
		return 0;
	}
	int sent = 0;
	int guard = 0;
	__try {
		for (auto* client = d2r::At<uint8_t*>(game, GameClients); client != nullptr && guard < 64;
		     client = d2r::At<uint8_t*>(client, ClientNext), ++guard) {
			if (d2r::At<int32_t>(client, ClientState) != 4) {
				continue;
			}
			Unit* unit = g_clientUnit(client, 0);
			if (unit != nullptr && Send(unit, clientmsg::TypeSystem, colour, text)) {
				++sent;
			}
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed.fetch_add(1);
	}
	return sent;
}

auto SendClientChat(Unit* player, uint8_t type, uint8_t colour, const char* text) noexcept -> bool {
	return Send(player, type, colour, text);
}

void ClientMessageCounts(int* sent, int* failed) noexcept {
	*sent = g_sent.load();
	*failed = g_failed.load();
}

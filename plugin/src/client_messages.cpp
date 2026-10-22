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

// The client's packet table (24-byte entries {handler, size, 0}, index = packet id, docs/d2r-addresses.md section 13): the
// reader of the chat packet 0x26 is entry 0x26. Its handler gets the packet buffer; the plugin's own messages are taken out
// in front of it (plugin messages in client_messages_logic.h).
constexpr uint64_t PacketTableRva = 0x22bbc40;
constexpr uint64_t ChatReaderRva = 0x12c190;
constexpr int PacketTableEntry = 24;
// 0x12c190: mov [rsp+0x10],rbx / mov [rsp+0x18],rsi / push rdi / sub rsp,0x190 (15 bytes)
constexpr uint8_t ChatReaderEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x81, 0xec, 0x90};

// Quest reward notices: the server sets a unit message with 0x491960 (player, message, subject; the pending message goes
// out as packet 0x2c at the unit's next update) and the client's handler for packet 0x2c (table entry 0x2c, 0x12c870:
// packet -> unit id +2, unit type +1, message +6) plays Median's sounds for 0x5f-0x61 (client_messages_logic.h).
constexpr uint64_t UnitMessageRva = 0x491960;
constexpr uint64_t UnitPacketId = 0x2c;
constexpr uint64_t UnitPacketRva = 0x12c870;
constexpr uint64_t ClientFindRva = 0x9a5d0;   // (unit id, unit type) -> client unit
constexpr uint64_t PlaySoundRva = 0x1a0780;   // (sound row, unit, 0, 0, 0): the sound at a unit
// 0x491960: mov [rsp+8],rbx / mov [rsp+0x18],rsi / push rdi / sub rsp,0x20 (15 bytes)
constexpr uint8_t UnitMessageEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20};
// 0x12c870: movzx r8d,[rcx+6] / movzx edx,[rcx+1] / mov ecx,[rcx+2] / jmp (13 bytes)
constexpr uint8_t UnitPacketEntry[] = {0x44, 0x0f, 0xb7, 0x41, 0x06, 0x0f, 0xb6, 0x51, 0x01, 0x8b, 0x49, 0x02, 0xe9};
// 0x9a5d0: movsxd r9,edx / lea rax,[rip+..] (6 bytes)
constexpr uint8_t ClientFindEntry[] = {0x4c, 0x63, 0xca, 0x48, 0x8d, 0x05};
// 0x1a0780: push rbp rsi rdi r14 r15 / lea rbp,[rsp-0x2f] / sub rsp,... (15 bytes)
constexpr uint8_t PlaySoundEntry[] = {0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xd1, 0x48, 0x81};

using ChatReaderFn = void (*)(uint8_t* packet);
using UnitMessageFn = void (*)(Unit* player, int32_t message, Unit* subject);
using ClientFindFn = Unit* (*)(uint32_t id, uint32_t type);
using PlaySoundFn = void* (*)(int32_t sound, Unit* unit, int64_t, int64_t, int64_t);
using ClientOfFn = void* (*)(Unit* player);
using SendChatFn = void (*)(void* client, const uint8_t* record);
using ClientUnitFn = Unit* (*)(void* client, int32_t zero);

ClientOfFn g_clientOf = nullptr;
SendChatFn g_sendChat = nullptr;
ClientUnitFn g_clientUnit = nullptr;
constexpr int MenuOpenFlagRva = 0x2a486a4;  // client: u32, 1 while the NPC menu is up (harness_cmds.cpp reads the same)
uint64_t g_base = 0;
std::atomic<int> g_sent{0}, g_failed{0};
ChatReaderFn g_chatOriginal = nullptr;
PluginMessageFn g_pluginHandlers[256] = {};
UnitMessageFn g_unitMessage = nullptr;
ChatReaderFn g_unitPacketOriginal = nullptr;
ClientFindFn g_clientFind = nullptr;
PlaySoundFn g_playSound = nullptr;
std::atomic<int> g_rewardSounds{0}, g_rewardLastMessage{0}, g_rewardLastSound{0};
std::atomic<int> g_pluginReceived{0};

// Notices held for a later server frame (SendClientNotice).
struct Held {
	bool used;
	void* client;
	int framesOpen;
	ULONGLONG queuedAt;
	uint8_t record[clientmsg::ChatRecordSize];
};
constexpr int HeldMax = 16;
Held g_held[HeldMax] = {};

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

// The client's chat reader: a plugin message goes to its handler, everything else to D2R.
auto TakePluginMessage(uint8_t* packet) noexcept -> bool {
	__try {
		if (packet[0] != clientmsg::ChatPacketId || packet[1] != clientmsg::TypeHidden) {
			return false;
		}
		size_t nameLen = 0;
		while (nameLen <= clientmsg::ChatNameMax && packet[10 + nameLen] != 0) {
			++nameLen;
		}
		if (nameLen > clientmsg::ChatNameMax) {
			return false;
		}
		char kind = 0;
		uint8_t data[clientmsg::PluginPayloadMax];
		size_t n = 0;
		if (!clientmsg::ParsePluginMessage(reinterpret_cast<const char*>(packet + 11 + nameLen), &kind, data, sizeof(data), &n)) {
			return false;
		}
		g_pluginReceived.fetch_add(1);
		PluginMessageFn fn = g_pluginHandlers[static_cast<uint8_t>(kind)];
		if (fn != nullptr) {
			fn(data, n);
		}
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed.fetch_add(1);
		return true;
	}
}

void ChatReaderHook(uint8_t* packet) {
	if (!TakePluginMessage(packet)) {
		g_chatOriginal(packet);
	}
}

// Replaces the handler pointer of table entry 0x26 when it still points at D2R's reader. True when it did.
auto HookChatReader(uint64_t base) noexcept -> bool {
	auto* entry = reinterpret_cast<uint64_t*>(base + PacketTableRva + clientmsg::ChatPacketId * PacketTableEntry);
	if (*entry != base + ChatReaderRva || !Matches(base + ChatReaderRva, ChatReaderEntry, sizeof(ChatReaderEntry))) {
		return false;
	}
	DWORD old = 0;
	if (!VirtualProtect(entry, sizeof(uint64_t), PAGE_READWRITE, &old)) {
		return false;
	}
	g_chatOriginal = reinterpret_cast<ChatReaderFn>(*entry);
	*entry = reinterpret_cast<uint64_t>(&ChatReaderHook);
	VirtualProtect(entry, sizeof(uint64_t), old, &old);
	return true;
}

// The client's handler of packet 0x2c: a reward message (0x5f-0x61) plays Median's sound at the unit, everything else goes
// to D2R. The unit lookup failing (not in this client's game) swallows the message like D2R's handler does.
auto TakeRewardMessage(uint8_t* packet) noexcept -> bool {
	__try {
		uint16_t message = 0;
		std::memcpy(&message, packet + 6, sizeof(message));
		const int sound = clientmsg::RewardSound(message);
		if (sound == 0) {
			return false;
		}
		uint32_t id = 0;
		std::memcpy(&id, packet + 2, sizeof(id));
		Unit* unit = g_clientFind(id, packet[1]);
		if (unit != nullptr) {
			g_playSound(sound, unit, 0, 0, 0);
			g_rewardLastMessage.store(message);
			g_rewardLastSound.store(sound);
			g_rewardSounds.fetch_add(1);
		}
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed.fetch_add(1);
		return true;
	}
}

void UnitPacketHook(uint8_t* packet) {
	if (!TakeRewardMessage(packet)) {
		g_unitPacketOriginal(packet);
	}
}

// Replaces the handler pointer of table entry 0x2c when it still points at D2R's handler. True when it did.
auto HookUnitPacket(uint64_t base) noexcept -> bool {
	auto* entry = reinterpret_cast<uint64_t*>(base + PacketTableRva + UnitPacketId * PacketTableEntry);
	if (*entry != base + UnitPacketRva || !Matches(base + UnitPacketRva, UnitPacketEntry, sizeof(UnitPacketEntry)) ||
	    !Matches(base + UnitMessageRva, UnitMessageEntry, sizeof(UnitMessageEntry)) ||
	    !Matches(base + ClientFindRva, ClientFindEntry, sizeof(ClientFindEntry)) ||
	    !Matches(base + PlaySoundRva, PlaySoundEntry, sizeof(PlaySoundEntry))) {
		return false;
	}
	DWORD old = 0;
	if (!VirtualProtect(entry, sizeof(uint64_t), PAGE_READWRITE, &old)) {
		return false;
	}
	g_clientFind = reinterpret_cast<ClientFindFn>(base + ClientFindRva);
	g_playSound = reinterpret_cast<PlaySoundFn>(base + PlaySoundRva);
	g_unitMessage = reinterpret_cast<UnitMessageFn>(base + UnitMessageRva);
	g_unitPacketOriginal = reinterpret_cast<ChatReaderFn>(*entry);
	*entry = reinterpret_cast<uint64_t>(&UnitPacketHook);
	VirtualProtect(entry, sizeof(uint64_t), old, &old);
	return true;
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
		g_base = base;
	}
	const bool reader = ok && HookChatReader(base);
	const bool rewards = ok && HookUnitPacket(base);
	std::snprintf(report, reportSize, "%s%s",
	              !ok ? "off: entry bytes of 0x48fde0 / 0x477a30 differ"
	                  : (reader ? "on: chat packet 0x26 to one player, plugin messages read on the client"
	                            : "on: chat packet 0x26 to one player; plugin messages off (chat reader entry differs)"),
	              !ok ? "" : (rewards ? "; reward sounds on (unit packet 0x2c)" : "; reward sounds off (0x2c handler differs), reward notices are text"));
	return ok;
}

auto SendClientMessage(Unit* player, const char* text, uint8_t colour) noexcept -> bool {
	return Send(player, clientmsg::TypeSystem, colour, text);
}

auto SendClientNotice(Unit* player, const char* text, uint8_t colour) noexcept -> bool {
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
		for (Held& h : g_held) {
			if (!h.used) {
				h.used = true;
				h.client = client;
				h.framesOpen = 0;
				h.queuedAt = GetTickCount64();
				clientmsg::BuildChat(h.record, clientmsg::TypeSystem, colour, d2r::At<uint32_t>(player, 0x08), text);
				return true;
			}
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	g_failed.fetch_add(1);  // queue full or the unit faulted: the line is dropped
	return false;
}

void FlushClientMessages(d2r::Game* game) noexcept {
	bool any = false;
	for (const Held& h : g_held) {
		any = any || h.used;
	}
	if (!any || game == nullptr || g_base == 0) {
		return;
	}
	const ULONGLONG now = GetTickCount64();
	__try {
		const bool menuOpen = d2r::At<uint32_t>(reinterpret_cast<void*>(g_base), MenuOpenFlagRva) == 1;
		for (Held& h : g_held) {
			if (!h.used) {
				continue;
			}
			const uint64_t waited = now - h.queuedAt;
			bool inGame = false;
			int guard = 0;
			for (auto* client = d2r::At<uint8_t*>(game, GameClients); client != nullptr && guard < 64;
			     client = d2r::At<uint8_t*>(client, ClientNext), ++guard) {
				if (client == h.client && d2r::At<int32_t>(client, ClientState) == 4) {
					inGame = true;
					break;
				}
			}
			if (!inGame) {  // another game's frame; the player left or the game ended when it never turns up
				if (waited >= clientmsg::NoticeDropMs) {
					h.used = false;
					g_failed.fetch_add(1);
				}
				continue;
			}
			h.framesOpen += menuOpen ? 1 : 0;
			if (clientmsg::NoticeDue(menuOpen, h.framesOpen, waited)) {
				h.used = false;
				g_sendChat(h.client, h.record);
				g_sent.fetch_add(1);
			}
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed.fetch_add(1);
	}
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

auto SendGamePluginMessage(d2r::Game* game, char kind, const uint8_t* data, size_t n) noexcept -> int {
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
			if (unit != nullptr && SendPluginMessage(unit, kind, data, n)) {
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

void SetPluginMessageHandler(char kind, PluginMessageFn fn) noexcept {
	g_pluginHandlers[static_cast<uint8_t>(kind)] = fn;
}

auto PluginMessagesOn() noexcept -> bool {
	return g_chatOriginal != nullptr;
}

auto SendPluginMessage(Unit* player, char kind, const uint8_t* data, size_t n) noexcept -> bool {
	if (g_clientOf == nullptr || g_sendChat == nullptr || player == nullptr) {
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
		if (!clientmsg::BuildPluginMessage(record, d2r::At<uint32_t>(player, 0x08), kind, data, n)) {
			g_failed.fetch_add(1);
			return false;
		}
		g_sendChat(client, record);
		g_sent.fetch_add(1);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed.fetch_add(1);
		return false;
	}
}

auto SendRewardMessage(Unit* player, uint32_t message, const char* what, int count) noexcept -> bool {
	if (g_unitMessage == nullptr || clientmsg::RewardSound(message) == 0) {
		char line[96];
		clientmsg::RewardLine(what, count, line, sizeof(line));
		return SendClientNotice(player, line, clientmsg::Tan);
	}
	__try {
		if (player == nullptr || d2r::At<int32_t>(player, d2r::UnitType) != 0) {
			return false;
		}
		g_unitMessage(player, static_cast<int32_t>(message), player);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed.fetch_add(1);
		return false;
	}
}

auto RewardSoundsOn() noexcept -> bool {
	return g_unitPacketOriginal != nullptr;
}

void RewardSoundStats(int* played, int* lastMessage, int* lastSound) noexcept {
	*played = g_rewardSounds.load();
	*lastMessage = g_rewardLastMessage.load();
	*lastSound = g_rewardLastSound.load();
}

auto PluginMessagesReceived() noexcept -> int {
	return g_pluginReceived.load();
}

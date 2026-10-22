// Server -> one player text messages with D2R's own chat packet 0x26 (client_messages.cpp, layout in
// client_messages_logic.h). Used by the dungeon lockout, the quest reward notices and the goblin notice.
#pragma once

#include <D2RLPlugin/api.h>

#include "d2r.h"

#include <cstddef>
#include <cstdint>

// Checks the entry bytes of the two game functions it calls. True when messages can be sent. The report line says what is on.
auto InstallClientMessages(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Sends `text` (UTF-8, colour codes "\xC3\xBF" "c4" allowed) as a system line to `player`. False when messages are off,
// the unit is not a player with a client, or the call faulted. Must run on the game (server) thread.
auto SendClientMessage(d2r::Unit* player, const char* text, uint8_t colour = 0) noexcept -> bool;

// Like SendClientMessage for a line sent from a quest dialog handler (Akara's Heart, the Jerhyn / Cain crystals, Ormus's
// shrines): it is held until the NPC's speech is over and the menu is up (client_messages_logic.h NoticeDue), because a chat
// line during the speech freezes it and the NPC menu never opens. FlushClientMessages sends it. True when queued.
auto SendClientNotice(d2r::Unit* player, const char* text, uint8_t colour = 0) noexcept -> bool;

// Sends the held notices that are due; called once per server frame by the game-frame hook. Game (server) thread.
void FlushClientMessages(d2r::Game* game) noexcept;

// The same line to every player in the game (the clients in state 4); returns how many got it. Game (server) thread.
auto SendGameMessage(d2r::Game* game, const char* text, uint8_t colour = 0) noexcept -> int;

// Same with every field of the record chosen (harness verb `sysmsg`).
auto SendClientChat(d2r::Unit* player, uint8_t type, uint8_t colour, const char* text) noexcept -> bool;

// Counters for the `mxl` command: messages sent / failed this session.
void ClientMessageCounts(int* sent, int* failed) noexcept;

// Plugin messages (client_messages_logic.h): hidden chat packets from the server part of the plugin to the client part.
// The client reads them in front of D2R's chat reader and calls the handler registered for the kind letter.
using PluginMessageFn = void (*)(const uint8_t* data, size_t n);
void SetPluginMessageHandler(char kind, PluginMessageFn fn) noexcept;
// True when the client reader is hooked (the handlers can run in this process).
auto PluginMessagesOn() noexcept -> bool;
// Sends a plugin message of `kind` (payload up to clientmsg::PluginPayloadMax bytes) to `player`. Game (server) thread.
auto SendPluginMessage(d2r::Unit* player, char kind, const uint8_t* data, size_t n) noexcept -> bool;
// The same plugin message to every player in the game; returns how many got it. Game (server) thread.
auto SendGamePluginMessage(d2r::Game* game, char kind, const uint8_t* data, size_t n) noexcept -> int;
// Plugin messages the client part has read this session.
auto PluginMessagesReceived() noexcept -> int;

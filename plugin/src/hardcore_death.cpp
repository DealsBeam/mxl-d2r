// A dead Hardcore character becomes Softcore in Single Player (audit DC9 / HC1; Median 1.6.0: "Fixed bugs with the Hardcore to
// Softcore conversion on Single Player").
//
// Median plays offline, and on its Single Player a Hardcore character that dies is not lost: it converts to Softcore and
// goes on. D2R has two player death handlers (the dead-mode entries 0x42a5e0 and 0x42de40, slots of the player mode
// tables at 0x1d19148 and 0x1d192b8) that mark the character dead (player info flag 8) and, for a Hardcore one (info
// flag 4, `0x486900(info, 4)`), call 0x406e10, which starts the game-over timer (game +0x5800); the respawn handler
// 0x4b6160 refuses a Hardcore client (client record +0x5658 bit 0x800) and, for the Softcore branch, asserts that the info
// flag 4 is clear. The hooks clear both Hardcore marks before the handler runs, so the death takes D2R's ordinary Softcore
// path (respawn in town with the usual penalty) and the character is saved without the Hardcore status.
//
// D2R offline is always Single Player (there is no TCP/IP or LAN game); the rule is still limited to a game with one
// connected player, so a second player in the game would leave the Hardcore death alone.
#include "hardcore_death.h"

#include "d2r.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

constexpr uint64_t DeathRva = 0x42a5e0;       // (game, player unit)
constexpr uint64_t DeathLateRva = 0x42de40;   // (game, player unit): the same tail, reached from the mode handler 0x42dbd0
// mov [rsp+8],rbx / mov [rsp+0x18],rbp / push rsi / push rdi / push r14 / sub rsp,0x30
constexpr uint8_t DeathEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83};
// push rbx / push rsi / push rdi / sub rsp,0x50 / mov rsi,rcx / mov rdi,rdx / mov rcx,rdx
constexpr uint8_t DeathLateEntry[] = {0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x50, 0x48, 0x8b, 0xf1, 0x48, 0x8b, 0xfa, 0x48, 0x8b};
constexpr uint64_t PlayerInfoRva = 0x48fde0;  // (player) -> info struct (flags ushort at +0xe)
constexpr uint64_t InfoFlagRva = 0x486900;    // (info, mask) -> flags & mask
constexpr uint64_t InfoSetFlagRva = 0x4862b0; // (info, mask, on)
constexpr uint64_t ClientUnitRva = 0x484c90;  // (client, 0) -> player unit
constexpr int InfoHardcore = 4;
constexpr int GameClients = 0x138;
constexpr int ClientNext = 0x628;
constexpr int ClientState = 0x04;
constexpr int ClientFlags = 0x5658;
constexpr uint32_t ClientHardcore = 0x800;
constexpr int32_t ClientInGame = 4;

using DeathFn = void (*)(void* game, d2r::Unit* player);
using PlayerInfoFn = uint8_t* (*)(d2r::Unit* player);
using InfoFlagFn = uint16_t (*)(uint8_t* info, uint16_t mask);
using InfoSetFlagFn = void (*)(uint8_t* info, uint16_t mask, int32_t on);
using ClientUnitFn = d2r::Unit* (*)(void* client, int32_t zero);

DeathFn g_original = nullptr;
DeathFn g_originalLate = nullptr;
PlayerInfoFn g_playerInfo = nullptr;
InfoFlagFn g_infoFlag = nullptr;
InfoSetFlagFn g_infoSetFlag = nullptr;
ClientUnitFn g_clientUnit = nullptr;
std::atomic<unsigned> g_hardcoreDeaths{0}, g_converted{0};

void Convert(void* game, d2r::Unit* player) noexcept {
	if (game != nullptr && player != nullptr && d2r::At<int32_t>(player, d2r::UnitType) == 0) {
		uint8_t* info = g_playerInfo(player);
		if (info != nullptr && g_infoFlag(info, InfoHardcore) != 0) {
			g_hardcoreDeaths.fetch_add(1);
			uint8_t* own = nullptr;
			int players = 0;
			int guard = 0;
			for (auto* client = d2r::At<uint8_t*>(game, GameClients); client != nullptr && guard < 64;
			     client = d2r::At<uint8_t*>(client, ClientNext), ++guard) {
				if (d2r::At<int32_t>(client, ClientState) != ClientInGame) {
					continue;
				}
				++players;
				if (g_clientUnit(client, 0) == player) {
					own = client;
				}
			}
			if (players == 1 && own != nullptr) {
				g_infoSetFlag(info, InfoHardcore, 0);
				*reinterpret_cast<uint32_t*>(own + ClientFlags) &= ~ClientHardcore;
				g_converted.fetch_add(1);
			}
		}
	}
}

void PlayerDeath(void* game, d2r::Unit* player) {
	Convert(game, player);
	g_original(game, player);
}

void PlayerDeathLate(void* game, d2r::Unit* player) {
	Convert(game, player);
	g_originalLate(game, player);
}

}  // namespace

void HardcoreDeathStats(unsigned* hardcoreDeaths, unsigned* converted) noexcept {
	*hardcoreDeaths = g_hardcoreDeaths.load();
	*converted = g_converted.load();
}

auto InstallHardcoreDeath(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	g_playerInfo = reinterpret_cast<PlayerInfoFn>(context->exeBase + PlayerInfoRva);
	g_infoFlag = reinterpret_cast<InfoFlagFn>(context->exeBase + InfoFlagRva);
	g_infoSetFlag = reinterpret_cast<InfoSetFlagFn>(context->exeBase + InfoSetFlagRva);
	g_clientUnit = reinterpret_cast<ClientUnitFn>(context->exeBase + ClientUnitRva);
	const bool first = context->InstallInlineHook(DeathRva, DeathEntry, sizeof(DeathEntry), reinterpret_cast<void*>(&PlayerDeath),
	                                              reinterpret_cast<void**>(&g_original));
	const bool second = context->InstallInlineHook(DeathLateRva, DeathLateEntry, sizeof(DeathLateEntry), reinterpret_cast<void*>(&PlayerDeathLate),
	                                               reinterpret_cast<void**>(&g_originalLate));
	const bool ok = first && second;
	std::snprintf(report, reportSize, ok ? "on: a dead Hardcore player in a one-player game is made Softcore before D2R's death handlers 0x42a5e0 and 0x42de40"
	                                     : "partly off: death handler hook failed (0x42a5e0 %s, 0x42de40 %s)",
	              first ? "ok" : "entry bytes differ", second ? "ok" : "entry bytes differ");
	return ok;
}

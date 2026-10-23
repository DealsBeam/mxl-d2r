// mxl-core: D2RLoader plugin carrying the D2Sigma behaviour that data alone cannot express (docs/plugin.md).
//
// It loads with the MedianXL mod, logs the game build it runs on and adds the console command "mxl". Engine
// changes are tied to one game image (RVAs from the dump of the D2R image D2RLoader runs, tools/dump_image.py), so
// the plugin records which image it expects (the PE header timestamp; it changes with D2R patches and D2RLoader
// releases) and changes nothing on any other.
//
// Engine changes: room for more than 512 states (states_fix.cpp), level types above 36 (level_types.cpp),
// D2Sigma's calc codes (calc_codes.cpp), D2Sigma's added skill server functions (sigma_skills.cpp), D2Sigma's
// item spells (item_spells.cpp), D2Sigma's mana costs (mana_costs.cpp), D2Sigma's cube recipe conditions
// (cube_conditions.cpp).
#include <D2RLPlugin/api.h>

#include "sigma_skills.h"
#include "states_fix.h"
#include "level_types.h"
#include "calc_codes.h"
#include "combat.h"
#include "info_panels.h"
#include "buff_icons.h"
#include "stat_columns.h"
#include "item_events.h"
#include "cooldowns.h"
#include "cooldowns_logic.h"
#include "no_corpse.h"
#include "no_corpse_logic.h"
#include "properties_ext.h"
#include "item_spells.h"
#include "missile_hits.h"
#include "mana_costs.h"
#include "item_rules.h"
#include "stat_rules.h"
#include "skill_damage.h"
#include "heat.h"
#include "trophy_timer.h"
#include "skill_restrict.h"
#include "craft_flags.h"
#include "client_messages.h"
#include "stash_pages.h"
#include "strings.h"
#include "game_end.h"
#include "map_memory.h"
#include "cube_conditions.h"
#include "quests_act1.h"
#include "quests_act2_5.h"
#include "quests_rewards.h"
#include "dungeon_lockout.h"
#include "engine_gaps.h"
#include "gamble.h"
#include "town_rules.h"
#include "unique_limits.h"
#include "goblins.h"
#include "monster_ai.h"
#include "monster_looks.h"
#include "hirelings.h"
#include "monster_rules.h"
#include "items_ext.h"
#include "item_gen.h"
#include "npc_actions.h"
#include "requirement_gates.h"
#include "skill_tree.h"
#include "state_trace.h"
#include "testharness.h"
#include "item_text.h"
#include "tooltip_probe.h"
#include "travel.h"
#include "waypoints.h"
#include "collide_types.h"
#include "skill_starts.h"
#include "whirlwind.h"

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* PluginVersion = "0.61.0";
constexpr uint32_t ExpectedImage = 0x6ab3782cU;  // TimeDateStamp of the dumped image (D2R 3.3.0 build 93847, D2RLoader 1.3.1)

const D2RL::PluginContext* g_context = nullptr;
int g_skillsInstalled = 0;
char g_skillsReport[512] = "";
char g_statesReport[160] = "off: not the expected game image";
char g_levelTypesReport[160] = "off: not the expected game image";
char g_calcReport[160] = "off: not the expected game image";
char g_itemSpellsReport[160] = "off: not the expected game image";
char g_manaCostsReport[160] = "off: not the expected game image";
char g_itemRulesReport[320] = "off: not the expected game image";
char g_skillDamageReport[160] = "off: not the expected game image";
char g_skillRestrictReport[160] = "off: not the expected game image";
char g_heatReport[200] = "off: not the expected game image";
char g_statRulesReport[320] = "off: not the expected game image";
char g_trophyReport[200] = "off: not the expected game image";
char g_missileHitsReport[1024] = "";
char g_cubeReport[192] = "off: not the expected game image";
char g_craftReport[160] = "off: not the expected game image";
char g_gameEndReport[96] = "off: not the expected game image";
char g_mapMemoryReport[128] = "off: not the expected game image";
char g_noCorpseReport[160] = "off: not the expected game image";
char g_respawnWaitReport[160] = "off: not the expected game image";
char g_soulstealerReport[200] = "off: not the expected game image";
char g_corpsePickupReport[200] = "off: not the expected game image";
char g_cooldownsReport[320] = "off: not the expected game image";
char g_questsReport[192] = "off: not the expected game image";
char g_quests2Report[224] = "off: not the expected game image";
char g_lockoutReport[192] = "off: not the expected game image";
char g_goblinsReport[192] = "off: not the expected game image";
char g_monsterAiReport[192] = "off: not the expected game image";
char g_monsterLooksReport[256] = "off: not the expected game image";
char g_npcReport[256] = "off: not the expected game image";
char g_stringsReport[256] = "off: not the expected game image";
char g_hirelingsReport[192] = "off: not the expected game image";
char g_mercReviveReport[192] = "off: not the expected game image";
char g_monLevelReport[192] = "off: not the expected game image";
char g_playersXReport[192] = "off: not the expected game image";
char g_normalTcReport[192] = "off: not the expected game image";
char g_leashReport[192] = "off: not the expected game image";
char g_throneReport[192] = "off: not the expected game image";
char g_questXpReport[192] = "off: not the expected game image";
char g_questRewardsReport[256] = "off: not the expected game image";
char g_skillTreeReport[192] = "off: not the expected game image";
char g_reqGatesReport[256] = "off: not the expected game image";
char g_combatReport[256] = "off: not the expected game image";
char g_statColumnsReport[256] = "off: not the expected game image";
char g_infoPanelsReport[256] = "off: not the expected game image";
char g_itemTextReport[256] = "off: not the expected game image";
char g_tooltipProbeReport[256] = "off: test harness only";
char g_stashPagesReport[256] = "off: not the expected game image";
char g_buffIconsReport[256] = "off: not the expected game image";
char g_gambleReport[192] = "off: not the expected game image";
char g_uniqueLimitsReport[192] = "off: not the expected game image";
char g_engineGapsReport[256] = "off: not the expected game image";
char g_townRulesReport[256] = "off: not the expected game image";
char g_itemEventsReport[448] = "off: not the expected game image";
char g_propsReport[192] = "off: not the expected game image";
char g_itemsExtReport[320] = "off: not the expected game image";
char g_itemGenReport[320] = "off: not the expected game image";
char g_waypointsReport[256] = "off: not the expected game image";
char g_collideReport[256] = "off: not the expected game image";
char g_skillStartsReport[256] = "off: not the expected game image";
char g_clientMsgReport[192] = "off: not the expected game image";
char g_travelReport[448] = "off: not the expected game image";
int g_missileHitsInstalled = 0;

constexpr D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "mxl-core",
	.name        = "Median XL core",
	.version     = PluginVersion,
	.author      = "mxl-d2r",
	.description = "D2Sigma behaviour for the Median XL port (skills, formulas, engine limits).",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::ModScopedOnly | D2RL::PluginFlags::NativeHooks,
};

auto ImageStamp(const D2RL::PluginContext* context) noexcept -> uint32_t {
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(context->exeBase);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(context->exeBase + dos->e_lfanew);
	return nt->FileHeader.TimeDateStamp;
}

auto BuildMatches(const D2RL::PluginContext* context) noexcept -> bool {
	return context->exeBase != 0 && ImageStamp(context) == ExpectedImage;
}

// Diagnosis switch: mxl-core-off.txt next to the DLL lists parts to leave out (any of: cltdostub states levels calc skills
// items missiles mana townmana durability durabilitytip repairui quantity potionamounts stamina manaregen autoid skilldmg restrict heat trophy itemstat whirlwind cube craftflags clientmsg quests quests2 lockout goblins medianai namecolors monsterautomap spawnoverlays npc reqgates hirelings mercrevive monlevel playersx normaltc leash throne questxp questrewards wpauto mapreveal hotkeys skilltree combat damagered statcols itemevents nocorpse respawnwait corpsepickup cooldowns props runewords xp partyxp classlock ethereal sockets dyes; "trace" turns the bug #22 state trace on, "statesfix" turns the states fix on (off by default since 0.35.0), dyes mapmem; infopanels bufficons stashpages gamble hitfloor recast slowfloor uniquelimits npcheal tplock statclick respecbuffs soulstealer
// "keepcurrent" leaves the current-states array in place), separated by spaces, commas or lines.
char g_offList[256] = "";

void ReadOffList() noexcept {
	HMODULE self = nullptr;
	char path[MAX_PATH] = "";
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCSTR>(&ReadOffList), &self) ||
	    GetModuleFileNameA(self, path, MAX_PATH) == 0) {
		return;
	}
	char* slash = std::strrchr(path, '\\');
	if (slash == nullptr || static_cast<size_t>(slash - path) + 20 >= sizeof(path)) {
		return;
	}
	strcpy_s(slash + 1, sizeof(path) - static_cast<size_t>(slash + 1 - path), "mxl-core-off.txt");
	FILE* f = nullptr;
	if (fopen_s(&f, path, "rb") != 0 || f == nullptr) {
		return;
	}
	const size_t n = std::fread(g_offList, 1, sizeof(g_offList) - 1, f);
	g_offList[n] = '\0';
	std::fclose(f);
	for (char* c = g_offList; *c != '\0'; ++c) {
		*c = (*c >= 'A' && *c <= 'Z') ? static_cast<char>(*c - 'A' + 'a') : (std::isalpha(static_cast<unsigned char>(*c)) ? *c : ' ');
	}
}

auto Off(const char* part) noexcept -> bool {
	const size_t len = std::strlen(part);
	for (const char* p = std::strstr(g_offList, part); p != nullptr; p = std::strstr(p + 1, part)) {
		if ((p == g_offList || p[-1] == ' ') && (p[len] == ' ' || p[len] == '\0')) {
			return true;
		}
	}
	return false;
}

// Console line; with the test harness on it is also appended to mxl-testharness-console.txt (the console panel cannot
// be opened by an unattended run, see testharness.cpp).
void Say(const D2RL::ConsoleCommandContext* command, const char* text) noexcept {
	command->plugin->WriteConsoleMessage(text);
	HarnessEcho(text);
}

auto MxlCommand(D2R::Game::Client* client, const D2RL::ConsoleCommandContext* command, void* userData) noexcept
	-> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;
	if (command == nullptr || command->plugin == nullptr || g_context == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	char line[384];
	std::snprintf(line, sizeof(line), "mxl-core %s | D2R %s | image 0x%08x (expects 0x%08x, %s) | exe base 0x%llx",
	              PluginVersion, g_context->buildVersion ? g_context->buildVersion : "?", ImageStamp(g_context),
	              ExpectedImage, BuildMatches(g_context) ? "match" : "MISMATCH, hooks off",
	              static_cast<unsigned long long>(g_context->exeBase));
	Say(command, line);
	std::snprintf(line, sizeof(line), "states fix %s", g_statesReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "level types %s", g_levelTypesReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "calc codes %s", g_calcReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "skill functions %d/%d:%s", g_skillsInstalled, SigmaSkillCount(),
	              g_skillsReport[0] ? g_skillsReport : " none");
	Say(command, line);
	std::snprintf(line, sizeof(line), "item spells %s", g_itemSpellsReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "missile hits %d/%d:%s", g_missileHitsInstalled, MissileHitCount(),
	              g_missileHitsReport[0] ? g_missileHitsReport : " none");
	Say(command, line);
	std::snprintf(line, sizeof(line), "mana costs %s", g_manaCostsReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "item rules %s", g_itemRulesReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "skill damage %s", g_skillDamageReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "skill restrict %s", g_skillRestrictReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "heat %s", g_heatReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "stat rules %s", g_statRulesReport);
	Say(command, line);
	{
		char status[96];
		TrophyTimerStatus(status, sizeof(status));
		std::snprintf(line, sizeof(line), "trophy timer %s (%s)", g_trophyReport, status);
		Say(command, line);
	}
	std::snprintf(line, sizeof(line), "cube conditions %s", g_cubeReport);
	Say(command, line);
	{
		uint32_t cowOutside = 0, cowRefused = 0, noQuest = 0, flagSet = 0, noPortal = 0;
		CowPortalCounts(&cowOutside, &cowRefused, &noQuest, &flagSet, &noPortal);
		std::snprintf(line, sizeof(line), "craft flags %s | Cow portals outside act 1: %u opened, %u refused (quest not done %u, flag set %u, portal not created %u)", g_craftReport, cowOutside, cowRefused,
		              noQuest, flagSet, noPortal);
	}
	Say(command, line);
	std::snprintf(line, sizeof(line), "game end %s (games ended %d)", g_gameEndReport, GameEndCount());
	Say(command, line);
	std::snprintf(line, sizeof(line), "map memory %s", g_mapMemoryReport);
	Say(command, line);
	{
		long corpses = 0, penalties = 0, held = 0, respawns = 0;
		char report[160];
		if (NoCorpseCounts(&corpses, &penalties, &held, &respawns)) {
			nocorpse::FormatReport(report, sizeof(report), corpses, penalties);
		} else {
			std::snprintf(report, sizeof(report), "%s", g_noCorpseReport);
		}
		std::snprintf(line, sizeof(line), "no corpse %s", report);
		Say(command, line);
		std::snprintf(line, sizeof(line), "respawn wait %s (held %ld, let through %ld)", g_respawnWaitReport, held, respawns);
		Say(command, line);
		long heals = 0, deaths = 0, lastType = -1, lastClass = -1, lastLife = -1, lastMax = -1;
		if (SoulstealerHeals(&heals, &deaths, &lastType, &lastClass, &lastLife, &lastMax)) {
			nocorpse::FormatSoulstealerReport(report, sizeof(report), heals, deaths, lastType, lastClass, lastLife, lastMax);
		} else {
			std::snprintf(report, sizeof(report), "%s", g_soulstealerReport);
		}
		std::snprintf(line, sizeof(line), "soulstealer %s", report);
		Say(command, line);
		long enters = 0, picked = 0, kept = 0, faults = 0;
		if (CorpsePickupCounts(&enters, &picked, &kept, &faults)) {
			nocorpse::FormatPickupReport(report, sizeof(report), picked, kept, faults);
		} else {
			std::snprintf(report, sizeof(report), "%s", g_corpsePickupReport);
		}
		std::snprintf(line, sizeof(line), "corpse pickup %s (level entries %ld)", report, enters);
		Say(command, line);
	}
	{
		long starts = 0, reduced = 0, shared = 0, dropped = 0, monsters = 0;
		char report[360];
		if (CooldownCounts(&starts, &reduced, &shared, &dropped, &monsters)) {
			cooldowns::Counters c;
			c.starts = starts;
			c.reduced = reduced;
			c.shared = shared;
			c.dropped = dropped;
			c.monsters = monsters;
			cooldowns::FormatReport(report, sizeof(report), CooldownMemberCount(), CooldownGroupCount(), c);
		} else {
			std::snprintf(report, sizeof(report), "%s", g_cooldownsReport);
		}
		std::snprintf(line, sizeof(line), "cooldowns %s", report);
		Say(command, line);
	}
	std::snprintf(line, sizeof(line), "act 1 quests %s", g_questsReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "act 2-5 quests %s", g_quests2Report);
	Say(command, line);
	{
		int refused = 0, started = 0;
		DungeonLockoutCounts(&refused, &started);
		std::snprintf(line, sizeof(line), "dungeon lockout %s (refused %d, clocks started %d, portals taken across acts %d)", g_lockoutReport, refused, started,
		              DungeonActTravels());
		Say(command, line);
	}
	{
		char status[160] = "";
		GoblinsStatus(status, sizeof(status));
		std::snprintf(line, sizeof(line), "goblins %s | %s", g_goblinsReport, status);
		Say(command, line);
	}
	{
		char status[256] = "";
		MonsterAiStatus(status, sizeof(status));
		std::snprintf(line, sizeof(line), "monster ai %s | %s", g_monsterAiReport, status);
		Say(command, line);
	}
	{
		char status[224] = "";
		MonsterLooksStatus(status, sizeof(status));
		std::snprintf(line, sizeof(line), "monster looks %s | %s", g_monsterLooksReport, status);
		Say(command, line);
	}
	std::snprintf(line, sizeof(line), "skill tree %s", g_skillTreeReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "npc actions %s", g_npcReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "strings %s", g_stringsReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "requirement gates %s", g_reqGatesReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "hirelings %s", g_hirelingsReport);
	Say(command, line);
	{
		int32_t level = 0;
		const int32_t last = MercReviveLast(&level);
		std::snprintf(line, sizeof(line), "merc revive price %s (%d priced, last %d gold for level %d)", g_mercReviveReport,
		              MercRevivePrices(), last, level);
	}
	Say(command, line);
	std::snprintf(line, sizeof(line), "monster level %s", g_monLevelReport);
	Say(command, line);
	{
		const PlayersTrace pt = PlayersTraceLast();
		std::snprintf(line, sizeof(line), "players scaling %s (%d records, last players %d, life %d%% -> %d%%, exp %d%% -> %d%%)",
		              g_playersXReport, pt.records, pt.players, pt.hpIn, pt.hpOut, pt.expIn, pt.expOut);
		Say(command, line);
		const TcTrace tc = NormalTcTrace(false);
		std::snprintf(line, sizeof(line), "normal-difficulty treasure class %s (%d class lookups, %d with a level, last class %d level %d)",
		              g_normalTcReport, tc.calls, tc.leveled, tc.lastClass, tc.lastLevel);
		Say(command, line);
	}
	std::snprintf(line, sizeof(line), "summon leash %s", g_leashReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "Baal throne delay %s", g_throneReport);
	Say(command, line);
	{
		const QuestRewardCounts counts = QuestRewardsCounts();
		std::snprintf(line, sizeof(line), "quest experience %s (%d grants, last %d)", g_questXpReport, counts.xpGrants, counts.lastXp);
		Say(command, line);
		std::snprintf(line, sizeof(line), "quest rewards %s (hellforge %d, countess %d, prison of ice %d)", g_questRewardsReport,
		              counts.hellforge, counts.countess, counts.prisonItems);
		Say(command, line);
	}
	std::snprintf(line, sizeof(line), "stash pages %s", g_stashPagesReport);
	Say(command, line);
	StashPagesStatus(line, sizeof(line));
	Say(command, line);
	std::snprintf(line, sizeof(line), "waypoints %s", g_waypointsReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "collide types %s", g_collideReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "skill starts %s", g_skillStartsReport);
	Say(command, line);
	{
		char status[448];
		TravelStatus(status, sizeof(status));
		std::snprintf(line, sizeof(line), "travel %s", g_travelReport);
		Say(command, line);
		Say(command, status);
	}
	{
		int sent = 0, failed = 0;
		ClientMessageCounts(&sent, &failed);
		std::snprintf(line, sizeof(line), "client messages %s (sent %d, failed %d)", g_clientMsgReport, sent, failed);
		Say(command, line);
	}
	std::snprintf(line, sizeof(line), "combat %s", g_combatReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "gamble %s", g_gambleReport);
	Say(command, line);
	{
		unique_limits::Counts counts = {};
		if (UniqueLimitCounts(&counts)) {
			unique_limits::FormatReport(g_uniqueLimitsReport, sizeof(g_uniqueLimitsReport), true, counts);
		}
		std::snprintf(line, sizeof(line), "unique limits %s", g_uniqueLimitsReport);
		Say(command, line);
	}
	std::snprintf(line, sizeof(line), "engine gaps %s", g_engineGapsReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "town rules %s (portal refusals %d)", g_townRulesReport, TownPortalRefusals());
	Say(command, line);
	std::snprintf(line, sizeof(line), "stat columns %s", g_statColumnsReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "item text %s (%u lines written)", g_itemTextReport, ItemTextLinesWritten());
	Say(command, line);
	std::snprintf(line, sizeof(line), "tooltip probe %s", g_tooltipProbeReport);
	Say(command, line);
	{
		int asked = 0, opened = 0, failed = 0;
		InfoPanelCounts(&asked, &opened, &failed);
		std::snprintf(line, sizeof(line), "help panels %s (asked %d, opened %d, failed %d)", g_infoPanelsReport, asked, opened, failed);
		Say(command, line);
	}
	{
		char counters[320];
		BuffIconShown none[1];
		BuffIconsStatus(counters, sizeof(counters), none, 0);
		std::snprintf(line, sizeof(line), "buff icons %s (%s)", g_buffIconsReport, counters);
		Say(command, line);
	}
	std::snprintf(line, sizeof(line), "item events %s", g_itemEventsReport);
	Say(command, line);
	std::snprintf(line, sizeof(line), "property functions %s", g_propsReport);
	Say(command, line);
	{
		int extended = 0, refused = 0, changed = 0, seen = 0, padded = 0;
		ItemsExtCounts(&extended, &refused, &changed, &seen, &padded);
		char text[480];
		std::snprintf(text, sizeof(text), "items %s | runewords with extra properties %d (refused %d), found by Jewel padding %d | kills with a changed experience %d of %d",
		              g_itemsExtReport, extended, refused, padded, changed, seen);
		Say(command, text);
		char gen[400];
		ItemGenSummary(text, sizeof(text));
		std::snprintf(gen, sizeof(gen), "item generation %s | %s", g_itemGenReport, text);
		Say(command, gen);
	}
	return D2RL::ConsoleCommandResult::Handled;
}

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	g_context = context;
	char line[256];
	std::snprintf(line, sizeof(line), "mxl-core %s loaded (mod %s, D2R %s, image 0x%08x%s)", PluginVersion,
	              context->activeMod ? context->activeMod : "?", context->buildVersion ? context->buildVersion : "?",
	              ImageStamp(context), BuildMatches(context) ? "" : "; not the expected image, engine hooks stay off");
	context->LogInfo(line);
	if (BuildMatches(context)) {
		ReadOffList();
		if (g_offList[0] != '\0') {
			std::snprintf(line, sizeof(line), "mxl-core: mxl-core-off.txt leaves out: %s", g_offList);
			context->LogInfo(line);
		}
		constexpr const char* OffNote = "off: left out by mxl-core-off.txt";
		if (Off("clientmsg")) {
			std::snprintf(g_clientMsgReport, sizeof(g_clientMsgReport), "%s", OffNote);
		} else {
			InstallClientMessages(context, g_clientMsgReport, sizeof(g_clientMsgReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: client messages %s", g_clientMsgReport);
		context->LogInfo(line);
		// Median's strings by key (the `medianxl` namespace) and the plugin's own ids for D2R's NPC menu: before the users below.
		if (Off("strings")) {
			std::snprintf(g_stringsReport, sizeof(g_stringsReport), "%s", OffNote);
		} else {
			InstallStrings(context, g_stringsReport, sizeof(g_stringsReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: strings %s", g_stringsReport);
		context->LogInfo(line);
		// Off unless asked for (bug #22, 2026-10-14): D2RLoader 1.3.1 already syncs more than 512 states ("wide
		// states", D2RCore SendChangedWideStateStats) and reads both arrays at D2R's own offsets (+0xaf0, then the
		// "changed" flags right after), so moving them stopped every state change from reaching the client.
		if (!Off("statesfix")) {
			std::snprintf(g_statesReport, sizeof(g_statesReport),
			              "off: D2RLoader's wide states handle Median's states (\"statesfix\" in mxl-core-off.txt turns it on)");
		} else {
			InstallStatesFix(context, Off("keepcurrent"), g_statesReport, sizeof(g_statesReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: states fix %s", g_statesReport);
		context->LogInfo(line);
		if (Off("levels")) {
			std::snprintf(g_levelTypesReport, sizeof(g_levelTypesReport), "%s", OffNote);
		} else {
			InstallLevelTypeGuards(context, g_levelTypesReport, sizeof(g_levelTypesReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: level types %s", g_levelTypesReport);
		context->LogInfo(line);
		SetItemFunctionsOff(Off("itemstat"));
		if (Off("calc")) {
			std::snprintf(g_calcReport, sizeof(g_calcReport), "%s", OffNote);
		} else {
			InstallCalcCodes(context, g_calcReport, sizeof(g_calcReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: calc codes %s", g_calcReport);
		context->LogInfo(line);
		if (Off("skills")) {
			std::snprintf(g_skillsReport, sizeof(g_skillsReport), " %s", OffNote);
		} else {
			g_skillsInstalled = InstallSigmaSkills(context, g_skillsReport, sizeof(g_skillsReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: %d/%d skill functions:%s", g_skillsInstalled, SigmaSkillCount(),
		              g_skillsReport);
		context->LogInfo(line);
		if (Off("items")) {
			std::snprintf(g_itemSpellsReport, sizeof(g_itemSpellsReport), "%s", OffNote);
		} else {
			InstallItemSpells(context, g_itemSpellsReport, sizeof(g_itemSpellsReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: item spells %s", g_itemSpellsReport);
		context->LogInfo(line);
		if (Off("missiles")) {
			std::snprintf(g_missileHitsReport, sizeof(g_missileHitsReport), " %s", OffNote);
		} else {
			g_missileHitsInstalled = InstallMissileHits(context, g_missileHitsReport, sizeof(g_missileHitsReport), !Off("cltdostub"));
		}
		std::snprintf(line, sizeof(line), "mxl-core: %d/%d missile hits:%s", g_missileHitsInstalled, MissileHitCount(),
		              g_missileHitsReport);
		context->LogInfo(line);
		if (Off("mana")) {
			std::snprintf(g_manaCostsReport, sizeof(g_manaCostsReport), "%s", OffNote);
		} else {
			InstallManaCosts(context, !Off("townmana"), g_manaCostsReport, sizeof(g_manaCostsReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: mana costs %s", g_manaCostsReport);
		context->LogInfo(line);
		if (Off("skilldmg")) {
			std::snprintf(g_skillDamageReport, sizeof(g_skillDamageReport), "%s", OffNote);
		} else {
			InstallSkillDamage(context, g_skillDamageReport, sizeof(g_skillDamageReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: skill damage %s", g_skillDamageReport);
		context->LogInfo(line);
		if (Off("restrict")) {
			std::snprintf(g_skillRestrictReport, sizeof(g_skillRestrictReport), "%s", OffNote);
		} else {
			InstallSkillRestrict(context, g_skillRestrictReport, sizeof(g_skillRestrictReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: skill restrict %s", g_skillRestrictReport);
		context->LogInfo(line);
		if (Off("heat")) {
			std::snprintf(g_heatReport, sizeof(g_heatReport), "%s", OffNote);
		} else {
			InstallHeat(context, !Off("manaregen"), g_heatReport, sizeof(g_heatReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: heat %s", g_heatReport);
		context->LogInfo(line);
		{
			const StatRulesParts parts{!Off("potionamounts"), !Off("stamina"), !Off("statclick")};
			if (!parts.potionAmounts && !parts.stamina && !parts.shiftClick) {
				std::snprintf(g_statRulesReport, sizeof(g_statRulesReport), "%s", OffNote);
			} else {
				InstallStatRules(context, parts, g_statRulesReport, sizeof(g_statRulesReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: stat rules %s", g_statRulesReport);
			context->LogInfo(line);
		}
		if (Off("trophy")) {
			std::snprintf(g_trophyReport, sizeof(g_trophyReport), "%s", OffNote);
		} else {
			InstallTrophyTimer(context, g_trophyReport, sizeof(g_trophyReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: trophy timer %s", g_trophyReport);
		context->LogInfo(line);
		{
			const ItemRulesParts parts{!Off("durability"), !Off("quantity"), !Off("durabilitytip"), !Off("repairui")};
			if (!parts.durability && !parts.quantity && !parts.tooltip && !parts.repairUi) {
				std::snprintf(g_itemRulesReport, sizeof(g_itemRulesReport), "%s", OffNote);
			} else {
				InstallItemRules(context, parts, g_itemRulesReport, sizeof(g_itemRulesReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: item rules %s", g_itemRulesReport);
			context->LogInfo(line);
		}
		{
			char report[96];
			if (Off("whirlwind")) {
				std::snprintf(report, sizeof(report), "%s", OffNote);
			} else {
				InstallWhirlwind(context, report, sizeof(report));
			}
			std::snprintf(line, sizeof(line), "mxl-core: whirlwind %s", report);
			context->LogInfo(line);
		}
		{
			char report[192];
			if (Off("cube")) {
				std::snprintf(report, sizeof(report), "%s", OffNote);
			} else {
				InstallCubeConditions(context, report, sizeof(report));
			}
			std::snprintf(g_cubeReport, sizeof(g_cubeReport), "%s", report);
			std::snprintf(line, sizeof(line), "mxl-core: cube conditions %s", report);
			context->LogInfo(line);
		}
		{
			if (Off("gameend")) {
				std::snprintf(g_gameEndReport, sizeof(g_gameEndReport), "%s", OffNote);
			} else {
				InstallGameEnd(context, g_gameEndReport, sizeof(g_gameEndReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: game end %s", g_gameEndReport);
			context->LogInfo(line);
		}
		{
			if (Off("mapmem")) {
				std::snprintf(g_mapMemoryReport, sizeof(g_mapMemoryReport), "%s", OffNote);
			} else {
				InstallMapMemory(context, g_mapMemoryReport, sizeof(g_mapMemoryReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: map memory %s", g_mapMemoryReport);
			context->LogInfo(line);
			NoCorpseRememberImage(context);  // the harness reads the corpse list also with the parts off (the controls of check corpsepickup)
			if (Off("nocorpse")) {
				std::snprintf(g_noCorpseReport, sizeof(g_noCorpseReport), "%s", OffNote);
			} else {
				InstallNoCorpse(context, g_noCorpseReport, sizeof(g_noCorpseReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: no corpse %s", g_noCorpseReport);
			context->LogInfo(line);
			if (Off("respawnwait")) {
				std::snprintf(g_respawnWaitReport, sizeof(g_respawnWaitReport), "%s", OffNote);
			} else {
				InstallRespawnWait(context, g_respawnWaitReport, sizeof(g_respawnWaitReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: respawn wait %s", g_respawnWaitReport);
			context->LogInfo(line);
			if (Off("soulstealer")) {
				std::snprintf(g_soulstealerReport, sizeof(g_soulstealerReport), "%s", OffNote);
			} else {
				InstallSoulstealer(context, g_soulstealerReport, sizeof(g_soulstealerReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: soulstealer %s", g_soulstealerReport);
			context->LogInfo(line);
			if (Off("corpsepickup")) {
				std::snprintf(g_corpsePickupReport, sizeof(g_corpsePickupReport), "%s", OffNote);
			} else {
				InstallCorpsePickup(context, g_corpsePickupReport, sizeof(g_corpsePickupReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: corpse pickup %s", g_corpsePickupReport);
			context->LogInfo(line);
			if (Off("cooldowns")) {
				std::snprintf(g_cooldownsReport, sizeof(g_cooldownsReport), "%s", OffNote);
			} else {
				InstallCooldowns(context, g_cooldownsReport, sizeof(g_cooldownsReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: cooldowns %s", g_cooldownsReport);
			context->LogInfo(line);
		}
		{
			char report[160];
			if (Off("craftflags")) {
				std::snprintf(report, sizeof(report), "%s", OffNote);
			} else {
				InstallCraftFlags(context, report, sizeof(report));
			}
			std::snprintf(g_craftReport, sizeof(g_craftReport), "%s", report);
			std::snprintf(line, sizeof(line), "mxl-core: craft flags %s", report);
			context->LogInfo(line);
		}
		{
			if (Off("quests")) {
				std::snprintf(g_questsReport, sizeof(g_questsReport), "%s", OffNote);
			} else {
				InstallQuestsAct1(context, g_questsReport, sizeof(g_questsReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: act 1 quests %s", g_questsReport);
			context->LogInfo(line);
		}
		{
			if (Off("quests2")) {
				std::snprintf(g_quests2Report, sizeof(g_quests2Report), "%s", OffNote);
			} else {
				InstallQuestsAct2To5(context, !Off("respecbuffs"), g_quests2Report, sizeof(g_quests2Report));
			}
			std::snprintf(line, sizeof(line), "mxl-core: act 2-5 quests %s", g_quests2Report);
			context->LogInfo(line);
		}
		{
			if (Off("lockout")) {
				std::snprintf(g_lockoutReport, sizeof(g_lockoutReport), "%s", OffNote);
			} else {
				InstallDungeonLockout(context, g_lockoutReport, sizeof(g_lockoutReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: dungeon lockout %s", g_lockoutReport);
			context->LogInfo(line);
		}
		{
			const bool names = !Off("namecolors");
			const bool automap = !Off("monsterautomap");
			const bool overlays = !Off("spawnoverlays");
			if (!names && !automap && !overlays) {
				std::snprintf(g_monsterLooksReport, sizeof(g_monsterLooksReport), "%s", OffNote);
			} else {
				InstallMonsterLooks(context, names, automap, overlays, g_monsterLooksReport, sizeof(g_monsterLooksReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: monster looks %s", g_monsterLooksReport);
			context->LogInfo(line);
		}
		{
			if (Off("goblins")) {
				std::snprintf(g_goblinsReport, sizeof(g_goblinsReport), "%s", OffNote);
			} else {
				InstallGoblins(context, g_goblinsReport, sizeof(g_goblinsReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: goblins %s", g_goblinsReport);
			context->LogInfo(line);
		}
		{
			// "medianai" off: AI 155 stays alive with D2R's Andariel think; Summoner and MosquitoNest stay D2R's.
			InstallMonsterAi(context, !Off("medianai"), g_monsterAiReport, sizeof(g_monsterAiReport));
			std::snprintf(line, sizeof(line), "mxl-core: monster ai %s", g_monsterAiReport);
			context->LogInfo(line);
		}
		{
			if (Off("npc")) {
				std::snprintf(g_npcReport, sizeof(g_npcReport), "%s", OffNote);
			} else {
				InstallNpcActions(context, g_npcReport, sizeof(g_npcReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: npc actions %s", g_npcReport);
			context->LogInfo(line);
		}
		{
			if (Off("reqgates")) {
				std::snprintf(g_reqGatesReport, sizeof(g_reqGatesReport), "%s", OffNote);
			} else {
				InstallRequirementGates(context, g_reqGatesReport, sizeof(g_reqGatesReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: requirement gates %s", g_reqGatesReport);
			context->LogInfo(line);
		}
		{
			if (Off("hirelings")) {
				std::snprintf(g_hirelingsReport, sizeof(g_hirelingsReport), "%s", OffNote);
			} else {
				InstallHirelings(context, g_hirelingsReport, sizeof(g_hirelingsReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: hirelings %s", g_hirelingsReport);
			context->LogInfo(line);
		}
		{
			if (Off("mercrevive")) {
				std::snprintf(g_mercReviveReport, sizeof(g_mercReviveReport), "%s", OffNote);
			} else {
				InstallMercRevive(context, g_mercReviveReport, sizeof(g_mercReviveReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: merc revive price %s", g_mercReviveReport);
			context->LogInfo(line);
		}
		{
			if (Off("monlevel")) {
				std::snprintf(g_monLevelReport, sizeof(g_monLevelReport), "%s", OffNote);
			} else {
				InstallMonsterLevel(context, g_monLevelReport, sizeof(g_monLevelReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: monster level %s", g_monLevelReport);
			context->LogInfo(line);
		}
		{
			if (Off("playersx")) {
				std::snprintf(g_playersXReport, sizeof(g_playersXReport), "%s", OffNote);
			} else {
				InstallPlayersTable(context, g_playersXReport, sizeof(g_playersXReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: players scaling %s", g_playersXReport);
			context->LogInfo(line);
		}
		{
			if (Off("normaltc")) {
				std::snprintf(g_normalTcReport, sizeof(g_normalTcReport), "%s", OffNote);
			} else {
				InstallNormalTc(context, g_normalTcReport, sizeof(g_normalTcReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: normal-difficulty treasure class %s", g_normalTcReport);
			context->LogInfo(line);
		}
		{
			if (Off("leash")) {
				std::snprintf(g_leashReport, sizeof(g_leashReport), "%s", OffNote);
			} else {
				InstallLeash(context, g_leashReport, sizeof(g_leashReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: summon leash %s", g_leashReport);
			context->LogInfo(line);
		}
		{
			if (Off("throne")) {
				std::snprintf(g_throneReport, sizeof(g_throneReport), "%s", OffNote);
			} else {
				InstallThrone(context, g_throneReport, sizeof(g_throneReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: Baal throne delay %s", g_throneReport);
			context->LogInfo(line);
		}
		{
			if (Off("questxp")) {
				std::snprintf(g_questXpReport, sizeof(g_questXpReport), "%s", OffNote);
			} else {
				InstallQuestXp(context, g_questXpReport, sizeof(g_questXpReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: quest experience %s", g_questXpReport);
			context->LogInfo(line);
		}
		{
			if (Off("questrewards")) {
				std::snprintf(g_questRewardsReport, sizeof(g_questRewardsReport), "%s", OffNote);
			} else {
				InstallQuestRewards(context, g_questRewardsReport, sizeof(g_questRewardsReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: quest rewards %s", g_questRewardsReport);
			context->LogInfo(line);
		}
		{
			if (Off("waypoints")) {
				std::snprintf(g_waypointsReport, sizeof(g_waypointsReport), "%s", OffNote);
			} else {
				VerifyWaypoints(context->exeBase, g_waypointsReport, sizeof(g_waypointsReport));
				char panel[160];
				InstallWaypointPanel(context, panel, sizeof(panel));
				const size_t used = std::strlen(g_waypointsReport);
				std::snprintf(g_waypointsReport + used, sizeof(g_waypointsReport) - used, "; %s", panel);
			}
			std::snprintf(line, sizeof(line), "mxl-core: waypoints %s", g_waypointsReport);
			context->LogInfo(line);
		}
		{
			if (Off("skillstarts")) {
				std::snprintf(g_skillStartsReport, sizeof(g_skillStartsReport), "%s", OffNote);
			} else {
				const int started = InstallSkillStarts(context, g_skillStartsReport, sizeof(g_skillStartsReport));
				std::snprintf(g_skillStartsReport + std::strlen(g_skillStartsReport), sizeof(g_skillStartsReport) - std::strlen(g_skillStartsReport),
				              " (%d/2)", started);
			}
			std::snprintf(line, sizeof(line), "mxl-core: skill starts %s", g_skillStartsReport);
			context->LogInfo(line);
		}
		{
			if (Off("collidetypes")) {
				std::snprintf(g_collideReport, sizeof(g_collideReport), "%s", OffNote);
			} else {
				InstallCollideTypes(context, g_collideReport, sizeof(g_collideReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: collide types %s", g_collideReport);
			context->LogInfo(line);
		}
		{
			InstallTravel(context, !Off("wpauto"), !Off("mapreveal"), !Off("hotkeys"), g_travelReport, sizeof(g_travelReport));
			std::snprintf(line, sizeof(line), "mxl-core: travel %s", g_travelReport);
			context->LogInfo(line);
		}
		{
			if (Off("skilltree")) {
				std::snprintf(g_skillTreeReport, sizeof(g_skillTreeReport), "%s", OffNote);
			} else {
				InstallSkillTree(context, g_skillTreeReport, sizeof(g_skillTreeReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: skill tree %s", g_skillTreeReport);
			context->LogInfo(line);
		}
		{
			if (Off("combat")) {
				std::snprintf(g_combatReport, sizeof(g_combatReport), "%s", OffNote);
			} else {
				InstallCombat(context, !Off("damagered"), g_combatReport, sizeof(g_combatReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: combat %s", g_combatReport);
			context->LogInfo(line);
		}
		{
			if (Off("gamble")) {
				std::snprintf(g_gambleReport, sizeof(g_gambleReport), "%s", OffNote);
			} else {
				InstallGamble(context, g_gambleReport, sizeof(g_gambleReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: gamble %s", g_gambleReport);
			context->LogInfo(line);
		}
		{
			if (Off("uniquelimits")) {
				std::snprintf(g_uniqueLimitsReport, sizeof(g_uniqueLimitsReport), "%s", OffNote);
			} else {
				InstallUniqueLimits(context, g_uniqueLimitsReport, sizeof(g_uniqueLimitsReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: unique limits %s", g_uniqueLimitsReport);
			context->LogInfo(line);
		}
		{
			const bool hit = !Off("hitfloor"), rc = !Off("recast"), sl = !Off("slowfloor");
			if (!hit && !rc && !sl) {
				std::snprintf(g_engineGapsReport, sizeof(g_engineGapsReport), "%s", OffNote);
			} else {
				InstallEngineGaps(context, hit, rc, sl, g_engineGapsReport, sizeof(g_engineGapsReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: engine gaps %s", g_engineGapsReport);
			context->LogInfo(line);
		}
		{
			const bool heal = !Off("npcheal"), lock = !Off("tplock");
			if (!heal && !lock) {
				std::snprintf(g_townRulesReport, sizeof(g_townRulesReport), "%s", OffNote);
			} else {
				InstallTownRules(context, heal, lock, g_townRulesReport, sizeof(g_townRulesReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: town rules %s", g_townRulesReport);
			context->LogInfo(line);
		}
		{
			if (Off("statcols")) {
				std::snprintf(g_statColumnsReport, sizeof(g_statColumnsReport), "%s", OffNote);
			} else {
				InstallStatColumns(context, g_statColumnsReport, sizeof(g_statColumnsReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: stat columns %s", g_statColumnsReport);
			context->LogInfo(line);
		}
		{
			if (Off("infopanels")) {
				std::snprintf(g_infoPanelsReport, sizeof(g_infoPanelsReport), "%s", OffNote);
			} else {
				InstallInfoPanels(context, g_infoPanelsReport, sizeof(g_infoPanelsReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: help panels %s", g_infoPanelsReport);
			context->LogInfo(line);
		}
		{
			if (Off("stashpages")) {
				std::snprintf(g_stashPagesReport, sizeof(g_stashPagesReport), "%s", OffNote);
				StashPagesOff(context);
			} else {
				InstallStashPages(context, g_stashPagesReport, sizeof(g_stashPagesReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: stash pages %s", g_stashPagesReport);
			context->LogInfo(line);
		}
		{
			if (Off("bufficons")) {
				std::snprintf(g_buffIconsReport, sizeof(g_buffIconsReport), "%s", OffNote);
			} else {
				InstallBuffIcons(context, g_buffIconsReport, sizeof(g_buffIconsReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: buff icons %s", g_buffIconsReport);
			context->LogInfo(line);
		}
		{
			if (Off("itemevents")) {
				std::snprintf(g_itemEventsReport, sizeof(g_itemEventsReport), "%s", OffNote);
			} else {
				item_events::Install(context->exeBase, g_itemEventsReport, sizeof(g_itemEventsReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: item events %s", g_itemEventsReport);
			context->LogInfo(line);
		}
		{
			if (Off("props")) {
				std::snprintf(g_propsReport, sizeof(g_propsReport), "%s", OffNote);
			} else {
				InstallPropertiesExt(context, g_propsReport, sizeof(g_propsReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: property functions %s", g_propsReport);
			context->LogInfo(line);
		}
		{
			const bool rw = !Off("runewords");
			const bool xp = !Off("xp");
			const bool party = !Off("partyxp");
			if (!rw && !xp && !party) {
				std::snprintf(g_itemsExtReport, sizeof(g_itemsExtReport), "%s", OffNote);
			} else {
				InstallItemsExt(context, rw, xp, party, g_itemsExtReport, sizeof(g_itemsExtReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: items %s", g_itemsExtReport);
			context->LogInfo(line);
			const bool cl = !Off("classlock"), et = !Off("ethereal"), so = !Off("sockets"), dy = !Off("dyes"), id = !Off("autoid"), aq = !Off("allquals");
			if (!cl && !et && !so && !dy && !id && !aq) {
				std::snprintf(g_itemGenReport, sizeof(g_itemGenReport), "%s", OffNote);
			} else {
				InstallItemGen(context, cl, et, so, dy, id, aq, g_itemGenReport, sizeof(g_itemGenReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: item generation %s", g_itemGenReport);
			context->LogInfo(line);
		}
		if (Off("trace")) {  // diagnosis word, not a part: turns the state trace on (bug #22)
			char report[96];
			InstallStateTrace(context, report, sizeof(report));
			std::snprintf(line, sizeof(line), "mxl-core: state trace %s", report);
			context->LogInfo(line);
		}
	}
	{
		char report[192];  // off unless mxl-testharness.txt exists next to the DLL (unattended test input, testharness.cpp)
		const bool harness = InstallTestHarness(context, report, sizeof(report));
		if (harness) {
			std::snprintf(line, sizeof(line), "mxl-core: test harness %s", report);
			context->LogInfo(line);
		}
		// Median's item property lines (descfunc 32-38, class lock): the formatter of D2R is hooked for them, and for the
		// harness readback, which needs the hook even when the lines are left out by mxl-core-off.txt
		const bool linesOff = Off("itemtext");
		if (linesOff && !harness) {
			std::snprintf(g_itemTextReport, sizeof(g_itemTextReport), "%s", "off: left out by mxl-core-off.txt");
		} else {
			InstallItemText(context, !linesOff, g_itemTextReport, sizeof(g_itemTextReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: item text %s", g_itemTextReport);
		context->LogInfo(line);
		if (harness) {
			if (Off("tooltipprobe")) {  // the item text readback of the harness (h tip, h tiplog): hooks 0x2dc4b0 and watches 0x2d6520
				std::snprintf(g_tooltipProbeReport, sizeof(g_tooltipProbeReport), "%s", "off: left out by mxl-core-off.txt");
			} else {
				InstallTooltipProbe(context, g_tooltipProbeReport, sizeof(g_tooltipProbeReport));
			}
			std::snprintf(line, sizeof(line), "mxl-core: tooltip probe %s", g_tooltipProbeReport);
			context->LogInfo(line);
		}
	}
	return context->RegisterConsoleCommand("mxl", MxlCommand, "Median XL core plugin status.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	StopTestHarness();
	g_context = nullptr;
}

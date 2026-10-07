// mxl-core: D2RLoader plugin carrying the D2Sigma behaviour that data alone cannot express (docs/plugin.md).
//
// It loads with the MedianXL mod, logs the game build it runs on and adds the console command "mxl". Engine
// changes are tied to one game image (RVAs from the dump of the D2R image D2RLoader runs, tools/dump_image.py), so
// the plugin records which image it expects (the PE header timestamp; it changes with D2R patches and D2RLoader
// releases) and changes nothing on any other.
//
// Engine changes: room for more than 512 states (states_fix.cpp), level types above 36 (level_types.cpp),
// D2Sigma's calc codes (calc_codes.cpp), D2Sigma's added skill server functions (sigma_skills.cpp), D2Sigma's
// item spells (item_spells.cpp), D2Sigma's mana costs (mana_costs.cpp).
#include <D2RLPlugin/api.h>

#include "sigma_skills.h"
#include "states_fix.h"
#include "level_types.h"
#include "calc_codes.h"
#include "item_spells.h"
#include "missile_hits.h"
#include "mana_costs.h"
#include "state_trace.h"

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* PluginVersion = "0.34.1";
constexpr uint32_t ExpectedImage = 0x6ab3782cU;  // TimeDateStamp of the dumped image (D2R 3.3.0 build 93847, D2RLoader 1.3.1)

const D2RL::PluginContext* g_context = nullptr;
int g_skillsInstalled = 0;
char g_skillsReport[512] = "";
char g_statesReport[160] = "off: not the expected game image";
char g_levelTypesReport[160] = "off: not the expected game image";
char g_calcReport[160] = "off: not the expected game image";
char g_itemSpellsReport[160] = "off: not the expected game image";
char g_manaCostsReport[160] = "off: not the expected game image";
char g_missileHitsReport[512] = "";
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

// Diagnosis switch: mxl-core-off.txt next to the DLL lists parts to leave out (any of: states levels calc skills
// items missiles mana itemstat; "trace" turns the bug #22 state trace on, "keepcurrent" leaves the current-states array in place), separated by spaces, commas or lines.
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

auto MxlCommand(D2R::Game::Client* client, const D2RL::ConsoleCommandContext* command, void* userData) noexcept
	-> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;
	if (command == nullptr || command->plugin == nullptr || g_context == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	char line[256];
	std::snprintf(line, sizeof(line), "mxl-core %s | D2R %s | image 0x%08x (expects 0x%08x, %s) | exe base 0x%llx",
	              PluginVersion, g_context->buildVersion ? g_context->buildVersion : "?", ImageStamp(g_context),
	              ExpectedImage, BuildMatches(g_context) ? "match" : "MISMATCH, hooks off",
	              static_cast<unsigned long long>(g_context->exeBase));
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "states fix %s", g_statesReport);
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "level types %s", g_levelTypesReport);
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "calc codes %s", g_calcReport);
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "skill functions %d/%d:%s", g_skillsInstalled, SigmaSkillCount(),
	              g_skillsReport[0] ? g_skillsReport : " none");
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "item spells %s", g_itemSpellsReport);
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "missile hits %d/%d:%s", g_missileHitsInstalled, MissileHitCount(),
	              g_missileHitsReport[0] ? g_missileHitsReport : " none");
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "mana costs %s", g_manaCostsReport);
	command->plugin->WriteConsoleMessage(line);
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
		if (Off("states")) {
			std::snprintf(g_statesReport, sizeof(g_statesReport), "%s", OffNote);
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
			g_missileHitsInstalled = InstallMissileHits(context, g_missileHitsReport, sizeof(g_missileHitsReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: %d/%d missile hits:%s", g_missileHitsInstalled, MissileHitCount(),
		              g_missileHitsReport);
		context->LogInfo(line);
		if (Off("mana")) {
			std::snprintf(g_manaCostsReport, sizeof(g_manaCostsReport), "%s", OffNote);
		} else {
			InstallManaCosts(context, g_manaCostsReport, sizeof(g_manaCostsReport));
		}
		std::snprintf(line, sizeof(line), "mxl-core: mana costs %s", g_manaCostsReport);
		context->LogInfo(line);
		if (Off("trace")) {  // diagnosis word, not a part: turns the state trace on (bug #22)
			char report[96];
			InstallStateTrace(context, report, sizeof(report));
			std::snprintf(line, sizeof(line), "mxl-core: state trace %s", report);
			context->LogInfo(line);
		}
	}
	return context->RegisterConsoleCommand("mxl", MxlCommand, "Median XL core plugin status.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	g_context = nullptr;
}

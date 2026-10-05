// mxl-core: D2RLoader plugin carrying the D2Sigma behaviour that data alone cannot express (docs/plugin.md).
//
// Step 1 is only the skeleton: it loads with the MedianXL mod, logs the game build it runs on and adds the
// console command "mxl". Engine hooks come later and are tied to one D2R build (RVAs), so the plugin records
// which build it expects and refuses hooks on any other.
#include <D2RLPlugin/api.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr const char* PluginVersion = "0.1.0";
constexpr const char* ExpectedBuild = "93847";  // D2R build the RVAs (once there are any) were taken from

const D2RL::PluginContext* g_context = nullptr;

constexpr D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "mxl-core",
	.name        = "Median XL core",
	.version     = PluginVersion,
	.author      = "mxl-d2r",
	.description = "D2Sigma behaviour for the Median XL port (skills, formulas, engine limits).",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::ModScopedOnly,
};

auto BuildMatches(const D2RL::PluginContext* context) noexcept -> bool {
	const char* build = context->buildVersion;
	return build != nullptr && std::strstr(build, ExpectedBuild) != nullptr;
}

auto MxlCommand(D2R::Game::Client* client, const D2RL::ConsoleCommandContext* command, void* userData) noexcept
	-> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;
	if (command == nullptr || command->plugin == nullptr || g_context == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	char line[256];
	std::snprintf(line, sizeof(line), "mxl-core %s | game build %s (expects %s, %s) | exe base 0x%llx", PluginVersion,
	              g_context->buildVersion ? g_context->buildVersion : "?", ExpectedBuild,
	              BuildMatches(g_context) ? "match" : "MISMATCH, hooks off",
	              static_cast<unsigned long long>(g_context->exeBase));
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
	std::snprintf(line, sizeof(line), "mxl-core %s loaded (mod %s, game build %s%s)", PluginVersion,
	              context->activeMod ? context->activeMod : "?", context->buildVersion ? context->buildVersion : "?",
	              BuildMatches(context) ? "" : "; not the expected build, engine hooks stay off");
	context->LogInfo(line);
	return context->RegisterConsoleCommand("mxl", MxlCommand, "Median XL core plugin status.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	g_context = nullptr;
}

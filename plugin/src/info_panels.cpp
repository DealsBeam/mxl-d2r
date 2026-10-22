// Median's infographic help panels (misc2 Infographic, item event 13, packet 0x16; docs/unported.md).
//
// Median's server opens a help panel on the client when the player right clicks an item with an infographic (the catalyst of
// disenchantment). The panel is a picture with positioned text; there is no such art in D2R, so tools/infographics.py turns
// each infographics row into a text panel on the stock modal frame (title, the blocks in the picture's order) and this file
// registers them with D2RLoader's PanelService, as the SDK's ui-panel sample does: the layout JSON as a resource at
// data/global/ui/layouts/mxl-core/<id>hd.json (root widget "mxl-core/<id>"), the panel with CloseOnEscape.
//
// The server part (item_spells.cpp) sends the plugin message 'I' (one byte: the panel index) to the player; the client part
// reads it on the game's network thread and queues the open on the UI thread (ThreadService), the only place where
// openPanel may run.
#include "info_panels.h"

#include "client_messages.h"
#include "client_messages_logic.h"
#include "mxl_infographics.h"

#include <D2RLPlugin/panels.h>
#include <D2RLPlugin/resources.h>
#include <D2RLPlugin/threads.h>
#include <D2RLPlugin/widgets.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

const D2RL::PluginContext* g_context = nullptr;
const D2RL::PanelService* g_panels = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
D2RL::Panels::RegistrationHandle g_handles[mxl_infographics::PanelCount] = {};
bool g_ready = false;
std::atomic<int> g_asked{0}, g_opened{0}, g_failed{0};

void __cdecl OpenOnUiThread(const D2RL::PluginContext* context, void* user) noexcept {
	const auto panel = static_cast<int>(reinterpret_cast<intptr_t>(user));
	if (panel < 0 || panel >= mxl_infographics::PanelCount ||
	    g_panels->openPanel(context, g_handles[panel]) != D2RL::Panels::Result::Success) {
		g_failed.fetch_add(1);
		return;
	}
	g_opened.fetch_add(1);
}

// Plugin message 'I': one byte, the panel index.
void OnInfoMessage(const uint8_t* data, size_t n) {
	if (!g_ready || n != 1 || data[0] >= mxl_infographics::PanelCount) {
		g_failed.fetch_add(1);
		return;
	}
	g_asked.fetch_add(1);
	if (g_threads->runOnUiThread(g_context, &OpenOnUiThread, reinterpret_cast<void*>(static_cast<intptr_t>(data[0]))) !=
	    D2RL::Threads::Result::Success) {
		g_failed.fetch_add(1);
	}
}

}  // namespace

auto InstallInfoPanels(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const D2RL::ResourceService* resources = nullptr;
	if (context->QueryService(&resources) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasResourceServiceField(resources, D2RL::ResourceServiceRequiredSize) ||
	    context->QueryService(&g_panels) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasPanelServiceField(g_panels, D2RL::PanelServiceRequiredSize) ||
	    context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success) {
		std::snprintf(report, reportSize, "off: ResourceService / PanelService / ThreadService unavailable");
		return false;
	}
	int registered = 0;
	for (int i = 0; i < mxl_infographics::PanelCount; ++i) {
		char path[160];
		std::snprintf(path, sizeof(path), "data/global/ui/layouts/mxl-core/%shd.json", mxl_infographics::Panels[i].localId);
		const char* layout = mxl_infographics::Panels[i].layout;
		D2RL::Resources::ResourceRegistration resource{};
		resource.structSize = D2RL::Resources::ResourceRegistrationSize;
		resource.path = path;
		resource.bytes = layout;
		resource.byteCount = std::strlen(layout);
		D2RL::Resources::RegistrationHandle resourceHandle = D2RL::Resources::InvalidHandle;
		D2RL::Panels::PanelRegistration registration{};
		registration.structSize = D2RL::Panels::PanelRegistrationSize;
		registration.flags = D2RL::Panels::PanelFlags::CloseOnEscape;
		registration.localId = mxl_infographics::Panels[i].localId;
		if (resources->registerResource(context, &resource, &resourceHandle) == D2RL::Resources::Result::Success &&
		    g_panels->registerPanel(context, &registration, &g_handles[i]) == D2RL::Panels::Result::Success) {
			++registered;
		}
	}
	g_context = context;
	if (context->QueryService(&g_widgets) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasWidgetServiceField(g_widgets, D2RL::WidgetServiceRequiredSize)) {
		g_widgets = nullptr;
	}
	g_ready = registered == mxl_infographics::PanelCount;
	if (g_ready && PluginMessagesOn()) {
		SetPluginMessageHandler(clientmsg::PluginKindInfo, &OnInfoMessage);
	}
	std::snprintf(report, reportSize, "%s: %d of %d help panels registered (%d items open one)%s", g_ready ? "on" : "partial", registered,
	              static_cast<int>(mxl_infographics::PanelCount), static_cast<int>(sizeof(mxl_infographics::Items) / sizeof(mxl_infographics::Items[0])),
	              PluginMessagesOn() ? "" : "; plugin messages off, the client never gets the open request");
	return g_ready;
}

auto InfoPanelOf(uint32_t itemCode) noexcept -> int {
	for (const mxl_infographics::Item& item : mxl_infographics::Items) {
		if (item.code == itemCode) {
			return item.panel;
		}
	}
	return -1;
}

auto SendInfoPanel(d2r::Unit* player, int panel) noexcept -> bool {
	if (!g_ready || panel < 0 || panel >= mxl_infographics::PanelCount) {
		return false;
	}
	const uint8_t payload[1] = {static_cast<uint8_t>(panel)};
	return SendPluginMessage(player, clientmsg::PluginKindInfo, payload, sizeof(payload));
}

auto InfoPanelState(int panel) noexcept -> int {
	if (!g_ready || panel < 0 || panel >= mxl_infographics::PanelCount) {
		return -1;
	}
	D2RL::Panels::PanelInfo info{};
	info.structSize = D2RL::Panels::PanelInfoSize;
	if (g_panels->getPanelInfo(g_context, g_handles[panel], &info) != D2RL::Panels::Result::Success) {
		return -1;
	}
	return static_cast<int>(info.presentationState);
}

auto InfoPanelClose(int panel) noexcept -> bool {
	return g_ready && panel >= 0 && panel < mxl_infographics::PanelCount &&
	       g_panels->closePanel(g_context, g_handles[panel]) == D2RL::Panels::Result::Success;
}

void InfoPanelCounts(int* asked, int* opened, int* failed) noexcept {
	*asked = g_asked.load();
	*opened = g_opened.load();
	*failed = g_failed.load();
}

auto HarnessUiAction(const char* target, const char* command, const char* text) noexcept -> int {
	if (g_widgets == nullptr) {
		return -1;
	}
	D2RL::Widgets::UiAction action{};
	action.structSize = D2RL::Widgets::UiActionSize;
	action.target = target;
	action.command = command;
	action.text = text;
	return static_cast<int>(g_widgets->dispatchUiAction(g_context, &action));
}

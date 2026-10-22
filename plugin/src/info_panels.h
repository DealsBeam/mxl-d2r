// Median's infographic help panels on the plugin's PanelService (info_panels.cpp, generated mxl_infographics.h).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace d2r {
struct Unit;
}

// Registers the panels (resource layouts and PanelService panels; D2RLoaderLoadPlugin only) and the client handler of the
// plugin message 'I'. True when the panels are registered. The report line says what is on.
auto InstallInfoPanels(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// The panel an item code (the 4 characters as a little-endian word) opens on a right click, -1 for none.
auto InfoPanelOf(uint32_t itemCode) noexcept -> int;

// Asks the player's client to open panel `panel` (a plugin message). False when plugin messages are off or the send failed.
// Game (server) thread.
auto SendInfoPanel(d2r::Unit* player, int panel) noexcept -> bool;

// For the test harness (UI thread): the presentation state of a panel (0 unknown, 1 closed, 2 open; -1 not registered), closing
// it, and how many opens the client part has asked for / the UI thread has done / failed this session.
auto InfoPanelState(int panel) noexcept -> int;
auto InfoPanelClose(int panel) noexcept -> bool;
void InfoPanelCounts(int* asked, int* opened, int* failed) noexcept;
// For the test harness (UI thread): a native panel message through WidgetService, "target:command:text" as a button's
// onClickMessage has it (the harness opens D2R's Advanced Stats panel with "PanelManager", "OpenPanel", "AdvancedStatsPanel").
// 0 on success, else the service's result code (-1: no WidgetService).
auto HarnessUiAction(const char* target, const char* command, const char* text) noexcept -> int;

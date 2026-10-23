// Median's personal stash pages on D2RLoader's custom player page (design and limits: docs/stash-pages.md).
//
// D2R has one personal stash page; Median has ten. D2RLoader lets ONE plugin own ONE custom player page (item page 6 in the .d2s,
// saved and restored by D2RCore with rollback), so the port has two personal pages: the stock stash (page 1) and a plugin panel with
// the same look and the same 14 x 14 grid (page 2). Both panels carry a "n / 2" strip with two arrows. The shared stash keeps its
// ten tabs (d2rloader.toml add_shared_tabs). Off word: stashpages.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Registers the page, its panel (layout resource, PanelService, grid binding) and the UI message listener. D2RLoaderLoadPlugin only.
// True when page and panel are registered.
auto InstallStashPages(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// The part is off (mxl-core-off.txt) or could not register: hides the stock stash's "1 / 2" strip when the stash is built, because nothing is
// behind its arrows. InstallStashPages does this itself until the page and panel are registered.
void StashPagesOff(const D2RL::PluginContext* context) noexcept;

// "<n> opens, ..." for the mxl console command.
void StashPagesStatus(char* out, size_t outSize) noexcept;

// ---- test harness (harness_cmds.cpp `stashpage`; client side = UI thread) ----

// Open / close / toggle the plugin panel. 0 queued, -1 not available.
auto StashPageOpen(int mode /* 0 close, 1 open, 2 toggle */) noexcept -> int;
// PanelState of the plugin panel: 0 unknown, 1 closed, 2 open, -1 not registered.
auto StashPageState() noexcept -> int;
// A native UI message through WidgetService ("PanelManager", "OpenPanel", "BankPanel"). The service's result code, -1 without the service.
auto StashPageUiAction(const char* target, const char* command, const char* text) noexcept -> int;
// Rect of a stock or plugin panel (child = "" for the panel itself) and whether it is visible-settable; text for the harness.
void StashPageWidget(const char* panel, const char* child, char* out, size_t outSize) noexcept;
// setWidgetVisible on a stock panel's child (UI thread).
auto StashPageSetVisible(const char* panel, const char* child, bool visible) noexcept -> int;
// The distinct stash related UI messages seen (target:command(text) ...).
void StashPageMessages(char* out, size_t outSize) noexcept;

// Queued work on the game thread (ItemService is game-thread only). op: "put <code> [x y]" creates an item on page 2, "list" lists the
// items of page 2 and the personal stash, "move <runtime id> <page 1|2> [x y]" moves one item (one atomic transaction),
// "fill <page>" fills a page with 1x1 runes until full, "drop <code>" destroys the items of that code on both pages (the checks' clean-up).
// Returns 1 queued, 0 refused (text says why); StashPageResult has the answer.
auto StashPageJob(const char* op, long long a, long long b, long long c, const char* code, char* out, size_t outSize) noexcept -> int;
// 1 finished ok, 0 finished failed, -1 pending, -2 never queued; the text in `out`.
auto StashPageResult(char* out, size_t outSize) noexcept -> int;

// Median's personal stash pages (design, save layout, failure modes: docs/stash-pages.md).
//
// D2RLoader's InventoryService gives a plugin one custom player page: item page 6 in the .d2s, restored on load by D2RCore (which
// stops the load rather than drop an item it cannot place), moved with the cursor like any stash page. This part registers that
// page at 14 x 14 (Median's page size) and a panel, "mxl-core/StashPage2", whose layout looks like the stash and whose grid is
// bound to the page (tools/ui_layouts.py writes mxl_stash_page.h). The stock stash panel gets a strip "1 / 2" with arrows whose
// onClickMessage is PanelManager:TogglePanel:mxl-core/StashPage2 (the same tool, bankexpansionlayouthd.json); the plugin panel's
// strip sends PanelManager:OpenPanel:BankExpansionLayout (the stock layout's root name). Both are D2R's own messages and the two
// panels share the left slot, so no plugin code is in the switch. A custom message target never reaches a UiMessageListener.
//
// Nothing here touches the save or the item lists: the pages hold ordinary items, D2RCore saves them. The ItemService is used by the
// test harness only (`stashpage put|list|move`).
#include "stash_pages.h"

#include "mxl_stash_page.h"

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/panels.h>
#include <D2RLPlugin/resources.h>
#include <D2RLPlugin/shared_events.h>
#include <D2RLPlugin/threads.h>
#include <D2RLPlugin/widgets.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

constexpr int PageWidth = 14, PageHeight = 14;  // Median's stash page (inventory.txt Big Bank, tools/ui_layouts.py STASH_GRID)
constexpr char StockStashPanel[] = "BankExpansionLayout";      // the stock stash's PanelManager name: its layout's root (tools/ui_layouts.py)
constexpr char StockStripName[] = "MxlPersonalNavigation";     // the "1 / 2" strip tools/ui_layouts.py adds to it

const D2RL::PluginContext* g_context = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::PanelService* g_panels = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
const D2RL::ItemService* g_items = nullptr;
D2RL::Inventory::PageRegistrationHandle g_page = 0;
D2RL::Panels::RegistrationHandle g_panel = 0;
bool g_ready = false;
bool g_panelBound = false;

std::atomic<uint32_t> g_openFailed{0}, g_messages{0};

constexpr uint32_t UiNames = 16, UiNameSize = 96;  // the last messages, newest last (g_messages counts them all)
char g_uiNames[UiNames][UiNameSize] = {};

auto ContainsNoCase(const char* hay, const char* needle) noexcept -> bool {
	if (hay == nullptr || needle == nullptr) {
		return false;
	}
	const size_t n = std::strlen(needle);
	for (; *hay != '\0'; ++hay) {
		size_t i = 0;
		while (i < n && hay[i] != '\0' && (hay[i] | 0x20) == (needle[i] | 0x20)) {
			++i;
		}
		if (i == n) {
			return true;
		}
	}
	return false;
}

// ---- the stash related UI messages, for the harness (`stashpage msgs`); the page switch itself is D2R's own PanelManager messages ----

// Without the page (the part is off, or another plugin owns the only custom page) the strip that bankexpansionlayouthd.json adds to the
// stock stash would have arrows that do nothing: hide it when the stock stash is built. Widgets are touched on the UI thread only.
void __cdecl HideStripOnUiThread(const D2RL::PluginContext*, void*) noexcept {
	if (g_ready || g_widgets == nullptr) {
		return;
	}
	D2RL::Widgets::WidgetHandle panel = 0, strip = 0;
	if (g_widgets->findPanel(g_context, StockStashPanel, &panel) == D2RL::Widgets::Result::Success &&
	    g_widgets->findWidget(g_context, panel, StockStripName, &strip) == D2RL::Widgets::Result::Success) {
		g_widgets->setWidgetVisible(g_context, strip, false);
	}
}

auto __cdecl OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept -> D2RL::SharedEvents::UiMessageAction {
	if (event == nullptr || event->target == nullptr || event->command == nullptr) {
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}
	if (!g_ready && g_threads != nullptr && event->text != nullptr && std::strcmp(event->text, StockStashPanel) == 0 &&
	    (std::strcmp(event->command, "LoadPanel") == 0 || std::strcmp(event->command, "OpenPanel") == 0)) {
		(void)g_threads->runOnUiThread(g_context, &HideStripOnUiThread, nullptr);
	}
	if (ContainsNoCase(event->target, "bank") || ContainsNoCase(event->target, "stash") || ContainsNoCase(event->text, "bank") ||
	    ContainsNoCase(event->text, "stash")) {
		char key[UiNameSize];
		std::snprintf(key, sizeof(key), "%s:%s(%s)", event->target, event->command, event->text != nullptr ? event->text : "");
		const uint32_t n = g_messages.fetch_add(1);
		std::memcpy(g_uiNames[n % UiNames], key, sizeof(key));
	}
	return D2RL::SharedEvents::UiMessageAction::Continue;
}

auto WatchUiMessages(const D2RL::PluginContext* context) noexcept -> bool {
	const D2RL::SharedEventService* events = nullptr;
	if (context->QueryService(&events) != D2RL::ServiceQueryResult::Success || !D2RL::HasSharedEventServiceField(events, D2RL::SharedEventServiceRequiredSize)) {
		return false;
	}
	D2RL::SharedEvents::UiMessageListener listener{};
	listener.structSize = D2RL::SharedEvents::UiMessageListenerSize;
	listener.callback = &OnUiMessage;
	D2RL::SharedEvents::ListenerHandle handle = 0;
	return events->registerUiMessageListener(context, &listener, &handle) == D2RL::SharedEvents::Result::Success;
}

// ---- game thread jobs (harness) ----

struct Job {
	std::atomic<int> state{0};  // 0 never queued, 1 queued, 2 finished
	char op[12] = "";
	long long a = 0, b = 0, c = 0;
	char code[8] = "";
	int ok = 0;
	char text[900] = "";
};
Job g_job;

struct Listing {
	char* text;
	size_t size;
	size_t used;
	uint32_t count;
	uint64_t wantId;                 // runtime id to find (move)
	D2RL::ItemHandle found;
	D2RL::Items::ItemInfo foundInfo;
	uint32_t wantCode = 0;           // collect the handles of the items with this code (drop)
	D2RL::ItemHandle* handles = nullptr;
	uint32_t handleCap = 0, handleCount = 0;
};

auto __cdecl OnItem(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* l = static_cast<Listing*>(user);
	++l->count;
	if (l->wantId != 0 && item->runtimeId == l->wantId) {
		l->found = item->handle;
		l->foundInfo = *item;
	}
	if (l->wantCode != 0 && item->code == l->wantCode && l->handleCount < l->handleCap) {
		l->handles[l->handleCount++] = item->handle;
	}
	if (l->text != nullptr && l->used + 48 < l->size) {
		const char* where = item->container == D2RL::Items::ItemContainer::CustomPage ? "p2" : item->container == D2RL::Items::ItemContainer::PersonalStash ? "p1" : "??";
		char code[5] = {static_cast<char>(item->code), static_cast<char>(item->code >> 8), static_cast<char>(item->code >> 16), static_cast<char>(item->code >> 24), 0};
		for (int i = 3; i >= 0 && code[i] == ' '; --i) {
			code[i] = '\0';
		}
		const int w = std::snprintf(l->text + l->used, l->size - l->used, "%s%s:%s@%d,%d#%u", l->used > 0 ? " " : "", where, code, item->x, item->y, item->runtimeId);
		if (w > 0) {
			l->used += static_cast<size_t>(w);
		}
	}
	return D2RL::Inventory::IterationAction::Continue;
}

auto Scan(const D2RL::PluginContext* context, D2RL::PlayerHandle player, Listing* l) noexcept -> bool {
	D2RL::Inventory::ItemFilter filter{};
	filter.structSize = D2RL::Inventory::ItemFilterSize;
	filter.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::PersonalStash) | D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::CustomPage);
	return g_inventory->forEachInventoryItem(context, player, &filter, &OnItem, l) == D2RL::Inventory::Result::Success;
}

void FillDestination(D2RL::Items::ItemDestination* d, int page, long long x, long long y) noexcept {
	d->structSize = D2RL::Items::ItemDestinationSize;
	d->container = page == 2 ? D2RL::Items::ItemContainer::CustomPage : D2RL::Items::ItemContainer::PersonalStash;
	d->customPageHandle = page == 2 ? g_page : 0;
	d->placement = x >= 0 && y >= 0 ? D2RL::Items::Placement::Exact : D2RL::Items::Placement::Automatic;
	d->x = x >= 0 ? static_cast<uint32_t>(x) : 0;
	d->y = y >= 0 ? static_cast<uint32_t>(y) : 0;
}

auto CreateOne(const D2RL::PluginContext* context, D2RL::PlayerHandle player, uint32_t code, int page, long long x, long long y, D2RL::ItemHandle* out) noexcept -> int {
	D2RL::Items::ItemCreateSpec spec{};
	spec.structSize = D2RL::Items::ItemCreateSpecSize;
	spec.code = code;
	spec.quality = D2RL::Items::Quality::Normal;
	spec.qualityRecordId = D2RL::Items::RandomQualityRecord;
	spec.itemLevel = 1;
	spec.seedMode = D2RL::Items::SeedMode::Random;
	spec.quantity = D2RL::Items::DefaultValue;
	spec.durability = D2RL::Items::DefaultValue;
	FillDestination(&spec.destination, page, x, y);
	return static_cast<int>(g_items->createItem(context, player, &spec, out));
}

void __cdecl JobOnGameThread(const D2RL::PluginContext* context, void*) noexcept {
	Job& job = g_job;
	job.ok = 0;
	job.text[0] = '\0';
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g_inventory->getLocalPlayer(context, &player) != D2RL::Inventory::Result::Success) {
		std::snprintf(job.text, sizeof(job.text), "no local player");
		job.state.store(2);
		return;
	}
	if (std::strcmp(job.op, "put") == 0) {
		char padded[5] = "    ";
		for (int i = 0; i < 4 && job.code[i] != '\0'; ++i) {
			padded[i] = job.code[i];
		}
		D2RL::ItemHandle item = D2RL::InvalidItemHandle;
		const int rc = CreateOne(context, player, D2RL::Items::MakeItemCode(padded[0], padded[1], padded[2], padded[3]), 2, job.a, job.b, &item);
		D2RL::Items::ItemInfo info{};
		info.structSize = D2RL::Items::ItemInfoSize;
		if (rc == 0 && g_items->getItemInfo(context, item, &info) == D2RL::Items::Result::Success) {
			std::snprintf(job.text, sizeof(job.text), "put create=0 container=%u at %d,%d id=%u", static_cast<unsigned>(info.container), info.x, info.y, info.runtimeId);
			job.ok = 1;
		} else {
			std::snprintf(job.text, sizeof(job.text), "put create=%d", rc);
		}
	} else if (std::strcmp(job.op, "fill") == 0) {
		int made = 0, rc = 0;
		for (int i = 0; i < 400 && rc == 0; ++i) {
			D2RL::ItemHandle item = D2RL::InvalidItemHandle;
			rc = CreateOne(context, player, D2RL::Items::MakeItemCode("r01"), static_cast<int>(job.a), -1, -1, &item);
			made += rc == 0 ? 1 : 0;
		}
		std::snprintf(job.text, sizeof(job.text), "fill page %lld: %d runes placed, stopped with create=%d", job.a, made, rc);
		job.ok = made > 0 ? 1 : 0;
	} else if (std::strcmp(job.op, "list") == 0) {
		Listing l{job.text + 8, sizeof(job.text) - 8, 0, 0, 0, 0, {}};
		std::memcpy(job.text, "list    ", 8);
		job.text[8] = '\0';
		job.ok = Scan(context, player, &l) ? 1 : 0;
		char head[24];
		std::snprintf(head, sizeof(head), "n=%u ", l.count);
		const size_t hl = std::strlen(head);
		std::memmove(job.text + hl, job.text + 8, l.used + 1);
		std::memcpy(job.text, head, hl);
	} else if (std::strcmp(job.op, "move") == 0) {
		Listing l{nullptr, 0, 0, 0, static_cast<uint64_t>(job.a), 0, {}};
		if (!Scan(context, player, &l) || l.found == 0) {
			std::snprintf(job.text, sizeof(job.text), "move: item #%lld not found on the pages", job.a);
		} else {
			D2RL::Items::ExistingItemOperation op{};
			op.structSize = D2RL::Items::ExistingItemOperationSize;
			op.kind = D2RL::Items::ExistingItemOperationKind::Move;
			op.item = l.found;
			FillDestination(&op.move.destination, static_cast<int>(job.b), job.c >= 0 ? (job.c & 0xff) : -1, job.c >= 0 ? (job.c >> 8) : -1);
			D2RL::Items::ExistingItemTransaction tx{};
			tx.structSize = D2RL::Items::ExistingItemTransactionSize;
			tx.player = player;
			tx.operationCount = 1;
			tx.operations = &op;
			D2RL::Items::ExistingItemTransactionResult result{};
			result.structSize = D2RL::Items::ExistingItemTransactionResultSize;
			const auto rc = g_items->executeExistingItemTransaction(context, &tx, &result);
			std::snprintf(job.text, sizeof(job.text), "move rc=%u committed=%u fail=%u", static_cast<unsigned>(rc), result.committedOperationCount, result.failureIndex);
			job.ok = rc == D2RL::Items::Result::Success ? 1 : 0;
		}
	} else if (std::strcmp(job.op, "drop") == 0) {
		// the test's clean-up: destroy every item of one code on the two pages (the checks use it on their own items only)
		static D2RL::ItemHandle handles[512];
		char padded[5] = "    ";
		for (int i = 0; i < 4 && job.code[i] != '\0'; ++i) {
			padded[i] = job.code[i];
		}
		Listing l{nullptr, 0, 0, 0, 0, 0, {}, D2RL::Items::MakeItemCode(padded[0], padded[1], padded[2], padded[3]), handles, 512, 0};
		int destroyed = 0;
		if (Scan(context, player, &l)) {
			for (uint32_t i = 0; i < l.handleCount; ++i) {
				destroyed += g_items->destroyItem(context, player, handles[i], D2RL::Items::SocketedItemPolicy::DestroyContents) == D2RL::Items::Result::Success ? 1 : 0;
			}
		}
		std::snprintf(job.text, sizeof(job.text), "drop %s: %u found, %d destroyed", job.code, l.handleCount, destroyed);
		job.ok = destroyed == static_cast<int>(l.handleCount) ? 1 : 0;
	} else {
		std::snprintf(job.text, sizeof(job.text), "unknown op %s", job.op);
	}
	job.state.store(2);
}

void __cdecl OpenOnUiThread(const D2RL::PluginContext* context, void* user) noexcept {
	const int mode = static_cast<int>(reinterpret_cast<intptr_t>(user));
	if (g_panels == nullptr || g_panel == 0) {
		return;
	}
	const auto rc = mode == 0 ? g_panels->closePanel(context, g_panel) : mode == 1 ? g_panels->openPanel(context, g_panel) : g_panels->togglePanel(context, g_panel);
	if (rc != D2RL::Panels::Result::Success) {
		g_openFailed.fetch_add(1);
	}
}

}  // namespace

void StashPagesOff(const D2RL::PluginContext* context) noexcept {
	g_context = context;
	if (context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success) {
		g_threads = nullptr;
	}
	if (context->QueryService(&g_widgets) != D2RL::ServiceQueryResult::Success || !D2RL::HasWidgetServiceField(g_widgets, D2RL::WidgetServiceRequiredSize)) {
		g_widgets = nullptr;
	}
	if (g_threads != nullptr && g_widgets != nullptr) {
		WatchUiMessages(context);  // hides the stock stash's "1 / 2" strip when it is built: nothing is behind its arrows
	}
}

auto InstallStashPages(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	StashPagesOff(context);  // the strip hider stays until the page and panel are registered (g_ready)
	const D2RL::ResourceService* resources = nullptr;
	if (context->QueryService(&g_inventory) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceRequiredSize) ||
	    context->QueryService(&resources) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasResourceServiceField(resources, D2RL::ResourceServiceRequiredSize) ||
	    context->QueryService(&g_panels) != D2RL::ServiceQueryResult::Success || !D2RL::HasPanelServiceField(g_panels, D2RL::PanelServiceRequiredSize) ||
	    g_threads == nullptr) {
		std::snprintf(report, reportSize, "off: InventoryService / ResourceService / PanelService / ThreadService unavailable");
		return false;
	}
	if (context->QueryService(&g_items) != D2RL::ServiceQueryResult::Success || !D2RL::HasItemServiceField(g_items, D2RL::ItemServiceRequiredSize)) {
		g_items = nullptr;
	}

	// the page: 14 x 14, default item policy (anything), charms stay inactive there like in Median's stash
	D2RL::Inventory::PlayerPageRegistration registration{};
	registration.structSize = D2RL::Inventory::PlayerPageRegistrationSize;
	registration.logicalId = "stash2";
	const auto reg = g_inventory->registerPlayerPage(context, &registration, &g_page);
	if (reg != D2RL::Inventory::Result::Success) {
		std::snprintf(report, reportSize, "off: the loader has a custom page already (registerPlayerPage result %u; one plugin owns the only page)", static_cast<unsigned>(reg));
		return false;
	}
	D2RL::Inventory::PlayerPageConfiguration configuration{};
	configuration.structSize = D2RL::Inventory::PlayerPageConfigurationSize;
	configuration.width = PageWidth;
	configuration.height = PageHeight;
	const auto cfg = g_inventory->configurePlayerPage(context, g_page, &configuration);
	if (cfg != D2RL::Inventory::Result::Success) {
		std::snprintf(report, reportSize, "off: configurePlayerPage %ux%u failed (result %u)", PageWidth, PageHeight, static_cast<unsigned>(cfg));
		return false;
	}

	// the panel
	char path[160];
	std::snprintf(path, sizeof(path), "data/global/ui/layouts/mxl-core/%shd.json", mxl_stash_page::PanelLocalId);
	D2RL::Resources::ResourceRegistration resource{};
	resource.structSize = D2RL::Resources::ResourceRegistrationSize;
	resource.path = path;
	resource.bytes = mxl_stash_page::Layout;
	resource.byteCount = std::strlen(mxl_stash_page::Layout);
	D2RL::Resources::RegistrationHandle resourceHandle = D2RL::Resources::InvalidHandle;
	D2RL::Panels::PanelRegistration panel{};
	panel.structSize = D2RL::Panels::PanelRegistrationSize;
	panel.flags = D2RL::Panels::PanelFlags::CloseOnEscape | D2RL::Panels::PanelFlags::GameplayLeftSlot;
	panel.localId = mxl_stash_page::PanelLocalId;
	if (resources->registerResource(context, &resource, &resourceHandle) != D2RL::Resources::Result::Success ||
	    g_panels->registerPanel(context, &panel, &g_panel) != D2RL::Panels::Result::Success) {
		std::snprintf(report, reportSize, "partial: the page is registered but the panel is not (layout resource / registerPanel failed)");
		return false;
	}
	D2RL::Panels::PlayerPageGridBinding binding{};
	binding.structSize = D2RL::Panels::PlayerPageGridBindingSize;
	binding.panel = g_panel;
	binding.page = g_page;
	binding.childName = mxl_stash_page::GridName;
	g_panelBound = g_panels->bindPlayerPageGrid(context, &binding) == D2RL::Panels::Result::Success;

	g_ready = g_panelBound;
	std::snprintf(report, reportSize, "%s: personal stash page 2 (%d x %d, item page 6) registered, grid %s", g_ready ? "on" : "partial", PageWidth, PageHeight,
	              g_panelBound ? "bound" : "NOT bound");
	return g_ready;
}

void StashPagesStatus(char* out, size_t outSize) noexcept {
	std::snprintf(out, outSize, "stash pages: %s; panel opens failed %u, %u stash messages seen", g_ready ? "ready" : "not ready", g_openFailed.load(), g_messages.load());
}

auto StashPageOpen(int mode) noexcept -> int {
	if (!g_ready || g_threads == nullptr) {
		return -1;
	}
	return g_threads->runOnUiThread(g_context, &OpenOnUiThread, reinterpret_cast<void*>(static_cast<intptr_t>(mode))) == D2RL::Threads::Result::Success ? 0 : -1;
}

auto StashPageState() noexcept -> int {
	if (!g_ready) {
		return -1;
	}
	D2RL::Panels::PanelInfo info{};
	info.structSize = D2RL::Panels::PanelInfoSize;
	if (g_panels->getPanelInfo(g_context, g_panel, &info) != D2RL::Panels::Result::Success) {
		return -1;
	}
	return static_cast<int>(info.presentationState);
}

auto StashPageUiAction(const char* target, const char* command, const char* text) noexcept -> int {
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

void StashPageWidget(const char* panel, const char* child, char* out, size_t outSize) noexcept {
	if (g_widgets == nullptr) {
		std::snprintf(out, outSize, "no WidgetService");
		return;
	}
	D2RL::Widgets::WidgetHandle p = 0, w = 0;
	const auto rp = g_widgets->findPanel(g_context, panel, &p);
	if (rp != D2RL::Widgets::Result::Success) {
		std::snprintf(out, outSize, "findPanel(%s) = %u", panel, static_cast<unsigned>(rp));
		return;
	}
	w = p;
	if (child != nullptr && child[0] != '\0') {
		const auto rw = g_widgets->findWidget(g_context, p, child, &w);
		if (rw != D2RL::Widgets::Result::Success) {
			std::snprintf(out, outSize, "findPanel(%s) ok, findWidget(%s) = %u", panel, child, static_cast<unsigned>(rw));
			return;
		}
	}
	D2RL::Widgets::Rect r{};
	const auto rr = g_widgets->getWidgetRect(g_context, w, &r);
	std::snprintf(out, outSize, "%s/%s rect result %u x=%d y=%d w=%d h=%d", panel, child != nullptr ? child : "", static_cast<unsigned>(rr), r.x, r.y, r.width, r.height);
}

auto StashPageSetVisible(const char* panel, const char* child, bool visible) noexcept -> int {
	if (g_widgets == nullptr) {
		return -1;
	}
	D2RL::Widgets::WidgetHandle p = 0, w = 0;
	if (g_widgets->findPanel(g_context, panel, &p) != D2RL::Widgets::Result::Success ||
	    g_widgets->findWidget(g_context, p, child, &w) != D2RL::Widgets::Result::Success) {
		return -2;
	}
	return static_cast<int>(g_widgets->setWidgetVisible(g_context, w, visible));
}

void StashPageMessages(char* out, size_t outSize) noexcept {
	size_t used = 0;
	out[0] = '\0';
	const uint32_t total = g_messages.load();
	const uint32_t first = total > UiNames ? total - UiNames : 0;
	for (uint32_t i = first; i < total && used + 1 < outSize; ++i) {
		const int w = std::snprintf(out + used, outSize - used, "%s%s", used > 0 ? " " : "", g_uiNames[i % UiNames]);
		used = w > 0 && used + static_cast<size_t>(w) < outSize ? used + static_cast<size_t>(w) : outSize;
	}
}

auto StashPageJob(const char* op, long long a, long long b, long long c, const char* code, char* out, size_t outSize) noexcept -> int {
	if (!g_ready || g_items == nullptr || g_threads == nullptr || !D2RL::HasItemServiceField(g_items, D2RL::ItemServiceRequiredSize)) {
		std::snprintf(out, outSize, "stash pages or ItemService not ready");
		return 0;
	}
	if (g_job.state.load() == 1) {
		std::snprintf(out, outSize, "a job is still queued");
		return 0;
	}
	std::snprintf(g_job.op, sizeof(g_job.op), "%s", op);
	std::snprintf(g_job.code, sizeof(g_job.code), "%s", code != nullptr ? code : "");
	g_job.a = a;
	g_job.b = b;
	g_job.c = c;
	g_job.state.store(1);
	if (g_threads->runOnGameThread(g_context, &JobOnGameThread, nullptr) != D2RL::Threads::Result::Success) {
		g_job.state.store(0);
		std::snprintf(out, outSize, "runOnGameThread refused");
		return 0;
	}
	std::snprintf(out, outSize, "queued");
	return 1;
}

auto StashPageResult(char* out, size_t outSize) noexcept -> int {
	const int state = g_job.state.load();
	if (state == 0) {
		std::snprintf(out, outSize, "never queued");
		return -2;
	}
	if (state == 1) {
		std::snprintf(out, outSize, "pending");
		return -1;
	}
	std::snprintf(out, outSize, "%s", g_job.text);
	return g_job.ok;
}

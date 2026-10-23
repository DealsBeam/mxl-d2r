// Median's travel conveniences on D2R (rules and evidence in travel_logic.h; docs/unported.md "Rules audit gaps").
//
// 1. Waypoint auto-activation (audit WP2). Median's client takes a waypoint the way it takes gold: after a walk order, when the
//    distance to the object is below 0x24 and the player does not know the waypoint, it sets the bit (D2AutoPickup.cpp 0x4c7e0).
//    D2R does it on the server: TravelGameFrame runs after the server frame (goblins.cpp hooks 0x406550), every fifth frame, for
//    every player in the game. A player within PickupRadius of a waypoint object of his own level (objects.txt OperateFn 23) who
//    lacks that level's waypoint number gets the bit set with 0x3d6890, the engine's own setter. The waypoint panel needs nothing
//    else: the object's operate function 0x58fe00 sends the whole record (packet 0x63) when he clicks the waypoint.
// 2. Hotkeys through D2RLoader's InputService (Median's table, D2Sigma 0x1d9110): the auto pickup toggle (Median's no-pickup flag,
//    here the switch of 1.), the map reveal and the transmute while the Horadric Cube panel is open. Default keys X, L and K (see
//    travel_logic.h: R and C are D2R's run toggle and Character screen). They appear in the Controls menu.
// 3. Map reveal (audit MU2). D2R has the debug menu's "Reveal Automap" (0xd42b0): a breadth-first walk over the rooms joined to the
//    player's room that adds each room's tiles and preset objects to the automap layer (visitor 0xd2240), and then flips the debug
//    flag byte 0x2a2add4. The hotkey calls it on the UI thread and writes the flag back. Median reveals every level of the act
//    (D2Sigma 0x887c0, once per act per game); this reveals the connected rooms of the level the player is in.
// 4. The transmute hotkey calls the Horadric Cube panel's own Transmute function 0x2cd720 (it gathers the items in the cube and
//    sends the transmute command).
//
// No inline hook is installed here; the functions called are verified by their first bytes (entry bytes of the 93847 dump).
// Off words: wpauto, mapreveal, hotkeys.
#include "travel.h"

#include "d2r.h"
#include "travel_logic.h"

#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint64_t WpSetRva = 0x3d6890;        // (record, number)
constexpr uint64_t WpTestRva = 0x3d6930;       // (record, number) -> nonzero when set
constexpr uint64_t PlayerDataRva = 0x34b240;   // (unit) -> player data (+0x58 + difficulty * 8 = waypoint record)
constexpr uint64_t UnitRoomRva = 0x34b440;     // (unit) -> room
constexpr uint64_t RoomLevelRva = 0x2efc10;    // (room) -> level id
constexpr uint64_t ClientUnitRva = 0x484c90;   // (client, 0) -> the client's player unit
constexpr uint64_t RevealRva = 0xd42b0;        // the debug menu's Reveal Automap: no arguments
constexpr uint64_t TransmuteRva = 0x2cd720;    // the cube panel's Transmute: no arguments
constexpr uint64_t LocalIdRva = 0x8b2d0;       // () -> the client's own player id
constexpr uint64_t UnitByIdRva = 0x9a480;      // (id) -> the client unit or null
constexpr uint64_t RevealFlagRva = 0x2a2add4;  // byte flipped by the reveal
constexpr uint64_t LayerListRva = 0x2a2cf60;   // first automap layer; next at +0xa8
constexpr uint64_t CurLayerRva = 0x2a2cf68;    // current layer (first int = layer number)

// first bytes of every function that is called (dump of build 93847)
constexpr uint8_t WpSetEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x0f, 0xb7, 0xda, 0x48, 0x8b, 0xf9};
constexpr uint8_t WpTestEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x0f, 0xb7, 0xda, 0x48, 0x8b, 0xf9};
constexpr uint8_t RevealEntry[] = {0x48, 0x83, 0xec, 0x58, 0x48, 0x8d, 0x4c, 0x24, 0x20, 0x48, 0xc7, 0x44, 0x24, 0x20, 0x01, 0x00};
constexpr uint8_t TransmuteEntry[] = {0x4c, 0x8b, 0xdc, 0x55, 0x53, 0x48, 0x8d, 0x6c, 0x24, 0x88, 0x48, 0x81, 0xec, 0x78, 0x01, 0x00};
constexpr uint8_t UnitByIdEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x83, 0xf9, 0x08};

constexpr int GameClients = 0x138;
constexpr int GameDifficulty = 0x104;
constexpr int ClientNext = 0x628;
constexpr int ClientState = 0x04;
constexpr int HashObjects = 0x546;  // game + (base + (id & 0x7f)) * 8
constexpr int UnitNextInHash = 0x158;
constexpr int UnitClass = 0x04;
constexpr int PlayerDataRecord = 0x58;
constexpr int MaxPlayers = 8;

using WpFn = int32_t (*)(void* record, uint16_t number);
using PlayerDataFn = uint8_t* (*)(Unit*);
using UnitRoomFn = void* (*)(Unit*);
using RoomLevelFn = int32_t (*)(void*);
using ClientUnitFn = Unit* (*)(void* client, int32_t);
using VoidFn = void (*)();
using LocalIdFn = uint32_t (*)();
using UnitByIdFn = Unit* (*)(uint32_t);

const D2RL::PluginContext* g_context = nullptr;
uint64_t g_base = 0;
WpFn g_wpSet = nullptr;
WpFn g_wpTest = nullptr;
PlayerDataFn g_playerData = nullptr;
UnitRoomFn g_unitRoom = nullptr;
RoomLevelFn g_roomLevel = nullptr;
ClientUnitFn g_clientUnit = nullptr;
VoidFn g_reveal = nullptr;
VoidFn g_transmute = nullptr;
LocalIdFn g_localId = nullptr;
UnitByIdFn g_unitById = nullptr;

bool g_wpautoOn = false;
bool g_mapRevealOn = false;
bool g_hotkeysOn = false;
std::atomic<bool> g_pickupOn{true};  // the auto pickup hotkey
std::atomic<uint32_t> g_activated{0};
std::atomic<uint32_t> g_scans{0};
std::atomic<int> g_lastLevel{-1};
std::atomic<int> g_lastNumber{-1};
std::atomic<uint32_t> g_pickupPresses{0}, g_revealPresses{0}, g_transmutePresses{0};
std::atomic<uint32_t> g_reveals{0}, g_revealCells{0}, g_transmutes{0}, g_transmuteBlocked{0};
std::atomic<bool> g_cubeOpen{false};
std::atomic<uint32_t> g_cubeMessages{0};
constexpr uint32_t UiNames = 96, UiNameSize = 96;
char g_uiNames[UiNames][UiNameSize] = {};
std::atomic<uint32_t> g_uiCount{0};
char g_cubeLast[96] = "";

const D2RL::ThreadService* g_threads = nullptr;
const D2RL::InputService* g_input = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ItemService* g_items = nullptr;
bool g_bound[travel::HotkeyCount] = {};
char g_bindingNote[travel::HotkeyCount][48] = {};

template <typename T>
auto Fn(uint64_t rva) noexcept -> T {
	return reinterpret_cast<T>(g_base + rva);
}

auto EntryMatches(uint64_t rva, const uint8_t* bytes, size_t n) noexcept -> bool {
	return std::memcmp(reinterpret_cast<const void*>(g_base + rva), bytes, n) == 0;
}

auto UnitType(const Unit* u) noexcept -> int32_t {
	return d2r::At<int32_t>(u, d2r::UnitType);
}

// ---------------------------------------------------------------------------------------------------------------------------
// 1. waypoint auto-activation

auto WaypointRecordOf(Game* game, Unit* player) noexcept -> uint8_t* {
	uint8_t* data = g_playerData(player);
	if (data == nullptr) {
		return nullptr;
	}
	return d2r::At<uint8_t*>(data, PlayerDataRecord + static_cast<int>(d2r::At<uint8_t>(game, GameDifficulty)) * 8);
}

// A waypoint object of the player's own level within PickupRadius, or nullptr.
auto NearWaypointObject(Game* game, Unit* player, int32_t level) noexcept -> Unit* {
	int32_t px = 0, py = 0;
	if (!d2r::UnitXY(player, &px, &py)) {
		return nullptr;
	}
	int guard = 0;
	for (int b = 0; b < 128; ++b) {
		for (auto* u = d2r::At<Unit*>(game, (HashObjects + b) * 8); u != nullptr && guard < 20000; u = d2r::At<Unit*>(u, UnitNextInHash), ++guard) {
			if (UnitType(u) != 2 || !travel::IsWaypointObject(d2r::At<int32_t>(u, UnitClass))) {
				continue;
			}
			int32_t ox = 0, oy = 0;
			if (!d2r::UnitXY(u, &ox, &oy) || !travel::InPickupRange(px, py, ox, oy)) {
				continue;
			}
			void* room = g_unitRoom(u);
			if (room != nullptr && g_roomLevel(room) == level) {
				return u;
			}
		}
	}
	return nullptr;
}

void ActivateFor(Game* game, Unit* player) noexcept {
	void* room = g_unitRoom(player);
	if (room == nullptr) {
		return;
	}
	const int32_t level = g_roomLevel(room);
	const int number = travel::WaypointNumberOfLevel(level);
	if (number < 0) {
		return;
	}
	uint8_t* record = WaypointRecordOf(game, player);
	if (record == nullptr) {
		return;
	}
	const bool known = g_wpTest(record, static_cast<uint16_t>(number)) != 0;
	if (known || !g_pickupOn.load(std::memory_order_relaxed)) {
		return;
	}
	const bool inRange = NearWaypointObject(game, player, level) != nullptr;
	if (travel::Decide(true, inRange, number, known) != travel::Decision::Activate) {
		return;
	}
	g_wpSet(record, static_cast<uint16_t>(number));
	g_lastLevel.store(level);
	g_lastNumber.store(number);
	const uint32_t n = g_activated.fetch_add(1) + 1;
	if (n <= 20 && g_context != nullptr) {
		char line[128];
		std::snprintf(line, sizeof(line), "mxl-core: waypoint %d of level %d activated by walking up to it", number, level);
		g_context->LogInfo(line);
	}
}

void ScanGame(Game* game) noexcept {
	Unit* players[MaxPlayers];
	int count = 0;
	int guard = 0;
	for (auto* client = d2r::At<uint8_t*>(game, GameClients); client != nullptr && count < MaxPlayers && guard < 64;
	     client = d2r::At<uint8_t*>(client, ClientNext), ++guard) {
		if (d2r::At<int32_t>(client, ClientState) != 4) {
			continue;
		}
		Unit* unit = g_clientUnit(client, 0);
		if (unit != nullptr && UnitType(unit) == 0) {
			players[count++] = unit;
		}
	}
	for (int i = 0; i < count; ++i) {
		ActivateFor(game, players[i]);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------
// 3. map reveal, 4. transmute (both on the UI thread)

auto LocalPlayer() noexcept -> Unit* {
	return g_unitById(g_localId());
}

auto LayerCells(const uint8_t* layer) noexcept -> uint64_t {
	uint64_t counts[4];
	for (int i = 0; i < 4; ++i) {
		counts[i] = d2r::At<uint64_t>(layer, travel::LayerCountOffsets[i]);
	}
	return travel::LayerCells(counts);
}

auto AllCells(int* layers) noexcept -> uint64_t {
	uint64_t total = 0;
	int n = 0;
	for (auto* layer = *reinterpret_cast<uint8_t* const*>(g_base + LayerListRva); layer != nullptr && n < 64;
	     layer = d2r::At<uint8_t*>(layer, 0xa8), ++n) {
		total += LayerCells(layer);
	}
	if (layers != nullptr) {
		*layers = n;
	}
	return total;
}

void RevealOnce() noexcept {
	auto* flag = reinterpret_cast<volatile uint8_t*>(g_base + RevealFlagRva);
	const uint8_t before = *flag;
	g_reveal();
	*flag = before;  // the debug menu's checkbox state is not ours to change
}

auto DoReveal() noexcept -> int {
	if (!g_mapRevealOn || LocalPlayer() == nullptr) {
		return -1;
	}
	__try {
		const uint64_t before = AllCells(nullptr);
		RevealOnce();
		const uint64_t after = AllCells(nullptr);
		g_reveals.fetch_add(1);
		g_revealCells.store(static_cast<uint32_t>(after > before ? after - before : 0));
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		if (g_context != nullptr) {
			g_context->LogWarn("mxl-core: map reveal crashed in the game call; switched off");
		}
		g_mapRevealOn = false;
		return -1;
	}
	return 0;
}

void __cdecl RevealOnUi(const D2RL::PluginContext*, void*) noexcept {
	DoReveal();
}

void __cdecl TransmuteOnUi(const D2RL::PluginContext*, void*) noexcept {
	if (LocalPlayer() == nullptr) {
		return;
	}
	if (!g_cubeOpen.load()) {
		g_transmuteBlocked.fetch_add(1);
		return;
	}
	__try {
		g_transmute();
		g_transmutes.fetch_add(1);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		if (g_context != nullptr) {
			g_context->LogWarn("mxl-core: transmute hotkey crashed in the game call");
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------
// 2. hotkeys

// One press of action `which` (0 pickup toggle, 1 map reveal, 2 transmute). The InputService callback and the harness verb `hotkey` both
// come here, so a test drives exactly what a key press runs.
auto PressAction(const D2RL::PluginContext* context, int which) noexcept -> bool {
	switch (which) {
	case 0:  // X
		g_pickupPresses.fetch_add(1);
		g_pickupOn.store(!g_pickupOn.load());
		context->LogInfo(g_pickupOn.load() ? "mxl-core: auto pickup on (hotkey)" : "mxl-core: auto pickup off (hotkey)");
		return true;
	case 1:  // L
		g_revealPresses.fetch_add(1);
		if (g_mapRevealOn && g_threads != nullptr) {
			g_threads->runOnUiThread(context, RevealOnUi, nullptr);
		}
		return true;
	case 2:  // K
		g_transmutePresses.fetch_add(1);
		if (g_threads != nullptr) {
			g_threads->runOnUiThread(context, TransmuteOnUi, nullptr);
		}
		return true;
	default:
		return false;
	}
}

auto __cdecl OnAction(const D2RL::PluginContext* context, const D2RL::Input::ActionEvent* event, void* userData) noexcept -> D2RL::Input::ActionResult {
	if (context == nullptr || !D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) ||
	    event->kind != D2RL::Input::ActionEventKind::Pressed) {
		return D2RL::Input::ActionResult::Ignored;
	}
	return PressAction(context, static_cast<int>(reinterpret_cast<intptr_t>(userData))) ? D2RL::Input::ActionResult::Handled
	                                                                                       : D2RL::Input::ActionResult::Ignored;
}

auto RegisterHotkeys(const D2RL::PluginContext* context) noexcept -> int {
	int registered = 0;
	for (int i = 0; i < travel::HotkeyCount; ++i) {
		const travel::Hotkey& h = travel::Hotkeys[i];
		D2RL::Input::ActionRegistration reg{};
		reg.structSize = D2RL::Input::ActionRegistrationSize;
		reg.logicalId = h.id;
		reg.displayName = h.name;
		reg.category = "Median XL";
		reg.defaultPrimary = {static_cast<D2RL::Input::Key>(h.key), D2RL::Input::Modifier::None};
		reg.defaultSecondary = {D2RL::Input::Key::None, D2RL::Input::Modifier::None};
		reg.callback = &OnAction;
		reg.userData = reinterpret_cast<void*>(static_cast<intptr_t>(i));
		D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
		const D2RL::Input::Result result = g_input->registerAction(context, &reg, &handle);
		g_bound[i] = result == D2RL::Input::Result::Success && handle != D2RL::Input::InvalidHandle;
		if (!g_bound[i]) {
			std::snprintf(g_bindingNote[i], sizeof(g_bindingNote[i]), "registration failed (%u)", static_cast<unsigned>(result));
			continue;
		}
		++registered;
		D2RL::Input::Binding primary{};
		if (g_input->getBinding(context, handle, D2RL::Input::BindingSlot::Primary, &primary) == D2RL::Input::Result::Success) {
			std::snprintf(g_bindingNote[i], sizeof(g_bindingNote[i]), "key 0x%02x modifier %u", static_cast<unsigned>(primary.key),
			              static_cast<unsigned>(primary.modifier));
		} else {
			std::snprintf(g_bindingNote[i], sizeof(g_bindingNote[i]), "binding unknown");
		}
	}
	return registered;
}

// The Horadric Cube panel's own messages tell whether it is open (the C hotkey must not transmute while it is closed).
auto __cdecl OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept -> D2RL::SharedEvents::UiMessageAction {
	if (event == nullptr || event->target == nullptr || event->command == nullptr) {
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}
	const int state = travel::CubePanelState(event->target, event->command, event->text);
	if (state != 0) {
		g_cubeOpen.store(state > 0);
	}
	{  // the distinct messages seen, for finding the cube panel's names (harness `travel` prints them)
		char key[UiNameSize];
		std::snprintf(key, sizeof(key), "%s:%s(%s)", event->target, event->command, event->text != nullptr ? event->text : "");
		const uint32_t have = g_uiCount.load();
		bool known = false;
		for (uint32_t i = 0; i < have && i < UiNames && !known; ++i) {
			known = std::strcmp(g_uiNames[i], key) == 0;
		}
		if (!known && have < UiNames) {
			std::memcpy(g_uiNames[have], key, sizeof(key));
			g_uiCount.store(have + 1);
		}
	}
	if (travel::ContainsNoCase(event->target, "cube") || travel::ContainsNoCase(event->command, "cube") || travel::ContainsNoCase(event->text, "cube")) {
		const uint32_t n = g_cubeMessages.fetch_add(1);
		if (n < 40) {  // the message names are not documented: the first ones go to the log (and `mxl`)
			std::snprintf(g_cubeLast, sizeof(g_cubeLast), "%s:%s", event->target, event->command);
			char line[160];
			std::snprintf(line, sizeof(line), "mxl-core: cube ui message %s:%s -> %d", event->target, event->command, state);
			if (g_context != nullptr) {
				g_context->LogInfo(line);
			}
		}
	}
	return D2RL::SharedEvents::UiMessageAction::Continue;
}

// ---------------------------------------------------------------------------------------------------------------------------
// stash probe (the stashpages check): create one item in a shared stash tab on the game thread, report, destroy it

struct StashJob {
	std::atomic<int> state{0};  // 0 never queued, 1 queued, 2 finished
	int page = 0;
	int createResult = -1;
	int destroyResult = -1;
	char note[96] = "";
};
StashJob g_stash;

void __cdecl StashOnGameThread(const D2RL::PluginContext* context, void*) noexcept {
	StashJob& job = g_stash;
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g_inventory->getLocalPlayer(context, &player) != D2RL::Inventory::Result::Success) {
		std::snprintf(job.note, sizeof(job.note), "no local player");
		job.state.store(2);
		return;
	}
	D2RL::Items::ItemCreateSpec spec{};
	spec.structSize = D2RL::Items::ItemCreateSpecSize;
	spec.code = D2RL::Items::MakeItemCode("r01");
	spec.quality = D2RL::Items::Quality::Normal;
	spec.qualityRecordId = D2RL::Items::RandomQualityRecord;
	spec.itemLevel = 1;
	spec.seedMode = D2RL::Items::SeedMode::Random;
	spec.quantity = D2RL::Items::DefaultValue;
	spec.durability = D2RL::Items::DefaultValue;
	spec.destination.structSize = D2RL::Items::ItemDestinationSize;
	spec.destination.container = D2RL::Items::ItemContainer::SharedStash;
	spec.destination.placement = D2RL::Items::Placement::Automatic;
	spec.destination.sharedStashPage = static_cast<uint32_t>(job.page);
	D2RL::ItemHandle item = D2RL::InvalidItemHandle;
	job.createResult = static_cast<int>(g_items->createItem(context, player, &spec, &item));
	if (job.createResult == static_cast<int>(D2RL::Items::Result::Success)) {
		D2RL::Items::ItemInfo info{};
		info.structSize = D2RL::Items::ItemInfoSize;
		if (g_items->getItemInfo(context, item, &info) == D2RL::Items::Result::Success) {
			std::snprintf(job.note, sizeof(job.note), "item in container %u, shared tab %u", static_cast<unsigned>(info.container),
			              info.sharedStashPage);
		}
		job.destroyResult = static_cast<int>(g_items->destroyItem(context, player, item, D2RL::Items::SocketedItemPolicy::DestroyContents));
	}
	job.state.store(2);
}

}  // namespace

auto InstallTravel(const D2RL::PluginContext* context, bool wpauto, bool mapreveal, bool hotkeys, char* report, size_t reportSize) noexcept -> bool {
	g_context = context;
	g_base = context->exeBase;
	char notes[256] = "";
	size_t used = 0;
	auto note = [&](const char* fmt, auto... args) {
		const int w = std::snprintf(notes + used, sizeof(notes) - used, fmt, args...);
		used = w > 0 && used + static_cast<size_t>(w) < sizeof(notes) ? used + static_cast<size_t>(w) : used;
	};
	if (!wpauto && !mapreveal && !hotkeys) {
		std::snprintf(report, reportSize, "off: left out by mxl-core-off.txt");
		return false;
	}
	g_wpSet = Fn<WpFn>(WpSetRva);
	g_wpTest = Fn<WpFn>(WpTestRva);
	g_playerData = Fn<PlayerDataFn>(PlayerDataRva);
	g_unitRoom = Fn<UnitRoomFn>(UnitRoomRva);
	g_roomLevel = Fn<RoomLevelFn>(RoomLevelRva);
	g_clientUnit = Fn<ClientUnitFn>(ClientUnitRva);
	g_reveal = Fn<VoidFn>(RevealRva);
	g_transmute = Fn<VoidFn>(TransmuteRva);
	g_localId = Fn<LocalIdFn>(LocalIdRva);
	g_unitById = Fn<UnitByIdFn>(UnitByIdRva);

	// waypoint auto-activation
	if (!wpauto) {
		note("waypoint auto-activation off (word wpauto)");
	} else if (!EntryMatches(WpSetRva, WpSetEntry, sizeof(WpSetEntry)) || !EntryMatches(WpTestRva, WpTestEntry, sizeof(WpTestEntry))) {
		note("waypoint auto-activation off: entry bytes of 0x3d6890 / 0x3d6930 differ");
	} else {
		g_wpautoOn = true;
		note("waypoint auto-activation on (%d objects, radius %d, needs the goblins frame hook)", static_cast<int>(sizeof(travel::WaypointObjects) / sizeof(travel::WaypointObjects[0])), travel::PickupRadius);
	}

	// map reveal
	const bool uiBytes = EntryMatches(UnitByIdRva, UnitByIdEntry, sizeof(UnitByIdEntry));
	if (!mapreveal) {
		note("; map reveal off (word mapreveal)");
	} else if (!uiBytes || !EntryMatches(RevealRva, RevealEntry, sizeof(RevealEntry))) {
		note("; map reveal off: entry bytes of 0xd42b0 / 0x9a480 differ");
	} else {
		g_mapRevealOn = true;
		note("; map reveal on");
	}

	// hotkeys
	if (!hotkeys) {
		note("; hotkeys off (word hotkeys)");
	} else if (!uiBytes || !EntryMatches(TransmuteRva, TransmuteEntry, sizeof(TransmuteEntry))) {
		note("; hotkeys off: entry bytes of 0x2cd720 / 0x9a480 differ");
	} else if (context->QueryService(&g_input) != D2RL::ServiceQueryResult::Success || !D2RL::HasInputServiceField(g_input, D2RL::InputServiceRequiredSize) ||
	           context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
	           !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize)) {
		note("; hotkeys off: InputService / ThreadService missing");
	} else {
		const int n = RegisterHotkeys(context);
		g_hotkeysOn = n > 0;
		const D2RL::SharedEventService* events = nullptr;
		bool listening = false;
		if (context->QueryService(&events) == D2RL::ServiceQueryResult::Success &&
		    D2RL::HasSharedEventServiceField(events, D2RL::SharedEventServiceRequiredSize)) {
			D2RL::SharedEvents::UiMessageListener listener{};
			listener.structSize = D2RL::SharedEvents::UiMessageListenerSize;
			listener.callback = &OnUiMessage;
			D2RL::SharedEvents::ListenerHandle handle = 0;
			listening = events->registerUiMessageListener(context, &listener, &handle) == D2RL::SharedEvents::Result::Success;
		}
		note("; hotkeys %d of %d registered%s", n, travel::HotkeyCount, listening ? ", cube panel watched" : ", cube panel NOT watched (C does nothing)");
	}
	// the stash probe uses the item and inventory services; it is only a test aid
	(void)context->QueryService(&g_inventory);
	(void)context->QueryService(&g_items);
	if (g_threads == nullptr) {
		(void)context->QueryService(&g_threads);
	}
	std::snprintf(report, reportSize, "%s", notes);
	return g_wpautoOn || g_mapRevealOn || g_hotkeysOn;
}

void TravelGameFrame(void* gamePtr, uint32_t frame) noexcept {
	if (!g_wpautoOn || gamePtr == nullptr || !travel::IsScanFrame(frame)) {
		return;
	}
	g_scans.fetch_add(1);
	__try {
		ScanGame(static_cast<Game*>(gamePtr));
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_wpautoOn = false;
		if (g_context != nullptr) {
			g_context->LogWarn("mxl-core: waypoint auto-activation crashed in the game call; switched off");
		}
	}
}

auto TravelAutoPickupOn() noexcept -> bool {
	return g_pickupOn.load(std::memory_order_relaxed);
}

auto TravelRevealNow() noexcept -> int {
	return DoReveal();
}

// Test harness: the press of a hotkey action without the key (0 pickup, 1 reveal, 2 transmute); false = no such action or no context.
auto TravelPressAction(int which) noexcept -> bool {
	return g_context != nullptr && PressAction(g_context, which);
}

void TravelStatus(char* out, size_t outSize) noexcept {
	std::snprintf(out, outSize, "%u waypoints activated (last: number %d, level %d, %u scans, pickup %s) | reveals %u (+%u cells) | keys pickup %u reveal %u transmute %u | transmutes %u (blocked %u, cube %s)",
	              g_activated.load(), g_lastNumber.load(), g_lastLevel.load(), g_scans.load(), g_pickupOn.load() ? "on" : "off", g_reveals.load(),
	              g_revealCells.load(), g_pickupPresses.load(), g_revealPresses.load(), g_transmutePresses.load(), g_transmutes.load(), g_transmuteBlocked.load(),
	              g_cubeOpen.load() ? "open" : "closed");
}

auto TravelAutomap(uint64_t* cells, int* layer, int* revealFlag, int* layers, uint64_t* currentCells) noexcept -> bool {
	if (g_base == 0) {
		return false;
	}
	__try {
		*cells = AllCells(layers);
		const auto* cur = *reinterpret_cast<uint8_t* const*>(g_base + CurLayerRva);
		*layer = cur != nullptr ? d2r::At<int32_t>(cur, 0) : -1;
		if (currentCells != nullptr) {
			*currentCells = cur != nullptr ? LayerCells(cur) : 0;
		}
		*revealFlag = *reinterpret_cast<const uint8_t*>(g_base + RevealFlagRva);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void TravelHotkeyReport(char* out, size_t outSize) noexcept {
	size_t used = 0;
	for (int i = 0; i < travel::HotkeyCount && used < outSize; ++i) {
		const int w = std::snprintf(out + used, outSize - used, "%s%s: %s", i > 0 ? " | " : "", travel::Hotkeys[i].id,
		                            g_bound[i] ? g_bindingNote[i] : (g_bindingNote[i][0] != '\0' ? g_bindingNote[i] : "not registered"));
		used = w > 0 && used + static_cast<size_t>(w) < outSize ? used + static_cast<size_t>(w) : outSize;
	}
	char status[320];
	TravelStatus(status, sizeof(status));
	if (used < outSize) {
		std::snprintf(out + used, outSize - used, " || %s", status);
	}
}

void TravelUiMessages(const char* contains, char* out, size_t outSize) noexcept {
	size_t used = 0;
	out[0] = '\0';
	const uint32_t n = g_uiCount.load();
	for (uint32_t i = 0; i < n && i < UiNames && used + 1 < outSize; ++i) {
		if (contains != nullptr && !travel::ContainsNoCase(g_uiNames[i], contains)) {
			continue;
		}
		const int w = std::snprintf(out + used, outSize - used, "%s%s", used > 0 ? " " : "", g_uiNames[i]);
		used = w > 0 && used + static_cast<size_t>(w) < outSize ? used + static_cast<size_t>(w) : outSize;
	}
}

auto TravelStashProbe(int page, char* out, size_t outSize) noexcept -> int {
	if (g_inventory == nullptr || g_items == nullptr || g_threads == nullptr || g_context == nullptr ||
	    !D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceRequiredSize) ||
	    !D2RL::HasItemServiceField(g_items, D2RL::ItemServiceRequiredSize)) {
		std::snprintf(out, outSize, "services missing");
		return 0;
	}
	if (!D2RL::HasItemServiceCapability(g_items, D2RL::Items::ItemServiceCapability::SharedStashWrite)) {
		std::snprintf(out, outSize, "ItemService has no SharedStashWrite");
		return 0;
	}
	if (g_stash.state.load() == 1) {
		std::snprintf(out, outSize, "a probe is still queued");
		return 0;
	}
	g_stash.page = page;
	g_stash.createResult = -1;
	g_stash.destroyResult = -1;
	g_stash.note[0] = '\0';
	g_stash.state.store(1);
	if (g_threads->runOnGameThread(g_context, StashOnGameThread, nullptr) != D2RL::Threads::Result::Success) {
		g_stash.state.store(0);
		std::snprintf(out, outSize, "runOnGameThread refused");
		return 0;
	}
	std::snprintf(out, outSize, "queued");
	return 1;
}

auto TravelStashResult(char* out, size_t outSize) noexcept -> int {
	const int state = g_stash.state.load();
	if (state == 0) {
		std::snprintf(out, outSize, "never queued");
		return -2;
	}
	if (state == 1) {
		std::snprintf(out, outSize, "pending");
		return -1;
	}
	std::snprintf(out, outSize, "page=%d create=%d destroy=%d %s", g_stash.page, g_stash.createResult, g_stash.destroyResult, g_stash.note);
	return g_stash.createResult == static_cast<int>(D2RL::Items::Result::Success) ? 1 : 0;
}

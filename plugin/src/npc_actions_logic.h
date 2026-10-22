// Pure rules of Median's NPC menu actions (npc_actions.cpp; docs/subsystems/npc-hirelings.md section 1.5).
// No engine access, so mxl_tests can run them: the tables are mxl_npc_actions.h (generated) and the requirement
// evaluator is requirements.cpp; every game effect goes through the Host callbacks.
#pragma once

#include "mxl_npc_actions.h"
#include "requirements.h"

#include <cstddef>
#include <cstdint>

namespace npc {

// Packet 0x38 is (kind u32, NPC unit id u32); the game uses kinds 1 trade, 2 gamble, 3 hire. Median's slots start here.
inline constexpr uint32_t KindBase = 0x10;
inline constexpr int SlotsPerNpc = 16;
inline constexpr int MaxActionDepth = 4;

constexpr auto KindOf(int npcRow, int slot) noexcept -> uint32_t {
	return KindBase + static_cast<uint32_t>(npcRow * SlotsPerNpc + slot);
}

// npcactions types (docs/subsystems/npc-hirelings.md section 1.2)
enum Type : int32_t {
	Compound = 1, Page = 3, Travel = 8, Imbue = 11, Personalize = 12, Sockets = 13, Respec = 14, Waypoint = 15,
	Continue = 16, CreateItem = 17, Confirm = 18, Message = 20,
};

// Run: the server does it. Native: D2R already does it under the same condition (the imbue / sockets / personalize lines
// and NPC travel with its waypoint grant; npc_services_logic.h checks that against the game's tables), so no line is
// drawn and nothing is run.
enum class Support : uint8_t { None, Run, Native };
enum class Result : uint8_t { NotFound, Denied, Done, Nothing, Native, Page };

constexpr auto Merge(Support a, Support b) noexcept -> Support {
	return a == Support::Run || b == Support::Run ? Support::Run : (a == Support::Native || b == Support::Native ? Support::Native : Support::None);
}

constexpr auto SupportOf(int row, int depth = 0) noexcept -> Support {
	if (row <= 0 || row >= mxl_npc::ActionCount || depth > MaxActionDepth) {
		return Support::None;
	}
	const mxl_npc::Action& a = mxl_npc::Actions[row];
	switch (a.type) {
	case CreateItem:
	case Respec:
		return Support::Run;
	case Travel:
	case Waypoint:
	case Imbue:
	case Sockets:
	case Personalize:
		return Support::Native;
	case Compound: {
		Support s = Support::None;
		for (const int32_t v : a.value) {
			if (v > 0) {
				s = Merge(s, SupportOf(v, depth + 1));
			}
		}
		return s;
	}
	default:  // Confirm, Message, Page, Continue: client-side steps, nothing to do on the server
		return Support::None;
	}
}

constexpr auto FindSlot(uint32_t kind, int npcClass) noexcept -> int {
	for (int i = 0; i < mxl_npc::SlotCount; ++i) {
		const mxl_npc::Slot& s = mxl_npc::Slots[i];
		if (s.npcClass == npcClass && KindOf(s.npcRow, s.slot) == kind) {
			return i;
		}
	}
	return -1;
}

constexpr auto IsPage(int slotIndex) noexcept -> bool {
	const mxl_npc::Slot& s = mxl_npc::Slots[slotIndex];
	return s.action > 0 && s.action < mxl_npc::ActionCount && mxl_npc::Actions[s.action].type == Page;
}

// The sub-entries of the text-page slot `pageIndex`: the slots right behind it that belong to its submenu (sub != 0).
// Writes their slot indices to `out` (at most `cap`) and returns how many there are.
constexpr auto SubSlots(int pageIndex, int* out, int cap) noexcept -> int {
	int n = 0;
	const int npcRow = mxl_npc::Slots[pageIndex].npcRow;
	for (int j = pageIndex + 1; j < mxl_npc::SlotCount && mxl_npc::Slots[j].npcRow == npcRow && mxl_npc::Slots[j].sub != 0; ++j) {
		if (n < cap) {
			out[n] = j;
		}
		++n;
	}
	return n;
}

// What a slot does: a text-page slot (Cain's "crafting materials") stands for the sub-entries that follow it.
constexpr auto SlotSupport(int i) noexcept -> Support {
	const mxl_npc::Slot& s = mxl_npc::Slots[i];
	if (s.action > 0 && s.action < mxl_npc::ActionCount && mxl_npc::Actions[s.action].type == Page) {
		Support sup = Support::None;
		for (int j = i + 1; j < mxl_npc::SlotCount && mxl_npc::Slots[j].npcRow == s.npcRow && mxl_npc::Slots[j].sub != 0; ++j) {
			sup = Merge(sup, SlotSupport(j));
		}
		return sup;
	}
	return SupportOf(s.action);
}

// Quality 1 in npcactions rows is D2Sigma's "default": the cube and the catalysts are not low-quality items, so
// 0 and 1 mean normal (2); 2..9 pass through.
constexpr auto ItemQuality(int32_t value) noexcept -> int {
	return value <= 1 ? 2 : (value > 9 ? 2 : value);
}

constexpr auto ItemCode(int32_t value) noexcept -> uint32_t { return static_cast<uint32_t>(value); }

struct Host {
	void* user = nullptr;
	mxl_requirements::Queries queries;
	bool (*createItem)(void* user, uint32_t code, int itemLevel, int quality) = nullptr;
	bool (*respec)(void* user) = nullptr;
	// A text-page slot was clicked: `mask` has bit j set for the j-th sub-entry whose requirement holds. The client opens
	// the submenu with those entries (Median's page shows the entries whose requirement holds).
	void (*page)(void* user, int npcClass, uint32_t pageKind, uint32_t mask) = nullptr;
	void (*note)(void* user, const char* what, int a, int b) = nullptr;  // refusals and stubs, for the log
};

inline void Note(const Host& h, const char* what, int a, int b) noexcept {
	if (h.note != nullptr) {
		h.note(h.user, what, a, b);
	}
}

constexpr auto Join(Result a, Result b) noexcept -> Result {
	if (a == Result::Done || b == Result::Done) return Result::Done;
	if (a == Result::Native || b == Result::Native) return Result::Native;
	if (a == Result::Denied || b == Result::Denied) return Result::Denied;
	return Result::Nothing;
}

inline auto RunAction(int row, const Host& h, int depth = 0) noexcept -> Result {
	if (row <= 0 || row >= mxl_npc::ActionCount || depth > MaxActionDepth) {
		return Result::Nothing;
	}
	const mxl_npc::Action& a = mxl_npc::Actions[row];
	if (!mxl_requirements::Evaluate(a.requirement, h.queries)) {
		return Result::Denied;
	}
	switch (a.type) {
	case Compound: {
		Result r = Result::Nothing;
		for (const int32_t v : a.value) {
			if (v > 0) {
				r = Join(r, RunAction(v, h, depth + 1));
			}
		}
		return r;
	}
	case CreateItem:
		if (h.createItem == nullptr) {
			return Result::Nothing;
		}
		return h.createItem(h.user, ItemCode(a.value[0]), a.value[2] < 0 ? 0 : a.value[2], ItemQuality(a.value[1])) ? Result::Done
		                                                                                                          : Result::Nothing;
	case Respec:
		return h.respec != nullptr && h.respec(h.user) ? Result::Done : Result::Nothing;
	case Travel:
	case Waypoint:
	case Imbue:
	case Sockets:
	case Personalize:
		Note(h, "D2R's own service, nothing to run (type, row)", a.type, row);
		return Result::Native;
	default:
		return Result::Nothing;
	}
}

// Bit j: the j-th sub-entry of the page slot `pageIndex` is open (its requirement holds).
inline auto PageMask(int pageIndex, const Host& h) noexcept -> uint32_t {
	int subs[32];
	const int n = SubSlots(pageIndex, subs, 32);
	uint32_t mask = 0;
	for (int j = 0; j < n && j < 32; ++j) {
		if (mxl_requirements::Evaluate(mxl_npc::Slots[subs[j]].requirement, h.queries)) {
			mask |= 1u << j;
		}
	}
	return mask;
}

// A click on slot `kind` of an NPC of class `npcClass`: the slot's requirement, then its action. A text-page slot only
// reports which of its sub-entries are open (the client then shows them); each entry is clicked on its own and runs its
// action under its own requirement.
inline auto RunSlot(uint32_t kind, int npcClass, const Host& h) noexcept -> Result {
	const int i = FindSlot(kind, npcClass);
	if (i < 0) {
		return Result::NotFound;
	}
	const mxl_npc::Slot& s = mxl_npc::Slots[i];
	if (!mxl_requirements::Evaluate(s.requirement, h.queries)) {
		return Result::Denied;
	}
	if (IsPage(i)) {
		if (h.page != nullptr) {
			h.page(h.user, npcClass, kind, PageMask(i, h));
		}
		return Result::Page;
	}
	return RunAction(s.action, h);
}

// --- Confirmation (npcext PromptConfirm / PromptLabel) ---------------------------------------------------------------
// Akara's "Reset Stat/Skill Points" asks first: the client shows a list with the prompt text as its header and the
// stock Yes / No lines (D2R's own confirmation, the one its Repair All uses), and sends the kind only after Yes.
inline constexpr uint16_t StringYes = 0xd10;  // "Yes" (D2R string 3344)
inline constexpr uint16_t StringNo = 0xd11;   // "No" (D2R string 3345)

constexpr auto NeedsConfirm(int slotIndex) noexcept -> bool {
	return slotIndex >= 0 && slotIndex < mxl_npc::SlotCount && mxl_npc::Slots[slotIndex].confirm;
}

// The header text of the prompt: the prompt label, else the line's own label.
constexpr auto ConfirmText(int slotIndex) noexcept -> uint16_t {
	const mxl_npc::Slot& s = mxl_npc::Slots[slotIndex];
	return s.prompt != 0 ? s.prompt : s.label;
}

// The pending confirmation of the client: one at a time, answered once (the list's Yes, No and Escape all end it).
struct Confirmation {
	bool open = false;
	uint32_t kind = 0;
	uint16_t text = 0;

	void Open(uint32_t k, uint16_t t) noexcept {
		open = true;
		kind = k;
		text = t;
	}
	// The kind to send for this answer, or 0 (a No, or an answer with nothing pending).
	auto Answer(bool yes) noexcept -> uint32_t {
		const uint32_t send = open && yes ? kind : 0;
		open = false;
		kind = 0;
		return send;
	}
};

// --- Page answer: the 'N' plugin message (client_messages_logic.h) -----------------------------------------------
// 8 bytes: u32 page kind, u16 NPC class, u16 mask of the open sub-entries.
inline constexpr size_t PagePayloadSize = 8;
inline void BuildPagePayload(uint8_t* p, uint32_t pageKind, uint16_t npcClass, uint16_t mask) noexcept {
	for (int i = 0; i < 4; ++i) {
		p[i] = static_cast<uint8_t>(pageKind >> (8 * i));
	}
	p[4] = static_cast<uint8_t>(npcClass);
	p[5] = static_cast<uint8_t>(npcClass >> 8);
	p[6] = static_cast<uint8_t>(mask);
	p[7] = static_cast<uint8_t>(mask >> 8);
}
inline auto ParsePagePayload(const uint8_t* p, size_t n, uint32_t* pageKind, uint16_t* npcClass, uint16_t* mask) noexcept -> bool {
	if (n != PagePayloadSize) {
		return false;
	}
	*pageKind = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
	            (static_cast<uint32_t>(p[3]) << 24);
	*npcClass = static_cast<uint16_t>(p[4] | (p[5] << 8));
	*mask = static_cast<uint16_t>(p[6] | (p[7] << 8));
	return true;
}

// The submenu the client shows for a page answer: the sub-entries whose mask bit is set, in order. Returns the count.
struct PageEntry {
	uint16_t label;  // D2R string id
	uint32_t kind;   // packet 0x38 kind of the entry's own slot
};
inline auto PageEntries(int npcClass, uint32_t pageKind, uint32_t mask, PageEntry* out, int cap) noexcept -> int {
	const int page = FindSlot(pageKind, npcClass);
	if (page < 0 || !IsPage(page)) {
		return 0;
	}
	int subs[32];
	const int n = SubSlots(page, subs, 32);
	int count = 0;
	for (int j = 0; j < n && j < 32 && count < cap; ++j) {
		if ((mask >> j) & 1u) {
			const mxl_npc::Slot& s = mxl_npc::Slots[subs[j]];
			out[count++] = {s.label, KindOf(s.npcRow, s.slot)};
		}
	}
	return count;
}

// --- Client menu table (48 records of 0x48 bytes; docs/d2r-addresses.md section 9) ------------------------------
inline constexpr int RecordSize = 0x48;
inline constexpr int RecCountOffset = 0x04;    // u32: lines including the final Cancel
inline constexpr int RecIdsOffset = 0x08;      // u16[8]: string ids per line
inline constexpr int RecHandlersOffset = 0x18; // u64[5]: click handlers per line
inline constexpr int RecMaxLines = 5;
inline constexpr uint16_t IdentifyLine = 0xfb4;  // the builder (0x1147a0) rewrites the count at this line; keep it last

// Adds the lines (string id + handler) that are not in the record yet: before Identify when the record has it, else at
// the end, and sets the count to lines + 1 every call (the builder lowers it when it skips Identify). Returns the
// number of lines the record has afterwards, or -1 when it would not fit.
inline auto AddMenuLines(uint8_t* record, const uint16_t* ids, const uint64_t* handlers, int n) noexcept -> int {
	auto id = [&](int i) -> uint16_t& { return *reinterpret_cast<uint16_t*>(record + RecIdsOffset + i * 2); };
	auto fn = [&](int i) -> uint64_t& { return *reinterpret_cast<uint64_t*>(record + RecHandlersOffset + i * 8); };
	int lines = 0;
	while (lines < RecMaxLines && id(lines) != 0) {
		++lines;
	}
	for (int k = 0; k < n; ++k) {
		bool present = false;
		for (int i = 0; i < lines; ++i) {
			present = present || (id(i) == ids[k] && fn(i) == handlers[k]);
		}
		if (present) {
			continue;
		}
		if (lines >= RecMaxLines) {
			return -1;
		}
		int pos = lines;
		for (int i = 0; i < lines; ++i) {
			if (id(i) == IdentifyLine) {
				pos = i;
				break;
			}
		}
		for (int i = lines; i > pos; --i) {
			id(i) = id(i - 1);
			fn(i) = fn(i - 1);
		}
		id(pos) = ids[k];
		fn(pos) = handlers[k];
		++lines;
	}
	*reinterpret_cast<uint32_t*>(record + RecCountOffset) = static_cast<uint32_t>(lines + 1);
	return lines;
}

}  // namespace npc

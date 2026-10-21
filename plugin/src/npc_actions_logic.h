// Pure rules of Median's NPC menu actions (npc_actions.cpp; docs/subsystems/npc-hirelings.md section 1.5).
// No engine access, so mxl_tests can run them: the tables are mxl_npc_actions.h (generated) and the requirement
// evaluator is requirements.cpp; every game effect goes through the Host callbacks.
#pragma once

#include "mxl_npc_actions.h"
#include "requirements.h"

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
	Compound = 1, Page = 3, Travel = 8, Imbue = 11, Sockets = 12, Personalize = 13, Respec = 14, Waypoint = 15,
	Continue = 16, CreateItem = 17, Confirm = 18, Message = 20,
};

// Run: the server does it. Stub: known, logged, not done (travel is D2R's own; the panel services need quest-state work).
enum class Support : uint8_t { None, Run, Stub };
enum class Result : uint8_t { NotFound, Denied, Done, Nothing, Stubbed };

constexpr auto Merge(Support a, Support b) noexcept -> Support {
	return a == Support::Run || b == Support::Run ? Support::Run : (a == Support::Stub || b == Support::Stub ? Support::Stub : Support::None);
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
		return Support::Stub;
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
	void (*note)(void* user, const char* what, int a, int b) = nullptr;  // refusals and stubs, for the log
};

inline void Note(const Host& h, const char* what, int a, int b) noexcept {
	if (h.note != nullptr) {
		h.note(h.user, what, a, b);
	}
}

constexpr auto Join(Result a, Result b) noexcept -> Result {
	if (a == Result::Done || b == Result::Done) return Result::Done;
	if (a == Result::Stubbed || b == Result::Stubbed) return Result::Stubbed;
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
		Note(h, "action not implemented (type, row)", a.type, row);
		return Result::Stubbed;
	default:
		return Result::Nothing;
	}
}

// A click on slot `kind` of an NPC of class `npcClass`: the slot's requirement, then its action; a text-page slot
// runs the sub-entries that follow it, each under its own requirement.
inline auto RunSlot(uint32_t kind, int npcClass, const Host& h) noexcept -> Result {
	const int i = FindSlot(kind, npcClass);
	if (i < 0) {
		return Result::NotFound;
	}
	const mxl_npc::Slot& s = mxl_npc::Slots[i];
	if (!mxl_requirements::Evaluate(s.requirement, h.queries)) {
		return Result::Denied;
	}
	if (s.action > 0 && s.action < mxl_npc::ActionCount && mxl_npc::Actions[s.action].type == Page) {
		Result r = Result::Nothing;
		for (int j = i + 1; j < mxl_npc::SlotCount && mxl_npc::Slots[j].npcRow == s.npcRow && mxl_npc::Slots[j].sub != 0; ++j) {
			const mxl_npc::Slot& c = mxl_npc::Slots[j];
			if (mxl_requirements::Evaluate(c.requirement, h.queries)) {
				r = Join(r, RunAction(c.action, h));
			}
		}
		return r;
	}
	return RunAction(s.action, h);
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

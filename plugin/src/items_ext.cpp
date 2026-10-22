// Median's item and experience rules on D2R's functions (docs/subsystems/items-xp.md).
//
// Runewords. Median's runes.txt has 13 property slots, D2R's record holds 7: 197 of the 237 runewords lost properties 8-13
// when the tables were converted (txt_migrate overflow/runes.txt; tools/items_data.py puts them in mxl_runewords.h). D2R
// applies a runeword's properties in one function, 0x3d44c0 (item, owner): it gets the matched record from the matcher
// 0x372260, makes the item's runeword stat list (state 0xab, flags 0x40), runs the seven properties through the property
// function 0x3d4620 and finishes the list. The hook is that function again with the extra properties after the seven, so
// the list, the item flag and the stat snapshot D2RLoader keeps around it are handled exactly once. The record is
// identified by its row in D2R's runes table (0xe8-byte records; the table pointer comes from 0x3143b0) and by the item
// code of its first rune, so a table that does not match the generated one is left alone.
//
// Experience. D2R's kill experience goes through 0x44fef0 (player level, monster level, experience) with 1.13c's two
// 11-entry percent tables. Median's replacement (D2Experience.cpp 0x1005e7b0) is a formula, see items_ext_logic.h.
//
// Party experience. D2R's kill function 0x44a3b0 collects the party members near the monster with the party iterator
// 0x506f90 (callback 0x44fd50 fills a context: monster, units[8], levels[8], count, level sum) and then pools and splits
// the experience by level inline. Median (D2Experience.cpp 0x5e830) pays every collected member the whole award instead.
// The hook runs the iterator, then does Median's loop (the per-member award 0x44ece0 and the add 0x44f2e0, which are the
// functions the inline code calls) and sets the count to 0, which makes the kill function skip its own split. The collect
// range constant at 0x44fe7b goes from 0x1900 to 0x7d00 as in Median's patch table.
//
// Entry bytes checked against the dump of build 93847; whole instructions, no rip-relative.
#include "items_ext.h"

#include "d2r.h"
#include "items_ext_logic.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Unit;

constexpr uint64_t RunewordApplyRva = 0x3d44c0;   // (item, owner) -> 1 when the properties were applied
constexpr uint64_t RunewordMatchRva = 0x372260;   // (item) -> the matching runes record or null
constexpr uint64_t StatListRva = 0x2f5b80;        // (unit, state, flags) -> stat list or null
constexpr uint64_t SetItemFlagRva = 0x36d8f0;     // (item, flag, on)
constexpr uint64_t SnapshotRva = 0x2f8120;        // (unit, &flag) -> snapshot or null
constexpr uint64_t SnapshotBeginRva = 0x2f8290;   // (snapshot, unit)
constexpr uint64_t SnapshotEndRva = 0x2f81a0;     // (snapshot, unit, flag): loader slot, jmp [rip+x]
constexpr uint64_t FinishListRva = 0x2f3d00;      // (unit, list): loader slot, jmp [rip+x]
constexpr uint64_t ApplyPropertyRva = 0x3d4620;   // (type, unit, owner, mods, index, set, &property, state, flags, 0)
constexpr uint64_t UnitDataFlagRva = 0x34a0e0;    // (unit) -> data bank byte
constexpr uint64_t RunesTableRva = 0x3143b0;      // (dataFlag) -> {records, count} of runes.txt
constexpr uint64_t ItemRecordRva = 0x314110;      // (dataFlag, class id) -> items record
constexpr uint64_t XpPenaltyRva = 0x44fef0;       // (player level, monster level, experience) -> experience
constexpr uint64_t UnitTypeRva = 0x34b9d0;        // (unit) -> 0 player .. 4 item
constexpr uint64_t ItemDataRva = 0x34a500;        // (item) -> item data (+0 quality, +0x18 flags)
constexpr uint64_t IsItemTypeRva = 0x373890;      // (item, item type) -> non-zero when the item is of that type
constexpr uint64_t UnitClassRva = 0x349860;       // (unit, file, line) -> class id
constexpr uint64_t UnitInventoryRva = 0x34a360;   // (unit, file, line) -> inventory (the sockets of an item)
constexpr uint64_t FirstNodeRva = 0x388c10;       // (inventory) -> first node
constexpr uint64_t NodeUnitRva = 0x38aab0;        // (node) -> unit
constexpr uint64_t NextNodeRva = 0x38aba0;        // (node) -> next node
constexpr uint64_t GetStatRva = 0x2f5020;         // (unit, stat, layer)
constexpr uint64_t PartyIterateRva = 0x506f90;    // (game, killer, callback, ctx): collects the party members
constexpr uint64_t PartyCollectRva = 0x44fd50;    // the callback the kill function passes
constexpr uint64_t PartyRangeRva = 0x44fe7b;      // imm32 of `cmp ebx,0x1900`: squared distance of a member to the monster
constexpr uint64_t MemberAwardRva = 0x44ece0;     // (game, unit, level, monster level, xp) -> award
constexpr uint64_t AddExperienceRva = 0x44f2e0;   // (game, unit, old level, amount)
constexpr uint64_t BaseStatRva = 0x2f48c0;        // (unit, stat, layer) base value
constexpr int32_t StatLevel = 12;
constexpr int32_t StatExperience = 13;  // of a monster: its experience
// The context the kill function builds on its stack (0x70 bytes, zeroed).
constexpr int PartyMonster = 0x00;
constexpr int PartyUnits = 0x08;   // Unit* x 8
constexpr int PartyLevels = 0x48;  // int32 x 8
constexpr int PartyCount = 0x68;   // int32

constexpr int RunesRecordSize = 0xe8;
constexpr int RunesRuneClass = 0x5c;   // int32 x 6: item class ids of the runes
constexpr int RunesProperties = 0x74;  // 7 x {int prop, param, min, max}
constexpr int D2rSlots = 7;
constexpr int RunesComplete = 0x44;    // u8: the row is in use
constexpr int RunesItypes = 0x4a;      // int16 x 6: item types the runeword fits
constexpr int RunesEtypes = 0x56;      // int16 x 3: item types it does not fit
constexpr int ItCode = 0x80;           // items record: code (4 chars)
constexpr int ItNoRuneword = 0x13a;    // items record: u8, set: no runeword
constexpr int ItemDataFlags = 0x18;    // item data: flags (bit 31 set: type checks are skipped)
constexpr int UnitTypeItem = 4;
constexpr int32_t StatSockets = 0xc2;
constexpr uint32_t JewelCode = 0x2077656a;  // "jew "
constexpr const char* NoFile = "mxl-core items_ext";
constexpr int32_t RunewordState = 0xab;
constexpr int32_t MagicStatList = 0x40;
constexpr int32_t RunewordFlag = 0x4000000;
constexpr int32_t PropertyTypeRuneword = 6;

struct Property {
	int32_t prop, param, min, max;
};
struct RecordVector {
	const uint8_t* records;
	uint64_t count;
};

using RunewordFn = uint64_t (*)(Unit* item, void* owner);
using MatchFn = const uint8_t* (*)(Unit* item);
using StatListFn = void* (*)(Unit* unit, int32_t state, uint32_t flags);
using SetFlagFn = void (*)(Unit* item, uint32_t flag, int32_t on);
using SnapshotFn = void* (*)(Unit* unit, uint32_t* flag);
using SnapshotBeginFn = void (*)(void* snapshot, Unit* unit);
using SnapshotEndFn = void (*)(void* snapshot, Unit* unit, uint32_t flag);
using FinishListFn = void (*)(Unit* unit, void* list);
using ApplyPropertyFn = void (*)(int32_t type, Unit* unit, void* owner, void* mods, int32_t index, int32_t set,
                                 const Property* property, int32_t state, int32_t flags, void* extra);
using DataFlagFn = uint64_t (*)(Unit* unit);
using RunesTableFn = const RecordVector* (*)(uint64_t dataFlag);
using ItemRecordFn = const uint8_t* (*)(uint64_t dataFlag, int32_t classId);
using UnitTypeFn = int32_t (*)(Unit* unit);
using ItemDataFn = const uint8_t* (*)(Unit* item);
using IsItemTypeFn = int32_t (*)(Unit* item, int32_t type);
using UnitClassFn = int32_t (*)(Unit* unit, const char* file, int32_t line);
using InventoryFn = void* (*)(Unit* unit, const char* file, int32_t line);
using NodeFn = void* (*)(void* node);
using NodeUnitFn = Unit* (*)(void* node);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);

const D2RL::PluginContext* g_context = nullptr;
MatchFn g_match = nullptr;
StatListFn g_statList = nullptr;
SetFlagFn g_setFlag = nullptr;
SnapshotFn g_snapshot = nullptr;
SnapshotBeginFn g_snapshotBegin = nullptr;
SnapshotEndFn g_snapshotEnd = nullptr;
FinishListFn g_finish = nullptr;
ApplyPropertyFn g_apply = nullptr;
DataFlagFn g_dataFlag = nullptr;
RunesTableFn g_runesTable = nullptr;
ItemRecordFn g_itemRecord = nullptr;
UnitTypeFn g_unitType = nullptr;
ItemDataFn g_itemData = nullptr;
IsItemTypeFn g_isItemType = nullptr;
UnitClassFn g_unitClass = nullptr;
InventoryFn g_inventory = nullptr;
NodeFn g_firstNode = nullptr;
NodeUnitFn g_nodeUnit = nullptr;
NodeFn g_nextNode = nullptr;
GetStatFn g_getStat = nullptr;
MatchFn g_originalMatch = nullptr;

int g_padded = 0;
int g_extended = 0;
int g_refused = 0;
int g_xpChanged = 0;
int g_xpSeen = 0;
int g_partyPaid = 0;
using PartyIterateFn = void (*)(void* game, Unit* killer, void* callback, uint8_t* ctx);
using MemberAwardFn = int32_t (*)(void* game, Unit* unit, int32_t level, int32_t monsterLevel, int32_t xp);
using AddExperienceFn = void (*)(void* game, Unit* unit, int32_t oldLevel, int32_t amount);
using BaseStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
PartyIterateFn g_partyOriginal = nullptr;
MemberAwardFn g_memberAward = nullptr;
AddExperienceFn g_addExperience = nullptr;
BaseStatFn g_baseStat = nullptr;
uint64_t g_collectAddress = 0;

// 0x3d44c0: push rbp / push rdi / push r15 / sub rsp,0x60 / mov r15,rdx / mov rdi,rcx
constexpr uint8_t RunewordEntry[] = {0x40, 0x55, 0x57, 0x41, 0x57, 0x48, 0x83, 0xec, 0x60, 0x4c, 0x8b, 0xfa, 0x48, 0x8b, 0xf9};
// 0x372260: mov [rsp+0x10],rbx / mov [rsp+0x18],rsi / mov [rsp+0x20],rdi / push rbp
constexpr uint8_t MatchEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x55};
// 0x44fef0: sub rsp,0x28 / movsxd r9,r8d / cmp edx,ecx (then jg rel32)
constexpr uint8_t XpEntry[] = {0x48, 0x83, 0xec, 0x28, 0x4d, 0x63, 0xc8, 0x3b, 0xd1};

// 0x506f90: mov [rsp+8],rbx / mov [rsp+0x10],rbp / mov [rsp+0x18],rsi / push rdi / push r14 / push r15 / sub rsp,0x30
constexpr uint8_t PartyEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
                                  0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x30};
// 0x44fe79: cmp ebx,0x1900 / ja: the immediate is what changes
constexpr uint8_t PartyRangeBytes[] = {0x00, 0x19, 0x00, 0x00};

auto Matches(uint64_t base, uint64_t rva, const uint8_t* bytes, size_t size) noexcept -> bool {
	return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes, size) == 0;
}

RunewordFn g_originalRuneword = nullptr;

// D2R's matcher 0x372260 with Median's Jewel padding (items_ext_logic.h PaddedRunesMatch) for the items it refuses: the
// same checks in the same order (item type, quality 4-9, no-runeword flag of the base, sockets all filled), then every
// row in use whose padded rune list equals the socketed list, with the row's excluded types, then included types.
auto PaddedMatch(Unit* item) noexcept -> const uint8_t* {
	if (g_unitType(item) != UnitTypeItem) {
		return nullptr;
	}
	const uint8_t* data = g_itemData(item);
	if (data == nullptr) {
		return nullptr;
	}
	const uint32_t quality = d2r::At<uint32_t>(data, 0);
	if (quality >= 4 && quality <= 9) {
		return nullptr;
	}
	const uint64_t flag = g_dataFlag(item);
	const uint8_t* base = g_itemRecord(flag, g_unitClass(item, NoFile, 0));
	if (base == nullptr || d2r::At<uint8_t>(base, ItNoRuneword) != 0) {
		return nullptr;
	}
	void* inventory = g_inventory(item, NoFile, 0);
	if (inventory == nullptr) {
		return nullptr;
	}
	int32_t socketed[6] = {};
	int32_t n = 0;
	for (void* node = g_firstNode(inventory); node != nullptr; node = g_nextNode(node)) {
		Unit* gem = g_nodeUnit(node);
		if (gem == nullptr || n >= 6) {
			return nullptr;
		}
		socketed[n++] = g_unitClass(gem, NoFile, 0);
	}
	if (n == 0 || (g_getStat(item, StatSockets, 0) & 0xff) != n) {
		return nullptr;
	}
	const RecordVector* table = g_runesTable(flag);
	if (table == nullptr || table->count == 0) {
		return nullptr;
	}
	const int32_t jewel = d2r::At<int32_t>(table->records, RunesRuneClass);
	const uint8_t* jewelRecord = g_itemRecord(flag, jewel);
	if (jewel <= 0 || jewelRecord == nullptr || d2r::At<uint32_t>(jewelRecord, ItCode) != JewelCode) {
		return nullptr;  // not the table the padding was read from
	}
	const bool unchecked = static_cast<int32_t>(d2r::At<uint32_t>(data, ItemDataFlags)) < 0;
	for (uint64_t row = 0; row < table->count; ++row) {
		const uint8_t* record = table->records + row * RunesRecordSize;
		if (d2r::At<uint8_t>(record, RunesComplete) == 0 ||
		    !items_ext::PaddedRunesMatch(socketed, n, reinterpret_cast<const int32_t*>(record + RunesRuneClass), jewel)) {
			continue;
		}
		bool excluded = false;
		for (int j = 0; j < 3 && !unchecked; ++j) {
			const int16_t type = d2r::At<int16_t>(record, RunesEtypes + 2 * j);
			if (type == 0) {
				break;
			}
			if (g_isItemType(item, type) != 0) {
				excluded = true;
				break;
			}
		}
		if (excluded) {
			continue;
		}
		for (int j = 0; j < 6; ++j) {
			const int16_t type = d2r::At<int16_t>(record, RunesItypes + 2 * j);
			if (type == 0) {
				break;
			}
			if (g_isItemType(item, type) != 0 || unchecked) {
				++g_padded;
				return record;
			}
		}
	}
	return nullptr;
}

const uint8_t* MatchHook(Unit* item) {
	const uint8_t* record = g_originalMatch(item);
	if (record != nullptr || item == nullptr) {
		return record;
	}
	return PaddedMatch(item);
}

// The extra row for a matched record, or null: row inside the table, then the first rune's item code against the live
// record (a different table order or build would fail one of the two).
auto ExtraFor(Unit* item, const uint8_t* record) noexcept -> const mxl_runewords::Extra* {
	const uint64_t flag = g_dataFlag(item);
	const RecordVector* table = g_runesTable(flag);
	if (table == nullptr) {
		return nullptr;
	}
	const int64_t row = items_ext::RowOfRecord(record, table->records, table->count, RunesRecordSize);
	if (row < 0) {
		return nullptr;
	}
	const mxl_runewords::Extra* extra = items_ext::FindExtra(static_cast<int32_t>(row));
	if (extra == nullptr) {
		return nullptr;
	}
	const uint8_t* rune = g_itemRecord(flag, d2r::At<int32_t>(record, RunesRuneClass));
	if (rune == nullptr || d2r::At<uint32_t>(rune, ItCode) != extra->rune1) {
		++g_refused;
		return nullptr;
	}
	return extra;
}

// D2R's 0x3d44c0 with the extra properties after the seven (see the file header).
uint64_t RunewordHook(Unit* item, void* owner) {
	const uint8_t* record = g_match(item);
	if (record == nullptr) {
		return 0;
	}
	if (g_statList(item, RunewordState, MagicStatList) != nullptr) {
		return 0;
	}
	g_setFlag(item, RunewordFlag, 1);
	uint32_t flag = 0;
	void* snapshot = g_snapshot(item, &flag);
	if (snapshot != nullptr) {
		g_snapshotBegin(snapshot, item);
	}
	const auto* props = reinterpret_cast<const Property*>(record + RunesProperties);
	int32_t index = 0;
	for (; index < D2rSlots; ++index) {
		if (static_cast<int16_t>(props[index].prop) < 0) {
			break;
		}
		g_apply(PropertyTypeRuneword, item, owner, nullptr, index, 0, &props[index], RunewordState, MagicStatList, nullptr);
	}
	if (const mxl_runewords::Extra* extra = ExtraFor(item, record); extra != nullptr) {
		const int32_t n = items_ext::UsedSlots(*extra);
		for (int32_t k = 0; k < n; ++k) {
			const Property p {extra->props[k].prop, extra->props[k].param, extra->props[k].min, extra->props[k].max};
			g_apply(PropertyTypeRuneword, item, owner, nullptr, D2rSlots + k, 0, &p, RunewordState, MagicStatList, nullptr);
		}
		++g_extended;
		if (g_extended <= 6 && g_context != nullptr) {
			char line[128];
			std::snprintf(line, sizeof(line), "mxl-core: runeword row %d: %d + %d properties", extra->row, index, n);
			g_context->LogInfo(line);
		}
	}
	g_finish(item, g_statList(item, RunewordState, MagicStatList));
	if (snapshot != nullptr) {
		g_snapshotEnd(snapshot, item, flag);
	}
	return 1;
}

// D2R's 0x44fef0 (rcx player level, rdx monster level, r8d experience) with Median's penalty.
int32_t XpPenaltyHook(int32_t playerLevel, int32_t monsterLevel, int32_t xp) {
	const int32_t result = items_ext::PenalizedXp(xp, playerLevel, monsterLevel);
	++g_xpSeen;
	g_xpChanged += result != xp ? 1 : 0;
	return result;
}

// The party iterator, then Median's award for the kill function's collect callback.
void PartyIterateHook(void* game, Unit* killer, void* callback, uint8_t* ctx) {
	g_partyOriginal(game, killer, callback, ctx);
	if (reinterpret_cast<uint64_t>(callback) != g_collectAddress || ctx == nullptr) {
		return;  // another caller of the iterator
	}
	__try {
		const int32_t count = *reinterpret_cast<int32_t*>(ctx + PartyCount);
		auto* monster = *reinterpret_cast<Unit**>(ctx + PartyMonster);
		if (count < 1 || count > 8 || monster == nullptr) {
			return;
		}
		const int32_t xp = items_ext::MedianPartyAward(g_baseStat(monster, StatExperience, 0));
		const int32_t monsterLevel = g_baseStat(monster, StatLevel, 0);
		auto* units = reinterpret_cast<Unit**>(ctx + PartyUnits);
		auto* levels = reinterpret_cast<int32_t*>(ctx + PartyLevels);
		for (int32_t i = 0; i < count; ++i) {
			if (units[i] == nullptr) {
				continue;
			}
			const int32_t amount = g_memberAward(game, units[i], levels[i], monsterLevel, xp);
			g_addExperience(game, units[i], levels[i], amount);
			++g_partyPaid;
		}
		*reinterpret_cast<int32_t*>(ctx + PartyCount) = 0;  // the kill function skips its pool and split
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

}  // namespace

auto InstallItemsExt(const D2RL::PluginContext* context, bool runewords, bool xp, bool party, char* report, size_t reportSize) noexcept
	-> bool {
	g_context = context;
	const uint64_t base = context->exeBase;
	const char* rw = "left out";
	const char* xs = "left out";
	const char* ps = "left out";
	bool ok = true;
	if (runewords) {
		if (!Matches(base, RunewordApplyRva, RunewordEntry, sizeof(RunewordEntry)) ||
		    !Matches(base, RunewordMatchRva, MatchEntry, sizeof(MatchEntry))) {
			rw = "off: entry bytes of 0x3d44c0 / 0x372260 differ";
			ok = false;
		} else {
			g_unitType = reinterpret_cast<UnitTypeFn>(base + UnitTypeRva);
			g_itemData = reinterpret_cast<ItemDataFn>(base + ItemDataRva);
			g_isItemType = reinterpret_cast<IsItemTypeFn>(base + IsItemTypeRva);
			g_unitClass = reinterpret_cast<UnitClassFn>(base + UnitClassRva);
			g_inventory = reinterpret_cast<InventoryFn>(base + UnitInventoryRva);
			g_firstNode = reinterpret_cast<NodeFn>(base + FirstNodeRva);
			g_nodeUnit = reinterpret_cast<NodeUnitFn>(base + NodeUnitRva);
			g_nextNode = reinterpret_cast<NodeFn>(base + NextNodeRva);
			g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
			g_match = reinterpret_cast<MatchFn>(base + RunewordMatchRva);
			g_statList = reinterpret_cast<StatListFn>(base + StatListRva);
			g_setFlag = reinterpret_cast<SetFlagFn>(base + SetItemFlagRva);
			g_snapshot = reinterpret_cast<SnapshotFn>(base + SnapshotRva);
			g_snapshotBegin = reinterpret_cast<SnapshotBeginFn>(base + SnapshotBeginRva);
			g_snapshotEnd = reinterpret_cast<SnapshotEndFn>(base + SnapshotEndRva);
			g_finish = reinterpret_cast<FinishListFn>(base + FinishListRva);
			g_apply = reinterpret_cast<ApplyPropertyFn>(base + ApplyPropertyRva);
			g_dataFlag = reinterpret_cast<DataFlagFn>(base + UnitDataFlagRva);
			g_runesTable = reinterpret_cast<RunesTableFn>(base + RunesTableRva);
			g_itemRecord = reinterpret_cast<ItemRecordFn>(base + ItemRecordRva);
			const bool hooked = context->InstallInlineHook(RunewordApplyRva, RunewordEntry, sizeof(RunewordEntry),
			                                               reinterpret_cast<void*>(&RunewordHook),
			                                               reinterpret_cast<void**>(&g_originalRuneword));
			// the matcher goes second: the apply hook above calls it through its (now redirected) entry
			const bool matcher = context->InstallInlineHook(RunewordMatchRva, MatchEntry, sizeof(MatchEntry),
			                                                reinterpret_cast<void*>(&MatchHook),
			                                                reinterpret_cast<void**>(&g_originalMatch));
			rw = hooked && matcher ? "hooked (properties 8-13, Jewel padding)" : hooked ? "hooked (properties only, matcher NOT hooked)" : "NOT hooked";
			ok = ok && hooked && matcher;
		}
	}
	if (xp) {
		if (!Matches(base, XpPenaltyRva, XpEntry, sizeof(XpEntry))) {
			xs = "off: entry bytes of 0x44fef0 differ";
			ok = false;
		} else {
			const bool hooked = context->InstallInlineHook(XpPenaltyRva, XpEntry, sizeof(XpEntry),
			                                               reinterpret_cast<void*>(&XpPenaltyHook), nullptr);
			xs = hooked ? "hooked" : "NOT hooked";
			ok = ok && hooked;
		}
	}
	if (party) {
		if (!Matches(base, PartyIterateRva, PartyEntry, sizeof(PartyEntry)) ||
		    !Matches(base, PartyRangeRva, PartyRangeBytes, sizeof(PartyRangeBytes))) {
			ps = "off: entry bytes of 0x506f90 / 0x44fe7b differ";
			ok = false;
		} else {
			g_collectAddress = base + PartyCollectRva;
			g_memberAward = reinterpret_cast<MemberAwardFn>(base + MemberAwardRva);
			g_addExperience = reinterpret_cast<AddExperienceFn>(base + AddExperienceRva);
			g_baseStat = reinterpret_cast<BaseStatFn>(base + BaseStatRva);
			const bool hooked = context->InstallInlineHook(PartyIterateRva, PartyEntry, sizeof(PartyEntry),
			                                               reinterpret_cast<void*>(&PartyIterateHook),
			                                               reinterpret_cast<void**>(&g_partyOriginal));
			const bool range = context->PatchWriteU32(PartyRangeRva, PartyRangeBytes, sizeof(PartyRangeBytes),
			                                          items_ext::MedianPartyRangeSq);
			ps = hooked && range ? "hooked (whole award to every member, range 179)" : hooked ? "hooked, range NOT widened" : "NOT hooked";
			ok = ok && hooked && range;
		}
	}
	std::snprintf(report, reportSize, "%s: runewords (%d rows with properties 8-13: 0x3d44c0; Jewel padding: 0x372260) %s, kill experience penalty (0x44fef0) %s, party experience (0x506f90) %s",
	              ok ? "on" : "partial", mxl_runewords::ExtraCount, rw, xs, ps);
	return ok;
}

void ItemsExtCounts(int* extended, int* refused, int* xpChanged, int* xpSeen, int* padded) noexcept {
	*padded = g_padded;
	*extended = g_extended;
	*refused = g_refused;
	*xpChanged = g_xpChanged;
	*xpSeen = g_xpSeen;
}

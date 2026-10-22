// Tests of the pure NPC action rules (npc_actions_logic.h) over the generated tables. Run through mxl_tests
// (requirements_test.cpp main).
#include "../src/npc_actions_logic.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL (npc actions): %s\n", what);
	}
}

struct Fixture {
	std::set<std::pair<int, int>> bits;  // quest, bit (game difficulty only)
	std::set<std::uint32_t> held;
	std::vector<std::uint32_t> created;
	std::vector<int> createdLevel;
	std::vector<int> createdQuality;
	int respecs = 0;
	int notes = 0;
	int pages = 0;
	std::uint32_t pageKind = 0;
	int pageClass = 0;
	std::uint32_t pageMask = 0;
};

bool QBit(void* user, int quest, int bit, int) { return static_cast<Fixture*>(user)->bits.count({quest, bit}) != 0; }
int QClass(void*) { return 1; }
bool QHolds(void* user, const char* code) {
	std::uint32_t w = 0;
	std::memcpy(&w, code, 4);
	return static_cast<Fixture*>(user)->held.count(w) != 0;
}
bool MakeItem(void* user, std::uint32_t code, int level, int quality) {
	auto* f = static_cast<Fixture*>(user);
	f->created.push_back(code);
	f->createdLevel.push_back(level);
	f->createdQuality.push_back(quality);
	f->held.insert(code);
	return true;
}
bool DoRespec(void* user) {
	++static_cast<Fixture*>(user)->respecs;
	return true;
}
void Note(void* user, const char*, int, int) { ++static_cast<Fixture*>(user)->notes; }
void Page(void* user, int npcClass, std::uint32_t kind, std::uint32_t mask) {
	auto* f = static_cast<Fixture*>(user);
	++f->pages;
	f->pageClass = npcClass;
	f->pageKind = kind;
	f->pageMask = mask;
}

npc::Host MakeHost(Fixture& f) {
	npc::Host h;
	h.user = &f;
	h.queries = {&f, QBit, QClass, QHolds};
	h.createItem = MakeItem;
	h.respec = DoRespec;
	h.page = Page;
	h.note = Note;
	return h;
}

constexpr std::uint32_t C(const char (&s)[5]) {
	return static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) | static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 8 |
	       static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 16 | static_cast<std::uint32_t>(static_cast<unsigned char>(s[3])) << 24;
}

int SlotOf(int npcClass, int slot) {
	for (int i = 0; i < mxl_npc::SlotCount; ++i) {
		if (mxl_npc::Slots[i].npcClass == npcClass && mxl_npc::Slots[i].slot == slot) {
			return i;
		}
	}
	return -1;
}

std::uint32_t KindFor(int npcClass, int slot) {
	const int i = SlotOf(npcClass, slot);
	return i < 0 ? 0 : npc::KindOf(mxl_npc::Slots[i].npcRow, mxl_npc::Slots[i].slot);
}

}  // namespace

int RunNpcActionsTests() {
	using namespace npc;

	// the tables: 31 actions, 34 slots, kinds do not collide with the game's 1..3
	Check(mxl_npc::ActionCount == 31, "31 npcactions rows");
	Check(mxl_npc::SlotCount == 34, "34 menu slots");
	Check(KindOf(0, 0) == KindBase && KindBase > 3, "kinds start above trade, gamble and hire");
	{
		std::set<std::uint32_t> kinds;
		for (const auto& s : mxl_npc::Slots) {
			kinds.insert(KindOf(s.npcRow, s.slot));
		}
		Check(kinds.size() == static_cast<std::size_t>(mxl_npc::SlotCount), "every slot has its own kind");
	}

	// what the client shows and what stays stubbed
	Check(SlotSupport(SlotOf(148, 0)) == Support::Run, "Akara respec runs");
	Check(SlotSupport(SlotOf(244, 0)) == Support::Run, "Cain crafting materials page runs");
	Check(SlotSupport(SlotOf(244, 2)) == Support::Run, "Cain catalyst entry runs");
	for (const int cls : {154, 511, 512}) {
		Check(SlotSupport(SlotOf(cls, 0)) == Support::Native, "Charsi / Larzuk / Anya services are D2R's own");
	}
	for (const int cls : {155, 175, 210, 264, 367}) {
		Check(SlotSupport(SlotOf(cls, 0)) == Support::Native, "travel packages are D2R's own");
	}

	// item values: quality 1 is the default (normal), others pass
	Check(ItemQuality(1) == 2 && ItemQuality(0) == 2 && ItemQuality(5) == 5 && ItemQuality(12) == 2, "item quality mapping");

	// unknown kinds and wrong NPC classes are not found
	{
		Fixture f;
		const npc::Host h = MakeHost(f);
		Check(RunSlot(KindFor(148, 0), 244, h) == Result::NotFound, "Akara's kind from a Cain is rejected");
		Check(RunSlot(3, 148, h) == Result::NotFound, "the game's own kinds are not Median's");
		Check(RunSlot(0xffff, 148, h) == Result::NotFound, "unknown kind");
	}

	// Akara: row 26 = Den of Evil done (quest 1 bit 0) and quest 41 bit 1
	{
		Fixture f;
		const npc::Host h = MakeHost(f);
		const std::uint32_t kind = KindFor(148, 0);
		Check(RunSlot(kind, 148, h) == Result::Denied && f.respecs == 0, "respec denied without the quests");
		f.bits.insert({1, 0});
		Check(RunSlot(kind, 148, h) == Result::Denied && f.respecs == 0, "respec needs the flag too");
		f.bits.insert({41, 1});
		Check(RunSlot(kind, 148, h) == Result::Done && f.respecs == 1, "respec runs once the requirement holds");
	}

	// Cain: a click on the page gives nothing, it reports which entries are open; each entry runs on its own click
	{
		Fixture f;
		const npc::Host h = MakeHost(f);
		for (const int cls : {244, 245, 246, 265, 520}) {
			Check(RunSlot(KindFor(cls, 0), cls, h) == Result::Denied, "Cain denied before the search for Cain is done");
		}
		Check(f.pages == 0 && f.created.empty(), "a denied page tells the client nothing and gives nothing");
		f.bits.insert({4, 0});  // Cain rescued: the cube
		Check(RunSlot(KindFor(244, 0), 244, h) == Result::Page, "Cain's page opens");
		Check(f.pages == 1 && f.pageClass == 244 && f.pageKind == KindFor(244, 0) && f.pageMask == 1, "only the cube entry is open");
		Check(f.created.empty(), "the page itself gives nothing");
		Check(RunSlot(KindFor(244, 1), 244, h) == Result::Done, "the cube entry gives the cube");
		Check(f.created.size() == 1 && f.created[0] == C("box ") && f.createdLevel[0] == 1 && f.createdQuality[0] == 2,
		      "only the cube, normal quality");
		Check(RunSlot(KindFor(244, 0), 244, h) == Result::Denied, "the cube is held and nothing else is open: page denied (row 31)");
		Check(RunSlot(KindFor(244, 1), 244, h) == Result::Denied && f.created.size() == 1, "the cube is not given twice");
		f.bits.insert({6, 0});  // Andariel (Normal): catalyst of disenchantment
		f.bits.insert({13, 0});  // row 20 reads quest 13 bit 0 (Nightmare in the table; the fixture ignores the difficulty)
		f.bits.insert({25, 0});
		Check(RunSlot(KindFor(245, 0), 245, h) == Result::Page && f.pageClass == 245 && f.pageMask == 0xe,
		      "the catalysts are open, the held cube is not");
		Check(RunSlot(KindFor(245, 2), 245, h) == Result::Done && f.created.back() == C("3o[ "), "the first catalyst");
		Check(RunSlot(KindFor(245, 2), 245, h) == Result::Denied && f.created.size() == 2, "nothing twice");
		Check(RunSlot(KindFor(246, 3), 246, h) == Result::Done && f.created.back() == C("0o[ "), "second catalyst");
		Check(RunSlot(KindFor(246, 4), 246, h) == Result::Done && f.created.back() == C("jcat"), "third catalyst");
		Check(RunSlot(KindFor(246, 0), 246, h) == Result::Denied, "everything held: the page is closed");

		// the client's side: the mask picks the entries and their kinds
		npc::PageEntry e[8];
		Check(PageEntries(244, KindFor(244, 0), 0, e, 8) == 0, "empty mask: no entries");
		Check(PageEntries(244, KindFor(244, 0), 0xf, e, 8) == 4, "full mask: four entries");
		Check(e[0].kind == KindFor(244, 1) && e[3].kind == KindFor(244, 4), "entries keep the slot order");
		Check(e[0].label == mxl_npc::Slots[SlotOf(244, 1)].label && e[2].label == mxl_npc::Slots[SlotOf(244, 3)].label,
		      "entry labels are the slots'");
		Check(PageEntries(244, KindFor(244, 0), 0xa, e, 8) == 2 && e[0].kind == KindFor(244, 2) && e[1].kind == KindFor(244, 4),
		      "mask 0xa picks entries 2 and 4");
		Check(PageEntries(244, KindFor(244, 0), 0xf, e, 2) == 2, "capacity limits the list");
		Check(PageEntries(148, KindFor(148, 0), 0xf, e, 8) == 0, "Akara's line is not a page");
		Check(PageEntries(245, KindFor(244, 0), 0xf, e, 8) == 0, "a page kind of another NPC is not found");
		std::uint8_t payload[PagePayloadSize];
		BuildPagePayload(payload, 0x12345678u, 520, 0x0a05);
		std::uint32_t pk = 0;
		std::uint16_t pc = 0, pm = 0;
		Check(ParsePagePayload(payload, sizeof(payload), &pk, &pc, &pm) && pk == 0x12345678u && pc == 520 && pm == 0x0a05,
		      "page payload round trip");
		Check(!ParsePagePayload(payload, 7, &pk, &pc, &pm), "short payload rejected");
	}

	// D2R-native services: logged, no effect
	{
		Fixture f;
		f.bits.insert({6, 0});  // row 11: Warriv's "go east" (quest 6, Andariel)
		const npc::Host h = MakeHost(f);
		Check(RunSlot(KindFor(155, 0), 155, h) == Result::Native && f.notes > 0 && f.created.empty() && f.respecs == 0,
		      "travel package is D2R's own: logged, nothing run");
		Check(RunSlot(KindFor(154, 0), 154, h) == Result::Denied, "Charsi's imbue keeps its quest gate");
	}

	// the client record: [Talk, Identify] gets the line before Identify; the builder's count change is undone
	{
		alignas(8) std::uint8_t rec[RecordSize] = {};
		auto* ids = reinterpret_cast<std::uint16_t*>(rec + RecIdsOffset);
		auto* fns = reinterpret_cast<std::uint64_t*>(rec + RecHandlersOffset);
		*reinterpret_cast<std::uint16_t*>(rec) = 244;
		*reinterpret_cast<std::uint32_t*>(rec + RecCountOffset) = 3;
		ids[0] = 0xd35;
		ids[1] = IdentifyLine;
		fns[0] = 0x1111;
		fns[1] = 0x2222;
		const std::uint16_t newId[1] = {40516};
		const std::uint64_t newFn[1] = {0x3333};
		Check(AddMenuLines(rec, newId, newFn, 1) == 3, "Cain record has 3 lines");
		Check(ids[0] == 0xd35 && ids[1] == 40516 && ids[2] == IdentifyLine && fns[0] == 0x1111 && fns[1] == 0x3333 && fns[2] == 0x2222,
		      "line inserted before Identify, handlers moved with their ids");
		Check(*reinterpret_cast<std::uint32_t*>(rec + RecCountOffset) == 4, "count = lines + Cancel");
		*reinterpret_cast<std::uint32_t*>(rec + RecCountOffset) = 2;  // what the builder does when Identify is skipped
		Check(AddMenuLines(rec, newId, newFn, 1) == 3 && ids[1] == 40516 && ids[3] == 0, "idempotent");
		Check(*reinterpret_cast<std::uint32_t*>(rec + RecCountOffset) == 4, "count restored");
	}
	// bug #23: the plugin patches the table on every menu open. The record as D2R builds it (read from the heap in game,
	// 2026-10-21: count 3, ids 0xd35 0xfb4, handlers 0x112eb0 / 0x114000, nothing behind) must take the line and stay at
	// three lines over many opens, whether the builder shows or skips Identify (it rewrites the count to 3 or 2).
	{
		alignas(8) std::uint8_t rec[RecordSize] = {};
		auto* ids = reinterpret_cast<std::uint16_t*>(rec + RecIdsOffset);
		auto* fns = reinterpret_cast<std::uint64_t*>(rec + RecHandlersOffset);
		auto* count = reinterpret_cast<std::uint32_t*>(rec + RecCountOffset);
		*reinterpret_cast<std::uint16_t*>(rec) = 265;
		*count = 3;
		ids[0] = 0xd35;
		ids[1] = IdentifyLine;
		fns[0] = 0x140112eb0;
		fns[1] = 0x140114000;
		rec[0x40] = 1;
		const std::uint16_t newId[1] = {40516};
		const std::uint64_t newFn[1] = {0x7ffa12340001};
		bool ok = true;
		for (int open = 0; open < 12; ++open) {
			ok = ok && AddMenuLines(rec, newId, newFn, 1) == 3 && *count == 4;
			*count = (open % 2 == 0) ? 3u : 2u;  // the builder: Identify shown (3) or skipped (2)
		}
		Check(ok, "twelve menu opens keep Cain at three lines (no 'record is full')");
		Check(ids[0] == 0xd35 && ids[1] == 40516 && ids[2] == IdentifyLine && ids[3] == 0 && ids[4] == 0, "ids stay [Talk, crafting, Identify]");
		Check(fns[0] == 0x140112eb0 && fns[1] == 0x7ffa12340001 && fns[2] == 0x140114000 && fns[3] == 0 && rec[0x40] == 1,
		      "handlers follow their ids, the flag byte behind the five handler slots is untouched");
	}
	// the respec asks first (npcext PromptConfirm), nothing else does
	{
		const int akara = SlotOf(148, 0);
		Check(akara >= 0 && NeedsConfirm(akara), "Akara's respec line asks Yes / No first");
		Check(ConfirmText(akara) == mxl_npc::Slots[akara].label && ConfirmText(akara) != 0,
		      "the prompt text is the respec label (npcext PromptLabel)");
		int asking = 0;
		for (int i = 0; i < mxl_npc::SlotCount; ++i) {
			asking += NeedsConfirm(i) ? 1 : 0;
		}
		Check(asking == 1, "only the respec line has a confirmation");
		Check(!NeedsConfirm(-1) && !NeedsConfirm(mxl_npc::SlotCount), "out of range slots do not ask");
		Check(StringYes == 3344 && StringNo == 3345, "D2R's stock Yes / No string ids");

		Confirmation c;
		Check(!c.open && c.Answer(true) == 0, "Yes with nothing pending sends nothing");
		c.Open(KindFor(148, 0), ConfirmText(akara));
		Check(c.open && c.Answer(true) == KindFor(148, 0) && !c.open, "Yes sends the line's kind once");
		Check(c.Answer(true) == 0, "a second Yes sends nothing");
		c.Open(KindFor(148, 0), ConfirmText(akara));
		Check(c.Answer(false) == 0 && !c.open, "No sends nothing and ends the confirmation");
		c.Open(KindFor(148, 0), 1);
		c.Open(KindFor(244, 0), 2);
		Check(c.Answer(true) == KindFor(244, 0), "a new confirmation replaces the pending one");
	}
	// Akara [Talk, Trade] gets the line at the end; a full record refuses
	{
		alignas(8) std::uint8_t rec[RecordSize] = {};
		auto* ids = reinterpret_cast<std::uint16_t*>(rec + RecIdsOffset);
		*reinterpret_cast<std::uint32_t*>(rec + RecCountOffset) = 3;
		ids[0] = 0xd35;
		ids[1] = 0xd44;
		const std::uint16_t newId[1] = {11168};
		const std::uint64_t newFn[1] = {0x4444};
		Check(AddMenuLines(rec, newId, newFn, 1) == 3 && ids[2] == 11168, "appended after Trade");
		for (int i = 3; i < RecMaxLines; ++i) {
			ids[i] = 0x100 + static_cast<std::uint16_t>(i);
		}
		const std::uint16_t more[1] = {5555};
		const std::uint64_t moreFn[1] = {0x5555};
		Check(AddMenuLines(rec, more, moreFn, 1) == -1, "a record with five lines refuses a sixth");
	}
	return failures;
}

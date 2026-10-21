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

npc::Host MakeHost(Fixture& f) {
	npc::Host h;
	h.user = &f;
	h.queries = {&f, QBit, QClass, QHolds};
	h.createItem = MakeItem;
	h.respec = DoRespec;
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

	// Cain: the page gives every material whose requirement holds and that is not held yet
	{
		Fixture f;
		const npc::Host h = MakeHost(f);
		for (const int cls : {244, 245, 246, 265, 520}) {
			Check(RunSlot(KindFor(cls, 0), cls, h) == Result::Denied, "Cain denied before the search for Cain is done");
		}
		f.bits.insert({4, 0});  // Cain rescued: the cube
		Check(RunSlot(KindFor(244, 0), 244, h) == Result::Done, "Cain gives the cube");
		Check(f.created.size() == 1 && f.created[0] == C("box ") && f.createdLevel[0] == 1 && f.createdQuality[0] == 2,
		      "only the cube, normal quality");
		Check(RunSlot(KindFor(244, 0), 244, h) == Result::Denied, "the cube is held: page denied (row 31)");
		f.bits.insert({6, 0});  // Andariel (Normal): catalyst of disenchantment
		Check(RunSlot(KindFor(245, 0), 245, h) == Result::Done && f.created.size() == 2 && f.created[1] == C("3o[ "),
		      "Andariel done: the first catalyst");
		Check(RunSlot(KindFor(245, 0), 245, h) == Result::Denied && f.created.size() == 2, "nothing twice");
		// a sub-entry on its own, with its own requirement (3o[ is held, so row 28 fails)
		Check(RunSlot(KindFor(246, 2), 246, h) == Result::Denied, "catalyst entry denied while held");
		Check(RunSlot(KindFor(246, 3), 246, h) == Result::Denied, "second catalyst needs its quest");
		f.bits.insert({13, 0});  // row 20 reads quest 13 bit 0 (Nightmare in the table; the fixture ignores the difficulty)
		Check(RunSlot(KindFor(246, 3), 246, h) == Result::Done && f.created.back() == C("0o[ "), "second catalyst");
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

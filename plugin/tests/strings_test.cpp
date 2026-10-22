// Standalone test of the run-time string helpers (strings_logic.h; not part of the plugin DLL) and of what the generated
// tables hand over: every NPC menu string and every trophy text is a native id or a full `medianxl:` key.
#include "../src/client_messages_logic.h"
#include "../src/items_ext_logic.h"
#include "../src/mxl_items.h"
#include "../src/npc_actions_logic.h"
#include "../src/strings_logic.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: %s\n", what);
	}
}
}  // namespace

int main() {
	using namespace mxlstr;
	char out[64];

	// Substitute: one placeholder, %% and the rest untouched
	Substitute(out, sizeof(out), "(Required Level: %d)", "85");
	Check(std::strcmp(out, "(Required Level: 85)") == 0, "%d is replaced");
	Substitute(out, sizeof(out), "Closing in: %s", "01:35");
	Check(std::strcmp(out, "Closing in: 01:35") == 0, "%s is replaced");
	Substitute(out, sizeof(out), "%02d%% of %s", "7");
	Check(std::strcmp(out, "7% of %s") == 0, "width, %%, second placeholder stays");
	Substitute(out, sizeof(out), "no placeholder", "x");
	Check(std::strcmp(out, "no placeholder") == 0, "text without a placeholder");
	Substitute(out, 8, "Closing in: %s", "01:35");
	Check(std::strcmp(out, "Closing") == 0, "cut to the buffer, terminated");
	Substitute(out, sizeof(out), nullptr, nullptr);
	Check(out[0] == '\0', "null text is empty");
	Substitute(out, sizeof(out), "100%", "x");
	Check(std::strcmp(out, "100%") == 0, "a trailing % stays");

	// the lockout texts follow the template they are given
	char line[64];
	clientmsg::ClosingLine(95, line, sizeof(line), "Schliesst in: %s");
	Check(std::strcmp(line, "Schliesst in: 01:35") == 0, "ClosingLine uses the template");
	clientmsg::RequiredLevelLine(85, line, sizeof(line), "(Stufe %d)");
	Check(std::strcmp(line, "(Stufe 85)") == 0, "RequiredLevelLine uses the template");
	clientmsg::RequiredLevelLine(85, line, sizeof(line));
	Check(std::strcmp(line, "(Required Level: 85)") == 0, "English without a template");
	char lines[3][64];
	Check(clientmsg::RequirementLines(85, 0, 0, 80, 0, 0, lines, "(Stufe %d)") == 1 && std::strcmp(lines[0], "(Stufe 85)") == 0,
	      "RequirementLines passes the template on");
	clientmsg::GoblinAppears(nullptr, line, sizeof(line), " erscheint!");
	Check(std::strcmp(line, "Treasure Goblin erscheint!") == 0, "goblin suffix");

	// SkipLeadingNumberLine: the number line of a help text is no part of it
	Check(std::strcmp(SkipLeadingNumberLine("12\nHeart of Sin"), "Heart of Sin") == 0, "number line skipped");
	Check(std::strcmp(SkipLeadingNumberLine("  3 \r\nText"), "Text") == 0, "blanks around the number");
	Check(std::strcmp(SkipLeadingNumberLine("Heart of Sin"), "Heart of Sin") == 0, "no number line: unchanged");
	Check(std::strcmp(SkipLeadingNumberLine("12 apples\nx"), "12 apples\nx") == 0, "a number with text is no number line");
	Check(std::strcmp(SkipLeadingNumberLine(nullptr), "") == 0, "null is empty");

	// ForEachLine / trophy lines
	std::vector<std::string> got;
	const int n = ForEachLine("  Join another game  \n\nThis area is locked\r\n", 250,
	                          [&](const char* l, int) { got.push_back(l); });
	Check(n == 2 && got.size() == 2 && got[0] == "Join another game" && got[1] == "This area is locked", "lines trimmed, blanks dropped");
	got.clear();
	ForEachLine("abcdefghij", 4, [&](const char* l, int) { got.push_back(l); });
	Check(got.size() == 1 && got[0] == "abcd", "a line is cut to the maximum");
	bool firstIsTitle = false;
	int titles = 0;
	items_ext::ForEachTrophyLine("Title\nline", [&](const char*, bool title) {
		titles += title ? 1 : 0;
		firstIsTitle = firstIsTitle || title;
	});
	Check(titles == 1 && firstIsTitle, "only the first trophy line is the title");

	// Ref
	const Ref none{};
	const Ref native{3382, nullptr};
	const Ref key{0, "medianxl:AkaraMenu1b"};
	Check(none.IsNone() && !native.IsNone() && !key.IsNone() && key.IsKey() && !native.IsKey(), "Ref kinds");
	Check((Ref{0, ""}).IsNone(), "an empty key is none");
	Check(key == Ref{0, "medianxl:AkaraMenu1b"} && !(key == native) && none == Ref{0, ""}, "Ref equality");

	// IdRegistry
	{
		IdRegistry r;
		auto all = [](uint16_t) { return true; };
		const uint16_t a = r.Add("medianxl:A", all);
		const uint16_t b = r.Add("medianxl:B", all);
		Check(a == IdRegistry::FirstId && b == IdRegistry::FirstId - 1, "ids count down from the first id");
		Check(r.Add("medianxl:A", all) == a && r.Count() == 2, "a key keeps its id");
		Check(std::strcmp(r.KeyOf(a), "medianxl:A") == 0 && r.KeyOf(0x1234) == nullptr, "KeyOf");
		Check(r.IndexOf(a) == 0 && r.IndexOf(b) == 1 && r.IndexOf(1) == -1, "IndexOf");
		const uint16_t c = r.Add("medianxl:C", [&](uint16_t id) { return id != b - 1 && id != b - 2; });
		Check(c == b - 3, "ids D2R has text for are skipped");
		Check(r.Add("medianxl:D", [](uint16_t) { return false; }) == 0, "no free id: 0");
		Check(r.Add(nullptr, all) == 0, "null key: 0");
		std::string longKey(IdRegistry::KeyMax + 4, 'k');
		Check(r.Add(longKey.c_str(), all) == 0, "too long key: 0");
		IdRegistry full;
		bool distinct = true;
		for (int i = 0; i < IdRegistry::Capacity; ++i) {
			char k[32];
			std::snprintf(k, sizeof(k), "medianxl:K%d", i);
			distinct = distinct && full.Add(k, all) != 0;
		}
		Check(distinct && full.Count() == IdRegistry::Capacity && full.Add("medianxl:over", all) == 0, "capacity");
	}

	// the generated tables: a string is a D2R id or a full key, never both, never a bare key
	for (int i = 0; i < mxl_npc::SlotCount; ++i) {
		const mxl_npc::Slot& s = mxl_npc::Slots[i];
		const Ref label = npc::LabelOf(s);
		const Ref prompt = npc::PromptOf(s);
		Check(!(label.id != 0 && label.IsKey()) && !(prompt.id != 0 && prompt.IsKey()), "slot string: id or key, not both");
		Check(!label.IsKey() || std::strncmp(label.key, "medianxl:", 9) == 0, "slot label key is namespaced");
		Check(!prompt.IsKey() || std::strncmp(prompt.key, "medianxl:", 9) == 0, "slot prompt key is namespaced");
	}
	for (const auto& t : mxl_items::Trophies) {
		Check(t.key != nullptr && std::strncmp(t.key, "medianxl:", 9) == 0 && std::strlen(t.key) > 9, "trophy key is a full key");
	}

	if (failures == 0) {
		std::printf("strings tests passed\n");
	}
	return failures == 0 ? 0 : 1;
}

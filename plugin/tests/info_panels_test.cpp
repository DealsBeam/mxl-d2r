// Tests of the generated help panel layouts (plugin/src/mxl_infographics.h, tools/infographics.py).
#include "../src/client_messages_logic.h"
#include "../src/mxl_infographics.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: info panels: %s\n", what);
	}
}

auto Count(const char* text, const char* what) -> int {
	int n = 0;
	for (const char* p = std::strstr(text, what); p != nullptr; p = std::strstr(p + 1, what)) {
		++n;
	}
	return n;
}

}  // namespace

int RunInfoPanelsTests() {
	using namespace mxl_infographics;
	Check(PanelCount >= 1 && sizeof(Panels) / sizeof(Panels[0]) == static_cast<size_t>(PanelCount), "one layout per panel");
	for (int i = 0; i < PanelCount; ++i) {
		const char* layout = Panels[i].layout;
		char root[64];
		std::snprintf(root, sizeof(root), "\"name\": \"mxl-core/%s\"", Panels[i].localId);
		Check(layout[0] == '{' && std::strstr(layout, "\"type\": \"Panel\"") != nullptr, "the root widget is a Panel");
		Check(std::strstr(layout, root) != nullptr, "the root is named <plugin id>/<local id>, as PanelService needs");
		Check(Count(layout, "{") == Count(layout, "}") && Count(layout, "[") == Count(layout, "]"), "braces and brackets balance");
		Check(Count(layout, "\"TextBoxWidget\"") >= 3, "a title and text blocks");
		Check(std::strstr(layout, "\xC3\xBF" "c") == nullptr, "the widget text is plain (a TextBoxWidget would print colour codes)");
		Check(std::strlen(Panels[i].localId) <= 64 && std::strchr(Panels[i].localId, '/') == nullptr, "a PanelService local id");
	}
	// the catalyst of disenchantment (misc2 row 571, "3o[") is the item with an infographic
	bool catalyst = false;
	for (const Item& item : Items) {
		catalyst = catalyst || (item.code == 0x205b6f33 && item.panel == 0);
		Check(item.panel >= 0 && item.panel < PanelCount, "an item opens a registered panel");
	}
	Check(catalyst, "the catalyst of disenchantment opens panel 0");
	Check(std::strstr(Panels[0].layout, "Catalyst of Disenchantment") != nullptr && std::strstr(Panels[0].layout, "Arcane Crystal") != nullptr,
	      "panel 0 has the catalyst's title and text");
	// the lines of a block are in reading order (Median stores them bottom up)
	const char* first = std::strstr(Panels[0].layout, "Transmute the");
	const char* last = std::strstr(Panels[0].layout, "up to 10 unique items");
	Check(first != nullptr && last != nullptr && first < last, "block lines run in reading order");
	Check(clientmsg::PluginKindInfo != clientmsg::PluginKindNpcPage && clientmsg::PluginKindInfo != clientmsg::PluginKindTimer, "a plugin message kind of its own");
	return failures;
}

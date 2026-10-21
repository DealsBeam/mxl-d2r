// Eight skill pages in the skill tree panel (docs/plugin-spec.md, "Skill tree pages").
//
// SkillsTreePanel (D2Client SkillsTreePanel.cpp; the HD and the classic panel are the same class) is built for
// three pages plus a fourth "tab" for the common / item skills (a controller feature, no widget in the stock layout):
//   - the build function 0x14c4520 walks the pages 3, 2, 1 (r13d, counting down) and gives page p the widget "Tab<n>"
//     with n counting up from 0, so Tab0 holds page 3, Tab1 page 2, Tab2 page 1 (D2R's tab order left to right is
//     the reverse of the SkillPage numbers); each skill whose skilldesc SkillPage equals the page becomes a
//     "SkillButton<page> <col>,<row>" under that tab's "SkillButtonContainer";
//   - the tab setter 0x14c3b10 (the "SkillsTreePanelMessage:ActivateTab:<n>" handler) clamps the index to 0..3, the
//     current tab is the global 0x3bbd8bc; tab 3 is the item-skills tab (seven compares "== 3" decide that the skill
//     counter, the cancel buttons and the skill button draw hooks are skipped for it); the controller bumpers wrap
//     0..3 (0x14c6810) and the tab styling loop covers tabs 0..3 (0x14c789b).
// Median has 8 pages per class, and its page n is the n-th tab left to right. So:
//   - the page loop starts at page 8 and counts the tab widget down (Tab7 .. Tab0), i.e. page p lives in "Tab<p-1>";
//   - the clamp, the controller wrap and the styling loop allow 8 tabs;
//   - the "== 3" compares test 127 (no tab, so the item-skills tab never shows).
// Before this, tabs 4-8 clicked as tab 4, no skill of pages 4-8 had a button, and the three pages that did were
// shown in the reverse order (tab 1 drew the skills of page 3 on the sockets and arrows of page 1).
// Every site is checked byte for byte; all or nothing through one D2RLoader mutation transaction.
#include "skill_tree.h"

#include <D2RLPlugin/mutations.h>

#include <cstdint>
#include <cstdio>

namespace {

struct Site {
	uint64_t rva;
	uint8_t size;
	uint8_t from[40];
	uint8_t to[40];
};

constexpr Site Sites[] = {
	// tab setter: clamp to 0..3 -> 0..7 ("mov eax, 3")
	{0x14c3b28, 4, {0x03, 0x00, 0x00, 0x00}, {0x07, 0x00, 0x00, 0x00}},
	// controller bumper: next tab wraps at 3 -> 7 ("cmp eax, 3"), previous tab wraps to 3 -> 7 ("mov edx, 3")
	{0x14c6bc4, 1, {0x03}, {0x07}},
	{0x14c6bfb, 4, {0x03, 0x00, 0x00, 0x00}, {0x07, 0x00, 0x00, 0x00}},
	// tab styling loop: tabs 0..3 -> 0..7 ("cmp ebx, 4")
	{0x14c789d, 1, {0x04}, {0x08}},
	// "cmp [current tab], 3" (the item-skills tab) -> 127: draw hook, controller prompt, three in the panel
	// refresh, two for the item header
	{0x14c408a, 1, {0x03}, {0x7f}},
	{0x14c742e, 1, {0x03}, {0x7f}},
	{0x14c790d, 1, {0x03}, {0x7f}},
	{0x14c798c, 1, {0x03}, {0x7f}},
	{0x14c7bc6, 1, {0x03}, {0x7f}},
	{0x14c8211, 1, {0x03}, {0x7f}},
	{0x14c8231, 1, {0x03}, {0x7f}},
	// build function: page counter 3 -> 8 (r13d and [rsp+0x3c]), tab widget index counter 0 -> 7 ([rsp+0x38], eax)
	//   was: mov r14d,3 / mov [rbp-0x30],r14 / mov r13d,r14d / mov [rsp+0x3c],r14d / mov byte [rbx+rax],0 /
	//        xor eax,eax / mov [rsp+0x38],eax / mov [rbp+0x130],rbx / nop
	//   now: push 3; pop r14 / mov [rbp-0x30],r14 / push 8; pop r13 / mov [rsp+0x3c],r13d / mov byte [rbx+rax],0 /
	//        mov eax,7 / mov [rsp+0x38],eax / mov [rbp+0x130],rbx / nop
	{0x14c488a, 38,
	 {0x41, 0xbe, 0x03, 0x00, 0x00, 0x00, 0x4c, 0x89, 0x75, 0xd0, 0x45, 0x8b, 0xee, 0x44, 0x89, 0x74, 0x24, 0x3c, 0xc6, 0x04,
	  0x03, 0x00, 0x33, 0xc0, 0x89, 0x44, 0x24, 0x38, 0x48, 0x89, 0x9d, 0x30, 0x01, 0x00, 0x00, 0x0f, 0x1f, 0x00},
	 {0x6a, 0x03, 0x41, 0x5e, 0x4c, 0x89, 0x75, 0xd0, 0x6a, 0x08, 0x41, 0x5d, 0x44, 0x89, 0x6c, 0x24, 0x3c, 0xc6, 0x04, 0x03,
	  0x00, 0xb8, 0x07, 0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x38, 0x48, 0x89, 0x9d, 0x30, 0x01, 0x00, 0x00, 0x90}},
	// build function, loop end: next tab widget index "inc eax" -> "dec eax"
	{0x14c531a, 2, {0xff, 0xc0}, {0xff, 0xc8}},
};

}  // namespace

auto InstallSkillTree(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const D2RL::MutationService* mutations = nullptr;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success || mutations == nullptr ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: no mutation service");
		return false;
	}
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		std::snprintf(report, reportSize, "off: no transaction");
		return false;
	}
	int staged = 0;
	for (const Site& s : Sites) {
		const D2RL::Mutations::BytePatchRequest request {
			.structSize   = D2RL::Mutations::BytePatchRequestSize,
			.flags        = 0,
			.rva          = s.rva,
			.expected     = s.from,
			.expectedSize = s.size,
			.reserved     = 0,
			.bytes        = s.to,
			.size         = s.size,
			.reserved2    = 0,
		};
		D2RL::Mutations::OperationHandle operation = 0;
		if (mutations->stageBytePatch(context, transaction, &request, &operation) != D2RL::Mutations::Result::Success) {
			mutations->cancelTransaction(context, transaction);
			std::snprintf(report, reportSize, "off: site 0x%llx did not match", static_cast<unsigned long long>(s.rva));
			return false;
		}
		++staged;
	}
	D2RL::Mutations::CommitResult result {.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		std::snprintf(report, reportSize, "off: commit failed (reason %u)", static_cast<unsigned>(result.failureReason));
		return false;
	}
	std::snprintf(report, reportSize, "on: %d sites, 8 pages (page n = tab n, no item-skills tab)", staged);
	return true;
}

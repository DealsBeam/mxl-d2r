// Tests of the requirement gates on D2R's NPC table (plugin/src/requirement_gates_logic.h).
#include "../src/requirement_gates_logic.h"

#include <cstdio>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: gates: %s\n", what);
	}
}

using gates::D2rEntry;
using gates::Verdict;

}  // namespace

int RunRequirementGatesTests() {
	// Folding: constants fold, anything that reads the player is dynamic.
	Check(gates::Fold(-1) == Verdict::Allow && gates::Fold(0) == Verdict::Allow, "none allows");
	Check(gates::Fold(32) == Verdict::Deny, "row 32 is always false");
	Check(gates::Fold(9) == Verdict::Dynamic && gates::Fold(14) == Verdict::Dynamic, "quest bit rows are dynamic");
	Check(gates::Fold(1) == Verdict::Dynamic, "class row is dynamic");
	Check(gates::Fold(31) == Verdict::Dynamic && gates::Fold(26) == Verdict::Dynamic, "AND / OR over player rows are dynamic");
	Check(gates::Fold(99) == Verdict::Deny, "a row past the table denies");
	Check(gates::ServiceVerdict(false, -1) == Verdict::Deny, "flag clear: no service");
	Check(gates::ServiceVerdict(true, 32) == Verdict::Deny && gates::ServiceVerdict(true, -1) == Verdict::Allow, "flag set");

	// Hire gate: D2R stores quest id + 1; only "quest bit 0, game difficulty" fits.
	Check(gates::HireGateOf(9) == 3, "row 9 (Blood Raven, quest 2) is gate 3");
	Check(gates::HireGateOf(14) == 37, "row 14 (quest 36) is gate 37");
	Check(gates::HireGateOf(15) == 0, "row 15 reads bit 1: not expressible");
	Check(gates::HireGateOf(19) == 0, "row 19 names a difficulty: not expressible");
	Check(gates::HireGateOf(1) == 0 && gates::HireGateOf(-1) == 0 && gates::HireGateOf(99) == 0, "other rows");

	// The shipped service rows only use none, row 32 (shop) and rows 9 / 14 (hire), and every hire row has a D2R gate.
	for (const mxl_npc::Service& s : mxl_npc::Services) {
		Check(s.shop == -1 || s.shop == 32, "shop rows are none or 32");
		Check(s.gamble == -1, "no gamble requirement in the data");
		Check(s.hire == -1 || gates::HireGateOf(s.hire) != 0, "hire rows fit D2R's quest gate");
	}

	// D2R's table as of build 93847, for the entries the data touches: Atma has no shop, Gheed trades and gambles,
	// Kashya hires behind quest 2, Qual-Kehk behind quest 36.
	auto plan = [](int cls, int hire, int trade, int gamble, int gate) {
		return gates::PlanFor(D2rEntry{static_cast<int16_t>(cls), static_cast<uint8_t>(hire), static_cast<uint8_t>(trade),
		                               static_cast<uint8_t>(gamble), gate});
	};
	{
		const gates::Plan p = plan(176, 0, 0, 0, 0);  // Atma: shop behind row 32, D2R has none
		Check(p.found && !p.clearTrade && !p.clearGamble && !p.clearHire && !p.setHireGate, "Atma: nothing to change");
		Check(p.dynamic == 0, "Atma: no dynamic service");
	}
	{
		const gates::Plan p = plan(176, 0, 1, 0, 0);  // if D2R did give Atma a shop, row 32 closes it
		Check(p.clearTrade && !p.clearGamble, "Atma with a D2R shop: cleared");
	}
	{
		const gates::Plan p = plan(147, 0, 1, 1, 0);  // Gheed
		Check(!p.clearTrade && !p.clearGamble && !p.clearHire && p.dynamic == 0, "Gheed keeps both services");
	}
	{
		const gates::Plan p = plan(150, 1, 0, 0, 3);  // Kashya: gate 3 is row 9
		Check(!p.setHireGate && !p.clearHire && p.dynamic == 0, "Kashya: D2R's gate is row 9");
	}
	{
		const gates::Plan p = plan(150, 1, 0, 0, 0);  // gate lost: restore it
		Check(p.setHireGate && p.hireGate == 3, "Kashya without a gate gets gate 3");
	}
	{
		const gates::Plan p = plan(515, 1, 0, 0, 37);  // Qual-Kehk
		Check(!p.setHireGate && !p.clearHire, "Qual-Kehk: D2R's gate is row 14");
	}
	{
		const gates::Plan p = plan(198, 1, 0, 0, 0);  // Greiz: hirer without a requirement
		Check(!p.setHireGate && !p.clearHire, "Greiz: hire stays open");
	}
	{
		const gates::Plan p = plan(251, 0, 1, 1, 0);  // an NPC Sigma gives no service: both close
		Check(!p.found && p.clearTrade && p.clearGamble, "NPC without a Sigma row: services cleared");
	}
	{
		const gates::Plan p = plan(514, 0, 0, 1, 0);  // Nihlathak: gamble only
		Check(!p.clearGamble && !p.clearTrade, "Nihlathak: gamble only");
	}
	{
		const gates::Plan p = plan(148, 0, 0, 0, 42);  // Akara has no trade in this fixture: Sigma allows one, D2R has none
		Check(p.missing == 1 && !p.clearTrade, "Sigma-only service is counted, not forced on");
	}
	return failures;
}

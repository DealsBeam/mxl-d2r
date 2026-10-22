// Tests of the D2R-native NPC service check (npc_services_logic.h) with the table values read from the dump of build 93847
// (NPC table RVA 0x237ea60, client menu fix table RVA 0x22a70e0). Run through mxl_tests (requirements_test.cpp main).
#include "../src/npc_services_logic.h"

#include <cstdio>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL (npc services): %s\n", what);
	}
}

int SlotOfClass(int cls) {
	for (int i = 0; i < mxl_npc::SlotCount; ++i) {
		if (mxl_npc::Slots[i].npcClass == cls && mxl_npc::Slots[i].sub == 0) {
			return i;
		}
	}
	return -1;
}

}  // namespace

int RunNpcServicesTests() {
	using namespace npcsvc;

	// what Median asks for
	const Expect charsi = ExpectOf(SlotOfClass(154));
	const Expect larzuk = ExpectOf(SlotOfClass(511));
	const Expect anya = ExpectOf(SlotOfClass(512));
	Check(charsi.service == Service::Imbue && charsi.quest == 3 && charsi.bit == 1, "Charsi: imbue, quest 3 bit 1");
	Check(larzuk.service == Service::Sockets && larzuk.quest == 35 && larzuk.bit == 1, "Larzuk: sockets, quest 35 bit 1");
	Check(anya.service == Service::Personalize && anya.quest == 38 && anya.bit == 1, "Anya: personalize, quest 38 bit 1");
	const Expect warriv1 = ExpectOf(SlotOfClass(155));
	const Expect warriv2 = ExpectOf(SlotOfClass(175));
	const Expect meshif2 = ExpectOf(SlotOfClass(210));
	const Expect meshif3 = ExpectOf(SlotOfClass(264));
	const Expect tyrael = ExpectOf(SlotOfClass(367));
	Check(warriv1.service == Service::Travel && warriv1.dest == 40 && warriv1.quest == 6 && warriv1.bit == 0 && warriv1.wantsWaypoint,
	      "Warriv act 1: level 40 with waypoint, quest 6 bit 0");
	Check(warriv2.service == Service::Travel && warriv2.dest == 1 && warriv2.quest == -1 && !warriv2.wantsWaypoint,
	      "Warriv act 2: level 1, no gate, no waypoint");
	Check(meshif2.service == Service::Travel && meshif2.dest == 75 && meshif2.quest == 14 && meshif2.wantsWaypoint,
	      "Meshif act 2: level 75 with waypoint, quest 14");
	Check(meshif3.service == Service::Travel && meshif3.dest == 40 && meshif3.quest == -1 && !meshif3.wantsWaypoint,
	      "Meshif act 3: level 40, no gate, no waypoint");
	Check(tyrael.service == Service::Travel && tyrael.dest == 109 && tyrael.quest == 28 && tyrael.wantsWaypoint,
	      "Tyrael: level 109 with waypoint, quest 28");
	Check(ExpectOf(SlotOfClass(148)).service == Service::None && ExpectOf(SlotOfClass(244)).service == Service::None,
	      "Akara and Cain are not native services");
	{
		int native = 0;
		for (int i = 0; i < mxl_npc::SlotCount; ++i) {
			native += ExpectOf(i).service != Service::None ? 1 : 0;
		}
		Check(native == 8, "eight native service slots");
	}

	// the game's tables (build 93847): NPC table {service, destination, gate}, fix table {quest, bit, cond}
	const D2rFix none;
	Check(Judge(charsi, {true, 1, 0, 4}, {true, 3, 1, 1}) == Verdict::Agree, "Charsi agrees with D2R");
	Check(Judge(larzuk, {true, 2, 0, 36}, {true, 35, 1, 1}) == Verdict::Agree, "Larzuk agrees with D2R");
	Check(Judge(anya, {true, 3, 0, 39}, {true, 38, 1, 1}) == Verdict::Agree, "Anya agrees with D2R");
	Check(Judge(warriv1, {true, 0, 40, 7}, {true, 6, 0, 1}) == Verdict::Agree, "Warriv act 1: D2R gates on quest 6 and grants the waypoint");
	Check(Judge(warriv2, {true, 0, 1, 0}, none) == Verdict::Agree, "Warriv act 2 agrees");
	Check(Judge(meshif2, {true, 0, 75, 15}, {true, 14, 0, 1}) == Verdict::Agree, "Meshif act 2 agrees");
	Check(Judge(meshif3, {true, 0, 40, 0}, none) == Verdict::Agree, "Meshif act 3 agrees");
	// D2R gates Tyrael on quest 26 (gate 27); Median's row 13 reads quest 28 (gate 29): the table byte is moved at load
	Check(Judge(tyrael, {true, 0, 109, 27}, none) == Verdict::GateDiffers, "Tyrael: D2R's gate is quest 26, Median's is 28");
	Check(TravelGateFix(tyrael, {true, 0, 109, 27}) == 29, "Tyrael: the gate byte becomes 29 (quest 28)");
	Check(Judge(tyrael, {true, 0, 109, 29}, none) == Verdict::Agree, "Tyrael agrees once the gate is 29");
	Check(TravelGateFix(tyrael, {true, 0, 109, 29}) == -1, "Tyrael: nothing to fix at 29");
	Check(TravelGateFix(warriv1, {true, 0, 40, 7}) == -1 && TravelGateFix(meshif2, {true, 0, 75, 15}) == -1,
	      "Warriv act 1 and Meshif act 2 already match: no write");
	Check(TravelGateFix(warriv2, {true, 0, 1, 0}) == -1 && TravelGateFix(meshif3, {true, 0, 40, 0}) == -1,
	      "ungated west trips are not gated by the fix");
	Check(TravelGateFix(tyrael, {true, 0, 108, 27}) == -1 && TravelGateFix(tyrael, {true, 0, 109, 0}) == -1 &&
	          TravelGateFix(tyrael, {false, 0, 0, 0}) == -1,
	      "a different destination, an ungated line or a missing entry is left alone");
	Check(TravelGateFix(charsi, {true, 1, 0, 5}) == -1, "services other than travel are never rewritten");

	// drift is noticed
	Check(Judge(charsi, {false, 0, 0, 0}, {true, 3, 1, 1}) == Verdict::NoNpcEntry, "no NPC table entry");
	Check(Judge(charsi, {true, 2, 0, 4}, {true, 3, 1, 1}) == Verdict::ServiceDiffers, "wrong service number");
	Check(Judge(charsi, {true, 1, 0, 5}, {true, 3, 1, 1}) == Verdict::GateDiffers, "wrong gate");
	Check(Judge(charsi, {true, 1, 0, 4}, none) == Verdict::NoClientLine, "no client line");
	Check(Judge(charsi, {true, 1, 0, 4}, {true, 3, 0, 1}) == Verdict::ClientLineDiffers, "client line on another bit");
	Check(Judge(warriv1, {true, 0, 41, 7}, {true, 6, 0, 1}) == Verdict::DestDiffers, "wrong destination");
	Check(Judge(warriv1, {true, 0, 40, 0}, none) == Verdict::GateDiffers, "ungated D2R travel where Median gates");
	Check(Judge(warriv2, {true, 0, 1, 5}, none) == Verdict::GateDiffers, "gated D2R travel where Median does not");
	Check(Judge(meshif2, {true, 0, 75, 15}, {true, 14, 1, 1}) == Verdict::ClientLineDiffers, "travel line on another bit");

	return failures;
}

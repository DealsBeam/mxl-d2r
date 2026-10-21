// Tests of Median's mercenary class bonuses and the hire gate edit (plugin/src/hirelings_logic.h).
#include "../src/hirelings_logic.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
	if (!ok) {
		++failures;
		std::printf("FAIL: hirelings: %s\n", what);
	}
}

using hire::Bonuses;
using hire::Kind;
using namespace mxl_hirelings;

auto Value(const Bonuses& b, int32_t stat) -> int32_t {
	int32_t last = -1000000;  // the last set wins, as on the mercenary
	for (size_t i = 0; i < b.count; ++i) {
		if (b.sets[i].stat == stat) {
			last = b.sets[i].value;
		}
	}
	return last;
}

auto Has(const Bonuses& b, int32_t stat) -> bool {
	for (size_t i = 0; i < b.count; ++i) {
		if (b.sets[i].stat == stat) {
			return true;
		}
	}
	return false;
}

}  // namespace

int RunHirelingsTests() {
	// classes come from the migrated hireling.txt
	Check(hire::KindOf(ClassRogue) == Kind::Rogue && hire::KindOf(ClassDesert) == Kind::Desert, "rogue, desert classes");
	Check(hire::KindOf(ClassIronWolf) == Kind::IronWolf && hire::KindOf(ClassBarbarian) == Kind::Barbarian, "wolf, barbarian");
	Check(hire::KindOf(0) == Kind::None && hire::KindOf(560) == Kind::None, "other monsters get nothing");
	Check(hire::Compute(Kind::None, 30, 0, 100, 100).count == 0, "no bonuses for no class");

	// D2R's attribute rule: base + delta * perLevel / 8, at least 10
	Check(hire::Attribute(35, 10, 0) == 35, "attribute at the row level");
	Check(hire::Attribute(35, 10, 8) == 45, "attribute per 8 levels");
	Check(hire::Attribute(35, 10, 7) == 43, "attribute truncates");
	Check(hire::Attribute(5, 0, 3) == 10, "attribute floor");
	Check(hire::Regen(100) == 80 * 256 && hire::Regen(35) == 28 * 256, "regeneration units");

	// rogue (level 30, strength 60, dexterity 120)
	{
		const Bonuses b = hire::Compute(Kind::Rogue, 30, 0, 60, 120);
		Check(Value(b, StatDamageResist) == 7, "rogue damage resist level / 4");
		Check(Value(b, StatDamagePercent) == 60, "rogue damage from dexterity");
		Check(Value(b, StatHpRegen) == 96 * 256, "rogue regeneration from dexterity");
		Check(Value(b, StatVelocity) == 100, "rogue speed");
		Check(Value(b, StatDodge) == 3, "rogue dodge dexterity / 40");
		Check(Value(b, StatEnergy) == (30 + 4) * 6, "rogue energy");
		Check(Value(b, StatSlowsAttacker) == 30 / 5 + 25, "rogue slows attacker");
		Check(Value(b, StatPoisonLengthResist) == 200 && Value(b, StatAddExperience) == 300, "common stats");
		Check(!Has(b, StatSkillArmor) && !Has(b, StatFireMastery), "rogue has no armor or masteries");
	}
	Check(Value(hire::Compute(Kind::Rogue, 90, 0, 60, 4000), StatDodge) == 60, "rogue dodge cap 60");
	// desert: strength based, masteries only for the dm3 hirelings
	{
		const Bonuses b = hire::Compute(Kind::Desert, 40, 6, 200, 50);
		Check(Value(b, StatSkillArmor) == 40, "desert skill armor");
		Check(Value(b, StatDamagePercent) == 100 && Value(b, StatHpRegen) == 160 * 256, "desert from strength");
		Check(Value(b, StatVelocity) == 80, "desert speed");
		Check(Value(b, StatEnergy) == 40 * 4 + 16, "desert energy");
		Check(Value(b, StatCrushingBlow) == 40 / 10 + 15 && Value(b, StatDamageResist) == 40 / 5 + 25, "desert crushing, resist");
		Check(!Has(b, StatFireMastery) && !Has(b, StatColdMastery), "type 6 has no masteries");
	}
	for (const int32_t id : MasteryHirelingIds) {
		const Bonuses b = hire::Compute(Kind::Desert, 40, id, 200, 50);
		Check(Value(b, StatFireMastery) == 40 && Value(b, StatColdMastery) == 40 && !Has(b, StatLightningMastery),
		      "dm3 hirelings get fire and cold mastery");
	}
	Check(sizeof(MasteryHirelingIds) / sizeof(MasteryHirelingIds[0]) == 3, "three dm3 hireling ids");
	// iron wolf: the later skill armor wins
	{
		const Bonuses b = hire::Compute(Kind::IronWolf, 50, 15, 100, 100);
		Check(Value(b, StatSkillArmor) == (50 + 60) * 5, "wolf skill armor is the level based one");
		Check(Value(b, StatEnergy) == (50 + 9) * 20 && Value(b, StatMedian485) == (50 + 50) * 5, "wolf energy, stat 485");
		Check(Value(b, StatFireMastery) == 50 && Value(b, StatColdMastery) == 50 && Value(b, StatLightningMastery) == 50,
		      "wolf masteries");
		Check(!Has(b, StatCrushingBlow), "wolf has no crushing blow");
	}
	// barbarian
	{
		const Bonuses b = hire::Compute(Kind::Barbarian, 60, 24, 180, 40);
		Check(Value(b, StatEnergy) == 2 * 60 + 8, "barbarian energy");
		Check(Value(b, StatToStrength) == 60 / 4 + 20 && Value(b, StatToDexterity) == 60 / 3 + 20, "barbarian strength, dexterity");
		Check(Value(b, StatMedian184) == 20, "barbarian stat 184");
		Check(Value(b, StatDamagePercent) == 90 && Value(b, StatVelocity) == 80, "barbarian shared part");
		Check(b.count <= Bonuses::Capacity, "list fits");
	}

	// Kashya's level gate: only the value D2R ships is cleared, only for that class
	{
		uint8_t table[hire::NpcRecordLayout::Size * 3] = {};
		auto entry = [&](int i) { return table + i * hire::NpcRecordLayout::Size; };
		entry(0)[0] = 148;
		entry(1)[0] = 150;
		entry(1)[hire::NpcRecordLayout::QuestGate] = 3;
		entry(1)[hire::NpcRecordLayout::LevelGate] = 8;
		entry(2)[0] = 198;
		entry(2)[hire::NpcRecordLayout::LevelGate] = 8;
		Check(hire::ClearLevelGate(table, 3, 150, 3, 8), "Kashya's byte is cleared");
		Check(entry(1)[hire::NpcRecordLayout::LevelGate] == 0 && entry(1)[hire::NpcRecordLayout::QuestGate] == 3,
		      "quest gate kept, level gate gone");
		Check(entry(2)[hire::NpcRecordLayout::LevelGate] == 8, "other NPCs untouched");
		Check(!hire::ClearLevelGate(table, 3, 150, 3, 8), "second run: value no longer D2R's, nothing done");
		Check(!hire::ClearLevelGate(table, 3, 151, 3, 8), "unknown class");
	}
	return failures;
}

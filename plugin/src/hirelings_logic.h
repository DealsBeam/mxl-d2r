// Median's mercenary class bonuses, pure arithmetic (D2Sigma D2Hirelings.cpp FUN_10066900, run once on every hire;
// docs/subsystems/npc-hirelings.md 2.2). No game types, so plugin/tests can build it.
//
// D2Sigma sets (not adds) a handful of stats on the new mercenary with D2Common 10887, from the mercenary's level, its
// strength / dexterity and its hireling id. Order and values below are the decompile's; "set" makes the list safe to run
// again after a level change, which is how the plugin uses it (every time D2R re-derives the mercenary's stats).
#pragma once

#include "mxl_hirelings.h"

#include <cstddef>
#include <cstdint>

namespace hire {

enum class Kind { None, Rogue, Desert, IronWolf, Barbarian };

struct StatSet {
	int32_t stat;
	int32_t value;
};

struct Bonuses {
	static constexpr size_t Capacity = 16;
	StatSet sets[Capacity];
	size_t count = 0;

	void Add(int32_t stat, int32_t value) noexcept {
		if (count < Capacity) {
			sets[count++] = {stat, value};
		}
	}
};

constexpr auto KindOf(int32_t monsterClass) noexcept -> Kind {
	if (monsterClass == mxl_hirelings::ClassRogue) return Kind::Rogue;
	if (monsterClass == mxl_hirelings::ClassDesert) return Kind::Desert;
	if (monsterClass == mxl_hirelings::ClassIronWolf) return Kind::IronWolf;
	if (monsterClass == mxl_hirelings::ClassBarbarian) return Kind::Barbarian;
	return Kind::None;
}

constexpr auto HasMasteries(int32_t hirelingId) noexcept -> bool {
	for (const int32_t id : mxl_hirelings::MasteryHirelingIds) {
		if (id == hirelingId) {
			return true;
		}
	}
	return false;
}

// D2R's own per-level attribute (0x544f60): base + (level - row level) * perLevel / 8, at least 10. Strength and
// dexterity of the hireling.txt row, so the result does not include what the mercenary wears.
constexpr auto Attribute(int32_t base, int32_t perLevel, int32_t levelDelta) noexcept -> int32_t {
	const int32_t value = base + (levelDelta * perLevel) / 8;
	return value < 10 ? 10 : value;
}

// "(attribute * 8 / 10) * 256": Median's hit point regeneration, in the stat's 1/256 units
constexpr auto Regen(int32_t attribute) noexcept -> int32_t {
	return ((attribute * 8) / 10) * 256;
}

constexpr auto Min(int32_t a, int32_t b) noexcept -> int32_t {
	return a < b ? a : b;
}

// What D2Sigma sets on a mercenary of this class, in its order. Rogue scales with dexterity, the others with strength.
inline auto Compute(Kind kind, int32_t level, int32_t hirelingId, int32_t strength, int32_t dexterity) noexcept -> Bonuses {
	using namespace mxl_hirelings;
	Bonuses b;
	if (kind == Kind::None || level < 0) {
		return b;
	}
	if (kind == Kind::Rogue) {
		b.Add(StatDamageResist, level / 4);
		b.Add(StatDamagePercent, dexterity / 2);
		b.Add(StatHpRegen, Regen(dexterity));
		b.Add(StatVelocity, 100);
		b.Add(StatDodge, Min(dexterity / 40, 60));
	} else {
		b.Add(StatSkillArmor, 40);
		b.Add(StatDamagePercent, strength / 2);
		b.Add(StatHpRegen, Regen(strength));
		b.Add(StatVelocity, 80);
	}
	b.Add(StatPoisonLengthResist, 200);
	b.Add(StatAddExperience, level * 10);
	switch (kind) {
	case Kind::Rogue:
		b.Add(StatEnergy, (level + 4) * 6);
		b.Add(StatSlowsAttacker, level / 5 + 25);
		break;
	case Kind::Desert:
		b.Add(StatEnergy, level * 4 + 16);
		b.Add(StatCrushingBlow, level / 10 + 15);
		b.Add(StatDamageResist, level / 5 + 25);
		if (HasMasteries(hirelingId)) {
			b.Add(StatFireMastery, level);
			b.Add(StatColdMastery, level);
		}
		break;
	case Kind::IronWolf:
		b.Add(StatEnergy, (level + 9) * 20);
		b.Add(StatSkillArmor, (level + 60) * 5);
		b.Add(StatMedian485, (level + 50) * 5);
		b.Add(StatFireMastery, level);
		b.Add(StatColdMastery, level);
		b.Add(StatLightningMastery, level);
		break;
	case Kind::Barbarian:
		b.Add(StatEnergy, level * 2 + 8);
		b.Add(StatToStrength, level / 4 + 20);
		b.Add(StatToDexterity, level / 3 + 20);
		b.Add(StatMedian184, 20);
		break;
	case Kind::None:
		break;
	}
	return b;
}

// Median's mercenary equip rules (D2Items 0x1007e470, docs/subsystems/npc-hirelings.md 2.4 item 7). Beyond the ordinary
// requirements the item must pass the hire class's lists (0x10066fd0): an item of an excluded type (hireclasses list B) is
// refused, an item of a listed type (list A) is allowed, anything else is refused. The class lock (item stat 492) is
// compared with the class the mercenary equips as (hireclasses +4) instead of a player's class.
constexpr auto MercClassOf(int32_t monsterClass) noexcept -> const mxl_hirelings::MercClass* {
	for (const auto& c : mxl_hirelings::MercClasses) {
		if (c.monsterClass == monsterClass) {
			return &c;
		}
	}
	return nullptr;
}

// isType(typeId): the item is of that item type (D2R's 0x373890, which follows the parent types)
template <typename IsType>
constexpr auto MercMayWield(const mxl_hirelings::MercClass& c, IsType&& isType) -> bool {
	for (const int32_t t : c.deny) {
		if (t < 0) {
			break;
		}
		if (isType(t)) {
			return false;
		}
	}
	for (const int32_t t : c.allow) {
		if (t < 0) {
			break;
		}
		if (isType(t)) {
			return true;
		}
	}
	return false;
}

// Kashya's hire gate. D2R's NPC table (0x237ea60, 0x1c bytes per NPC) lets a hirer through when the quest is done or the
// player is at least the level in byte +0x11; Median's hirer gate is the quest alone (npcext requirement 9 for Kashya).
// Clears that level byte for the NPC class when it holds the value D2R ships, and says whether it did.
struct NpcRecordLayout {
	static constexpr size_t Size = 0x1c;
	static constexpr size_t QuestGate = 0x10;
	static constexpr size_t LevelGate = 0x11;
};

inline auto ClearLevelGate(uint8_t* table, size_t entries, int16_t npcClass, uint8_t expectedQuestGate,
                           uint8_t expectedLevel) noexcept -> bool {
	for (size_t i = 0; i < entries; ++i) {
		uint8_t* entry = table + i * NpcRecordLayout::Size;
		const int16_t id = static_cast<int16_t>(entry[0] | (entry[1] << 8));
		if (id != npcClass) {
			continue;
		}
		if (entry[NpcRecordLayout::QuestGate] != expectedQuestGate || entry[NpcRecordLayout::LevelGate] != expectedLevel) {
			return false;
		}
		entry[NpcRecordLayout::LevelGate] = 0;
		return true;
	}
	return false;
}

}  // namespace hire

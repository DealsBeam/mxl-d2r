// Median's item generation rules on D2R's functions (docs/subsystems/items-xp.md section 5).
//
// Class lock. Median's requirement check (D2Items.cpp 0x1007e470) also fails when the item carries stat 463 (D2R 492) =
// class + 1 and the player is another class; the 14 recipes `properties_422` lock a unique to a class that way. D2R keeps the
// stat but nobody reads it. Hook: the item requirement check 0x36bc50 (item, unit, flag, &strOk, &dexOk, &levelOk, flag2),
// the original runs first and a success is turned into a failure for a player of another class. A mercenary gets the same
// lock against the class it equips as (hireclasses +4) plus Median's weapon / shield type lists (hirelings_logic.h,
// hireclasses StateListA/B, which are item type ids: 0x1007e470 -> 0x10066fd0 -> 0xbf950 "item is of type").
//
// Ethereal. D2R's generator 0x442d60 (1.13c 0x10a20) lets 5 of 100 items of item level 100+ become ethereal through 0x3d3510
// (+50% damage / defence) and then halves the durability. Median's 0x1007e120 rolls `rand(1000) <= qualities.txt[quality].word2`
// (25, so 2.6%), makes the item ethereal with +25% and gives it 75% durability. Hook A wraps 0x442d60 and remembers the item and
// the generator record; hook B replaces 0x3d3510 while that generator runs: the items that passed D2R's 5% are accepted with
// probability 26/50 (Median's overall rate), accepted items get Median's effect, and after the generator returns hook A sets the
// durability to 75% of the maximum read before D2R's halving (or restores it for the items that were not accepted).
// Forced ethereal (generator flags +0xc0 bit 4, +0x54 bit 0x400000) always applies.
//
// Sockets. Median's 0x1007e240 (the call that replaces 1.13c's socket roll for quality 1-3 items) gives items whose base can
// have sockets (and without the generator's no-socket flag +0xc0 bit 8) `rand() % 7` sockets, none for a remainder of 0. D2R's
// own roll is 0x441fc0 (called for the same qualities from 0x442d60 and 0x443670): 33 in 100 items get rand(max) + 1 sockets.
// Hook: 0x441fc0 replaced by Median's roll, with D2R's setter 0x375560 doing the clipping; forced sockets (generator flags
// +0xc0 bit 0x10, +0x54 bit 0x800) keep D2R's own code.
//
// Dye. Median's item colour 0x1007e6d0 paints an item with the colour index in stat 183 (D2R 368, 1..21) instead of the base's.
// D2R's colour function 0x3710d0 (player, item, &colourByte, mode) writes (transform kind << 5) + colour into the byte and
// returns the palette entry 0x145dfb0(kind, colour); the dye hook does the same with the dye's colour.
//
// Entry bytes checked against the dump of build 93847; whole instructions, no rip-relative.
#include "item_gen.h"

#include "d2r.h"
#include "hirelings_logic.h"
#include "item_gen_logic.h"
#include "item_rules_logic.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Unit;

constexpr uint64_t RequirementRva = 0x36bc50;    // (item, unit, flag, &strOk, &dexOk, &levelOk, flag2) -> 1 when usable
constexpr uint64_t GenerateRva = 0x442d60;       // (game, &item, genRecord) -> 1: quality dispatch of the item generator
constexpr uint64_t MakeEtherealRva = 0x3d3510;   // (item): ethereal flag and +50% damage / defence
constexpr uint64_t SocketRollRva = 0x441fc0;     // (game, item, genRecord): D2R's socket roll for quality 1-3
constexpr uint64_t ItemColourRva = 0x3710d0;     // (player, item, &colourByte, mode) -> palette entry or 0
constexpr uint64_t PaletteRva = 0x145dfb0;       // (kind, colour) -> palette entry
constexpr uint64_t UnitTypeRva = 0x34b9d0;       // (unit) -> 0 player .. 4 item
constexpr uint64_t UnitClassRva = 0x349860;      // (unit, file, line) -> class id
constexpr uint64_t UnitDataFlagRva = 0x34a0e0;   // (unit) -> data bank byte
constexpr uint64_t ItemRecordRva = 0x314110;     // (dataFlag, class id) -> items record
constexpr uint64_t GetStatRva = 0x2f5020;        // (unit, stat, layer)
constexpr uint64_t SetStatRva = 0x2f7940;        // (unit, stat, value, layer)
constexpr uint64_t GetBaseStatRva = 0x2f48c0;    // (unit, stat, layer): loader slot
constexpr uint64_t SetBaseStatRva = 0x2f7d10;    // (unit, stat, value, layer): loader slot
constexpr uint64_t SetItemFlagRva = 0x36d8f0;    // (item, mask, on)
constexpr uint64_t IsItemTypeRva = 0x373890;     // (item, type)
constexpr uint64_t UnitSeedRva = 0x34a1e0;       // (unit) -> &seed
constexpr uint64_t RandRva = 0x153b00;           // (&seed, n) -> 0..n-1
constexpr uint64_t QualityRva = 0x36cf60;        // (item) -> quality
constexpr uint64_t HasDurabilityRva = 0x373540;  // (item)
constexpr uint64_t CanSocketRva = 0x373610;      // (item): the base can have sockets (items record +0x147)
constexpr uint64_t NoSocketBaseRva = 0x374660;   // (item): items record +0x142, nonzero keeps the item without sockets
constexpr uint64_t ItemLevelRva = 0x3730d0;      // (item) -> item level (item data +0x40)
constexpr uint64_t NoEtherealBaseRva = 0x3744d0; // (item): items record +0x13a, nonzero keeps D2R's generator from making it ethereal
constexpr uint64_t SetSocketsRva = 0x375560;     // (item, count): sets stat 0xc2, clipped to what the item can hold

constexpr int GenFlags = 0xc0;       // generator record: bit 2 no ethereal, 4 force ethereal, 8 no sockets, 0x10 force sockets
constexpr int GenItemFlags = 0x54;   // generator record: item flag word (0x400000 ethereal, 0x800 socketed)
constexpr int ItColourKind = 0x151;  // items record: transform kind of the character colour (1..8)
constexpr int ItColourKindInv = 0x152;
constexpr int32_t StatDye = 368;
constexpr int32_t StatClassLock = 492;
constexpr int32_t StatDurability = 0x48;
constexpr int32_t StatMaxDurability = 0x49;
constexpr int32_t StatDefence = 0x1f;
constexpr int32_t WeaponStats[] = {0x15, 0x16, 0x17, 0x18, 0x9f, 0xa0};
constexpr int32_t ItemTypeWeapon = 0x2d;
constexpr int32_t ItemTypeArmour = 0x32;
constexpr int32_t ItemTypeShield = 2;  // itemtypes row `shie`
constexpr uint32_t FlagEthereal = 0x400000;
constexpr uint32_t FlagSocketed = 0x800;
constexpr int UnitTypePlayer = 0;
constexpr int UnitTypeMonster = 1;
constexpr int UnitTypeItem = 4;
constexpr const char* NoFile = "mxl-core item_gen";

using RequirementFn = uint64_t (*)(Unit* item, Unit* unit, int32_t a3, uint32_t* r4, uint32_t* r5, uint32_t* r6, int32_t a7);
using GenerateFn = uint64_t (*)(void* game, Unit** item, uint8_t* gen);
using EtherealFn = void (*)(Unit* item);
using SocketRollFn = void (*)(void* game, Unit* item, uint8_t* gen);
using ColourFn = int64_t (*)(Unit* player, Unit* item, uint8_t* colour, int32_t mode);
using PaletteFn = int64_t (*)(uint8_t kind, uint8_t colour);
using UnitTypeFn = int32_t (*)(Unit* unit);
using UnitClassFn = int32_t (*)(Unit* unit, const char* file, int32_t line);
using DataFlagFn = uint64_t (*)(Unit* unit);
using ItemRecordFn = const uint8_t* (*)(uint64_t dataFlag, int32_t classId);
using GetStatFn = int32_t (*)(Unit* unit, int32_t stat, int32_t layer);
using SetStatFn = void (*)(Unit* unit, int32_t stat, int32_t value, int32_t layer);
using SetFlagFn = void (*)(Unit* item, uint32_t mask, int32_t on);
using IsTypeFn = int32_t (*)(Unit* item, int32_t type);
using SeedFn = void* (*)(Unit* unit);
using RandFn = int32_t (*)(void* seed, int32_t n);
using ItemFn = int32_t (*)(Unit* item);
using SetSocketsFn = void (*)(Unit* item, uint32_t count);
using ItemLevelFn = uint16_t (*)(Unit* item);

RequirementFn g_originalRequirement = nullptr;
GenerateFn g_originalGenerate = nullptr;
bool g_etherealOn = false;   // Median's ethereal roll in the generator hook
bool g_identifyDrops = false;  // magic, set, rare and unique items are born identified
EtherealFn g_originalEthereal = nullptr;
SocketRollFn g_originalSocketRoll = nullptr;
ColourFn g_originalColour = nullptr;
PaletteFn g_palette = nullptr;
UnitTypeFn g_unitType = nullptr;
UnitClassFn g_unitClass = nullptr;
DataFlagFn g_dataFlag = nullptr;
ItemRecordFn g_itemRecord = nullptr;
GetStatFn g_getStat = nullptr;
SetStatFn g_setStat = nullptr;
GetStatFn g_getBase = nullptr;
SetStatFn g_setBase = nullptr;
SetFlagFn g_setFlag = nullptr;
IsTypeFn g_isType = nullptr;
SeedFn g_seed = nullptr;
RandFn g_rand = nullptr;
ItemFn g_quality = nullptr;
ItemFn g_hasDurability = nullptr;
ItemFn g_canSocket = nullptr;
ItemFn g_noSocketBase = nullptr;
SetSocketsFn g_setSockets = nullptr;
ItemLevelFn g_itemLevel = nullptr;
ItemFn g_noEtherealBase = nullptr;

int g_lockChecked = 0;
int g_lockDenied = 0;
int g_mercChecked = 0;
int g_mercDenied = 0;
int g_etherealSeen = 0;
int g_etherealMade = 0;
int g_etherealLow = 0;
int g_gateGenerated = 0;
int g_gateLevel = 0;
int g_gateType = 0;
int g_gateDurable = 0;
int g_gateQuality = 0;
int g_gateBase = 0;
int g_levelIn = -1;
int g_levelOut = -1;
int g_qualityIn = -1;
int g_qualityOut = -1;
int g_socketsRolled = 0;
int g_socketsGiven = 0;
int g_dyed = 0;

// 0x36bc50: mov [rsp+0x20],r9 / mov [rsp+0x18],r8d
constexpr uint8_t RequirementEntry[] = {0x4c, 0x89, 0x4c, 0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18};
// 0x442d60: push rbx / push rbp / push r14 / sub rsp,0x50
constexpr uint8_t GenerateEntry[] = {0x40, 0x53, 0x55, 0x41, 0x56, 0x48, 0x83, 0xec, 0x50};
// 0x3d3510: mov [rsp+0x10],rbx / push rdi / sub rsp,0x20
constexpr uint8_t EtherealEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20};
// 0x441fc0: mov [rsp+0x10],rbp / mov [rsp+0x18],rsi / push rdi / sub rsp,0x20
constexpr uint8_t SocketRollEntry[] = {0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20};
// 0x3710d0: mov [rsp+8],rbx / mov [rsp+0x20],r9d / push rbp / push rsi / push rdi
constexpr uint8_t ColourEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x44, 0x89, 0x4c, 0x24, 0x20, 0x55, 0x56, 0x57};

auto Matches(uint64_t base, uint64_t rva, const uint8_t* bytes, size_t size) noexcept -> bool {
	return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes, size) == 0;
}

// --- Class lock ---------------------------------------------------------------------------------------------------

// A mercenary (a monster of one of the four hire classes): Median's class lock against the class it equips as, and its
// weapon / shield type lists (hirelings_logic.h). Armour is left to D2R, whose mercenary has the slots for it.
uint64_t MercRequirement(Unit* item, Unit* merc, uint64_t result) {
	const auto* hireClass = hire::MercClassOf(g_unitClass(merc, NoFile, 0));
	if (hireClass == nullptr) {
		return result;
	}
	++g_mercChecked;
	const int32_t lock = g_getStat(item, StatClassLock, 0);
	if (lock != 0 && !item_gen::ClassLockAllows(lock, hireClass->equivalentClass)) {
		++g_mercDenied;
		return 0;
	}
	if (g_isType(item, ItemTypeWeapon) != 0 || g_isType(item, ItemTypeShield) != 0) {
		if (!hire::MercMayWield(*hireClass, [item](int32_t type) { return g_isType(item, type) != 0; })) {
			++g_mercDenied;
			return 0;
		}
	}
	return result;
}

uint64_t RequirementHook(Unit* item, Unit* unit, int32_t a3, uint32_t* r4, uint32_t* r5, uint32_t* r6, int32_t a7) {
	const uint64_t result = g_originalRequirement(item, unit, a3, r4, r5, r6, a7);
	if ((result & 0xff) == 0 || item == nullptr || unit == nullptr) {
		return result;
	}
	if (g_unitType(item) != UnitTypeItem) {
		return result;
	}
	const int unitType = g_unitType(unit);
	if (unitType == UnitTypeMonster) {
		return MercRequirement(item, unit, result);
	}
	if (unitType != UnitTypePlayer) {
		return result;
	}
	++g_lockChecked;
	const int32_t lock = g_getStat(item, StatClassLock, 0);
	if (lock == 0 || item_gen::ClassLockAllows(lock, g_unitClass(unit, NoFile, 0))) {
		return result;
	}
	++g_lockDenied;
	return 0;
}

// --- Ethereal -----------------------------------------------------------------------------------------------------

struct GenState {
	Unit** item = nullptr;
	const uint8_t* gen = nullptr;
	bool touched = false;
	bool accepted = false;
	int32_t maxDurability = 0;
};
thread_local GenState* g_state = nullptr;

// How many generated items pass each part of D2R's ethereal gate in 0x442d60 (item level 100+, weapon or armour type,
// durability, quality other than 1 and 5, the base's flag at items record +0x13a clear), for the itemroll check.
void ProbeGate(Unit* item, const uint8_t* gen) {
	++g_gateGenerated;
	if (g_itemLevel(item) < 100 || (d2r::At<uint8_t>(gen, GenFlags) & 2) != 0) {
		return;
	}
	++g_gateLevel;
	if (g_isType(item, ItemTypeWeapon) == 0 && g_isType(item, ItemTypeArmour) == 0) {
		return;
	}
	++g_gateType;
	if (g_hasDurability(item) == 0) {
		return;
	}
	++g_gateDurable;
	const int32_t quality = g_quality(item);
	if (quality == 1 || quality == 5) {
		return;
	}
	++g_gateQuality;
	if (g_noEtherealBase(item) == 0) {
		++g_gateBase;
	}
}

void MakeEthereal(Unit* item);

// D2R's gate (item level 100+, no generator flag 2, weapon or armour with durability and a base that allows it) never lets
// quality 1 (low) through to 0x3d3510, Median's roll does (qualities word2 = 25 for quality 1 as for the others). Items of
// that quality get Median's roll here, after the generator, with the same conditions as D2R's gate.
bool GatePasses(Unit* item, const uint8_t* gen) {
	return g_itemLevel(item) >= 100 && (d2r::At<uint8_t>(gen, GenFlags) & 2) == 0 &&
	       (g_isType(item, ItemTypeWeapon) != 0 || g_isType(item, ItemTypeArmour) != 0) && g_hasDurability(item) != 0 &&
	       g_noEtherealBase(item) == 0;
}

void RollLowQualityEthereal(Unit* item, const uint8_t* gen) {
	if (g_quality(item) != 1 || !GatePasses(item, gen)) {
		return;
	}
	const bool forced = (d2r::At<uint8_t>(gen, GenFlags) & 4) != 0 || (d2r::At<uint32_t>(gen, GenItemFlags) & FlagEthereal) != 0;
	if (!forced && !item_gen::EtherealRolls(item_gen::EtherealChancePerMille(1), g_rand(g_seed(item), item_gen::MedianEtherealDie))) {
		return;
	}
	const int32_t maxDurability = g_getStat(item, StatMaxDurability, 0);
	MakeEthereal(item);
	++g_etherealLow;
	if (maxDurability > 0) {
		const int32_t value = item_gen::EtherealDurability(maxDurability);
		g_setStat(item, StatMaxDurability, value, 0);
		g_setStat(item, StatDurability, value, 0);
	}
}

// Audit IT3: Median patches the magic, rare, set and unique generators (D2Game 0x104c1, 0x15443, 0x13daa and the unique
// picker) to push 1 instead of 0 into SetItemFlag(item, 0x10 identified): those items are born identified.
void IdentifyDrop(Unit** item, uint64_t result) {
	if (g_identifyDrops && result != 0 && item != nullptr && *item != nullptr && itemrules::DropsIdentified(g_quality(*item))) {
		g_setFlag(*item, itemrules::ItemFlagIdentified, 1);
	}
}

// Hook A.
uint64_t GenerateHook(void* game, Unit** item, uint8_t* gen) {
	if (!g_etherealOn) {
		const uint64_t plain = g_originalGenerate(game, item, gen);
		IdentifyDrop(item, plain);
		return plain;
	}
	if (item != nullptr && *item != nullptr) {
		g_levelIn = g_itemLevel(*item);
		g_qualityIn = g_quality(*item);
	}
	GenState state;
	state.item = item;
	state.gen = gen;
	GenState* outer = g_state;
	g_state = &state;
	const uint64_t result = g_originalGenerate(game, item, gen);
	g_state = outer;
	if (result != 0 && item != nullptr && *item != nullptr && !state.touched) {
		RollLowQualityEthereal(*item, gen);
	}
	if (item != nullptr && *item != nullptr) {
		g_levelOut = g_itemLevel(*item);
		g_qualityOut = g_quality(*item);
		ProbeGate(*item, gen);
	}
	if (state.touched && item != nullptr && *item != nullptr && state.maxDurability > 0 && g_hasDurability(*item) != 0) {
		const int32_t value = state.accepted ? item_gen::EtherealDurability(state.maxDurability) : state.maxDurability;
		g_setStat(*item, StatMaxDurability, value, 0);
		g_setStat(*item, StatDurability, value, 0);
	}
	IdentifyDrop(item, result);
	return result;
}

void MakeEthereal(Unit* item) {
	g_setFlag(item, FlagEthereal, 1);
	g_setFlag(item, 0x100000, 0);
	if (g_isType(item, ItemTypeWeapon) != 0) {
		for (const int32_t stat : WeaponStats) {
			const int32_t value = g_getBase(item, stat, 0);
			if (value != 0) {
				g_setBase(item, stat, item_gen::EtherealStat(value), 0);
			}
		}
	} else {
		const int32_t value = g_getBase(item, StatDefence, 0);
		if (value != 0) {
			g_setBase(item, StatDefence, item_gen::EtherealStat(value), 0);
		}
	}
}

// Hook B.
void EtherealHook(Unit* item) {
	GenState* state = g_state;
	if (state == nullptr || state->item == nullptr || *state->item != item || state->touched) {
		MakeEthereal(item);  // outside the generator (cube outputs, ...): Median's +25%, not D2R's +50%
		return;
	}
	state->touched = true;
	++g_etherealSeen;
	state->maxDurability = g_getStat(item, StatMaxDurability, 0);
	const bool forced = (d2r::At<uint8_t>(state->gen, GenFlags) & 4) != 0 || (d2r::At<uint32_t>(state->gen, GenItemFlags) & FlagEthereal) != 0;
	bool accept = forced;
	if (!accept) {
		const int32_t take = item_gen::EtherealAcceptOutOf(item_gen::EtherealChancePerMille(g_quality(item)));
		accept = take > 0 && g_rand(g_seed(item), item_gen::D2rEtherealGatePerMille) < take;
	}
	state->accepted = accept;
	if (accept) {
		MakeEthereal(item);
		++g_etherealMade;
	}
}

// --- Sockets ------------------------------------------------------------------------------------------------------

// 0x441fc0 (game, item, genRecord).
void SocketRollHook(void* game, Unit* item, uint8_t* gen) {
	if (item == nullptr || gen == nullptr) {
		g_originalSocketRoll(game, item, gen);
		return;
	}
	const bool forced = (d2r::At<uint8_t>(gen, GenFlags) & 0x10) != 0 || (d2r::At<uint32_t>(gen, GenItemFlags) & FlagSocketed) != 0;
	const int32_t quality = g_quality(item);
	if (forced || quality < 1 || quality > 3) {
		g_originalSocketRoll(game, item, gen);
		return;
	}
	if ((d2r::At<uint8_t>(gen, GenFlags) & 8) != 0 || g_canSocket(item) == 0 || g_noSocketBase(item) != 0) {
		return;
	}
	++g_socketsRolled;
	const int32_t count = item_gen::SocketsFromRoll(g_rand(g_seed(item), item_gen::SocketRollModulus));
	if (count == 0) {
		return;
	}
	g_setFlag(item, FlagSocketed, 1);
	g_setSockets(item, static_cast<uint32_t>(count));
	++g_socketsGiven;
}

// --- Dye ----------------------------------------------------------------------------------------------------------

int64_t ColourHook(Unit* player, Unit* item, uint8_t* colour, int32_t mode) {
	if (item != nullptr && colour != nullptr && g_unitType(item) == UnitTypeItem) {
		const int32_t index = item_gen::DyeIndex(g_getStat(item, StatDye, 0));
		if (index >= 0) {
			const uint8_t* record = g_itemRecord(g_dataFlag(item), g_unitClass(item, NoFile, 0));
			if (record != nullptr) {
				const uint8_t kind = d2r::At<uint8_t>(record, ItColourKind);
				const uint8_t kindInv = d2r::At<uint8_t>(record, ItColourKindInv);
				*colour = item_gen::ComposeColour(kind, index);
				++g_dyed;
				return g_palette(mode != 0 ? kindInv : kind, static_cast<uint8_t>(index));
			}
		}
	}
	return g_originalColour(player, item, colour, mode);
}

}  // namespace

auto InstallItemGen(const D2RL::PluginContext* context, bool classLock, bool ethereal, bool sockets, bool dyes, bool identify, char* report,
                    size_t reportSize) noexcept -> bool {
	const uint64_t base = context->exeBase;
	const char* lock = "left out";
	const char* eth = "left out";
	const char* sock = "left out";
	const char* dye = "left out";
	const char* ident = "left out";
	g_etherealOn = ethereal;
	g_identifyDrops = identify;
	bool ok = true;
	g_unitType = reinterpret_cast<UnitTypeFn>(base + UnitTypeRva);
	g_unitClass = reinterpret_cast<UnitClassFn>(base + UnitClassRva);
	g_dataFlag = reinterpret_cast<DataFlagFn>(base + UnitDataFlagRva);
	g_itemRecord = reinterpret_cast<ItemRecordFn>(base + ItemRecordRva);
	g_getStat = reinterpret_cast<GetStatFn>(base + GetStatRva);
	g_setStat = reinterpret_cast<SetStatFn>(base + SetStatRva);
	g_getBase = reinterpret_cast<GetStatFn>(base + GetBaseStatRva);
	g_setBase = reinterpret_cast<SetStatFn>(base + SetBaseStatRva);
	g_setFlag = reinterpret_cast<SetFlagFn>(base + SetItemFlagRva);
	g_isType = reinterpret_cast<IsTypeFn>(base + IsItemTypeRva);
	g_seed = reinterpret_cast<SeedFn>(base + UnitSeedRva);
	g_rand = reinterpret_cast<RandFn>(base + RandRva);
	g_quality = reinterpret_cast<ItemFn>(base + QualityRva);
	g_hasDurability = reinterpret_cast<ItemFn>(base + HasDurabilityRva);
	g_canSocket = reinterpret_cast<ItemFn>(base + CanSocketRva);
	g_noSocketBase = reinterpret_cast<ItemFn>(base + NoSocketBaseRva);
	g_setSockets = reinterpret_cast<SetSocketsFn>(base + SetSocketsRva);
	g_itemLevel = reinterpret_cast<ItemLevelFn>(base + ItemLevelRva);
	g_noEtherealBase = reinterpret_cast<ItemFn>(base + NoEtherealBaseRva);
	g_palette = reinterpret_cast<PaletteFn>(base + PaletteRva);

	if (classLock) {
		if (!Matches(base, RequirementRva, RequirementEntry, sizeof(RequirementEntry))) {
			lock = "off: entry bytes of 0x36bc50 differ";
			ok = false;
		} else {
			const bool hooked = context->InstallInlineHook(RequirementRva, RequirementEntry, sizeof(RequirementEntry),
			                                               reinterpret_cast<void*>(&RequirementHook),
			                                               reinterpret_cast<void**>(&g_originalRequirement));
			lock = hooked ? "hooked" : "NOT hooked";
			ok = ok && hooked;
		}
	}
	if (ethereal || identify) {
		if (!Matches(base, GenerateRva, GenerateEntry, sizeof(GenerateEntry)) ||
		    (ethereal && !Matches(base, MakeEtherealRva, EtherealEntry, sizeof(EtherealEntry)))) {
			eth = "off: entry bytes of 0x442d60 / 0x3d3510 differ";
			ident = eth;
			ok = false;
		} else {
			const bool a = context->InstallInlineHook(GenerateRva, GenerateEntry, sizeof(GenerateEntry),
			                                          reinterpret_cast<void*>(&GenerateHook),
			                                          reinterpret_cast<void**>(&g_originalGenerate));
			const bool b = !ethereal || context->InstallInlineHook(MakeEtherealRva, EtherealEntry, sizeof(EtherealEntry),
			                                                       reinterpret_cast<void*>(&EtherealHook),
			                                                       reinterpret_cast<void**>(&g_originalEthereal));
			if (ethereal) {
				eth = a && b ? "hooked" : "NOT hooked";
			}
			if (identify) {
				ident = a ? "hooked" : "NOT hooked";
			}
			ok = ok && a && b;
		}
	}
	if (sockets) {
		if (!Matches(base, SocketRollRva, SocketRollEntry, sizeof(SocketRollEntry))) {
			sock = "off: entry bytes of 0x441fc0 differ";
			ok = false;
		} else {
			const bool hooked = context->InstallInlineHook(SocketRollRva, SocketRollEntry, sizeof(SocketRollEntry),
			                                               reinterpret_cast<void*>(&SocketRollHook),
			                                               reinterpret_cast<void**>(&g_originalSocketRoll));
			sock = hooked ? "hooked" : "NOT hooked";
			ok = ok && hooked;
		}
	}
	if (dyes) {
		if (!Matches(base, ItemColourRva, ColourEntry, sizeof(ColourEntry))) {
			dye = "off: entry bytes of 0x3710d0 differ";
			ok = false;
		} else {
			const bool hooked = context->InstallInlineHook(ItemColourRva, ColourEntry, sizeof(ColourEntry),
			                                               reinterpret_cast<void*>(&ColourHook),
			                                               reinterpret_cast<void**>(&g_originalColour));
			dye = hooked ? "hooked" : "NOT hooked";
			ok = ok && hooked;
		}
	}
	std::snprintf(report, reportSize, "%s: class lock (0x36bc50) %s, ethereal (0x442d60, 0x3d3510) %s, sockets (0x441fc0) %s, dyes (0x3710d0) %s, identified drops (0x442d60) %s",
	              ok ? "on" : "partial", lock, eth, sock, dye, ident);
	return ok;
}

void ItemGenSummary(char* text, size_t size) noexcept {
	std::snprintf(text, size, "class lock denied %d of %d locked-item checks, mercenary equip refused %d of %d | ethereal %d of %d at item level 100+ | sockets given %d of %d rolls | dyed colours drawn %d",
	              g_lockDenied, g_lockChecked, g_mercDenied, g_mercChecked, g_etherealMade, g_etherealSeen, g_socketsGiven, g_socketsRolled, g_dyed);
}

void ItemGenGateText(char* text, size_t size) noexcept {
	std::snprintf(text, size, "ethereal gate of 0x442d60: %d generated, level 100+ %d, weapon or armour %d, durable %d, quality ok %d, base ok %d | hook B called %d, ethereal made %d, quality 1 made %d | last item level %d -> %d, quality %d -> %d",
	              g_gateGenerated, g_gateLevel, g_gateType, g_gateDurable, g_gateQuality, g_gateBase, g_etherealSeen, g_etherealMade, g_etherealLow, g_levelIn, g_levelOut, g_qualityIn, g_qualityOut);
}

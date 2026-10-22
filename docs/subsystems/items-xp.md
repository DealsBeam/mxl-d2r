# Items, runewords, sets and experience (research + port lane, 2026-10-21)

Source: `D2Sigma.dll` (1.10, base 0x10000000, Ghidra read-only copy), tables from `work/named`, `work/sigma`, the D2Game
1.13c copy for what Median replaced, and the D2R dump (image 0x6ab3782c, RVAs relative to the base) for the other side.
Addresses are D2Sigma RVAs (`0x7e470` = VA `0x1007e470`) unless marked D2R. Format follows
[ubers-events.md](ubers-events.md) and [npc-hirelings.md](npc-hirelings.md). Confidence tags: **read** (decompiled and
checked against the call sites), **inferred** (the function is clear, the game meaning is a reading), **open**.

Modules: D2Experience.cpp is one function plus its penalty helper; D2Runeword.cpp is one matcher; D2SetItems.cpp is four
count helpers; the item modules are D2Game's code compiled into Sigma with a handful of Median changes.

## Short version

- **D2Sigma is D2Game + D2Common merged into one DLL, patched into 1.13c.** Besides the 520 D2Game patch records
  (docs/d2r-addresses.md Appendix B) there is a second table at VA `0x10173b90` of the same 5-dword records
  `{rva, 0, value, flag (0 constant / 1 rel32), module}` for the other modules (module 5 and 3 appear; which is
  D2Common / D2Client is **open**). The item functions below are reached through those records: the requirement check
  (`0x7e470`) from module-5 RVA `0x26db0`, the runeword matcher (`0xa2b60`) from module-5 `0x26ba0`, the item colour
  (`0x7e6d0`) from module-5 `0x213fd`, `0x2155e` and four module-3 sites.
- **Most of these modules are vanilla.** The packet handlers of D2ItemActions.cpp, the four set counters of
  D2SetItems.cpp and half of D2ItemEventFunc.cpp are 1.13c with extra anti-hack log lines or immunity checks; D2R has its
  own server for those. What Median really changed is five things (section 8): the **runeword table** (13 property
  slots instead of 7, **Jewel padding** of the rune list), the **kill-experience penalty**, the **party split of kill
  experience**, a handful of **item event functions** (ids 11, 12, 32, 33, 34 and the monster immunity checks of 15/16)
  and the **item generation** details (ethereal roll, socket roll, item level 100+).
- **Shipped here** (plugin `items_ext.cpp`, `tools/items_data.py`, off words `runewords`, `xp` and `partyxp`, no version bump):
  runeword properties 8-13 (197 of 237 runewords had lost properties), Jewel padding in the runeword matcher, and the
  kill-experience penalty. Everything else is classified in the tables and left for the next lane with a spec.
- **Nothing in these modules gates the ubers or the endgame areas** (that was ubers-events.md). The generator keeps
  1.13c's branch for item level above 99 (ethereal roll) but with Median's own roll (`0x7e120`).

## 1. Function table

Bucket: **a** data-only, **b** D2RLoader service, **c** native hook, **d** UI, **e** cut / D2R covers it. Status:
**done** (ported or already data), **shipped** (this lane), **open**.

### D2Experience.cpp (region 0x5e7b0..0x5e940)

| Address | What it does | Bucket | Status | Content that depends on it |
|---|---|---|---|---|
| 0x5e7b0 | kill experience penalty for the level gap (formula in section 2). **read** | c | **shipped** (`xp`) | every kill; all of Median's leveling, the high-level farming areas |
| 0x5e900 | thunk (ecx/edx shuffle) that replaces the 1.13c call at D2Game `0xdc076`. read | c | n/a (D2R: hook the callee) | |
| 0x5e830 | party experience: collects the party members in range (D2Multiplayer callback `0x8f7f0`, up to 18), then for each gives `D2Game 0xdc030(xp, member, level)` (the per-player award with level cap and the +x% bonus) and `0xde6d0` (send). No division by the party size. Replaces the 1.13c function `0xde8f0`, which splits by weights (`__adj_fdiv` / ftol, single player short-circuit). **read; the split rule of 0xde8f0 is inferred from its shape** | c | open (multiplayer only; solo is identical) | parties / the test realm |
| 0x5e910 | thunk for the call above (D2Game `0xde8f1`). read | | | |

### D2ItemActions.cpp (0x6f240..0x70080)

| Address | What it does | Bucket | Status | Content |
|---|---|---|---|---|
| 0x6f240 | item spell 18 (containers): absorb matching items from the inventory into the clicked container (`gc01..` arcane shard containers), caps from `misc.txt len` | c | done (`item_spells.cpp`) | Median's shard containers, currencies |
| 0x6f460 | destination page of a quick move from the page the item is on (inventory / stash / cube / belt), returns 1/2/3/4 or -1; caller is the inventory screen (client) | d | n/a | |
| 0x9a720 | not in the module list but their dispatcher: client packet, action byte +1 picks the handler, item id at +2, NPC id at +7 | | | |
| 0x6f500 | action 0: move the item to a free inventory spot, resend the grid. inferred | e | n/a (D2R's server) | |
| 0x6f790 | action 1: move the item into the cube; logs "moved an item to cube from outside of town with no cube in inventory" (hack log) | e | n/a | |
| 0x6fd80 | action 2: move the item to the stash; logs "... to stash from outside of town" | e | n/a (D2R shared stash) | |
| 0x6ffd0 | action 3: sell the item to the vendor (id at +7); logs "sold an item to vendor from outside of town" | e | n/a | |
| 0x6fb30 | action 4: move the item to the inventory from stash / cube | e | n/a | |
| 0x70080 | list iterator helper | | | |

The anti-hack lines and the "town only" checks are the only Median additions; the 15 x 10 cube / 14 x 14 stash grids are
data (inventory.txt, `inventory_sizes` in txt_migrate.py).

### D2ItemEventFunc.cpp (0x7d570..0x7df20)

The functions are the `itemeventfunc1/2` columns of itemstatcost.txt. Sigma's table is at VA `0x101d94e8 + 4 * id`
(ids from the table positions: 0x1d9500 = id 6, 0x1d9534 = id 19, 0x1d9560 = id 30 agree with the data below). Median's
data uses ids 6-21, 28-31, 33 and 34; D2R's own itemstatcost uses 6-10, 13-21, 28-31.

| Id | Address | What it does | Status |
|---|---|---|---|
| 6, 10, 11, 12, 32 | 0x7d920, 0x7d760, 0x7d680, 0x7d5a0, 0x7d840 | "attacker takes damage" family: physical (target stat 0x24), lightning (0x29), fire (0x27), cold (0x2b), magic (0x25). Skips targets with flag `0x10000`, dead / inert modes (player 0 and 0x11, monster 0 and 0xc) and monsters whose resistance is 100 or more; then builds a 0x70-byte damage block from the stat and applies it. read | 6 and 10 are 1.13c's; **11, 12 and 32 are not in D2R's data**; Median's data uses 11 and 12 (attacker takes fire / cold damage). Whether D2R's table has code for them is **open (check in game)** |
| 15, 16 | 0x7de40, 0x7db40 | open wounds / crushing blow: skip monsters with stat 0xbd (Median-only monster stat, D2R 373: immune to both), else call 1.13c's function | open, c; D2R applies them to everything |
| 18 | 0x7db90 | wraps 1.13c's "heal after demon kill": nothing for dead / inert units | vanilla enough |
| 19 | 0x7df20 | slow: amount from the stat, capped at 50 for players; monsters: 25 when the unit flag `0x100bf030(0xc)` or the monster record flag `0x40` is set (meaning of both open, probably bosses), 50 for the flag of `0x100bf9e0`, else 90, and 50 when `0x100bf030(2)` and the amount is above 50; then stats 0x44 and 0x45 (attack rate, other anim rate) are lowered by the amount. read, meaning of the flags inferred | caps differ from 1.13c? **open**; D2R's value unknown |
| 28 | 0x7dbf0 | life per kill: `min(stat * 256, maxhp - hp)` when at least 1 life, through the owner chain (a pet's kill credits the player), then stat update 0x97. read | vanilla shape; owner chain **open** in D2R |
| 30 | 0x7da00 | chance to cast a skill on an event: stat -> skill and level, event table `0x101df948`. read | vanilla |
| 33 | 0x7dd40 | flat life on an event for players, not while the unit is in state 92 (`death_delay`); same arithmetic as 28. read | Median-only id (itemstatcost row 33's help text "item_absorb_slash_percent" is a renamed row) |
| 34 | 0x7de90 | wrapper of 30 with the same player-only / state-92 gate ("chance to cast on kill") | Median-only id |
| | 0x7d570 | small helper (`rand` style, -1 on failure) | |

Native port of 33 / 34 and the monster-immunity checks needs D2R's event table (the 8-byte function table is not found;
the dispatcher is reached from the property function `0x3d4620`). Only 1 + 1 stats use ids 33 / 34.

### D2ItemGeneration.cpp, D2Items.cpp (0x7e0f0..0x7e8c0)

| Address | What it does | Bucket | Status | Content |
|---|---|---|---|---|
| 0x7e0f0 | byte 0 of the qualities row for a quality below 10 (`work/sigma/qualities.txt`, 5-byte rows: `Automagics` [0] used here, `Runeword` [1] 1 for qualities 1-3 = runewords allowed (section 3), `EthChance` [2] 25 for most qualities, `MaxSock` [4] 1 from quality 5) | a | open | |
| 0x7e120 | Median's ethereal roll after generation: items with durability (`0x7fff0`: base flags, stat 0x49 non-zero, not indestructible stat 0x98) of item type 45 or 50: if a 1000-sided die passes, or the item is already ethereal, or the generator flag 4 is set, `0x7fdd0` makes it ethereal (flag `0x400000` set, `0x100000` cleared, +25 percent on type 45's five stats or on stat 0x1f) and durability and maximum durability become 75 percent (minimum 1). **Confirmed by the disassembly**: the die is `rand(1000) <= word [qualities row + 2]` (25 for qualities 1-4, 6, 7, 0 for 5, 8, 9: about 2.6 percent), the +25 is `value + value * 25 / 100`, the tail call `0x7fd70` is the durability step; the call is only reached for item level 100+ (it replaces 1.13c's call inside that branch). D2R's own branch (`0x442d60`) lets 5 in 100 through to `0x3d3510` (+50 percent, then durability / 2 + 1) | c | **shipped** (`ethereal`) | ethereal drops |
| 0x7e3b0 | thunk for 7e120, patched over the call at D2Game `0x10c88`: 1.13c's own branch of the generator `0x10a20` for item level above 99. read | | | |
| 0x7e240 | Median's version of the call 1.13c makes for quality 1-3 items at D2Game `0x106b0` / `0x10ca9` (old target `0x6fc2ec90`): a socket roll. **Read (disassembled; the decompiler had mixed up the registers)**: base flag `+0x137` (can have sockets), quality row byte 4 (`MaxSock`) clear, generator flag 8 clear, then `rand() % 7` is the count: none for a remainder of 0 (1 in 7), else the remainder 1-6 through `0x800b0` (stat 0xc2, clipped to what the item holds). For `MaxSock` qualities (5-9) the same function gives 6 sockets (clipped), unless a unique's row flag `0x10` is set; the 1.13c call sites only reach it for qualities 1-3, so that branch is dead in practice | c | **shipped** (`sockets`) | sockets on normal items |
| 0x7e3c0, 0x7e3e0 | pick one of two 1.13c functions by the flags `0x1000000` / `0x10000000` (thunks to D2Game) | | | |
| 0x7e400 | qualities row accessor (table at +0x68, 5-byte rows) | a | n/a | |
| 0x7e450, 0x7e830 | read / set-clear a mask in the item data flags (+0x18) | | | |
| 0x7e470 | **item requirement check** `(player, item, &strOk, &dexOk, &lvlOk)`: strength, dexterity and level against the item's requirements (`0x7ff70` / `0x7fef0`: record values plus stat 91 `item_req_percent`, minus 10 when ethereal), then **Median's class lock**: item stat 463 (D2R 492) gives class + 1; the player must be that class, and the base's class requirement (7 = none) must match too; a second branch for the other unit kind (`0x100bf9e0`) checks the same two conditions against the base class from the item type table (the mercenary). read | c | **shipped for players** (`classlock`) | the 14 cube recipes `properties_422` (7 classes x 2: unique conversions that lock an item to a class). D2R keeps stat 492 but nobody reads it. D2R's check is `0x36bc50` (section 5) |
| 0x7e670 | pack a 1-4 character code into a dword, padding spaces | | n/a | |
| 0x7e6c0 | colour index clamp: 1..21 -> 0..20, else 0 | | | |
| 0x7e6d0 | item colour: if stat 183 (D2R 368) is above 0 the dye (1..21) is the palette index, else 1.13c's. Patched at 2 module-5 and 4 module-3 sites. read | d | **shipped** (`dyes`; item_spells.cpp sets the stat, the colour function `0x3710d0` is hooked; look in game) | dyes (bug #11) |
| 0x7e750 | weapon Strength / Dexterity damage bonus of an item record (u16 at `+0x106` / `+0x108`, 1 means 100), default 50 for non-items; used by the damage code (`0x8adac`, `0x8ade4`) | a | n/a (the other lane's D2Damage; the columns are in weapons.txt) | |
| 0x7e7d0 | bit 2 of the item record's flags dword `+0xdc` (assert D2Items.cpp 0x19c); patched at D2Game `0x11e8f` / `0x4ee05` | | | |
| 0x7e860 | range check on the sets table | | | |
| 0x7e880 | range check on the items table | | | |

### D2Runeword.cpp (0xa2a70..0xa3130) and D2SetItems.cpp (0xa5580..0xa5960)

| Address | What it does | Bucket | Status | Content |
|---|---|---|---|---|
| 0xa2b60 | **runeword matcher** (section 3). read | c | **shipped** (Jewel padding, `runewords`) | all 237 runewords, Jewelwords |
| 0xa2a70, 0xa2ab0, 0xa2b30 | list equality, push, constructor (MSVC `std::list<int>`) | | | |
| 0xa30e0 | runes row `n` (stride 0x180, 13 property slots) | a | n/a | |
| 0xa3130 | compares two lazily evaluated optional values (unit callbacks) | | | |
| 0xa5580 | the stat list of a unit that carries set `n` (list type 5) | e | n/a | set bonus tooltips |
| 0xa55f0 | does the player own the set's item `idx` (other than the one shown): tooltip colouring of the "other pieces" | d | n/a (D2R colours them) | |
| 0xa5710 | the player owns item row `n` of a set (unit scan lambda `0xa5960`) | d | n/a | |
| 0xa57f0 | number of the set's items the player wears (loops the 6 slots at `+0x110`, unit scan lambda `0xa59c0`) | e | n/a | set bonuses (D2R counts the same way) |
| 0xa57b0, 0xa58e0 | bounds checks of the sets / setitems tables | | | |

Sets need no code: `split_large_sets` already moves items 7+ of Orphan's Call into "(2)" sets; the other sets fit 6.

## 2. Experience

Median (`0x5e7b0`, called as `xp = f(xp, playerLevel, monsterLevel)`): symmetric, no free pass for stronger monsters.

| Gap g = abs(plvl - mlvl) | share |
|---|---|
| 0..5 | 100% |
| 6 and up, plvl below 100 | (15 - g) * 10 %, 3% at g = 15 and above |
| 6 and up, plvl 100 or more | (10 - g) * 20 %, 3% at g = 10 and above |

The result is `xp * share / 100` (64-bit product, truncated). 1.13c (`D2Game 0xdaa40`) and D2R (`0x44fef0`) use two
11-entry tables in 1/256 steps (D2R `0x1d1ac38`: monster below the player, 100% up to a gap of 5 then 81, 62, 43, 24, 5%
from a gap of 10; `0x1d1ac68`: monster above the player, only for player level below 25: 88, 68, 36, 15, 2%), flat from
a gap of 10, and a level-25+ player gets a **bonus** from stronger monsters (`0x44a0b0(xp, plvl, mlvl)`, which I read as
`xp * mlvl / plvl`; not verified). So in D2R an endgame player gets 5% from every low monster where Median gives 90% at
gap 6 falling to 3% at gap 15, and from a monster 20 levels above D2R pays more than full experience where Median gives 3%.

Shipped: hook of `0x44fef0` (rcx player level, rdx monster level, r8d experience, called once from the kill function
`0x44ece0`); `items_ext_logic.h` `XpSharePercent` / `PenalizedXp`, 12 test lines. The caller clamps the experience to
0x7fffff and compares the player with the class's maximum level first, as 1.13c does; Median's `experience.txt` rows
(99 levels) are data.

## 3. Runewords

D2R side (verified in the dump): the table is a vector `{records, count}` per data bank (`0x3143b0(dataFlag)`), records of
**0xe8** bytes: `+0x44` row in use, `+0x4a` 6 x int16 item types it fits, `+0x56` 3 x int16 it does not fit, `+0x5c`
6 x int32 item class ids of the runes, `+0x74` 7 x `{prop, param, min, max}`. The matcher is `0x372260(item)`
(returns the record or null; 15 callers: socketing, loading, tooltips, `0x3d44c0`); the properties are applied by
`0x3d44c0(item, owner)`: runeword record -> item flag `0x4000000` -> stat list state `0xab` flags `0x40` -> the seven
properties through `0x3d4620(6, item, owner, 0, index, 0, &prop, 0xab, 0x40, 0)` -> `0x2f3d00` (finish) with
D2RLoader's snapshot pair `0x2f8120/0x2f8290/0x2f81a0` around it. `0x3d3670` is the general "apply mods of kind"
function (kinds 0-5: affixes, unique `0x15c`-byte records with 12 properties at `+0x98`, set items with 9 + 10 bonus
properties, ...); the runeword is not among its callers.

D2Sigma side (`0xa2b60`, records of 0x180 bytes: `+0x86` 6 itypes, `+0x92` 3 etypes, `+0x98` 6 runes, 13 property
slots): the same rules as 1.13c (all sockets filled, rune ids in order, an excluded type rejects, one included type
accepts) plus

1. **Jewel padding.** The record's rune list is padded at the **front** with the class of the first rune of row 0
   (Jewelword1's `jew`) up to the number of sockets, then compared with the socketed items in socket order. A 1-rune
   runeword on a 3-socket item works with `[jewel, jewel, rune]`.
2. Quality: the qualities table byte 1 (low, normal, superior only) or item stat 294 > 0. No data sets stat 294 (it is
   the renamed row `item_regenstamina_bytime`; with it the runeword's runes must not exceed that rune level). D2R rejects
   qualities 4-9 in the matcher, which is the same rule.
3. The base's flag `+0x12a` (D2R `+0x13a`) forbids runewords on the base.

**What was lost and what is shipped.**

- Properties 8-13: 197 of the 237 runewords have them (630 properties, e.g. Gehenna: `kill-skill` and `ltng-fx`);
  `txt_migrate` kept them in `work/mod/overflow/runes.txt` and D2R's record cannot hold them. `tools/items_data.py`
  writes `plugin/src/mxl_runewords.h` (row, first rune's item code, up to 6 properties with D2R property rows and
  migrated skill / monster parameters), `items_ext.cpp` hooks `0x3d44c0` (entry `40 55 57 41 57 48 83 ec 60 4c 8b fa 48
  8b f9`, no rip-relative) and applies them after the seven with the same call, list and snapshot. The record is found
  by `(record - table) / 0xe8`; a row is only extended when the first rune of the live record has the item code
  the generator saw (`refused` counts the others), so a differently ordered table cannot get wrong properties.
- Jewel padding: hook of `0x372260` (entry bytes `48 89 5c 24 10 48 89 74 24 18 48 89 7c 24 20 55`): the original runs
  first; when it finds nothing the same checks run with a padded list (`PaddedRunesMatch`): item type 4, quality not 4-9,
  base flag `+0x13a` clear, every socket filled, then each row in use, its excluded types, its included types (item data
  flag bit 31 skips the type checks, as in D2R). Rows made only of Jewels are not padded: Sigma pads every row, which
  makes Jewelword1 (row 0) match every all-Jewel item and leaves Jewelword2-6 dead; the exact match keeps counting the
  Jewels. The matcher returns the record of the *unpadded* runeword, so item names, the saved runeword id and the
  properties are the same as for an exact socketing.

## 4. Sets

D2SetItems.cpp has four helpers (count worn pieces of a set, a piece's presence, the stat list of a set) over 1.13c's
6-slot set record (`+0x110`); D2R does the same. Median's sets are data only: no `setitems` / `sets` columns overflow,
Orphan's Call is split. Nothing to port. Tooltip colouring of the other pieces is the client (D2ItemDesc).

## 5. Item colour, generation and flags (shipped by the item-gen lane: `item_gen.cpp`, `item_gen_logic.h`)

Off words `classlock`, `ethereal`, `sockets`, `dyes`. All four were read in the D2R dump; none has been seen in game yet
(docs/testing.md). Entry bytes below are whole instructions, no rip-relative.

- **Class lock (0x7e470 -> D2R `0x36bc50`).** D2R's requirement check is `0x36bc50(item, unit, flag, &strOk, &dexOk, &levelOk, flag2)`
  (entry `4c 89 4c 24 20 44 89 44 24 18`): item record `+0x11a` / `+0x11c` strength / dexterity, `item_req_percent` (stat 91), minus 10
  when ethereal, `0x376de0` for the level, then the item type's class requirement (type record `+0x20`, 7/8 = none). The hook runs
  the original and turns a success into a failure when the unit is a player and `GetStat(item, 492) = class + 1` (value 8 =
  Median's "no class" locks nothing) is not the player's class (`ClassLockAllows`). Mercenaries (Median's second branch) are
  left to the original. The cube output `properties_422` already writes the stat.
- **Ethereal (0x7e120 -> D2R `0x442d60` + `0x3d3510`).** `0x442d60(game, &item, gen)` (entry `40 53 55 41 56 48 83 ec 50`) is 1.13c's
  `0x10a20`: the quality switch, then for item level 100+ (not when gen `+0xc0` bit 2) of weapons / armour with durability,
  quality other than 1 and 5: `rand(100) < 5` or gen `+0x54 & 0x400000` or gen `+0xc0 & 4` calls `0x3d3510(item)` (entry
  `48 89 5c 24 10 57 48 83 ec 20`: flag 0x400000, +50 percent on stats 0x15-0x18, 0x9f, 0xa0 for weapons (type 0x2d) or on
  0x1f) and then halves the durability (`max / 2 + 1`). Median differs in the rate (about 2.6 percent by quality), the +25
  percent and the 75 percent durability (`item_gen_logic.h`). Hook A wraps `0x442d60` (remembers item and gen record, fixes
  the durability afterwards), hook B replaces `0x3d3510` while hook A runs (forced items always; the others are accepted with
  probability 26/50 so the overall rate is Median's; accepted items get Median's effect; the fix-up gives 75 percent or puts back
  what D2R halved). One difference kept from D2R: it never makes quality 1 (low) and 5 (set) ethereal, Median's 7e120 allows
  quality 1.
- **Sockets (0x7e240 -> D2R `0x441fc0`).** `0x441fc0(game, item, gen)` (entry `48 89 6c 24 10 48 89 74 24 18 57 48 83 ec 20`)
  is D2R's socket roll for quality 1-3, called from `0x442d60` and `0x443670` (the two 1.13c call sites). D2R rolls
  `rand(100) <= 32` for `rand(max) + 1` sockets (cap 3 / 4 / 6 by difficulty), Median `rand() % 7` clipped to what the
  base holds. The hook replaces it for unforced items (gen `+0xc0 & 0x10` or `+0x54 & 0x800` keep D2R's code) of bases that can
  have sockets (`0x373610`, record `+0x147`; Sigma `+0x137`) and are not flagged by `0x374660` (record `+0x142`), then calls D2R's
  own socket setter `0x375560(item, count)`, which clips to the item's size. Per the arithmetic 6 in 7 normal items of a
  socketable base get sockets (D2R: 1 in 3); that is Median's code as read, compare in the game.
- **Dye paint (0x7e6d0 -> D2R `0x3710d0`).** `0x3710d0(player, item, &colourByte, mode)` (entry
  `48 89 5c 24 08 44 89 4c 24 20 55 56 57`) is D2R's item colour: owner colour states, unique / set / affix colour records,
  then `0x376970`; it writes `(kind << 5) + colour` into the byte (`0x376600`) and returns the palette entry
  `0x145dfb0(kind, colour)` (`kind` = items record `+0x151`, or `+0x152` for `mode != 0`), 0 when the item has no colour.
  Median's `7e6d0` checks stat 183 first, so the hook reads stat 368 first: 1..21 gives palette index 0..20 and the same
  composition as D2R's. The stat is 5 bits signed in the data (Send Bits 5, Save Bits 5), so 16..21 may arrive as -16..-11
  at the client: `DyeIndex` adds 32 to negatives. If the game still shows nothing for 16..21 widen Send Bits to 6.
- **Item events 11 / 12 / 32 / 33 / 34 and the immunity of 15 / 16: located and ported (lane p4ev, 2026-10-22;
  `plugin/src/item_events.cpp`, `item_events_logic.h`, off word `itemevents`).** The registry is **not in the game's main image
  but in D2RLoader's `D2RCore.dll`**, which is why every byte scan of the exe found nothing. Details in section 5b.
  The earlier searches (byte scans for the record words `+0x44 / +0x46 / +0x48 / +0x4a`, the 8-byte pointer table scan, the
  `PlrModes.cpp` asserts) are void; and the earlier in-game test was wrong: a weapon `115` in `rhand` does **not** apply
  its stats to the owner in the harness (`stat player 397` stayed 0 for that reason), while a ring does (`equip rin 397=50
  lring`). Use a ring.
  Monster stat 373 (Median `0xbd`, immune to open wounds and crushing blow) is a data stat on the monster; it is the same
  stat as Median 189 of the absorb credit (combat-stats.md section 2.4).
- **Party experience (0x5e830): shipped (off word `partyxp`), multiplayer only, not run in game with two players.** Solo
  play is identical. Median overwrites the 1.13c share function `0xde8f0` (patch table `0xde8f0` = `E9` to thunk `0x5e910`)
  and pays every collected member the monster's full experience through the per-member award (`0xdc030`: cap, level gap
  penalty, ExpRatio, stat 0x55; hirelings capped) and the add (`0xde6d0`), with no division and no weighting; it also
  widens the collect range from `0x1900` to `0x7d00` squared units (80 to about 179; patch table `0xdafcb`).
  D2R's version: the kill function is `0x44a3b0` (not `0x44ece0`, which is the per-member award `(game, unit, level,
  monster level, xp)`); it collects with the party iterator `0x506f90(game, killer, callback, ctx)` and callback `0x44fd50`
  into a 0x70-byte stack context (monster `+0`, units[8] `+8`, levels[8] `+0x48`, count `+0x68`, level sum `+0x6c`) and
  splits inline at `0x44a62e..0x44a6d7`: pool `xp + (n-1) * xp * 0x59 / 256`, member share `level * pool / sum`
  (n = 1: the killer gets the full xp). `0x44a3b0` tests `count <= 0` after the iterator (`0x44a5e2`) and then skips the
  split. The port hooks `0x506f90` (15 stolen bytes, whole instructions): after the original it checks the callback is
  `0x44fd50`, pays each collected unit `0x44ece0(game, unit, level, monster level, base stat 13 of the monster)` then
  `0x44f2e0(game, unit, level, amount)`, and sets the count to 0. The range immediate at `0x44fe7b` (`cmp ebx,0x1900`) is
  patched to `0x7d00` (expected bytes checked). The collect callback also skips members with flag `+0x124 & 0x10000`
  where 1.13c tested `+0xc6 & 1`; not compared. D2R's table of the vanilla numbers (xp 1000, two level-80 players: pool
  1347, 673 each; three: 565 each; levels 60 and 90: 538 / 808) is in `items_ext_test.cpp`.

## 5b. Item events on D2R (lane p4ev, 2026-10-22)

How it works (read in the game with the harness verb `evtrace`, and in `D2RCore.dll` dumped from memory):

- **Registration.** Equipping an item copies its stats to the owner and, for every stat whose itemstatcost record has an
  event word (`+0x44` / `+0x46`) and a function word (`+0x48` / `+0x4a`; array `*(exe + 0x2a9a580 + 16) + 0x1258`, stride
  `0x144`), calls D2RLoader's **register** thunk. The thunks sit at `exe + 0x3e2a400` (register, unregister, has, clear,
  dispatch, registerFn: 10-byte `jmp [rip+x]` into `D2RCore.dll`, manually mapped, no MZ header; its base is the
  `AllocationBase` of the thunk target). The unit's list head is `unit + 0xe0`; a node is `0x40` bytes: `+8` =
  `(stat << 32) | layer`, `+0x28` the handler, `+0x38` next. `evtrace list [player|m<id>]` prints it.
- **Dispatch.** The game raises an event with `0x44d570(game, event, unitA, unitB, arg5)`; the core walks `unitA`'s list and
  calls the handler of every node whose event matches: `handler(game, event, unitA, unitB, arg5, code, a7, a8, a9)`.
  `unitA` is the event's owner (the victim for "attacked", the attacker for "dealt"). Events seen: 2 / 3 (thorns, stat 396 /
  397), 5 / 6 (open wounds / crushing blow, stats 135 / 136), 9 (kill: stats 265, 376).
- **Handler table.** `D2RCore.dll + 0x630260`, 8-byte pointers, slot = function id (the id from itemeventfunc). The shipped
  table gives: 10 / 11 / 12 (and 32) native handlers that do what Median's do (a damage struct with flags `0x4021` and
  the amount in the fire / lightning / magic / cold field, skipped by monsters resisting 100 %): **thorns 397 = 50 takes
  exactly 50 life points off the attacker per event** (check `itemevents`). 15 / 16 (open wounds, crushing blow) are
  native too. 33 / 34 are *forwarders* to the exe's table (`exe + 0x238e5c0`, entry 33 / 34) that return 0 at once for any stat whose
  high dword is not 0: they never did anything. The exe's own table has the working bodies under other ids:
  **28 (`0x583a70`) is Median's 33** (flat life: heals `min(stat * 256, missing life)` and clears state `0x97`) and **20
  (`0x583b30`, "OnHitOrAttack") is Median's 34** (chance to cast skill `layer >> 6` at level `layer & 63`, at the target or,
  for skills with the self flag, on the owner). Both take the code as `(stat << 16) | layer`, the core calls with
  `(stat << 32) | layer` (`ToD2rCode`).
- **What the port does** (`item_events.cpp`, entry bytes checked against the shipped `D2RCore.dll`, slots written with
  `VirtualProtect`): table slots 33 and 34 become wrappers that translate the code and call the exe's 28 and 20; slots 15 and
  16 become gates that return 0 when the attacker or the victim is a monster with stat 373 (Median's wrappers `0x7de40` /
  `0x7db40` test the unit they are given; the intent, "immune to open wounds and crushing blow", is the monster that is hit, so
  both ends are tested) and otherwise call D2RCore's handler. 10 / 11 / 12 are left alone. `mxl` prints `item events on: ...`.
- **In game** (`py -3 tools/game_checks.py all itemevents`, slot 3, plugin 0.50.0): ring `397=50` -> the monster loses 50 points
  (12800 raw) on event 3; ring `265=50` -> +50 life points on a kill (event 9); ring `376:2309=100` (Fire Bolt 5 at 100 %) -> a
  class-58 missile appears, **the owner does not need to know the skill**; ring `135=100 136=100` -> a plain monster loses life
  on events 5 / 6, a monster with `stat m<id> 373 0 set 1` loses none.
- **Not ported / open**, listed in docs/unported.md: function 32 (no stat of the mod's itemstatcost uses it); Median 34's
  post-handler for skills with flag `0x40`; Fire Ball (47) as the cast skill returned success with the synthetic kill event but no
  missile was seen (Fire Bolt 36 works; Median 47 may need a level or target the harness lacks).
- **Harness.** `evtrace on|off|clear|dump` (thunk-slot logger), `list`, `rec <stat>` (the record's event words), `peek`, `watch`
  (debug-register watch), `fire <event> player|m<id> player|m<id>|none` (calls `0x44d570`; the harness's `kill` does not raise the
  kill event), `calls` (counters of the replaced handlers 33 / 34 and their last results).

## 6. D2R addresses found here

| What | D2R RVA | Notes |
|---|---|---|
| runeword apply | `0x3d44c0` | `(item, owner)`; 1 when applied; entry above |
| runeword matcher | `0x372260` | `(item)` -> record or null |
| runes table | `0x3143b0(dataFlag)` -> `{records, count}`; record 0xe8 | layout in section 3 |
| property apply | `0x3d4620` | types: 3 unique, 4 set item, 6 runeword (1 property per call), 0/1/2/5 affixes |
| kill experience penalty | `0x44fef0` | `(plvl, mlvl, xp)`; tables `0x1d1ac38`, `0x1d1ac68` (11 x int32, 1/256) |
| kill experience function | `0x44ece0` | caller of the above; clamps the experience to 0x7fffff |
| item data | `0x34a500(item)` | `+0` quality, `+0x18` flags |
| item type test | `0x373890(item, type)` | |
| item inventory (sockets) | `0x34a360`, `0x388c10`, `0x38aab0`, `0x38aba0` | (unit, file, line), first node, node unit, next node |
| unit data bank | `0x34a0e0(unit)` -> byte | `unit + 0x1bd` |
| stat list lookup | `0x2f5b80(unit, state, flags)` | state 0xab = runeword |
| item flag set | `0x36d8f0(item, mask, on)` | `0x4000000` runeword, `0x400000` ethereal, `0x800` socketed |
| item requirement check | `0x36bc50` | section 5 |
| item generator quality switch | `0x442d60` | `(game, &item, gen)`; gen `+0x54` item flags, `+0xc0` generator flags |
| make ethereal | `0x3d3510` | `(item)` |
| socket roll / setter | `0x441fc0` / `0x375560` | `(game, item, gen)` / `(item, count)` |
| item colour | `0x3710d0` | `(player, item, &byte, mode)`; palette `0x145dfb0(kind, colour)` |

## 7. Tools

`tools/ghidra_scripts/ListFuncs.java` (functions in an RVA range), `ScanStarts.java` (16-aligned starts after INT3
padding: with `-noanalysis` the Sigma project knows only some of the functions), `ByteCallers.java` (rel32 and absolute
references by raw bytes, which finds callers in code Ghidra did not disassemble), `DumpDwords.java` (the patch tables).
`DecompOut.java` creates the function at an address, so a start from `ScanStarts` is enough.

## 8. Summary of the work

| Piece | Bucket | Where it stands |
|---|---|---|
| Runeword properties 8-13 | c + data | shipped: `items_data.py`, `items_ext.cpp` |
| Runeword Jewel padding | c | shipped |
| Kill experience penalty | c | shipped |
| Party experience split | c | shipped (`partyxp`, hook `0x506f90` + range `0x44fe7b`); two-player run open |
| Item events 11/12/32/33/34, immunity of 15/16 | c | shipped (`itemevents`: D2RCore handler table, section 5b); func 32 has no consumer |
| Class lock stat 463 | c | shipped (`classlock`, hook `0x36bc50`) |
| Ethereal / socket generation | c | shipped (`ethereal`, `sockets`; hooks `0x442d60`, `0x3d3510`, `0x441fc0`) |
| Dye colour | d | shipped (`dyes`, hook `0x3710d0`; check in game) |
| Item actions, sets | e | D2R covers them |

## 9. In-game checks (none done; the item generation ones are also in docs/testing.md)

1. `mxl` shows `items on: ...` with both hooks "hooked"; each failure names the entry that differed.
2. Make Gehenna (or any runeword whose row has extras, e.g. rows 6-11) and read the tooltip: the lines of slots 8-13
   must be there; `mxl` counts `runewords with extra properties`; the log prints the first six (`row`, `7 + n properties`).
3. A 1-rune runeword on a 3-socket base with two Jewels in the first two sockets: it must complete; with the Jewels
   behind the rune it must not. `found by Jewel padding` counts them. Jewelword rows must keep counting their Jewels.
4. Kill a monster 10 levels below the player at level 60 (expect half the experience of an equal-level kill), 20 levels
   below (3%), and one 10 levels above at level 60 (50% in Median; D2R's bonus would pay about 117%).
5. `mxl-core-off.txt` with `runewords` and `xp` restores D2R's behaviour.
6. Item generation (`classlock`, `ethereal`, `sockets`, `dyes`): `mxl` prints `item generation on: ...` and counters; see docs/testing.md.

## 10. Uncertainties

- The item event ids: 10 / 11 / 12 are D2RCore's own handlers, verified for 11 (thorns 397) only; 12 (cold, 396) and 10 are
  expected to behave the same (same code path, different damage field).
- The `0x7e240` socket arithmetic and `0x7e120` die are read from decompiler output with register arguments; treat as
  inferred.
- Whether D2R's inline hook relocates the `jg rel32` that follows the first 9 bytes of `0x44fef0` (the SDK source is
  not in the repository); if the hook crashes on the first kill, `xp` in `mxl-core-off.txt` removes it.
- Whether Jewelword1 shadowing is Median's behaviour or a side effect; the port keeps the counting rows.

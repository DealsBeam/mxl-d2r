# Combat, damage, stats and properties: what D2Sigma does and how it maps to D2R

Research lane, 2026-10-21. Source: `D2Sigma.dll` (1.10-style 32-bit build, base 0x10000000; addresses below are the
mapped addresses), read function by function with the Ghidra scripts in `tools/ghidra_scripts` (new: `ListFuncs`,
`DumpDwords`, `FindDword`); tables from `work/sigma`, `work/named`, `work/mod`. Modules: D2Damage.cpp (8 functions),
D2Combat.cpp (12), D2Stats.cpp (5 + 1 helper), D2ItemStats.cpp (5), D2Properties.cpp (7, which are 9 property functions
plus one helper), D2ItemCalc.cpp (1), D2Respec.cpp (4). "Confirmed" = read from the code and matched against D2R's code or
the table data; "likely" = inferred.

Summary: almost all of these modules are **D2Sigma's own copy of D2Game/D2Common's vanilla combat code** (the server
D2GS carries its own game rules), so for most functions D2R's version is the equivalent and nothing needs porting. The
**Median deltas** are few, and all of them are caps and factors rather than new formulas:

| Delta | D2Sigma | D2R | Bucket |
|---|---|---|---|
| dodge / avoid / evade limit | min(75, 60 + stat "maximum avoid chance") | none (raw stat) | **native hook, done** (combat.cpp) |
| block chance limit | min(80, 50 + stat "maximum block chance"); 50 with the skill Indomitable | D2R's own (75) | **native hook, done** (lower caps only) |
| ethereal bonus | +25% damage / defense | x3/2 (+50%) | **native hook, done** (combat.cpp) |
| property functions 27, 28, 29 | Median's own | table slots empty (skipped silently) | **native, done** (properties_ext.cpp) |
| propertiesext | default skill parameter of 4 properties | no such thing | **data, done** (txt_migrate.py `property_defaults`) |
| maximum resistances | 75 + max-res stat, hard cap 90; damage resist cap 50 | 75 + stat, cap 95 | native hook, **open** (see 2.4) |
| avoid check while moving without evade | falls through to block / dodge / avoid | returns 0 | cut (tiny) |
| mercenaries share the avoid / dodge / evade caps (not block) | yes | the `0x44ef70` hook accepts hirelings (`0x3af240`) | done, not exercised in game |

## 1. Function map

D2R columns name the equivalent in the dump (RVAs). Buckets as in the triage (a data-only, b service, c native hook,
d UI, e cut); "done" = nothing to port because D2R's code does the same, or ported in this lane.

### 1.1 D2Damage.cpp (the damage block and the hit result)

| D2Sigma | Role | D2R | Bucket / status |
|---|---|---|---|
| 0x1005bb60 | applies a hit result to the defender: from the result flags (bits 1, 2, 4, 8, 0x10, 0x200, 0x4000 and the 0x180 pair) queues the get-hit / block / death / knock-back mode (0x10041ee0 modes 0, 0x13, 0xd; 0x10042160 modes 3, 6, 0xd) and keeps the block-recovery bookkeeping (stats lastblockframe 0x5f, fasterblockrate 0x66, stun length 0x42); player-only and monster-only branches | `SUnitDmg.cpp` family, `0x44a9b0` (apply), `0x44ce80` (hit) | vanilla copy; **done** (D2R's) |
| 0x1005c290 | class damage bonus: strength and dexterity (stats 0, 2) times two charstats shorts (+0x106, +0x108), returned as a pair | `0x44c030` reads the same charstats columns | vanilla; **done** |
| 0x1005c340 | damage of one elemental type (1 fire .. 5 poison stats 0x30-0x3a, 0xb burning 0x13c/0x13d) plus the unit's mastery percent for it (0x100a6de0: stats 0x149-0x14c, 0x165 = D2R's passive_*_mastery 329-332, 357) | `0x44df10` family | vanilla; **done** |
| 0x1005c450 | the same for one pair of stats with a pierce / mastery stat | same | vanilla; **done** |
| 0x1005c4f0 | physical weapon damage (min/max): stats 0x15/0x16, or 0x17/0x18 when the class record asks for the second weapon, times (100 + damagepercent 0x19 + passive_mastery_melee_dmg 0x157 + the str/dex bonus of 0x1005c290) / 100 | `0x44c030` | vanilla; **done** |
| 0x1005c640 | unit-class test: the hireling type index of the monster (0x10067760, a D2Hirelings.cpp table walk) is one of 0x11, 0x13, 0xd, 0x47, 0x48, 0x49, 0x4d, 0x4e | n/a | used only by 0x1005bb60 to pick the damage owner (likely); **cut** (D2R's owner lookup) |
| 0x1005c6a0 | likely per-skill to-hit: evaluates the skill's calc at +0x144 (D2Common 10786) into the damage block (+0x69 short, +0x65) | `0x44c0a9`-`0x44ca67` | vanilla; **done** |
| 0x1005c720 | result flags: copies the caller's flags when bit 1 is set, else rolls the hit through 0x10057530 and ORs them | `0x44b930` wrapper + `0x44ef70` | vanilla shape; **done** |

Median's own damage rules are not in this module but in the skill do-functions: `srvdofunc 171` (area damage, ported:
`area_damage.cpp`), 161 (`Median's damage block`, plugin-spec.md), the pet scaling (`pet_scaling.cpp`) and
`D2SkillCalc`'s damage codes (`calc_codes.cpp`). D2Damage.cpp itself has no Median-only formula.

### 1.2 D2Combat.cpp (hit rolls and mitigation)

| D2Sigma | Role | D2R | Bucket / status |
|---|---|---|---|
| 0x10056d20 | queues a combat record on the defender (linked list at unit +0xac, 0x84-byte records), sets "stun" (2) when the summed damage exceeds the defender's life | `0x44aa5f`-`0x44ad9a` | vanilla; **done** |
| **0x10056e20** | **avoid check**: evade (stat 0x154) while walking / running, else block (mode 0xd), then avoid (0x153, missiles) or dodge (0x152, melee); the three passives are capped for players and mercenaries; returns 8 / 0x10 / 2 / 4 | **`0x44ef70`** (same results, no caps) | **native hook, done** (combat.cpp) |
| 0x10056fe0 | client: chance to hit a monster (character screen): attack rating vs the monster's defense with the level ratio, limited to 5-95 | the character sheet's own | vanilla; **done** (D2R UI) |
| 0x10057170 | client: chance a monster hits you (defense + armorclass_vs_hth) | same | vanilla; **done** |
| 0x10057300 | likely a difficulty-dependent defence value: monsters read their monstats row (+0x168 + difficulty), players get -12 / -25 / -50 | `difficultylevels.txt` columns | data (`difficultylevels.txt` migrated); **done** |
| 0x100573a0 | block chance of a unit: stat toblock (0x14) + 0x100bd8d0 for players; 0 when the monster's record forbids it (flag 0x4000000) | `0x450070` (hidden body) | vanilla shape; the formula is 2.2 below |
| 0x10057460 | "can block" wrapper: avoid check 0x10056e20 when called for hits, else the block roll of 0x100573a0 (a third of it for a player without a shield-type item) | `0x44b930` (calls 0x44ef70) | **done** |
| 0x10057530, 0x10057690 | hit attempt: line of sight / distance test (D2Common 11138), the to-hit roll, then the avoid check mapped to result flags (avoid 0x100, dodge 0x80, block 0x8000, evade clears the hit); the second form is the missile version | `0x44b2b0` (melee hit), `0x45bc40` (missile hit) | vanilla; **done** |
| 0x100577c0 | likely the damage percent between two units: 100 normally; 25 (0x19) between two mercenary-class units, a monstats value for a pet owner, 17 (0x11) for monsters hitting players, 0x15e / 200 for monsters with a negative owner state | `difficultylevels.txt` `PlayerDamagePercentVS*`, `MercenaryDamagePercentVS*` | data (migrated columns); **done**; PvP check in 4 |
| 0x10057a20 | absorb step of the damage block: the defender's absorb percent (cap 40) and flat stat cut the damage and the cut amount heals the defender (added to the block's absorbed total). Median runs every credited amount through the defender's stats 189 (divide, never below 1 point), 184 (percent off) and 417 (percent off, layer = damage type 1 fire / 2 lightning / 3 magic / 4 cold / 5 poison); the damage cut is unchanged | `0x450670` (called from the per-hit step `0x4523e0`) | **native hook, verified in game** (combat.cpp) |
| **0x10057c10** | the same three stats applied to the damage fields of a block. **Live, corrected 2026-10-22**: the stub `0x1009b7e0` calls it, and D2Sigma patches D2Game rva `0xdc4ea` so it replaces the call to `6fcfbd00` (the per-type mitigation step of `SUnitDmg`, `6fcfc0b0`). It wraps the original step, see section 2.4 | `0x4523e0` (the per-type step; `0x44df10` calls it for each of the twelve entries) | **native hook, done** (combat.cpp, off word `damagered`) |

### 1.3 D2Stats.cpp

| D2Sigma | Role | D2R | Bucket / status |
|---|---|---|---|
| 0x100b41b0 | the unit's level capped at the class' maximum level (experience table), times 10000: the fixed-point base of the `/lvl` stats | D2R's per-level stats (`op` columns) | likely; **done** (data) |
| 0x100b4240 | resist stat to its maximum stat (0x25 -> 0x26, 0x27 -> 0x28, 0x29 -> 0x2a, 0x2b -> 0x2c, 0x2d -> 0x2e) | resistance / maximum-stat ids in D2R's damage table (`0x44df10`, entry +8 / +0xc); the bundled `itemstatcost.maxstat` cells are blank | **done** |
| 0x100b42c0 | **resist limits**: players and mercenaries get 75 + the maximum-resist stat, never above 90; damage resist (0x24) limited to 50; monsters 1,000,000 | the clamp immediate in `0x4523e0` (and the two UI displays) | **done**: patched to 90 |
| 0x100b43a0 | difficulty resist penalty: the difficultylevels row's ResistPenalty for the resist stats 0x27, 0x29, 0x2b, 0x2d, 0x6e | `difficultylevels.txt` ResistPenalty (-30 / -70 migrated) | **done** (data) |
| 0x100b44a0 | `GetResist(stat)`: stat + penalty, at most the limit, at least -100 (damage resist: no penalty, at most 50) | D2R's resist read in `0x44df10` | vanilla shape; limits as above |
| 0x100b45d0 | calls a `std::function` for every 8-byte entry of a stat list's modifier array | n/a | utility; **cut** |

### 1.4 D2ItemStats.cpp, D2ItemCalc.cpp

| D2Sigma | Role | D2R | Bucket / status |
|---|---|---|---|
| 0x1007fd70 | at item creation: when max durability (73) is non-zero, durability and max durability become the base scaled by item_maxdurability_percent (75), at least 1 | D2R does the same in item creation | vanilla; **done** |
| **0x1007fdd0** | **ethereal**: marks the item (0x400000 plus a guard flag 0x100000) and adds **25%** to the weapon damage stats (0x15-0x18, 0x9f, 0xa0) or the defense (0x1f) | **`0x3d3510`** (x3/2) | **native hook, done** (combat.cpp) |
| 0x1007fef0, 0x1007ff70 | dexterity / strength requirement: the base from the item record (+0x10c / +0x10a) scaled by item_req_percent (0x5b), minus 10 when ethereal, at least 0 | same (D2R's requirement code) | vanilla; **done** |
| 0x100800b0 | set the number of sockets (stat 0xc2) to at most what the item type allows (D2Common 11151) | D2R's socket code | vanilla; **done** |
| 0x100700c0 | item calc operand: stat of the item (`param_2 == 1`) or of its owner, for the itemstatcost `op` columns | D2R's op codes | vanilla; **done** |

### 1.5 D2Properties.cpp (property functions)

D2Sigma replaces nine of D2Common's property functions with its own (code 0x1009cba0-0x1009d1a0, table at
0x101dd000). The slot minus 28 is the properties.txt `func`: consistent with the data (the rows that carry propertiesext defaults
use func 11 and 24, which are the wrapper slots; 27's layer / value shape matches its stat's tooltip; 22 is inferred). 0x1009cb20 is their shared helper (find or create the stat list node of a property on the item).

| Func | D2Sigma | What it does | D2R | Bucket / status |
|---|---|---|---|---|
| 11, 22, 24 | 0x1009d020, 0x1009d0e0, 0x1009ce00 | **wrappers** of D2Common's skill-event, single-skill and charges functions: **propertiesext** gives a default parameter (below), then the original runs | D2R's 11, 22, 24 (`0x3d12b0`, `0x3cc980`, `0x3d10a0`) | **data, done** (default written into the rows) |
| 25 | 0x1009cdb0 | value = param << 16 \| min << 8 \| max, layer 0 | slot 25 is D2R's | no Median row uses it; **cut** |
| 26 | 0x1009cba0 | set stat = min, layer = param | slot 26 empty | no Median row uses it; **cut** |
| **27** | 0x1009cbe0 | add stat: layer = param << 8 \| min (16 bits), value = max | slot 27 empty | **native, done** |
| **28** | 0x1009ccf0 | add stat: layer = param (16 bits), value = min (default param from propertiesext) | slot 28 empty | **native, done** |
| **29** | 0x1009cc30 | add stat: layer = param (16 bits), value = max (default param from propertiesext) | slot 29 empty | **native, done** |
| 30 | 0x1009cec0 | D2Common's "stat = random(min, max) << ValShift", plus a hack: stat 498 with min -666 removes the stat | D2R's func 1 | no Median row uses it; **cut** |
| n/a | 0x1009c930 | `class id < 7` | | utility (vanilla seven classes); **cut** |

Rows using the new functions (work/named): func 27 `innate_fire_damage_1f_of` (**107 of the 186 automagic rows**, the
elemental weapon prefixes EleWeapon1H1-4 / 2H1-4, 5 uniques, 14 cube recipes, 2 runes, 2 set items; layer =
`2 << 8 | element`, value = the percent; the stat has no reader in D2Sigma's server code, its tooltip is descfunc 35
"ModitemEleInnateSeq", so it is a display marker. The attribute-scaled damage comes from hidden passive skills granted by pr375, in addition to the sibling flat firemindam / lightmindam / coldmindam mods; see [damage-ied-audit.md](../damage-ied-audit.md));
func 28 `properties_419` (35 amulet uniques, a 1 / 1 / 1 marker on stat 494); func 29 `item_mana_bytime_2` / `_3`
(boss monster properties: stat "syn1" with the skill as layer and a percent value, the synergy `calc_codes.cpp` reads).
D2R's dispatcher (`0x3d4620`) looks the entry up in the 8-byte table at **0x2386ab0** (count 37 at 0x2386bd8) and silently
ends the property when it is empty, so before this lane those slots did nothing.

**propertiesext** (437 rows x one int32; `work/sigma/propertiesext.txt`; `tools/sigma_tables.py` names the field
`DefaultParam`): the **default parameter** of a property. It is read by the three wrappers above: if the property row's
ext value is a valid row (`FUN_100af700`: below a table count at D2TxtRecords +0xba0, likely the skills) and the property in the item or monster
property slot has parameter 0, the parameter becomes the ext value. Only four rows are non-zero: Median
`item_mana_bytime_2` (548), `properties_360` (548), `item_mana_bytime_3` (547), `properties_362` (547) - the skills
`mysterious_companion_skill` (547 -> 619) and 548 (-> 620). They are used by 33 monprop rows of bosses (Radament, Dark
Countess, Zhar, Nihlathak, general of vices, ... and the mysterious companion). D2R's functions have no such wrapper,
so `txt_migrate.py` now writes the migrated skill id into every empty parameter cell of those properties
(`property_defaults`, 36 cells in monprop; run after `remap_property_params`).

### 1.6 D2Respec.cpp

| D2Sigma | Role | D2R | Bucket / status |
|---|---|---|---|
| 0x100a2330 | the respec: 0x100a2170 (skills), 0x10042410 (stat points back to the class start values), 0x100a2120 (re-check equipment), then a client message | `0x580f20` (skills `0x4360f0`, stats `0x52ddf0`, equipment `0x470c90`) | same sequence; **done** |
| 0x100a2170 | skills: for every class skill of the player's class (D2Common 11120 / 10044), except the class' ten charstats starting skills (the list from charstats +0xae..+0xc2), add its level to a total, set the skill to 0, refresh; the total is added to stat 5 (new skill points) | `0x4360f0` | same; **done** |
| 0x100a2120 | registers the equipment re-check callback (a `std::function` lambda on a stat list) | `0x470c90` | **done** |
| 0x100a23a0 | equipment re-check: an item that no longer meets its requirements is unequipped (D2Common 10945, 11111, 11108 on the item) | `0x470c90` | **done** |

Difference to D2R: none in effect. Median calls the respec from an NPC action (npcactions row, Akara), D2R from client
packet 0x39 (`0x4b2530`). `npc_actions.cpp` already calls D2R's `0x580f20` and then `0x5d9ae0` (quest 41 bookkeeping, so
the respec is used up as in D2R). Nothing to implement. One thing to confirm in game: the ten charstats starting skills
stay (D2R's `0x4360f0` is believed to skip them as D2Sigma does).

## 2. What was implemented (plugin)

### 2.1 Property functions 27-29 (`properties_ext.cpp`, `properties_ext_logic.h`)

Fills the empty slots 27-29 of D2R's property function table with handlers that compute layer and value (section 1.5) and
call D2R's shared stat adder `0x3d5940` (dataFlag, type, unit, item, property, set, stat, layer, value, state, flags,
extra) the way D2R's own function 1 (`0x3cf960`) does; the stat comes from the row's `stat` column. Entry bytes of
`0x3d5940` are checked; the three slots are written with `PatchWriteU64` only while empty. Off word: `props`.
Unit tests: `plugin/tests/combat_test.cpp`.

### 2.2 Avoid / dodge / evade and block limits (`combat.cpp`, `combat_logic.h`)

D2R's `0x44ef70` (attacker unit, defender unit, missile flag; not a Game) is hooked. It runs, then for a player and a success whose stat is above
Median's cap the success is kept with probability cap / stat (one extra roll on the unit's seed), which equals rolling on
the cap. Caps: dodge / avoid / evade `min(75, 60 + stat 380)`, block `min(80, 50 + stat 387)`, 50 with the skill
Indomitable (base level > 0). Stats are the migrated ids (Median 205 -> 380, 213 -> 387; skill 1063 -> 1135).
The Indomitable lookup takes the data bank flag from the defender (`0x34a0e0`); until 0.51.0 it read byte `+0x106` of the attacker unit. Block uses D2R's own block chance (`0x450070`) as the "stat", so it can only be lowered to Median's cap, never above D2R's
75. Off word: `combat`.

### 2.3 Ethereal bonus (`combat.cpp`)

D2R's `0x3d3510` (make ethereal; callers `0x3ccccb`, `0x4434ff`, `0x527043`) is replaced: it sets the flag 0x400000 and
applies +25% instead of x3/2 to the same stats in the same order (weapon class 0x2d: 0x15-0x18, 0x9f, 0xa0; else 0x1f). The
guard flag 0x100000 of D2Sigma is not set (unknown meaning in D2R). Same off word `combat`.

### 2.4 Not implemented

- **Maximum resistances (90, not 95): done.** The earlier note (the resist read sits behind a protection thunk) was half right:
  the *stat getter* `0x2f5020` (and 38 more stat functions) is a 10-byte `jmp [rip+x]` thunk into **D2RCore.dll**,
  D2RLoader's own module that reimplements the stat engine (docs/d2r-addresses.md, section 10). The resist code itself is
  plain main-image code: the per-hit step `0x4523e0` (called for each of the twelve entries of `0x44df10`, through a
  D2RLoader wrapper at `0x3e2b496` that only tracks damage) clamps `75 + max-resistance stat` with `mov eax,0x5f` at
  `0x4524dd`; the character sheet (`0x14e7294`, function `0x14e6e70`) and the hireling panel (`0x14f4686`, function
  `0x14f3400`) have the same clamp for the displayed number. `InstallCombat` patches the immediate of all three from `0x5f`
  to `0x5a` (`PatchWriteU8` with the five instruction bytes as the expected value). Damage resistance (stat 36, cap 50)
  is a separate constant and already matches. Verified in game: fire resist 300 with max-resist 0 / +10 / +20 took
  25% / 15% / 10% of a fire bolt's damage (95 would have been 5%).
- **Median absorb stats 184 / 189 / 417 (D2R 369 / 373 / 446): done.** D2Sigma's absorb step (`0x10057a20`) is D2R's
  `0x450670`: percent first (stat limited to 40), then flat (`stat * 256`), each limited to what is left, and the cut
  amount is added to the damage block's absorbed total (`+0x130`), which `0x44ce80` turns into life through `0x451820`.
  Median additionally runs *each credited amount* through the defender's stats: 189 divides it (never below 1 life
  point), 184 and 417 take a percent off (417 by damage type: the layer is 1 fire / 2 lightning / 3 magic / 4 cold /
  5 poison, taken from the resistance stat of D2R's table entry). The damage cut itself is not changed, so these stats
  only change how much an absorb *heals*. No generic item rolls them: 189 and 417 are monster stats (monprop rows `time`
  and `stam`, 439 and 965 uses; a few skills), 184 is on two runewords (+5, -50) and on the Barbarian hireling (+20).
  Stat 189 doubles as the "immune to open wounds / crushing blow" flag (items-xp.md section 5). The hook replaces
  `0x450670` (15 verified entry bytes), keeps D2R's arithmetic and only changes what is credited; the first six cases
  where the credit differs from the cut are logged (`combat: absorb of N healed M (layer L)`). Verified in game: absorb 40%
  with fire resist 0 against a fire bolt shooter, net loss 70 / 106 / 249 per 30 s with 446 layer 1 = 0 / 50 and no absorb
  (the test Barbarian also carries stat 369 = 7 from a source not found, which is why the credit is 7% lower in the log).
  Median's second helper `0x10057c10` (the same stats on the damage fields) is **not** dead code (corrected 2026-10-22, [sigma-patch-audit.md](../sigma-patch-audit.md)):
  stub `0x1009b7e0` reaches it from D2Game rva `0xdc4ea`, so the stats also cut incoming damage; ported, section 2.4.
- **Damage reduction stats 184 / 189 / 417 on the incoming damage itself: done (lane pa-damagered, off word `damagered`).**
  What D2Sigma does, read from the code (`tools/sigma_fn.py`, `tools/disasm.py`) and not guessed:
  - The loop of `SUnitDmg` (D2Game `6fcfc0b0`, call at rva `0xdc4e9`) runs the per-type step `6fcfbd00` for the twelve entries of
    the damage table (`6fd22ab0`: Phys, Fire, Ligt, Cold, Magc, CLen, FLen, PLen, Pois, Life, Mana, Stam; D2Sigma points the loop at its
    own copy `0x101d8ea0`, which differs in one cell). D2Sigma redirects the call to the stub `0x1009b7e0` (`push [esp+4]; mov edx,eax;
    call 0x10057c10; ret 4`: ecx = context, edx = table entry, stack = the "no resistance" flag).
  - `0x10057c10` starts with `0x10041e00`, which is `push [esp+4]; mov eax,edx; call [6fcfbd00]; ret 4` (the pointer `0x101f1d44`
    is the import of `6fcfbd00`). So the **original step runs first, whole** (flat reduction, resistance, the absorb with Median's
    credit): the audit's worry that the step "is not run any more" does not apply. Nothing is lost; the stats come on top.
  - Then, when the entry's field (`block + entry[0]`) is not 0, it reads the defender's stats and applies them one after the other
    to the updated field: **189** (`0x10057d20`): a damage field becomes `field / divisor` (signed, truncating; **no floor** unlike the
    absorb credit's one life point), a duration (cold, freeze, poison length) becomes `max(field >> 1, 5)` whatever the divisor; **184**
    (`0x10057f10`): a damage field loses `field * stat / 100` (64-bit product, truncating); **417** (`0x10057f10` again, layer = the type
    from `0x10057d70`): the same cut. The percent steps do nothing to durations. The sets are two jump tables: damage offsets
    `0x8, 0x10, 0x14, 0x1c, 0x20, 0x24, 0x28, 0x38, 0x3c, 0x40` (physical, fire, burn, lightning, magic, cold, poison, the three
    leeches) and durations `0x18, 0x2c, 0x30, 0x34, 0x44`; a step needs its stat non-zero and the field above 0.
  - Layers of 417: fire 1, burn 11, lightning 2, magic 3, cold 4, poison 5, life leech 6, mana leech 7, stamina leech 8; physical
    and the durations 0. (The absorb credit uses 1-5 only: its layer comes from the resistance stat.)
  - Order against the rest of the hit: resistance, flat reduction and absorb first (inside the original step), so the **absorb credit
    is computed from the damage before these stats** and the heal is not reduced twice; then 189, 184, 417 on what is left; then the
    total of the block is summed from the fields.
  - D2R: `0x4523e0` (ctx, entry, flag) is the twin of `6fcfbd00` and returns the field's new value (it ends with `mov [r15],edi`,
    `mov eax,edi`). Entry `+0` points at the field, `+0x30` at its name string, so the hook (`HitStepHook`) calls the original, reads
    the name (the first four characters give kind and layer, `combat_logic.h` `DamageTypeOfName`), reads the defender's 373 / 369 /
    446 (`ctx + 0x18`) and writes the reduced field and the return value, which D2RLoader's wrapper (`0x3e2b496`) tracks. Entry
    bytes `48 89 6c 24 18 56 41 54 41 55 41 56 41 57 48 83 ec 50`, 18 bytes, no rip-relative operand. The immediate of the max-resist
    clamp at `0x4524dd` lies inside the same function, far from the hook.
  - Deviations: a result under 0 is held at 0 (a divisor or percent that is negative or above 100 would heal in Median; no Median
    row has one). Everything else is Median's arithmetic.
  - Who has the stats: monsters (monprop `time` / `stam` rows, 439 and 965 uses), a few skills, two runewords and the Barbarian
    hireling for 184. Stat 189 is also the "immune to open wounds / crushing blow" flag (items-xp.md section 5), so a monster with
    that immunity also takes damage divided by the value of the stat.
  - Check `damagered` (game_checks): fire, lightning, cold and magic hits at resist 200 against the player (25000 left of 100000)
    with each stat alone and all three on odd numbers; the damage block's field after the step equals the formula to the unit and
    the life lost is within 2 of it; the absorb credit of a hit with absorb and 189 equals the credit of the damage before the
    stats; freeze and cold lengths halve to 5 at least and ignore 184 / 417; physical takes 417 layer 0. Harness `hit` got the
    elements `pois life mana stam clen flen plen` and prints `field=`.
- **Mercenary caps: done, not exercised in game.** Resistances: the per-hit step tests the context flag `ctx+0x24`
  ("defender is a monster that is not a hireling"), which is 0 for mercenaries, so they already take the player branch and
  the patched 90 cap. Avoid / dodge / evade: D2Sigma caps them for "player or hireling" (`0x100bfad0` or `0x100bf9e0`), the
  block cap (`0x100bd8d0`) only for players; the `0x44ef70` hook now accepts a monster for which `0x3af240` is non-zero
  for the first three and keeps the player test for block.
- The **moving-without-evade fall-through**, the **PvP percent** (data columns exist) and the
  **client-only hit-chance panels** (D2R's character sheet has its own) stay open.

## 3. Open questions / in-game checks

1. Run `mxl` and read the two new lines: `combat on: ...` and `property functions on: ... (3/3)`.
2. A Barbarian / Amazon with 90 passive avoid and no "maximum avoid chance" item: the avoid rate should settle near 60%.
3. A weapon with the EleWeapon automagic affix (Act 1 drop): it should load (func 27 used to be skipped) and show its
   elemental damage mods; the innate line needs the descfunc 35 tooltip (UI lane).
4. An ethereal weapon or armour: +25% base damage / defense (D2R without this lane: +50%).
5. Radament (Act 2): the `item_mana_bytime_3` monster property now has its skill (619); needs a fresh `txt_migrate.py`
   run so that monprop carries the parameter.
6. Respec at Akara: the class' starting skills (Attack, Kick / Throw, ...) keep their levels.

### Maximum-resistance report: works as Median (lane maxres-cap, 2026-10-25)

The reported 90% ceiling is intentional. Keep `min(75 + maximum-resistance stat, 90)` for players and hirelings;
ordinary resistance gear alone cannot raise the initial 75% limit. No gameplay change or plugin version bump is needed.

Evidence available in this repository (no classic extraction required):

- `docs/sigma-patch-audit.md`, row **game6-005**, records D2Sigma replacing both damage-clamp immediates at
  D2Game RVAs `0xdb468` / `0xdb46c` with **0x5a (90)**. The D2Stats audit above independently records the
  matching player / mercenary display limit in `0x100b42c0`, used by `GetResist` (`0x100b44a0`).
- `assets/median/2_txt/itemstatcost.txt` ids **37/38, 39/40, 41/42, 43/44, 45/46** are resistance / maximum pairs
  (magic, fire, lightning, cold, poison). Maximum stats have 7 Save Bits and Save Add 20, which is storage range,
  not an effective cap. `maxstat` is blank in these source rows and in the installed migrated rows.
  `properties.txt` property **pr146** grants stats is40/is42/is44/is46 together; **pr166/pr180/pr175/pr200** grant
  individual elemental maxima. For example `uniqueitems.txt` **AzmodansHeart2** grants pr146 +2. These bonuses
  raise the initial limit; they do not override the engine's ceiling. No contrary rule was found in the bundled data.
- D2R 3.3 build 93847: `0x4524a1` reads the maximum-stat id from damage-table entry +0xc, `0x4524bb` adds 75,
  and `0x4524dd` clamps to 95. `combat.cpp::InstallCombat` changes that immediate to 90. The character sheet
  (`0x14e7294`) and hireling panel (`0x14f4686`) get the same immediate change. Off word **combat** disables
  these existing patches and the other combat rules; the existing `mxl` combat line reports the three clamps.

The input tree is labelled 2.14.4 in `assets/median/README.md`, while its `version.mxl` says 2.14.6. This report
uses the checked-in data and the recorded D2Sigma audit; it does not infer a different cap from that label mismatch.

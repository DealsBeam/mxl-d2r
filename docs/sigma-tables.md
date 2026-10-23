# D2Sigma-only tables

30 tables (plus 7 name-index tables) that only D2Sigma.dll reads. The other half of D2Sigma's engine changes, its run-time patches of the 1.13c game code, is audited in
[sigma-patch-audit.md](sigma-patch-audit.md). Tools:

- `tools/sigma_tables.py init|evidence|decode`: schema skeleton, evidence reports (`research/sigma_evidence/`,
  regenerable, not committed), decoded TSVs (`work/sigma/`, or `decode --out DIR` to compare after a schema change).
  A field with `"formula": "skills2code"` is printed as the formula it points to (`skills2code#<entry>: <text>`).
- `tools/sigma_evidence.py`: field accesses for a table (`<array> <rowsize>`), `--func NAME`, `--slot OFFSET`.
- `tools/sigma_imports.py`: the 2,356 game-function pointers D2Sigma resolves at startup
  (`research/sigma_imports.json`; module + offset + 1.13c decompile name).
- `tools/sigma_tables.py coverage`: per table the named columns and the non-zero bytes no field covers (the Status
  table below is its output).
- Schema: `research/sigma_schema.json` (generated once from the field descriptors D2Sigma registers, then hand-edited;
  the source of truth for names). A field's `name` is the txt column name of the descriptor (the `mxl_extracted/4_sigma_source`
  tables and `2_txt/sigma_named/`); `col` is the decoded column when it differs; types `u16x<N>` (N words, printed as a
  list) and `bits` (flag word, one 0/1 column per bit) were added for them.

## How the tables work
- `.bin` = u32 row count + packed fixed-size rows. D2Sigma keeps the same layout in memory: rows at
  `*(DAT_101f32ac + array)`, count at `*(DAT_101f32ac + count)`, read through bounds-checked getters
  (`D2TxtRecords.h`). Fields are packed (16/32-bit values at odd offsets).
- **16 tables extend a vanilla table row for row** (skills2, missiles2, monstats3, armor2/weapons2/misc2,
  uniqueitems2, objects2, propertiesext, itemtypesext, cubemainext, pettypeext, monumodexp, charstatsexp,
  hiredescext, npcext). For the port they become extra columns keyed by the vanilla row's name.
- **String ids** are `table * 10000 + index` (0 string, 1 patchstring, 2 expansionstring); **5382 = none**.
  1,414 Median strings have no key name; their key is their own id (e.g. "11860").
- The `*names` / `*types` tables only hold 0..N-1 (name-to-index lookups built at compile time).

## Status
Ranked by size. "Named" is named txt columns / all txt columns (a flag word counts one column per flag), from
`tools/sigma_tables.py coverage`. **All 428 columns of the 37 tables are named** and no non-zero byte is left uncovered
(2026-10-22: the names are the ones D2Sigma's own field descriptors register, checked against the code that reads each
field; before, 402 of 428 were named and 27 of 37 tables were complete).

| Table | Rows | Row size | Location | Named |
|---|---|---|---|---|
| skills2 | 3018 | 66 | extends `skills`, mem +0x78 | 20/20 |
| cubemainext | 7500 | 24 | extends `cubemain` | 6/6 |
| monstats3 | 3895 | 44 | extends `monstats`, mem +0x38/+0xf0 | 38/38 |
| misc2 | 1304 | 41 | extends `misc` | 5/5 |
| npcstores | 847 | 52 |  | 31/31 |
| weapons2 | 688 | 41 | extends `weapons` | 5/5 |
| armor2 | 476 | 41 | extends `armor` | 5/5 |
| npcext | 49 | 336 | extends `npc` | 149/149 |
| mysticorbs | 101 | 143 | mem +0x40/+0xf8 | 34/34 |
| objects2 | 589 | 15 | extends `objects`, mem +0x58/+0x10c | 6/6 |
| statscreen | 242 | 33 | mem +0x88/+0x138 | 15/15 |
| uniqueitems2 | 1834 | 4 | extends `uniqueitems`, mem +0x8c/+0x13c | 3/3 |
| missiles2 | 5609 | 1 | extends `missiles` | 1/1 |
| objpreset | 750 | 4 |  | 1/1 |
| stateicons | 236 | 8 | mem +0x84/+0x134 | 5/5 |
| propertiesext | 437 | 4 | extends `properties`: default skill parameter, 4 rows non-zero ([combat-stats.md](subsystems/combat-stats.md)) | 1/1 |
| npcactions | 31 | 52 | mem +0x48/+0xfc | 13/13 |
| requirements | 33 | 44 | mem +0x74/+0x124 | 11/11 |
| pettypeext | 83 | 14 | extends `pettype`, mem +0x60 | 9/9 |
| itemclasses | 110 | 5 | mem +0x24 | 3/3 |
| itemtypesext | 348 | 1 | extends `itemtypes` | 1/1 |
| cellfiles | 2 | 130 |  | 2/2 |
| skilltabs | 30 | 8 | mem +0x7c | 5/5 |
| infographics | 2 | 74 | mem +0x1c | 27/27 |
| hireclasses | 5 | 29 | mem +0x10/+0xd0 | 15/15 |
| monumodexp | 43 | 2 | extends `monumod`, mem +0x3c/+0xf4 | 1/1 |
| requirementtypes | 38 | 2 |  | 1/1 |
| requirementnames | 33 | 2 |  | 1/1 |
| npcactionnames | 31 | 2 |  | 1/1 |
| qualities | 10 | 5 | mem +0x68/+0x11c | 4/4 |
| charstatsexp | 8 | 6 | extends `charstats` | 2/2 |
| npcactiontypes | 22 | 2 |  | 1/1 |
| skilltree | 7 | 4 | mem +0x80/+0x130 | 2/2 |
| hiredescext | 10 | 2 | extends `hiredesc`, mem +0x14/+0xd4 | 1/1 |
| hireclassnames | 5 | 2 |  | 1/1 |
| difficultylevelnames | 3 | 2 |  | 1/1 |
| infographicsnames | 2 | 2 |  | 1/1 |

## Findings so far
- **skills2** (66 bytes): six aurastat layers and five passivestat layers (u32 each; the Median layer numbers),
  a cooldown group, three damage flags (stat-scaled damage, no damage-type bonus, alternate tooltip), the skill's base
  level cap `MaxLevel` (calc code `mlvl`) and four formula offsets into `skills2code` (`ManaCalc`, `LvlManaCalc`,
  `MaxLevelCalc` = `skcn`, plus `Calc32`, empty in every row). The formula columns are linked: the decode prints
  `skills2code#<entry>: <formula>`.
- **cubemainext**: gold cost / 100, the second condition (op, param, value, layer) and the first condition's layer.
- **npcext** (336 bytes): act, service flags, three service requirements, class greetings and sounds, six idle lines
  and 16 menu slots of 16 bytes (`Slot01Requirement` ... `Slot16PromptConfirm`: requirement, action, label, prompt label, menu id (10 = submenu entry), weight, confirm prompt; 149 columns in all); **npcactions**: type, requirement, notice flag,
  ten values; **requirements**: type, three arguments, children 4-10; **npcstores**: NPC, item code, difficulty bytes,
  five quality groups (requirement row, min, max, area level range).
- **monstats3** (44 bytes, 38 columns): `Flags` (15 bits: `NoLifebar`, `Automap`, `AutomapShowName`, the rank bits
  `Elite` / `Veteran` / `Uber` / `Heroic` / `Boss` that pick the name colour, `Goblin`, `RecordDamage`, ...),
  `NameColor`, `AutomapNameColor`, `AutomapVectorColor`, `DrawModeType`, the **spawn overlays** (`Overlay1/2`,
  `BossOverlay1/2`, `OverlayType`; these were first read as sound fields), the boss event (`BossEventMode`, two
  missiles), `PortraitGfx`, `Achievement`, the goblin rarity / level window, `NoKillTimestampUpdate`, the kill
  currency (`KillCurrencyBank/Add/Shared`) and `HireClass` (the hireclasses row of a mercenary).
- **mysticorbs** (143 bytes): level requirement, description, two included and two excluded item types, the cap per item,
  six property slots (prop, param, min, max) and the two quality bytes.
- **armor2 / weapons2 / misc2** share one 41-byte layout (the three are concatenated into one item table by
  `FUN_100b8010`: weapons, armor, misc): `Flags` (bit 0 `Filterable`), `Classifications` (up to 16 itemclasses rows,
  a comma list of names in the txt; the reader is the loot filter, `D2LootFilter.cpp`), `InteractString` (hover text in
  interact mode; set on the 20 trophy items), `Difficulty` (item spell 16 only works on difficulty N-1; the charged
  riftstones) and `Infographic` (the help panel the client opens on item event 13, packet 0x16; only the catalyst of
  disenchantment).
- **missiles2** `PalShift` (palette shift stamped on the missile at creation, 25 rows; the HD stand-in is chosen by the
  shifted colour, see hd-effects.md section 6), **itemtypesext** `NoShowILvl`
  (10 types hide the item level line), **propertiesext** `DefaultParam` (see above), **qualities** `Automagics`,
  `Runeword`, `EthChance` (1/1000 ethereal chance, 25 or 0) and `MaxSock` (qualities 5-9 get the most sockets).
- **hireclasses**, **pettypeext**, **uniqueitems2** (an equip limit per unique group), **charstatsexp**
  (skill tab titles), **itemclasses** and the **infographics** text blocks (text, font, colour, x, y) are named too.
- **statscreen** (33 bytes, 15 columns): page, type, x, y, `Text`, `Color` (+11), `Font` (+12; the two were swapped in the
  first reading), `Stat`, `Layer`, `IsBase`, `IsPercent`, `Align`, `HoverText`, `Min` (zero), `Max` (was `Cap`).
  `Min` is unread (no reader found): the `FUN_10117c10` dispatch and `engine/ui/StatScreen.cpp` handlers were searched
  for a row +0x19 read; the source descriptor still names the field `min`, and all 242 values are zero.
- **stateicons**: which states show a bottom-panel icon, which side, icon frame, tooltip
  (confirmed against `mxl_extracted/buff_timer_hook.cpp`).
- **skilltabs**: skill tree tabs (class, tab, title string, screen position).
- **objects2**: three treasure classes by difficulty (`Treasureclass_N/NM/H`; only the Halloween chest sets the Hell
  one) and the currency pattern as a **cost to operate** an object (`OperateCurrencyBank/Cost/Shared`).

## Still unnamed
Nothing: every txt column is named. For the former Phase 1 remainder, the field descriptors in
`../mxl_extracted/4_sigma_source/tables/sigma_tables.h` and the headers in `assets/median/2_txt/sigma_named/`
agree with the schema. The read paths are in the schema notes: `FUN_100e6040` / `FUN_10084df0` for item flags and
classes, `FUN_1008b100` for missile palette shifts, `FUN_10078fb0` for hidden item levels, and the property wrappers
documented in [combat-stats.md](subsystems/combat-stats.md). `statscreen` fields are also checked against the UI reads
in [stat-screen.md](subsystems/stat-screen.md). Fields that change game behaviour and that the port does not read yet
are listed in [unported.md](unported.md) ("Sigma table fields found in the phase 1 leftover").

The `skilltree` words are `PosX` and `PosY` in the field descriptors and decoded txt header. A direct
`sigma_evidence.py 0x80 4` scan reports no field accesses because the code uses wrappers: `FUN_100b09f0` gets the row,
`FUN_100b09b0` reads its first word, `FUN_100b09d0` reads its second, and `FUN_101114c0` in `ui/SkillTree.cpp` uses
both. The old "unread, probably" note was incorrect. Where a schema note says "no reader found", it describes a
searched field with an official txt name, not an inferred name.

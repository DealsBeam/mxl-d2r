# D2Sigma-only tables

30 tables (plus 7 name-index tables) that only D2Sigma.dll reads. Tools:

- `tools/sigma_tables.py init|evidence|decode`: schema skeleton, evidence reports (`research/sigma_evidence/`,
  regenerable, not committed), decoded TSVs (`work/sigma/`, or `decode --out DIR` to compare after a schema change).
  A field with `"formula": "skills2code"` is printed as the formula it points to (`skills2code#<entry>: <text>`).
- `tools/sigma_evidence.py`: field accesses for a table (`<array> <rowsize>`), `--func NAME`, `--slot OFFSET`.
- `tools/sigma_imports.py`: the 2,356 game-function pointers D2Sigma resolves at startup
  (`research/sigma_imports.json`; module + offset + 1.13c decompile name).
- Schema: `research/sigma_schema.json` (hand-edited; the source of truth for names).

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
Ranked by size. "Named" is named fields / all fields, counting every byte column that is not zero in all rows
(2026-10-21: names from the D2Sigma code that reads each table; "done" = no unnamed byte column is left).

| Table | Rows | Row size | Location | Named | Status |
|---|---|---|---|---|---|
| skills2 | 3018 | 66 | extends `skills`, mem +0x78 | 20/20 | done |
| cubemainext | 7500 | 24 | extends `cubemain` | 6/6 | done |
| monstats3 | 3895 | 44 | extends `monstats`, mem +0x38/+0xf0 | 22/23 | partial |
| misc2 | 1304 | 41 | extends `misc` | 3/8 | partial |
| npcstores | 847 | 52 |  | 31/31 | done |
| weapons2 | 688 | 41 | extends `weapons` | 0/9 | structure only |
| armor2 | 476 | 41 | extends `armor` | 0/9 | structure only |
| npcext | 49 | 336 | extends `npc` | 130/130 | done |
| mysticorbs | 101 | 143 | mem +0x40/+0xf8 | 34/34 | done |
| objects2 | 589 | 15 | extends `objects`, mem +0x58/+0x10c | 3/4 | partial |
| statscreen | 242 | 33 | mem +0x88/+0x138 | 4/17 | partial |
| uniqueitems2 | 1834 | 4 | extends `uniqueitems`, mem +0x8c/+0x13c | 2/2 | done |
| missiles2 | 5609 | 1 | extends `missiles` | 0/1 | structure only |
| objpreset | 750 | 4 |  | 1/1 | done |
| stateicons | 236 | 8 | mem +0x84/+0x134 | 5/5 | done |
| propertiesext | 437 | 4 | extends `properties`: default skill parameter, 4 rows non-zero ([combat-stats.md](subsystems/combat-stats.md)) | 0/1 | done (txt_migrate.py property_defaults) |
| npcactions | 31 | 52 | mem +0x48/+0xfc | 13/13 | done |
| requirements | 33 | 44 | mem +0x74/+0x124 | 11/11 | done |
| pettypeext | 83 | 14 | extends `pettype`, mem +0x60 | 8/8 | done |
| itemclasses | 110 | 5 | mem +0x24 | 3/3 | done |
| itemtypesext | 348 | 1 | extends `itemtypes` | 0/1 | structure only |
| cellfiles | 2 | 130 |  | 2/2 | done |
| skilltabs | 30 | 8 | mem +0x7c | 5/5 | done |
| infographics | 2 | 74 | mem +0x1c | 22/22 | done |
| hireclasses | 5 | 29 | mem +0x10/+0xd0 | 15/15 | done |
| monumodexp | 43 | 2 | extends `monumod`, mem +0x3c/+0xf4 | 1/1 | done |
| requirementtypes | 38 | 2 |  | 1/1 | done |
| requirementnames | 33 | 2 |  | 1/1 | done |
| npcactionnames | 31 | 2 |  | 1/1 | done |
| qualities | 10 | 5 | mem +0x68/+0x11c | 3/4 | partial |
| charstatsexp | 8 | 6 | extends `charstats` | 3/3 | done |
| npcactiontypes | 22 | 2 |  | 1/1 | done |
| skilltree | 7 | 4 | mem +0x80/+0x130 | 2/2 | done |
| hiredescext | 10 | 2 | extends `hiredesc`, mem +0x14/+0xd4 | 1/1 | done |
| hireclassnames | 5 | 2 |  | 1/1 | done |
| difficultylevelnames | 3 | 2 |  | 1/1 | done |
| infographicsnames | 2 | 2 |  | 1/1 | done |

## Findings so far
- **skills2** (66 bytes): six aurastat layers and five passivestat layers (u32 each; the Median layer numbers),
  a cooldown group, three damage flags (stat-scaled damage, no damage-type bonus, alternate tooltip), the skill's base
  level cap `MaxLevel` (calc code `mlvl`) and four formula offsets into `skills2code` (`ManaCalc`, `LvlManaCalc`,
  `MaxLevelCalc` = `skcn`, plus `Calc32`, empty in every row). The formula columns are linked: the decode prints
  `skills2code#<entry>: <formula>`.
- **cubemainext**: gold cost / 100, the second condition (op, param, value, layer) and the first condition's layer.
- **npcext** (336 bytes): act, service flags, three service requirements, class greetings and sounds, six idle lines
  and 16 menu slots of 16 bytes (`Slot01Requirement` ... `Slot16Order`); **npcactions**: type, requirement, notice flag,
  ten values; **requirements**: type, three arguments, children 4-10; **npcstores**: NPC, item code, difficulty bytes,
  five quality groups (requirement row, min, max, area level range).
- **monstats3** (44 bytes): description flags and type, automap colour, sounds, the goblin draw weight and level
  window, the kill-stamp flag, the currency reward and `HireClass` (the hireclasses row of a mercenary).
- **mysticorbs** (143 bytes): level requirement, description, two included and two excluded item types, the cap per item,
  six property slots (prop, param, min, max) and the two quality bytes.
- **hireclasses**, **pettypeext**, **qualities**, **uniqueitems2** (an equip limit per unique group), **charstatsexp**
  (skill tab titles), **itemclasses**, **misc2** (description string, difficulty, a client notice id) and the
  **infographics** text blocks (text, style, x, y) are named too.
- **stateicons**: which states show a bottom-panel icon, which side, icon frame, tooltip
  (confirmed against `mxl_extracted/buff_timer_hook.cpp`).
- **skilltabs**: skill tree tabs (class, tab, title string, screen position).
- **objects2**: the same currency pattern as a **cost to operate** an object.

## Still unnamed
No code in the decompile reads these through the table container (D2Sigma reaches them through pointers stored at load
time), so the remaining bytes stay unnamed until a subsystem triage finds the reader: armor2 and weapons2 (41-byte rows
shaped like misc2: small ids at +0x00..+0x0e, no reader found), missiles2, itemtypesext, propertiesext, objects2 `+0x04`,
statscreen (only the probable X, Y and two strings are named), monstats3 `+0x04`, qualities `+0x02`.
Offsets named with "probable" or "unread" in `research/sigma_schema.json` have a weaker basis (skilltree, statscreen,
npcext slot order, pettypeext UI groups).

# D2Sigma-only tables

30 tables (plus 7 name-index tables) that only D2Sigma.dll reads. Tools:

- `tools/sigma_tables.py init|evidence|decode`: schema skeleton, evidence reports (`research/sigma_evidence/`,
  regenerable, not committed), decoded TSVs (`work/sigma/`).
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
Ranked by size. "named" counts fields with a meaningful name.

| Table | Rows | Row size | Location | Named | Status |
|---|---|---|---|---|---|
| skills2 | 3018 | 66 | extends `skills`, mem +0x78 | 0/33 | structure only |
| cubemainext | 7500 | 24 | extends `cubemain` | 0/6 | structure only |
| monstats3 | 3895 | 44 | extends `monstats`, mem +0x38/+0xf0 | 8/15 | partial |
| misc2 | 1304 | 41 | extends `misc` | 0/0 | structure only |
| npcstores | 847 | 52 |  | 0/13 | structure only |
| weapons2 | 688 | 41 | extends `weapons` | 0/0 | structure only |
| armor2 | 476 | 41 | extends `armor` | 0/0 | structure only |
| npcext | 49 | 336 | extends `npc` | 0/84 | structure only |
| mysticorbs | 101 | 143 | mem +0x40/+0xf8 | 0/1 | structure only |
| objects2 | 589 | 15 | extends `objects`, mem +0x58/+0x10c | 3/4 | partial |
| statscreen | 242 | 33 | mem +0x88/+0x138 | 0/0 | structure only |
| uniqueitems2 | 1834 | 4 | extends `uniqueitems`, mem +0x8c/+0x13c | 0/2 | structure only |
| missiles2 | 5609 | 1 | extends `missiles` | 0/0 | structure only |
| objpreset | 750 | 4 |  | 1/1 | done |
| stateicons | 236 | 8 | mem +0x84/+0x134 | 5/5 | done |
| propertiesext | 437 | 4 | extends `properties` | 0/1 | structure only |
| npcactions | 31 | 52 | mem +0x48/+0xfc | 0/13 | structure only |
| requirements | 33 | 44 | mem +0x74/+0x124 | 0/11 | structure only |
| pettypeext | 83 | 14 | extends `pettype`, mem +0x60 | 0/7 | structure only |
| itemclasses | 110 | 5 | mem +0x24 | 0/0 | structure only |
| itemtypesext | 348 | 1 | extends `itemtypes` | 0/0 | structure only |
| cellfiles | 2 | 130 |  | 2/2 | done |
| skilltabs | 30 | 8 | mem +0x7c | 5/5 | done |
| infographics | 2 | 74 | mem +0x1c | 13/22 | partial |
| hireclasses | 5 | 29 | mem +0x10/+0xd0 | 0/3 | structure only |
| monumodexp | 43 | 2 | extends `monumod`, mem +0x3c/+0xf4 | 1/1 | done |
| requirementtypes | 38 | 2 |  | 1/1 | done |
| requirementnames | 33 | 2 |  | 1/1 | done |
| npcactionnames | 31 | 2 |  | 1/1 | done |
| qualities | 10 | 5 | mem +0x68/+0x11c | 0/4 | structure only |
| charstatsexp | 8 | 6 | extends `charstats` | 1/3 | partial |
| npcactiontypes | 22 | 2 |  | 1/1 | done |
| skilltree | 7 | 4 | mem +0x80/+0x130 | 0/2 | structure only |
| hiredescext | 10 | 2 | extends `hiredesc`, mem +0x14/+0xd4 | 1/1 | done |
| hireclassnames | 5 | 2 |  | 1/1 | done |
| difficultylevelnames | 3 | 2 |  | 1/1 | done |
| infographicsnames | 2 | 2 |  | 1/1 | done |

## Findings so far
- **monstats3**: per-monster sounds for normal vs champion/unique (`-1` = none), an achievement flag, and a
  **currency reward on kill** (type, amount, owner flag; D2Currency.cpp).
- **objects2**: the same currency pattern as a **cost to operate** an object.
- **stateicons**: which states show a bottom-panel icon, which side, icon frame, tooltip
  (confirmed against `mxl_extracted/buff_timer_hook.cpp`).
- **skilltabs**: skill tree tabs (class, tab, title string, screen position).
- **infographics**: help panels with up to 5 text blocks and positions.

## Not reachable through the container
npcext, npcstores, cubemainext, statscreen, armor2/weapons2/misc2, missiles2, itemtypesext, propertiesext and
most of skills2/mysticorbs show no direct reads in the decompile. D2Sigma most likely reaches them through
pointers stored at load time (e.g. in the vanilla records). Naming them belongs with each subsystem's triage
(Phase 3), where those code paths get read anyway.

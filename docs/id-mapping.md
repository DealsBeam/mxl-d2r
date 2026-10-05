# Row identity and ID mapping

## The problem
Median's compiled `.bin` tables keep no key names (properties `code`, ItemStatCost `Stat`, States `state`,
MonStats `Id`, Missiles `Missile`, Misc `name`, CubeMain `description`...). The decoder wrote placeholders
(`pr116`, `is467`, `st194`, `ms0`, `ml98`, `mi0`, `cm0`). Counts of placeholder references across
`mxl_extracted/2_txt`: pr 41,278 · ms 18,360 · sk 10,639 · is 10,338 · cm 7,506 · st 2,782 · sd 1,861 · mi 1,304.

## Median keeps vanilla rows at vanilla indexes
Rows compared against vanilla 1.13c (`D2Compare_102/TXT/113c`) at the same index, >=80% of shared columns
equal (blank == 0):

| Table | Median rows | 1.13c rows | aligned |
|---|---|---|---|
| overlay | 609 | 293 | 292 |
| objects | 589 | 573 | 571 |
| monstats2 | 1,634 | 609 | 606 |
| missiles | 5,609 | 684 | 671 |
| states | 967 | 185 | 177 |
| properties | 437 | 268 | 238 |
| monstats | 3,901 | 734 | 562 |
| skills | 3,018 | 357 | 293 |
| itemstatcost | 511 | 359 | 251 |
| misc | 1,304 | 151 | 60 |
| skilldesc / levels / superuniques | | | low (reworked) |

The engine (1.13c and D2R) addresses stats, states, skills, missiles, monsters and overlays **by row index**,
so index is a row's identity. Rows below the 1.13c count keep their vanilla name even where Median changed
the values.

## Collision with D2R's added rows
D2R appended rows directly after the 1.13c ranges, and its own code depends on several of them:

| Table | 1.13c | D2R 93847 | Median | first D2R additions |
|---|---|---|---|---|
| itemstatcost | 359 | 368 | 511 | skill_cooldown, skill_missile_damage_scale, psychicward |
| states | 185 | 232 | 967 | cooldown, sharedstash, hidedead |
| skills | 357 | 429 (base 427) | 3,018 | Interact, Loot, TownPortal |
| missiles | 684 | 742 (base 736) | 5,609 | recklessattacksmissile, leapattack |
| monstats / monstats2 | 734 / 609 | 751 / 623 | 3,901 / 1,634 | horse, cowking, darkelder |
| overlay | 293 | 334 | 609 | warlock casts, apocalypse |
| properties | 268 | 284 | 437 | noconsume, pierce-immunity-* |
| objects | 573 | 587 | 589 | ControlObject, desert shrines |
| levels | 137 | 138 | 282 | Act 5 - Colossal Summit |
| skilldesc | 221 | 263 (base 232) | 793 | |

Median's rows from the 1.13c count upward land on the same IDs.

## Banks
D2R loads `data/global/excel/base/*.txt` (Classic/LoD) and `data/global/excel/*.txt` (RotW).
Which bank a Median (LoD-based) character uses must be confirmed in game.

## Decision (2026-10-05): shift Median after D2R
D2R's rows stay where they are; Median's new rows start after D2R's last row.

## Resolver: `tools/mxl_names.py`
Output (ignored by git): `work/named/<table>.txt`, `work/named/idmap.json`, `work/named/report.txt`.

- Vanilla rows (below the 1.13c count) keep their ID and take **D2R's** name (D2R renamed some:
  `nehlithak*` -> `nihlathak*`, Sword Mastery -> Blade Mastery, `cr_lancer8` -> `cr_lancer9`, misc labels).
  skilldesc is the exception: D2R re-sorted it alphabetically and it is referenced by name only, so it keeps
  the 1.13c name for Median's row.
- **Repurposed slots:** D2R filled 1.13c's unused stat slots 183-213 with new stats (`lasthitreactframe`,
  `item_pierce_*_immunity`, `passive_mastery_*`...). Median also uses 20 of them, so those 20 Median stats
  move to the new-row range (IDs 368+) and D2R keeps the slots.
- New rows: ID = D2R row count (larger bank) + offset. Names: in-game text (skill names, NameStr,
  LevelName, descstrpos, overlay Filename), else the skill/monster/parent missile that uses the row, else
  `mxl_<entity>_<row>`.
- References are resolved per column (>= 90% placeholders), per cell for mixed columns (monpreset `Place`
  mixes monsters, superuniques and monplace codes). Skill and treasure-class columns the first decoder had
  already turned into display names are mapped back.

Result: 271 reference columns, ~85,000 references rewritten, 0 unresolved, all IDs unique.

| Entity | Median rows | new | new IDs start |
|---|---|---|---|
| itemstatcost | 511 | 172 | 368 |
| properties | 437 | 169 | 284 |
| states | 967 | 782 | 232 |
| skills | 3,018 | 2,661 | 429 |
| missiles | 5,609 | 4,925 | 742 |
| monstats | 3,901 | 3,167 | 751 |
| monstats2 | 1,634 | 1,025 | 623 |
| overlay | 609 | 316 | 334 |
| levels | 284 | 147 | 138 |
| superuniques | 90 | 24 | 66 |

Not yet remapped: the compiled formulas (`*code.txt`), which reference stats, skills and states by number.
That is the formula codec's job, using `idmap.json`.

## Row counting (verified against D2R's own ID columns)
Every row is a record except "Expansion" separator rows; rows with an empty key are records too (monsounds,
montype and the affix tables start with one). `skills *Id`, `monstats *hcIdx`, `overlay *ID`, ... all match this
rule. An earlier version skipped empty-key rows and shifted monsounds by one; fixed.

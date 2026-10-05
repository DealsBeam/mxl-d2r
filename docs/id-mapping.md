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

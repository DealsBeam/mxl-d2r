# txt-migrate: Median tables -> D2R layout

Tool: `tools/txt_migrate.py`. Pipeline:

```
python tools/mxl_names.py        # names + IDs   -> work/named/, idmap.json
python tools/formula.py convert  # formula cells -> work/named/ (in place)
python tools/txt_migrate.py      # D2R layout    -> work/mod/data/global/excel/{,base/}
```

Output (ignored by git, Median-derived): both banks, `base/` (Classic/LoD header) and the RotW folder, because
which bank a Median character loads is still to be confirmed in game. Report: `work/mod/migrate_report.md`.

## Row merge by table kind
| Kind | Tables | Rule |
|---|---|---|
| indexed | every table the engine reads by row number (skills, states, itemstatcost, missiles, monstats, monstats2, overlay, properties, levels, objects, monai, pettype, monprop, ...) | row *i* = Median row with new ID *i*, else D2R's row *i*. D2R additions (Interact, skill_cooldown, HeraldThorns, ControlObject, ...) keep their IDs; Median's new rows follow. The smaller bank borrows missing IDs from the other. |
| replace | weapons, armor, misc, treasureclassex, uniqueitems, setitems, sets, affixes, runes, cubemain, gamble, monpreset, ... | Median rows only. Median replaced the vanilla item bases (`hax`, `axe`, ... do not exist in Median); merging them back would let them drop through the auto `weapN`/`armoN` treasure classes. |
| d2r_wins | sounds, soundenviron, skillcalc, misscalc, playerclass, plrtype, storepage | D2R rows, then Median rows D2R lacks (D2R's audio system and its calc-code order must stay; Median's extra calc codes are appended). |
| keyed | the rest (itemtypes, skilldesc, monsounds, ...) | Median rows, then D2R rows whose key Median lacks. |

## Columns
- Same name (case-insensitive) copies. Curated renames: `Hralti*`->`Hratli*`, `Drehya*`->`Anya*`,
  monstats `TreasureClass1..4` -> `TreasureClass`/`Champ`/`Unique`/`Quest`, `NoShldBlock`->`ShieldBlockOverride`
  (same meaning: 1 = blocks without a shield), levels `MonLvl1..3(Ex)` -> `MonLvl`/`(N)`/`(H)`,
  `HireableBossDamagePercent`, skills `delay`->`localdelay`, uniqueitems `enabled`->`spawnable`,
  itemtypes `MaxSock1/25/40` -> `MaxSockets1..3` + thresholds 25/40, hireling `Resist` -> 4 element columns,
  soundenviron `EAX *` -> `SFX EAX *`.
- skills stop functions: D2R gives Whirlwind (srvdofunc 76 / cltdofunc 45) srvstopfunc and cltstopfunc 1 (they end
  the spin and its aurastate when the skill is released or interrupted); 1.13c rows had none. Median rows on a do
  function D2R pairs with a stop function get the same one (whirlwind_2, windform; `skill_rules`).
- missiles: D2R merged `LevRange` into `Range` (an expression now): `Range` becomes `Range+LevRange*lvl`.
- automap: the `.bin` stores `LevelName` as a level-type number (lost by the first decoder). D2R's automap
  names are its lvltypes names reformatted ("Act 1 - Town" -> "1 Town"), so vanilla types map exactly.
- D2R-only columns: from the matching vanilla D2R row (same ID/key), else empty. `*ID`-style comments get the ID.
- Median values with no D2R column -> `work/mod/overflow/<table>.txt` (nothing is lost). Columns D2R dropped on
  purpose (old ID columns, Beta/Expansion markers, overlay Frames, legacy sound mixing) are only counted.

## Open items found here (for plugins / later phases)
- **Runewords**: 197 of 237 use property slots 8-13; D2R's runes.txt has 7.
- **Objects**: `OperateRange` (545), `SpawnMax` (223), `TrapProb` (161), `Parm5-7` (~100), `Act` (584) have no D2R
  column. Median's object rows 573+ moved after D2R's (IDs 587+); **Median's map files (.ds1) reference objects by
  number and must be remapped in Phase 5.**
- **Automap**: Median's new level types 36-44, 51 have no D2R automap name. Test whether naming them
  "Act N - X" in lvltypes is enough, or whether the engine needs a plugin.
- **Calc codes**: Median added 38 skill-formula variables (`syn1-6`, `bl12..bdz7`, `area`, `skcn` used ~1,600 times,
  `mnhp`, ...) and 2 missile ones (`blvl`, `msyn`). They are appended to skillcalc/misscalc so formulas compile;
  a plugin must implement them (D2Sigma extended the calc variable callback).
- Smaller overflow: missiles `Qty`/`SpecialSetup`, skills `general`, itemstatcost `Divide`, itemtypes
  `CostFormula`, hireling equipment, charstats stat percentages, superuniques `EClass`, ...
- Tables with no D2R equivalent (arena, chartemplate, hiredesc, monitempercent, uniquetitle) are skipped;
  they need custom tables if their behaviour matters.

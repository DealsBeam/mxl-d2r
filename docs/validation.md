# Validation

Tools: `tools/validate.py` (checks), `tools/build.py` (whole pipeline: names, formulas, Sigma tables, strings,
migration, validation; rebuilds `work/named` and `work/mod` from scratch).

## Schema source
D2R's own data guide (`extract/d2r/<build>/data/global/dataguide/data/files/*.js`) types every field:
`reference` with its target table/column, `string` (string key), `text` with a maximum length and sometimes an
allowed-value list, `int`, `boolean`, `parse` (BBE formulas by scope, or property codes / parameters), and enum
validations (`enums.js`). armor/weapons/misc share fields through `SharedItems`, the affix tables through
`SharedItemMods`. The BBE reference (`docs/bbe-calc.html`) gives the formula scopes: skill (skillcalc codes),
missile (misscalc), monster (moncalc); item and treasure-class formulas take no identifiers. D2R added a
`cond('Name', param)` function (IsType, IsClass, Desecrated, Difficulty, MonsterTestElite, ItemIsType,
ItemIsModType, MonsterHasMod); none checks a state, so `mxl_hasstate` still needs a plugin.

## Severities
- **error**: rejected value or a reference to something that does not exist.
- **plugin**: valid once a D2RLoader plugin provides it.
- **warn**: D2R's own data has the same value, or the string key is missing from Median's own tables too.

## Current result (both banks)
**2 errors**: `stat('strength'.320)` in two skills' `prgcalc1` (broken bytecode in Median's own data; the 4
such cells are listed in work/formulas/report.md and need a manual decision).

**Plugin work (base bank, 4,667):**
| Kind | Uses | Notes |
|---|---|---|
| Median calc codes | 2,844 | `mlvl`/`skcp`/`skcn` in every skill's `skpoints` (~540 each), `syn1`-`syn6` (~580), `wdm` 141, `skcd` 129, `aura` 90, `bl12..` , `area` 58, `mnhp`, `mnar`, `pass`, `maxe`, missile `blvl`/`msyn` |
| Median level types | 975 | automap rows for level types 36-44, 51; D2R: "level types are static defined values that cannot be added" |
| Engine values outside D2R's lists | 702 | item use functions (`misc.pSpell`), missile hit functions, stat description functions (`descfunc`), cube ops, property functions, quest flags, state colour shifts, sound falloff, ... each a D2Sigma feature |
| Median formula functions | 140 | `mxl_statlayer` 76, `mxl_hasstate` 54, `mxl_itemstat` 8, `mxl_skilllevel` 1, `mxl_tabpoints` 1 |
| Median `stat()` modes | 6 | modes 5, 6 (and 86 in a broken formula) |

## Fixes the validator drove (all in the pipeline now)
- Generated names respect D2R key lengths (31 for missiles/objects, 39 lvlwarp, 47 others): ~2,400 errors.
- **Blank-key rows are records.** D2R's ID columns confirm: only "Expansion" rows are skipped. monsounds and
  montype start with a blank record; skipping it had shifted every monster-sound name by one.
- automap decoded straight from `automap.bin` (the first decoder's text did not line up with the records).
- D2R-only columns pointing into tables Median replaces (Terror Zone "Desecrated" treasure classes) are left
  empty; rows kept from D2R (RotW Colossal monsters) get such dangling references emptied.
- hireling `HireDesc` is a string key in D2R (`rs1` -> `strhirespecial1` via the Sigma table hiredescext);
  hireling is Median-only (D2R repeats the key per level, so key-merging re-added D2R's mercenaries).
- monseq keeps D2R's own sequence groups after Median's.
- Numeric string keys stay numeric (`115` = "Axe (1)"; item `namestr` cells use them; they are unique).
- Single-row formula columns are converted too; booleans normalised to 0/1; a stray control byte fixed.
- `sigma_tables.py` wrote `\r\r\n` line endings; fixed.

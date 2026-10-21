# Formulas (calc bytecode)

Tool: `tools/formula.py`
- `check`: decodes all 17,116 Median formulas and compares them with the first decoder's text.
- `convert`: rewrites the formula cells of `work/named/{skills,skilldesc,missiles,misc}.txt` with D2R-named
  text, writes full listings to `work/formulas/<file>.txt` and a report to `work/formulas/report.md`.
  Run it after `tools/mxl_names.py` (it reads `idmap.json` and edits the named tables in place).

## Bytecode
Taken from Fog.dll's evaluator (ordinal 10253, `FUN_6ff69e90` in the 1.13c decompile):

| Op | Meaning |
|---|---|
| `00` (or any unknown op) | end; the result is the top of the stack |
| `01 i` | call function `i` (pops its arguments) |
| `04 b` / `05 w` / `06 d` | variable (calc code) with 1/2/4-byte operand |
| `07` / `08` / `09` | push **signed** 8/16/32-bit constant |
| `0a`..`0f` | `<` `>` `<=` `>=` `==` `!=` |
| `10`..`14` | `+` `-` `*` `/` (x/0 = 0) `^` |
| `15` | unary minus |
| `16` | `cond ? a : b` |

Popping an empty stack gives 0 (Fog does not fail).

## Function tables
Vanilla tables, extended by D2Sigma through its patch records (D2Common `+0x99e5c` count 14 / code at
`+0x51c39` etc. -> table `0x101df3a8`; items table `0x101d94b4`; missiles table `0x101dc480`):

| Context | Functions |
|---|---|
| skills, skilldesc | 0 min, 1 max, 2 rand, 3 skill, 4 miss, 5 stat, 6 sklvl, **7 hasstate(state)**, 8, 9, **10 tabpoints(tab)**, **11 statlayer(stat, mode, layer)**, 12, **13 itemstat(bodyloc, stat)** |
| items (misc calc) | 0 min, 1 max, 2 rand, 3 stat, 4 (unused), **5 skilllevel(skill, flags)** |
| missiles | 0 min, 1 max, 2 rand, 3 skill, 4 (unused), **5 stat** (added by D2Sigma) |

What the Sigma functions do (from disassembly, `tools/disasm.py`):
- **hasstate(state)**: `STATES_CheckState` (D2Common #10494) on the unit.
- **tabpoints(tab)**: sum of skill levels of the unit's class skills whose skilldesc `SkillPage` is `tab`.
- **statlayer(stat, mode, layer)**: stat value at a layer; mode 0 = total, 1 = base, 2 = another D2Common query.
- **itemstat(bodyloc, stat)**: stat value on the item equipped in body location 1..12.
- **skilllevel(skill, flags)**: effective skill level including +skill bonuses (D2SkillValidation.cpp).

`skill()` sub-codes are skillcalc codes in every context; `miss()` sub-codes and missile-formula variables
are misscalc codes. Arguments that name rows (skill, stat, missile, state) are remapped through `idmap.json`.

## Results
`check`: 17,001 identical; 50 where the first decoder silently truncated at `fn11` (the old text lost most of
the expression); 44 that differ only in Sigma function naming; 20 malformed in Median's own data (decoded
and constant-folded with Fog's rules); 1 with a non-constant `stat()` argument. **0 unexplained.**

**Bug fixed:** the first decoder read 1-byte constants as unsigned, so every negative small constant in its
text was wrong (e.g. `236` instead of `-20`). Those wrong values were also in the decoded tables.

`convert` links each table cell to its bytecode exactly: formula cells in the `.bin` store the byte offset of
their formula in the concatenated buffer, and each column's offset inside the record is found by vote
(every row must agree). 74 formula columns, 11,329 cells:

| Table | Columns | Cells |
|---|---|---|
| skills | 34 | 7,964 |
| skilldesc | 30 | 2,560 |
| misc | 4 | 524 |
| missiles | 6 | 281 |

## For the D2R port
D2R's formula syntax matches (`skill('Name'.blvl)`, `stat('name'.accr)`, `miss('name'.code)`), but it has no
equivalents for the Sigma functions. In tables they appear as `mxl_hasstate` (~41 cells), `mxl_statlayer`
(~50), `mxl_itemstat` (6), `mxl_tabpoints` (1), `mxl_skilllevel` (1); see `work/formulas/report.md`.
Each needs either a D2RLoader plugin that adds formula functions (native hook on the calc function table,
like D2Sigma did) or a rewrite of the formula. 4 cells can't be expressed at all (Median's broken bytecode
passes a computed value where a stat name belongs) and need a manual decision.

`skills2code` (formulas used by the Sigma table skills2) is linked: skills2 `Calc32` (+0x32), `ManaCalc` (+0x36),
`LvlManaCalc` (+0x3a) and `MaxLevelCalc` (+0x3e) hold byte offsets into the formula bytecode (-1 = none), and
`tools/sigma_tables.py decode` prints them as `skills2code#<entry>: <formula>`. `export_skill_costs` and `export_skill_caps`
in `tools/formula.py` read the same three offsets.

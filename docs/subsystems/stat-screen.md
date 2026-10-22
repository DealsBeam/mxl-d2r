# Stat screen (D2Sigma ..\ui\StatScreen.cpp): Median's character sheet pages

Read 2026-10-21 (lane stat-pages). Result: Median's two extra pages are listed in D2R's Advanced Stats panel by data
(`itemstatcost.advdisplay`, tools/stat_screen.py + txt_migrate.py `advanced_stats`). The two things data cannot do - the
Str / Dex damage bonus rows and the total / % bonus / base columns - are done by two small plugin hooks on the panel's row
functions (section 3b, `stat_columns.cpp`, lane stat-columns 2026-10-22).

## 1. Median's side

`statscreen.bin` is the whole sheet as a table (u32 count 242, then 33-byte rows; D2Sigma keeps it as table `statscreen`,
array `+0x88`, count `+0x138`, schema in research/sigma_schema.json). The draw loop is `FUN_10117dc0` (rva 0x117dc0; asserts
`..\ui\StatScreen.cpp`): it walks the rows, takes those of the current page (255 = every page) and calls one handler per
type, each creating a `StatScreen:ContentItem` text component at the row's x, y (classic pixels of the 512 x 704 sheet).

| offset | field | meaning |
|---|---|---|
| +0 u8 | page | 0 main sheet (D2R's own panel draws it here), 1 and 2 the extra pages (stat-screen.dc6 pages 2 and 3), 255 the prev / next buttons (types 0x10, 0x11) |
| +1 u32 | type | 1 label, 2 stat value, 4 resistance (colour by cap), 0x12 stat with the unit's class as layer ("+N to <class> skills"), 0x15 stat / 10 (life regeneration, stat 74), 0x17 Strength / Dexterity damage bonus (below), 0x19 stat >> ValShift capped at `min(value, stat 205 + 60)` (evade / dodge / avoid, with the "/ 60%" label beside); 0x0d-0x0f page 0's name, class, vitals |
| +5 u16, +7 u16 | x, y | position |
| +9 u16 | label | string id (table * 10000 + index, so 26390 = expansionstring 6390; 5382 = none) of a type 1 row |
| +11 u8, +12 u8 | color, font | `Color` (text colour code, component +0x1c) and `Font` (font id, component +0x70: 6 on the value rows, 1 on the section headers, 4 on the column headers); the first reading had the two swapped |
| +13 u16 | stat | Median's itemstatcost row of a value row |
| +15 u16 | layer | the stat's parameter (0 on pages 1 and 2) |
| +17 u8 | base | 1 = `GetBaseStat` instead of the total (Strength / Dexterity / ... "base" column) |
| +18 u8 | percent | 1 = append "%" |
| +19 u32 | align | 0 right, 1 left, 2 centre |
| +23 u16 | tooltip | string id shown when hovering a label (Median's explanations) |
| +25 u32 | min | `Min`: zero in every row |
| +29 u32 | max | `Max` (was called cap): 0 none, 40 on the absorb percentages: `min(value, max)` |

Type 0x17 (rows 115 and 117, "Str. / Dex. Damage Bonus") is not a stat: stats 299 / 300 are placeholders for what
`FUN_1005c290` (D2Damage.cpp, handler `0x115e90`) computes: **total Strength x the wielded weapon's StrBonus / 100**
(row 115) and **total Dexterity x its DexBonus / 100** (row 117), a truncating 64-bit divide, 0 with no weapon or a
bonus of 0. (An earlier reading had two per-class charstats factors in it; the decompile has the weapon record's
`StrBonus` / `DexBonus` - the same terms D2R's damage function `0x44d710` adds.) The Advanced Stats list takes them as
rows 299 / 300 with the plugin supplying the number (section 3b).

Page 2 of the sheet ("General"): the four attributes as total / % bonus / base columns, life, mana, stamina, skill
levels, skill duration, Stat Signets (stat 185 of 400), weapon damage by element (min - max), physical damage %, innate
elemental damage %, Strength / Dexterity damage bonus, crushing blow, deadly strike, damage to demons / undead, attack
rating (flat and %), spell damage (elemental masteries and magic), Spell Focus (stat 485 / 1000), poison skill duration, and
the four elemental pierces. Page 3: speeds (attack, cast, block, hit recovery, movement), summon bonuses (damage, attack
rating, life, resistances / 105%), defenses (defense %, block chances, evade / dodge / avoid), stacked resists (six types),
damage absorb (percent and flat for four elements), physical / elemental damage reduction, curse length, poison length,
half freeze duration, cannot be frozen, sustain (regeneration, leech, on melee attack / striking, after each kill),
magic find, gold find, experience, light radius, slow target / attacker, activation frequency.

## 2. D2R's side: the Advanced Stats panel

`advancedstatspanelhd.json` (AdvancedStatsPanel: Title, ListContainer of `OptionTemplate` cells with a `StatText`, scroll bar,
close button) has no rows of its own. The client (D2R 93847, D2R Ghidra project; the panel class is `FUN_1414f1fe0` /
`FUN_1414ed3a0`, row text `FUN_1414eb250`, rebuild `FUN_1414eaf40`) builds them from **itemstatcost**:

- it walks every itemstatcost record (`+0x1258`, count `+0x1260`, 0x144 bytes each) and reads the byte at `+0x140`, the
  `advdisplay` column (D2R 3.x; empty in 1.13c tables);
- `advdisplay` 1: listed first, always; `advdisplay` 2: listed after those, only while the displayed value (stat >> ValShift)
  is not 0; empty: not listed. Order within a group = row order;
- the text is the stat's normal tooltip line (`FUN_1402d97e0` -> `FUN_1402d6520`, the item property formatter: descfunc,
  descval, descstrpos / descstrneg), the value being the unit's stat. Rows are not clickable, there are no section headers.

So the panel takes rows **by data**. No row table to patch, no hook needed. What it cannot do: Median's columns (total / % bonus /
base), grouped boxes, values computed by code (Str / Dex damage bonus, Spell Focus as a percentage, evade caps) and class
skills (the layer is fixed at 0).

## 3. What the build does (tools/txt_migrate.py `advanced_stats`, tools/stat_screen.py)

- `advdisplay` = 2 for exactly the 96 stats pages 2 and 3 show (value rows of types 2, 4, 0x15, 0x19, minus class skills,
  signets and the computed bonuses) and empty for the rest; D2R's own 97 rows (knockback, howl, ...) are gone. 13 of them
  are `advdisplay` 1, shown even at 0: attack rating, block chance, the five speeds and six resistances.
- The text of 65 of those stats read "+30 %+d to Strength" (the value twice): tbl2json.py keeps D2R's printf text
  ("%+d to Strength") where Median's has no placeholders, while Median's descfunc 1-4 puts the value in front. `advanced_stats`
  sets descfunc 19 (D2R's own: the text formats the value) on every listed stat whose text contains "%". This changes the
  tooltip line of those stats on items too (for the better; before: the doubled value).
- Not listed: stat 83 (class skills) and 185 (signets, no description). 299 / 300 are listed (`advdisplay` 1: shown even at
  0, like Median) and filled by the plugin; the percent stats that are columns now (below) are not rows.

Checked in game (HD, background mode, 2026-10-21): the button on the sheet's right edge opens the panel with, first, the thirteen
always rows (attack rating, block, six resistances, speeds), then the stats the character has ("+30 to Strength", "+3060 to
Life", "+50 Maximum Stamina", the pierce stats, ...). Scrolling was not testable (no wheel in background mode).

## 3b. The plugin side (`stat_columns.cpp`, `stat_columns_logic.h`, generated `mxl_statcols.h`; off word `statcols`)

Two inline hooks on the panel class's row functions (RVAs of D2R 93847):

| function | what it does | hook |
|---|---|---|
| `0x14eb1b0` (panel, record) | the value a row is listed by (`advdisplay` 2 rows show while it is not 0) | for stat 299 / 300 returns the computed damage bonus |
| `0x14eb250` (panel, string*, record) | the row text; the unit is `panel + 0x168` | for 299 / 300 rewrites the line with the computed bonus; for the attribute rows appends the columns |

- **Damage bonus**: the weapon is the item in body location 4 (right hand; a weapon swap moves items between 4 / 5 and
  11 / 12) of the unit's inventory (`0x34a360`, `0x3886d0`); `StrBonus` / `DexBonus` come from D2R's getters `0x3736e0` /
  `0x373490` (record `+0x116` / `+0x118`; a cell of 1 reads as 100, a blank cell is 0); the number is `stat_cols::DamageBonus`.
  The text is formatted by `0x2d6520` directly: the usual `0x2d97e0` first multiplies the value by the character level for
  the by-level ops (2-5), which the placeholder stats 299 / 300 carry.
- **Columns**: Median page 2 shows total / % stat / base for Strength, Dexterity, Vitality, Energy, Life and Mana
  and total / base for Stamina. The % stats are the Median rows 359-362 (the build maps them to D2R stat ids 388-391, the ID
  column of `work/named/itemstatcost.txt`) and the life / mana percent stats 76 / 77; `tools/stat_screen.py`
  `column_groups()` derives the groups from `statscreen.bin` (row, % stat) and `txt_migrate` writes them into `mxl_statcols.h`.
  The % stat rows drop out of the list (no `advdisplay`); the attribute row's line gets the suffix
  " (+N% bonus, M base)" (" (M base)" for Stamina), N = the % stat, M = the unit's base stat (`0x2f48c0`) >> ValShift.
  The total is the row's own value (`0x2f5020`); ValShift is 8 for life / mana / stamina.
- Install checks the entry bytes of both functions (15 and 14 whole instructions, no rip-relative bytes) and stays off when
  they differ; the status line reads "stat columns on: ...".

Checked in game (HD, slot 2, background mode, 2026-10-22, `py -3 tools/game_checks.py all statcols`; harness verb
`panelrow <stat>` runs the two row functions on a stand-in panel of the server's player unit): no weapon gives 0 / 0;
a 16 / 0 weapon (`hfh`) at Str 50 / 123 / 300 gives 8 / 19 / 48 % in the Strength row; a 0 / 6 weapon (`hst`) gives
Dex x 6 / 100 in the Dexterity row; a ring with +25% Strength / -10% Dexterity shows " (+25% bonus, N base)" and
" (-10% bonus, M base)". PASS. Not seen on screen: the real panel was not opened (no way to open it in background mode);
the lines are the ones the panel would draw.

Limits: the suffix is English text built by the plugin (no string-table entry); the columns are a suffix of the attribute
row, not three columns of Median's page art.

## 4. Left

- A second panel with Median's page art (stat-screen.dc6 pages 1 and 2, 60 boxes) and its own text widgets needs a plugin that
  feeds values to the boxes (the type table above is everything it needs) - not built; the flat list covers the content.
- Order and headings follow the stat id, not Median's sections.

## 5. All rows of pages 2 and 3

x, y are classic pixels on the 512 x 704 sheet; "label" is the nearest text to the left or the section header. Generated by
`py -3 tools/stat_screen.py`.

### Page 2 of the sheet (statscreen page 1)

| row | x,y | label (string id) | stat (Median row) | how the value is shown | tooltip |
|---|---|---|---|---|---|
| 49 | 253,102 | Strength (26390) | strength (0) | stat | Required to wear most items, increases damage on some builds |
| 50 | 337,102 | Strength (26390) | to_strength (359) | stat, % | Required to wear most items, increases damage on some builds |
| 51 | 421,102 | Strength (26390) | strength (0) | stat, base value | Required to wear most items, increases damage on some builds |
| 53 | 253,124 | Dexterity (26391) | dexterity (2) | stat | Required to wear certain items, provides Attack Rating, block chance with a shield, increases damage on some builds |
| 54 | 337,124 | Dexterity (26391) | to_dexterity (360) | stat, % | Required to wear certain items, provides Attack Rating, block chance with a shield, increases damage on some builds |
| 55 | 421,124 | Dexterity (26391) | dexterity (2) | stat, base value | Required to wear certain items, provides Attack Rating, block chance with a shield, increases damage on some builds |
| 57 | 253,146 | Vitality (26392) | vitality (3) | stat | Increases Life |
| 58 | 337,146 | Vitality (26392) | to_vitality (362) | stat, % | Increases Life |
| 59 | 421,146 | Vitality (26392) | vitality (3) | stat, base value | Increases Life |
| 61 | 253,168 | Energy (26393) | energy (1) | stat | Increases Mana Points, increases damage on most spells |
| 62 | 337,168 | Energy (26393) | to_energy (361) | stat, % | Increases Mana Points, increases damage on most spells |
| 63 | 421,168 | Energy (26393) | energy (1) | stat, base value | Increases Mana Points, increases damage on most spells |
| 65 | 253,190 | Life (26394) | maxhp (7) | stat |  |
| 66 | 337,190 | Life (26394) | item_maxhp_percent (76) | stat, % |  |
| 67 | 421,190 | Life (26394) | maxhp (7) | stat, base value |  |
| 69 | 253,211 | Mana (26395) | maxmana (9) | stat |  |
| 70 | 337,211 | Mana (26395) | item_maxmana_percent (77) | stat, % |  |
| 71 | 421,211 | Mana (26395) | maxmana (9) | stat, base value |  |
| 73 | 253,233 | Stamina (26396) | maxstamina (11) | stat |  |
| 74 | 420,233 | Stamina (26396) | maxstamina (11) | stat, base value |  |
| 76 | 210,257 | All Skill Levels (26397) | item_allskills (127) | stat |  |
| 78 | 422,257 | Class Skill Levels (26398) | item_addclassskills (83) | stat (class layer) |  |
| 80 | 210,279 | Skill Duration (26472) | skill_duration (409) | stat, % | Increases the duration of most skill effects |
| 82 | 400,279 | Stat Signets (26400) | itemstatcost_185 (185) | stat | Obtained from disenchanting items and killing enemies |
| 88 | 250,328 | Weapon Damage (26401) | firemindam (48) | stat |  |
| 89 | 300,328 | Weapon Damage (26401) | firemaxdam (49) | stat |  |
| 91 | 377,328 | Weapon Damage (26401) | coldmindam (54) | stat |  |
| 92 | 427,328 | Weapon Damage (26401) | coldmaxdam (55) | stat |  |
| 94 | 250,350 | Weapon Damage (26401) | lightmindam (50) | stat |  |
| 95 | 300,350 | Weapon Damage (26401) | lightmaxdam (51) | stat |  |
| 97 | 377,350 | Weapon Damage (26401) | magicmindam (52) | stat |  |
| 98 | 427,350 | Weapon Damage (26401) | magicmaxdam (53) | stat |  |
| 100 | 313,372 | Weapon Damage (26401) | poisonmindam (57) | stat |  |
| 101 | 363,372 | Weapon Damage (26401) | poisonmaxdam (58) | stat |  |
| 104 | 213,394 | W. Physical Damage (26404) | damagepercent (25) | stat, % | Multiplier to physical weapon damage (before conversion). Additive to Str/Dex Damage Bonus |
| 106 | 422,394 | Innate Ele Damage (26413) | innate_elemental_damage (484) | stat, % | Innate Elemental Damage multiplier. Only specific weapons provide Innate Elemental Damage |
| 115 | 213,416 | Str. Damage Bonus (26414) | item_tohit_undead_bytime (299) | str / dex dmg bonus, % | Weapon Physical Damage bonus from points in Strength |
| 117 | 422,416 | Dex. Damage Bonus (26415) | item_crushingblow_bytime (300) | str / dex dmg bonus, % | Weapon Physical Damage bonus from points in Dexterity |
| 113 | 213,438 | Crushing Blow (26406) | item_crushingblow (136) | stat, % | Chance to reduce a percent of enemy's life on hit. Does not work against bosses |
| 111 | 422,438 | Deadly Strike (26405) | item_deadlystrike (141) | stat, % | Chance to deal double physical weapon damage (before conversion) |
| 239 | 213,460 | Damage to Demons (3553) | item_demondamage_percent (121) | stat, % | Only affects weapon physical damage |
| 241 | 422,460 | Damage to Undead (3554) | item_undeaddamage_percent (122) | stat, % | Only affects weapon physical damage |
| 108 | 275,482 | Attack Rating (26407) | tohit (19) | stat | Increases the accuracy of your weapon damage skills |
| 109 | 402,482 | Attack Rating (26407) | item_tohit_percent (119) | stat, % | Increases the accuracy of your weapon damage skills |
| 119 | 275,509 | Spell Damage (26416) | passive_fire_mastery (329) | stat, % |  |
| 120 | 402,509 | Spell Damage (26416) | passive_cold_mastery (331) | stat, % |  |
| 121 | 275,531 | Spell Damage (26416) | passive_ltng_mastery (330) | stat, % |  |
| 122 | 402,531 | Spell Damage (26416) | passive_pois_mastery (332) | stat, % |  |
| 123 | 275,553 | Spell Damage (26416) | passive_mag_mastery (357) | stat, % |  |
| 124 | 402,553 | Spell Damage (26416) | passive_mag_mastery (357) | stat, % |  |
| 126 | 188,575 | Spell Focus (26417) | is485 (485) | stat | Multiplier to Spell Damage. 1% total damage per 10 Spell Focus |
| 129 | 422,575 | Poison Skill Dur. (26425) | bonus_to_poison_skill_durat (431) | stat, % | Increases the duration of poison spells |
| 131 | 275,602 | Elemental Pierce (26426) | passive_fire_pierce (333) | stat, % | Reduces enemy's resistance by a percentage |
| 132 | 402,602 | Elemental Pierce (26426) | passive_cold_pierce (335) | stat, % | Reduces enemy's resistance by a percentage |
| 133 | 275,624 | Elemental Pierce (26426) | passive_ltng_pierce (334) | stat, % | Reduces enemy's resistance by a percentage |
| 134 | 402,624 | Elemental Pierce (26426) | passive_pois_pierce (336) | stat, % | Reduces enemy's resistance by a percentage |

### Page 3 of the sheet (statscreen page 2)

| row | x,y | label (string id) | stat (Median row) | how the value is shown | tooltip |
|---|---|---|---|---|---|
| 137 | 211,72 | Attack (26453) | item_fasterattackrate (93) | stat, % | Attack Speed increases how quickly you perform melee swings or ranged weapon attacks |
| 139 | 324,72 | Cast (26454) | item_fastercastrate (105) | stat, % | Cast Speed increases how quickly you cast spells |
| 141 | 435,72 | Block (26456) | item_fasterblockrate (102) | stat, % | Block Rate determines how quickly you recover after blocking an attack |
| 143 | 265,94 | Hit Recovery (26455) | item_fastergethitrate (99) | stat, % | Hit Recovery determines how quickly you recover after being struck |
| 145 | 435,94 | Movement (26457) | item_fastermovevelocity (96) | stat, % | Increases walking and sprinting speed (Diminishing Returns) |
| 148 | 265,121 | Damage (26459) | to_summon_damage (470) | stat, % |  |
| 152 | 435,121 | Attack Rating (26461) | to_summon_attack_rating (500) | stat, % |  |
| 150 | 265,143 | Life (26460) | to_summon_life (444) | stat, % |  |
| 154 | 416,143 | Resistances (26462) | to_summon_elemental_resista (487) | stat |  |
| 158 | 210,197 | Bonus to Defense (26440) | skill_armor_percent (171) | stat, % |  |
| 164 | 405,197 | Evade (26441) | passive_evade (340) | stat >> shift, capped, % | Chance to ignore melee attacks and missiles while moving |
| 160 | 210,219 | Base Block Chance (26432) | toblock (20) | stat, % | One of two factors in calculating block chance (the other factor being Dexterity) |
| 167 | 405,219 | Dodge (26442) | passive_dodge (338) | stat >> shift, capped, % | Chance to ignore melee attacks while standing still, attacking, casting, and turning |
| 162 | 210,241 | Max Block Chance (26475) | maximum_block_chance (213) | stat, % | Extends the maximum block chance above 50% |
| 170 | 405,241 | Avoid (26443) | passive_avoid (339) | stat >> shift, capped, % | Chance to ignore missiles while standing still, attacking, casting, and turning |
| 173 | 253,263 | Stacked Resists (26437) | fireresist (39) | stat, % | Stacking resistances beyond their limit can be useful to counter debuffs or enemy elemental pierce |
| 174 | 337,263 | Stacked Resists (26437) | coldresist (43) | stat, % | Stacking resistances beyond their limit can be useful to counter debuffs or enemy elemental pierce |
| 175 | 421,263 | Stacked Resists (26437) | lightresist (41) | stat, % | Stacking resistances beyond their limit can be useful to counter debuffs or enemy elemental pierce |
| 176 | 253,285 | Stacked Resists (26437) | poisonresist (45) | stat, % | Stacking resistances beyond their limit can be useful to counter debuffs or enemy elemental pierce |
| 177 | 337,285 | Stacked Resists (26437) | magicresist (37) | stat, % | Stacking resistances beyond their limit can be useful to counter debuffs or enemy elemental pierce |
| 178 | 421,285 | Stacked Resists (26437) | damageresist (36) | stat, % | Stacking resistances beyond their limit can be useful to counter debuffs or enemy elemental pierce |
| 180 | 250,307 | Damage Absorb (26438) | item_absorbfire (143) | stat | Percent Absorb applies after resistances, reducing damage by a percentage and then healing that amount / Flat Absorb applies after Percent Absorb and also reduces damage then heals for a specific value |
| 184 | 300,307 | Damage Absorb (26438) | item_absorbfire_percent (142) | stat, %, cap 40 | Percent Absorb applies after resistances, reducing damage by a percentage and then healing that amount / Flat Absorb applies after Percent Absorb and also reduces damage then heals for a specific value |
| 181 | 377,307 | Damage Absorb (26438) | item_absorbcold (149) | stat | Percent Absorb applies after resistances, reducing damage by a percentage and then healing that amount / Flat Absorb applies after Percent Absorb and also reduces damage then heals for a specific value |
| 185 | 427,307 | Damage Absorb (26438) | item_absorbcold_percent (148) | stat, %, cap 40 | Percent Absorb applies after resistances, reducing damage by a percentage and then healing that amount / Flat Absorb applies after Percent Absorb and also reduces damage then heals for a specific value |
| 182 | 250,329 | Damage Absorb (26438) | item_absorblight (145) | stat | Percent Absorb applies after resistances, reducing damage by a percentage and then healing that amount / Flat Absorb applies after Percent Absorb and also reduces damage then heals for a specific value |
| 186 | 300,329 | Damage Absorb (26438) | item_absorblight_percent (144) | stat, %, cap 40 | Percent Absorb applies after resistances, reducing damage by a percentage and then healing that amount / Flat Absorb applies after Percent Absorb and also reduces damage then heals for a specific value |
| 183 | 377,329 | Damage Absorb (26438) | item_absorbmagic (147) | stat | Percent Absorb applies after resistances, reducing damage by a percentage and then healing that amount / Flat Absorb applies after Percent Absorb and also reduces damage then heals for a specific value |
| 187 | 427,329 | Damage Absorb (26438) | item_absorbmagic_percent (146) | stat, %, cap 40 | Percent Absorb applies after resistances, reducing damage by a percentage and then healing that amount / Flat Absorb applies after Percent Absorb and also reduces damage then heals for a specific value |
| 189 | 167,351 | Phys Damage Red. (26433) | normal_damage_reduction (34) | stat | Physical Damage Reduction, applies before Resistances |
| 193 | 308,351 | Curse Length Red. (26435) | curse_resistance (109) | stat, % | Reduces the duration of curses |
| 197 | 448,351 | Half Freeze Dur. (26474) | item_halffreezeduration (118) | stat |  |
| 191 | 167,373 | Ele Damage Red. (26434) | magic_damage_reduction (35) | stat | Elemental Damage Reduction, applies before Resistances |
| 195 | 308,373 | Poison Length Red. (26436) | item_poisonlengthresist (110) | resist, % | Reduces the length of poison damage dealt to your hero |
| 199 | 448,373 | Cannot Be Frozen (26439) | item_cannotbefrozen (153) | stat |  |
| 205 | 230,428 | Regeneration (26451) | hpregen (74) | stat / 10 | Life Regenerated per second / Percentage bonus to natural mana recovery |
| 206 | 359,428 | Regeneration (26451) | manarecoverybonus (27) | stat, % | Life Regenerated per second / Percentage bonus to natural mana recovery |
| 208 | 253,450 | Leech (26448) | lifedrainmindam (60) | stat, % | Leech a percentage of weapon physical damage dealt. Reduced effectiveness in nightmare and hell |
| 209 | 337,450 | Leech (26448) | manadrainmindam (62) | stat, % | Leech a percentage of weapon physical damage dealt. Reduced effectiveness in nightmare and hell |
| 214 | 253,472 | on Melee Attack (26450) | life_on_melee_attack (210) | stat |  |
| 215 | 337,472 | on Melee Attack (26450) | item_stamina_bytime (295) | stat |  |
| 211 | 253,494 | on Striking (26449) | life_on_striking (208) | stat | Life or Mana gained on any weapon hit |
| 212 | 337,494 | on Striking (26449) | mana_on_striking (209) | stat | Life or Mana gained on any weapon hit |
| 217 | 253,516 | After Each Kill (26447) | item_healafterkill (86) | stat |  |
| 218 | 337,516 | After Each Kill (26447) | item_manaafterkill (138) | stat |  |
| 221 | 210,570 | Magic Find (26464) | item_magicbonus (80) | stat, % | Does not affect items which don't roll quality like runes / Increases the chance items dropped will be Magic, Rare, Set or Unique |
| 223 | 426,570 | Gold Find (26465) | item_goldbonus (79) | stat, % |  |
| 225 | 210,592 | Experience Gain (26466) | item_addexperience (85) | stat, % |  |
| 227 | 422,592 | Light Radius (26473) | item_lightradius (89) | stat |  |
| 229 | 191,614 | Slow Target (26468) | item_slow (150) | stat, % | Reduces target movement and attack speed when dealing weapon damage. Cap of 25% against bosses |
| 232 | 404,614 | Slow Attacker (26470) | slows_attacker_by (363) | stat, % | Slows enemies when they attack or hit you with a missile |
| 235 | 426,636 | Activation Frequency (28609) | activation_frequency (427) | stat, % | Increases frequency of how often periodic skills activate their effects |


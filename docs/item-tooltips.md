# Item tooltip text (lane tooltip-verify, 2026-10-23)

Audit rank 3 (docs/sigma-owncode-audit.md section 11, items T15 / T16): the text of an item's property lines after Median's
`descfunc` change. Median-only descfuncs are 31-38; 65 stats had their wording rewritten. Everything below was read from the game
through the harness (no screenshots): `tipfmt` calls D2R's own property formatter `0x2d6520` for one stat at a chosen value and
layer, `tip` returns the lines of a real item as the game's item text builder produced them
([testing.md](testing.md) "tooltiptext"; [plugin-spec.md](plugin-spec.md) "Item property text").

## Result

The table at the end has one row per stat (and per group line) that has a property line in Median: stat, Median's descfunc (and the
descfunc of the built table when it differs), the expected text (Median's string with the sample value 12, worked out by
`tools/tooltip_audit.py`), the text of the unfixed build, the text shown now, and the verdict. Verdicts: `OK` = the shown text equals
the expected one; `ITEM` = the line depends on the item (skill names, monster names, charges, proc chances), checked on real items
below; `WRONG` / `DIFF` = fails (none left).

The original stat-only audit counted 72 of the 313 lines as wrong (53 leaked printf placeholders such as `+12% %+d%% Enhanced Defense`, 10 pointed at missing
strings, 6 had no line where Median has one, 3 showed Chronicle placeholder text). Its blank-line assumption for 31 / 34 was incorrect (follow-up below). Now: 294 `OK`, 19 `ITEM`, 0 wrong. The check
`tooltiptext` runs the whole table again on every run and fails on any leaked placeholder, missing string or empty line.

## Root causes and fixes

1. **Leaked placeholders (53).** `tools/tbl2json.py` keeps D2R's text for 296 string keys whose placeholders differ from Median's
   (so that D2R's own code keeps working), and the migrated `itemstatcost` pointed Median's stats at those keys: Median's `%+d%%`
   printf got D2R's text or the reverse. Fix: those keys now also exist as `<key>_mxl` alias keys (Median wording, Median-only,
   group 2 of [strings-namespace.md](strings-namespace.md)); `txt_migrate.median_stat_wording` makes `itemstatcost` name the alias
   for every stat that is displayed with Median's text (descstrpos / descstrneg / descstr2 and the group line columns). Descfuncs
   22 / 23 / 24 (reanimate, charged skill) are D2R's own code and keep D2R's text.
2. **Missing strings (10) and no line (6).** The migration blanked the strings of rows where Median draws the line in code
   (descfunc 31-38). Functions 31 / 34 read a string ID from the stat layer, not the descstrpos columns; the others get the plugin line below, or
   D2R's description when Median has none (restored in `item_stat_lines`). Group lines without strings are dropped.
3. **Median descfuncs 32-38** that D2R cannot draw are formatted by the plugin hook (`plugin/src/item_text.cpp`, off word
   `itemtext`), which only replaces the formatter's output for these stats and calls the original for the rest:

   | stat | descfunc | line |
   |---|---|---|
   | 534, 74 | 32 | value / 10 (at least 1) with a sign: `+1 Life Regenerated per Second (Based on Character Level)`; descval 1 puts the number first, 2 the text |
   | 309 | 33 | `Jitan's Gate Cooldown Reduced by 2` (seconds) |
   | 444 | 35 | `Innate Cold Damage: 13 (45.0% of Vitality)`: element in the layer's low byte, attribute in the second byte, the percentage is the value raised by the item's stat 513 |
   | 289 | 36 | `Orb Effects Applied to this Item are Doubled` (only at value 2) |
   | 478 | 37 | `Jitan's Gate Maximum Skill Level Increased by 12` |
   | 456 | 38 | `Activation Frequency +12` |
   | 404 / 406 / 407, 414 | 31, 34 | layered Median text, coloured by the value (31) or fixed orange (34); see the special-text follow-up below |
   | 112 and every descfunc 5 stat whose Median text has no `%` | 5 | `Hit Causes Monster to Flee 9%`: N = value * 100 / 128, the number before or after the text by descval (D2R's descfunc 5 text carries the `%` itself) |
   | 492 | class lock | `(Paladin Only)` etc., see below |

4. **Class lock line (stat 492, audit T7).** Value = class + 1 (1-7; 8 = every class, no line). The migration gives the stat a
   description row (`itemcls_classonly`) and the plugin line prints the `strXOnly` text of the class. Read on a unique relic
   (`cotw`, rows 1267 / 1212): `(Assassin Only)`; `tipfmt 492 4` gives `(Paladin Only)`. The refusal itself was already verified
   (row "Class-locked items" of [unported.md](unported.md)).

## Assumptions and what stays open

- **Descfunc 35 (innate damage, stat 444).** The percentage formula (`value + value * stat513 / 100`) and the layer bytes (element,
  attribute) are inferred from Median's strings and the data, not from Sigma's code (the flag check `FUN_100af880` in Sigma stays
  unresolved). The damage number in front of "of" is read with the local player's attributes (the item unit is not a player).
- **Descfunc 33 (cooldown, stat 309)** divides the stored value by 25 (frames per second); the divisor was not confirmed against
  Median.
- **Skills without a name.** Skills 2001, 2004, 669, 2073, 2076 and 741 (`skills_1929`, `skills_1932`, `skills_597`, `skills_2001`,
  `skills_2004`, `skills_669`) have no `skilldesc` row in Median; the item formatter asserts when a property names one
  (`nDesc >= 0 && nDesc < skillDescTbl.size()`, seen in the 2026-10-23 suite by `tooltiptext`). `txt_migrate.py`
  (`blank_skilldesc`) gives them a `blank` row (no name, the shape of Median's 29 rows with an empty `str name`), so the
  tooltip prints a blank name like Median. Lane suite-regressions.
- **Not spawned in game.** Set items cannot be created by the harness `item` verb, and no runeword was socketed; their property
  lines go through the same formatter and stat records, which the table covers for every stat, but no real set item or runeword was
  read. Charms (`cm1`-`cm3`) spawn without properties through the `item` verb; the charm stats were read through the table only.
- **`tip` needs one hover.** The item text builder `0x2dc4b0` takes arguments the harness cannot make up; `tip` reuses the ones of
  the last hover (the check hovers a ring in the background once). `tiplog reset` must come right before `tip`, because the hovered
  item re-renders every frame.
- **Weapon Strength / Dexterity damage lines (audit T9)** were not found in the lines `tip` returns (the formatter is not asked for
  them), so they are neither read nor ported; a hook of the item text builder is needed. Open row in [unported.md](unported.md).

## Real items read

| item | lines read (properties of interest) |
|---|---|
| unique 43 `\|wc` | `+10 to all Attributes`, `5% Innate Elemental Damage`, `Jitan's Gate Cooldown Reduced by 1` |
| unique 141 amulet | `+4 to All Skills`, `Orb Effects Applied to this Item are Doubled` |
| unique 128 amulet | `2% Reanimate as: Magineer`, `Activation Frequency +8`, `3% Chance to cast level 50 Singularity on Melee Attack` |
| unique 72 `wd02` | `Physical Resist 10%`, `+54 Life Regenerated per Second (Based on Character Level)`, `12% to Cold Spell Damage (Based on Character Level)` |
| unique 1557 `60@` | `Innate <Element> Damage: N (P% of <Attribute>)`, `+10 to Assassin Skill Levels`, `Orb Effects Applied to this Item are Doubled` |
| relic `cotw` | `(Assassin Only)` |
| rare / magic rings, amulets, jewels | no leaked placeholder, no missing string (`tooltiptext` reads six bases at two qualities) |

## Table

Generated by `py -3 tools/tooltip_audit.py report --results work/tooltip_audit/results.txt --baseline <unfixed readback>`; the
results file is what `game_checks.py tooltiptext` writes.

<!-- table:start -->
313 property lines (stats and group lines): 19 ITEM, 294 OK. The unfixed build had 72 wrong lines among them.

| stat | name | descfunc | expected | before (unfixed build) | shown | verdict |
|---|---|---|---|---|---|---|
| 0 | strength | 1 | +12 to Strength | +12 to Strength | +12 to Strength | OK |
| 1 | energy | 1 | +12 to Energy | +12 to Energy | +12 to Energy | OK |
| 2 | dexterity | 1 | +12 to Dexterity | +12 to Dexterity | +12 to Dexterity | OK |
| 3 | vitality | 1 | +12 to Vitality | +12 to Vitality | +12 to Vitality | OK |
| 7 | maxhp | 1 | +12 to Life | +12 to Life | +12 to Life | OK |
| 9 | maxmana | 1 | +12 to Mana | +12 to Mana | +12 to Mana | OK |
| 11 | maxstamina | 1 | +12 Maximum Stamina | +12 Maximum Stamina | +12 Maximum Stamina | OK |
| 16 | item_armor_percent | 4 | +12% Enhanced Defense | +12% %+d%% Enhanced Defense | +12% Enhanced Defense | OK |
| 17 | item_maxdamage_percent | 3 | Enhanced Maximum Damage | %+d%% Enhanced Maximum Damage | Enhanced Maximum Damage | OK |
| 18 | item_mindamage_percent | 3 | Enhanced Minimum Damage | %+d%% Enhanced Minimum Damage | Enhanced Minimum Damage | OK |
| 19 | tohit | 1 | +12 to Attack Rating | +12 to Attack Rating | +12 to Attack Rating | OK |
| 20 | toblock | 2 | 12% Base Block Chance | 12% Increased Chance of Blocking | 12% Base Block Chance | OK |
| 21 | mindamage | 1 | +12 to Minimum Damage | +12 %+d to Minimum Damage | +12 to Minimum Damage | OK |
| 22 | maxdamage | 1 | +12 to Maximum Damage | +12 %+d to Maximum Damage | +12 to Maximum Damage | OK |
| 23 | secondary_mindamage | 1 | +12 to Minimum Damage | +12 %+d to Minimum Damage | +12 to Minimum Damage | OK |
| 24 | secondary_maxdamage | 1 | +12 to Maximum Damage | +12 %+d to Maximum Damage | +12 to Maximum Damage | OK |
| 25 | damagepercent | 4 | Weapon Physical Damage +12% | Weapon Physical Damage +12% | Weapon Physical Damage +12% | OK |
| 27 | manarecoverybonus | 4 | Regenerate Mana +12% | Regenerate Mana 12% | Regenerate Mana +12% | OK |
| 28 | staminarecoverybonus | 2 | Heal Stamina Plus 12% | Heal Stamina Plus %d%% 12% | Heal Stamina Plus 12% | OK |
| 31 | armorclass | 1 | +12 Defense | +12 %+d Defense | +12 Defense | OK |
| 32 | armorclass_vs_missile | 1 | +12 Defense vs. Missile | +12 %+d Defense vs. Missile | +12 Defense vs. Missile | OK |
| 33 | armorclass_vs_hth | 1 | +12 Defense vs. Melee | +12 %+d Defense vs. Melee | +12 Defense vs. Melee | OK |
| 34 | normal_damage_reduction | 3 | Physical Damage Taken Reduced by 12 | Physical Damage Taken Reduced by 12 | Physical Damage Taken Reduced by 12 | OK |
| 35 | magic_damage_reduction | 3 | Elemental/Magic Damage Taken Reduced by 12 | Magic Damage Reduced by 12 | Elemental/Magic Damage Taken Reduced by 12 | OK |
| 36 | damageresist | 2 | Physical Resist 12% | Damage Reduced by 12 | Physical Resist 12% | OK |
| 37 | magicresist | 4 | Magic Resist +12% | Magic Resist +12% | Magic Resist +12% | OK |
| 38 | maxmagicresist | 4 | +12% Maximum Magic Resist | +12% %+d%% to Maximum Magic Resist | +12% Maximum Magic Resist | OK |
| 39 | fireresist | 4 | Fire Resist +12% | Fire Resist +12% | Fire Resist +12% | OK |
| 40 | maxfireresist | 4 | Maximum Fire Resist +12% | %+d%% to Maximum Fire Resist +12% | Maximum Fire Resist +12% | OK |
| 41 | lightresist | 4 | Lightning Resist +12% | Lightning Resist +12% | Lightning Resist +12% | OK |
| 42 | maxlightresist | 4 | Maximum Lightning Resist +12% | %+d%% to Maximum Lightning Resist +12% | Maximum Lightning Resist +12% | OK |
| 43 | coldresist | 4 | Cold Resist +12% | Cold Resist +12% | Cold Resist +12% | OK |
| 44 | maxcoldresist | 4 | Maximum Cold Resist +12% | %+d%% to Maximum Cold Resist +12% | Maximum Cold Resist +12% | OK |
| 45 | poisonresist | 4 | Poison Resist +12% | Poison Resist +12% | Poison Resist +12% | OK |
| 46 | maxpoisonresist | 4 | Maximum Poison Resist +12% | %+d%% to Maximum Poison Resist +12% | Maximum Poison Resist +12% | OK |
| 48 | firemindam | 1 | +12 to Minimum Fire Damage | +12 to Minimum Fire Damage | +12 to Minimum Fire Damage | OK |
| 49 | firemaxdam | 1 | +12 to Maximum Fire Damage | +12 to Maximum Fire Damage | +12 to Maximum Fire Damage | OK |
| 50 | lightmindam | 1 | +12 to Minimum Lightning Damage | +12 to Minimum Lightning Damage | +12 to Minimum Lightning Damage | OK |
| 51 | lightmaxdam | 1 | +12 to Maximum Lightning Damage | +12 to Maximum Lightning Damage | +12 to Maximum Lightning Damage | OK |
| 52 | magicmindam | 1 | +12 to Minimum Magic Damage | +12 to Minimum Magic Damage | +12 to Minimum Magic Damage | OK |
| 53 | magicmaxdam | 1 | +12 to Maximum Magic Damage | +12 to Maximum Magic Damage | +12 to Maximum Magic Damage | OK |
| 54 | coldmindam | 1 | +12 to Minimum Cold Damage | +12 to Minimum Cold Damage | +12 to Minimum Cold Damage | OK |
| 55 | coldmaxdam | 1 | +12 to Maximum Cold Damage | +12 to Maximum Cold Damage | +12 to Maximum Cold Damage | OK |
| 57 | poisonmindam | 1 | +12 to Minimum Poison Damage | +12 to Minimum Poison Damage | +12 to Minimum Poison Damage | OK |
| 58 | poisonmaxdam | 1 | +12 to Maximum Poison Damage | +12 to Maximum Poison Damage | +12 to Maximum Poison Damage | OK |
| 60 | lifedrainmindam | 2 | 12% Life stolen per Hit | 12% Life stolen per hit | 12% Life stolen per Hit | OK |
| 62 | manadrainmindam | 2 | 12% Mana stolen per Hit | 12% Mana stolen per hit | 12% Mana stolen per Hit | OK |
| 64 | stamdrainmindam | 2 | 12% Stamina stolen per Hit | 12% Stamina stolen per Hit | 12% Stamina stolen per Hit | OK |
| 66 | stunlength | 3 | Stun Attack | Stun Attack | Stun Attack | OK |
| 74 | hpregen | 32 | +1 Life Regenerated per Second | Replenish Life +12 | +1 Life Regenerated per Second | OK |
| 76 | item_maxhp_percent | 4 | Maximum Life +12% | Increase Maximum Life %d%% +12% | Maximum Life +12% | OK |
| 77 | item_maxmana_percent | 4 | Maximum Mana +12% | Increase Maximum Mana %d%% +12% | Maximum Mana +12% | OK |
| 78 | item_attackertakesdamage | 3 | Attacker Takes Damage of 12 | Attacker Takes Damage of %d 12 | Attacker Takes Damage of 12 | OK |
| 79 | item_goldbonus | 2 | 12% Gold Find | 12% Extra Gold from Monsters | 12% Gold Find | OK |
| 80 | item_magicbonus | 2 | 12% Magic Find | 12% Better Chance of Getting Magic Items | 12% Magic Find | OK |
| 81 | item_knockback | 3 | Knockback | Knockback | Knockback | OK |
| 83 | item_addclassskills | 13 | _(item)_ | +12 to Amazon Skill Levels | +12 to Amazon Skill Levels | ITEM (checked on real items) |
| 85 | item_addexperience | 4 | +12% to Experience Gained | +12% to Experience Gained | +12% to Experience Gained | OK |
| 86 | item_healafterkill | 1 | +12 Life after each Kill | +12 Life after each Kill | +12 Life after each Kill | OK |
| 87 | item_reducedprices | 20 | -12% to All Vendor Prices | -12% Reduces all Vendor Prices %d%% | -12% to All Vendor Prices | OK |
| 89 | item_lightradius | 1 | +12 to Light Radius | +12 to Light Radius | +12 to Light Radius | OK |
| 91 | item_req_percent | 4 | Requirements +12% | Requirements %+d%% +12% | Requirements +12% | OK |
| 92 | item_levelreq | 1 | +12 Required Level | +12 Required Level | +12 Required Level | OK |
| 93 | item_fasterattackrate | 2 | 12% Attack Speed | +12% Increased Attack Speed | 12% Attack Speed | OK |
| 94 | item_levelreqpct | 4 -> - | _(no line)_ | FLYING POLAR BUFFALO ERROR +12% | _(no line)_ | OK (no line) |
| 96 | item_fastermovevelocity | 2 | 12% Movement Speed | +12% Faster Run/Walk | 12% Movement Speed | OK |
| 97 | item_nonclassskill | 28 | _(item)_ | FLYING POLAR BUFFALO ERROR | +12 to Attack | ITEM (checked on real items) |
| 99 | item_fastergethitrate | 2 | 12% Hit Recovery | +12% Faster Hit Recovery | 12% Hit Recovery | OK |
| 102 | item_fasterblockrate | 2 | 12% Block Speed | +12% Faster Block Rate | 12% Block Speed | OK |
| 103 | skill_bypass_undead | 3 | Ignores Resistances of Undead | Ignores Resistances of Undead | Ignores Resistances of Undead | OK |
| 104 | skill_bypass_demons | 3 | Ignores Resistances of Demons | Ignores Resistances of Demons | Ignores Resistances of Demons | OK |
| 105 | item_fastercastrate | 2 | 12% Cast Speed | +12% Faster Cast Rate | 12% Cast Speed | OK |
| 106 | skill_bypass_beasts | 3 | Ignores Resistances of Beasts | Ignores Resistances of Beasts | Ignores Resistances of Beasts | OK |
| 107 | item_singleskill | 27 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 108 | item_restinpeace | 3 | Slain Monsters Rest in Peace | Slain Monsters Rest in Peace | Slain Monsters Rest in Peace | OK |
| 109 | curse_resistance | 2 | Curse Length Reduction 12% | Curse Length Reduction 12% | Curse Length Reduction 12% | OK |
| 110 | item_poisonlengthresist | 2 | Poison Length Reduction 12% | Poison Length Reduced by 12% | Poison Length Reduction 12% | OK |
| 111 | item_normaldamage | 1 | Damage +12 | Damage %+d +12 | Damage +12 | OK |
| 112 | item_howl | 5 | Hit Causes Monster to Flee 9% | Hit Causes Monster to Flee +9% | Hit Causes Monster to Flee 9% | OK |
| 113 | item_stupidity | 12 | Hit Blinds Target +12 | Hit Blinds Target +12 | Hit Blinds Target +12 | OK |
| 114 | item_damagetomana | 2 | 12% Weapon Damage Taken Restores Mana | 12% %+d%% Damage Taken Goes To Mana | 12% Weapon Damage Taken Restores Mana | OK |
| 115 | item_ignoretargetac | 3 | Ignore Target's Defense | Ignore Target's Defense | Ignore Target's Defense | OK |
| 116 | item_fractionaltargetac | 20 | -12% Target Defense | -12% -%d%% Target Defense | -12% Target Defense | OK |
| 117 | item_preventheal | 3 | Prevent Monster Heal | Prevent Monster Heal | Prevent Monster Heal | OK |
| 118 | item_halffreezeduration | 3 | Half Freeze Duration | Half Freeze Duration | Half Freeze Duration | OK |
| 119 | item_tohit_percent | 2 | 12% Bonus to Attack Rating | 12% Bonus to Attack Rating | 12% Bonus to Attack Rating | OK |
| 120 | item_damagetargetac | 1 | +12 to Monster Defense per Hit | +12 %d to Monster Defense Per Hit | +12 to Monster Defense per Hit | OK |
| 121 | item_demondamage_percent | 4 | +12% Damage to Demons | +12% Damage to Demons | +12% Damage to Demons | OK |
| 122 | item_undeaddamage_percent | 4 | +12% Damage to Undead | +12% Damage to Undead | +12% Damage to Undead | OK |
| 123 | item_demon_tohit | 1 | +12 to Attack Rating against Demons | +12 %+d to Attack Rating against Demons | +12 to Attack Rating against Demons | OK |
| 124 | item_undead_tohit | 1 | +12 to Attack Rating against Undead | +12 %+d to Attack Rating against Undead | +12 to Attack Rating against Undead | OK |
| 125 | item_throwable | 3 | Throwable | Throwable | Throwable | OK |
| 126 | item_elemskill | 1 -> 19 | +12 to Fire Skills | +12 FLYING POLAR BUFFALO ERROR | +12 to Fire Skills | OK |
| 127 | item_allskills | 1 | +12 to All Skills | +12 to All Skills | +12 to All Skills | OK |
| 128 | item_attackertakeslightdamage | 3 | Attacker Takes Lightning Damage of 12 | Attacker Takes Lightning Damage of %d 12 | Attacker Takes Lightning Damage of 12 | OK |
| 131 | thorns_percent | 2 | 12% Damage Reflected | 12% Damage Reflected | 12% Damage Reflected | OK |
| 134 | item_freeze | 12 | Freezes Target +12 | Freezes Target +12 | Freezes Target +12 | OK |
| 135 | item_openwounds | 2 | 12% Chance of Open Wounds | 12% %+d%% Chance of Open Wounds | 12% Chance of Open Wounds | OK |
| 136 | item_crushingblow | 2 | 12% Chance of Crushing Blow | +12% Chance of Crushing Blow | 12% Chance of Crushing Blow | OK |
| 137 | item_kickdamage | 1 | +12 Kick Damage | +12 %+d Kick Damage | +12 Kick Damage | OK |
| 138 | item_manaafterkill | 1 | +12 Mana after each Kill | +12 to Mana after each Kill | +12 Mana after each Kill | OK |
| 139 | item_healafterdemonkill | 1 | +12 Life after each Demon Kill | +12 %+d Life after each Demon Kill | +12 Life after each Demon Kill | OK |
| 141 | item_deadlystrike | 2 | 12% Deadly Strike | +12% Deadly Strike | 12% Deadly Strike | OK |
| 142 | item_absorbfire_percent | 2 | Fire Absorb 12% | Fire Absorb +12% | Fire Absorb 12% | OK |
| 143 | item_absorbfire | 1 | +12 Fire Absorb | +12 Fire Absorb | +12 Fire Absorb | OK |
| 144 | item_absorblight_percent | 2 | Lightning Absorb 12% | Lightning Absorb +12% | Lightning Absorb 12% | OK |
| 145 | item_absorblight | 1 | +12 Lightning Absorb | +12 Lightning Absorb | +12 Lightning Absorb | OK |
| 146 | item_absorbmagic_percent | 2 | Magic Absorb 12% | Magic Absorb +12% | Magic Absorb 12% | OK |
| 147 | item_absorbmagic | 1 | +12 Magic Absorb | +12 Magic Absorb | +12 Magic Absorb | OK |
| 148 | item_absorbcold_percent | 2 | Cold Absorb 12% | Cold Absorb +12% | Cold Absorb 12% | OK |
| 149 | item_absorbcold | 1 | +12 Cold Absorb | +12 Cold Absorb | +12 Cold Absorb | OK |
| 150 | item_slow | 2 | Slow Target 12% | Slows Target by 12% | Slow Target 12% | OK |
| 153 | item_cannotbefrozen | 3 | Cannot Be Frozen | Cannot Be Frozen | Cannot Be Frozen | OK |
| 154 | item_staminadrainpct | 20 | -12% to Stamina Drain | -12% %d%% Slower Stamina Drain | -12% to Stamina Drain | OK |
| 155 | item_reanimate | 23 | _(item)_ | 12% Reanimate as: Reanimated Warrior | 12% Reanimate as: Reanimated Warrior | ITEM (checked on real items) |
| 157 | item_magicarrow | 3 | Fires Magic Arrows | Fires Magic Arrows | Fires Magic Arrows | OK |
| 158 | item_explosivearrow | 3 | Fires Explosive Arrows or Bolts | Fires Explosive Arrows or Bolts | Fires Explosive Arrows or Bolts | OK |
| 164 | skill_concentration | 2 | 12% Chance of Uninterruptable Attack | 12% Chance of Uninterruptable Attack | 12% Chance of Uninterruptable Attack | OK |
| 171 | skill_armor_percent | 2 | 12% Bonus to Defense | 12% Bonus to Defense | 12% Bonus to Defense | OK |
| 182 | armor_override_percent | 3 | Total Defense = 0 | Total Defense = 0 | Total Defense = 0 | OK |
| 188 | item_addskill_tab | 14 | _(item)_ | +12 to Bow and Crossbow Skills (Amazon Only) | +12 to Bow and Crossbow Skills (Amazon Only) | ITEM (checked on real items) |
| 195 | item_skillonattack | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 196 | item_skillonkill | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 197 | item_skillondeath | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 198 | item_skillonhit | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 199 | item_skillonlevelup | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 201 | item_skillongethit | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 204 | item_charged_skill | 24 | _(item)_ | Level 36 Attack (12/0 Charges) | Level 0 Attack (12/0 Charges) | ITEM (checked on real items) |
| 214 | item_armor_perlevel | 6 | +12 Defense (Based on Character Level) | +12 %+d Defense (Based on Character Level) | +12 Defense (Based on Character Level) | OK |
| 215 | item_armorpercent_perlevel | 8 | +12% Enhanced Defense (Based on Character Level) | +12% %+d%% Enhanced Defense (Based on Character Level) | +12% Enhanced Defense (Based on Character Level) | OK |
| 216 | item_hp_perlevel | 6 | +12 to Life (Based on Character Level) | +12 %+d to Life (Based on Character Level) | +12 to Life (Based on Character Level) | OK |
| 217 | item_mana_perlevel | 6 | +12 to Mana (Based on Character Level) | +12 %+d to Mana (Based on Character Level) | +12 to Mana (Based on Character Level) | OK |
| 218 | item_maxdamage_perlevel | 6 | +12 to Maximum Damage (Based on Character Level) | +12 %+d to Maximum Damage (Based on Character Level) | +12 to Maximum Damage (Based on Character Level) | OK |
| 219 | item_maxdamage_percent_perlevel | 3 | ÿc2Shrine Blessed | Shrine Blessed | Shrine Blessed | OK |
| 225 | item_tohitpercent_perlevel | 7 | 12% Bonus to Attack Rating (Based on Character Level) | 12% %d%% Bonus to Attack Rating (Based on Character Level) | 12% Bonus to Attack Rating (Based on Character Level) | OK |
| 228 | item_ltng_damagemax_perlevel | 4 | +12% Mana Cost of Skills | +12% Mana Cost of Skills | +12% Mana Cost of Skills | OK |
| 229 | item_pois_damagemax_perlevel | 4 | +12% to Summon Physical Resist | +12% to Summon Physical Resist | +12% to Summon Physical Resist | OK |
| 230 | item_resist_cold_perlevel | 7 | Cold Resist 12% (Based on Character Level) | Cold Resist %+d%% 12% (Based on Character Level) | Cold Resist 12% (Based on Character Level) | OK |
| 231 | item_resist_fire_perlevel | 7 | Fire Resist 12% (Based on Character Level) | Fire Resist %+d%% 12% (Based on Character Level) | Fire Resist 12% (Based on Character Level) | OK |
| 232 | item_resist_ltng_perlevel | 7 | Lightning Resist 12% (Based on Character Level) | Lightning Resist %+d%% 12% (Based on Character Level) | Lightning Resist 12% (Based on Character Level) | OK |
| 233 | item_resist_pois_perlevel | 7 | Poison Resist 12% (Based on Character Level) | Poison Resist %+d%% 12% (Based on Character Level) | Poison Resist 12% (Based on Character Level) | OK |
| 234 | item_absorb_cold_perlevel | 1 | +12 to Strength | +12 %+d to Strength | +12 to Strength | OK |
| 239 | item_find_gold_perlevel | 7 | 12% Gold Find (Based on Character Level) | 12% %d%% Extra Gold from Monsters (Based on Character Level) | 12% Gold Find (Based on Character Level) | OK |
| 240 | item_find_magic_perlevel | 7 | 12% Magic Find (Based on Character Level) | 12% %d%% Better Chance of Getting Magic Items (Based on Character Level) | 12% Magic Find (Based on Character Level) | OK |
| 247 | item_crushingblow_perlevel | 7 | 12% Chance of Crushing Blow (Based on Character Level) | 12% %+d%% Chance of Crushing Blow (Based on Character Level) | 12% Chance of Crushing Blow (Based on Character Level) | OK |
| 248 | item_openwounds_perlevel | 7 | 12% Chance of Open Wounds (Based on Character Level) | 12% %+d%% Chance of Open Wounds (Based on Character Level) | 12% Chance of Open Wounds (Based on Character Level) | OK |
| 253 | item_replenish_quantity | 3 | Replenishes Quantity | Replenishes Quantity | Replenishes Quantity | OK |
| 254 | item_extra_stack | 3 | Increased Stack Size | Increased Stack Size | Increased Stack Size | OK |
| 257 | item_slash_damage_percent | 3 | ÿc8Nephalem: +25% Total Vessel Damage Bonus from Passives Unholy: Blood Thorns: +40% Damage Multiplier Holy: Holy Fire Deals 4.5% of  Strength as More Total Damage (max 250%)ÿc3 | Nephalem: +25% Total Vessel Damage Bonus from Passives Unholy: Blood Thorns: +40% Damage Multiplier Holy: Holy Fire Deals 4.5% of Strength as More Total Damage (max 250%) | Nephalem: +25% Total Vessel Damage Bonus from Passives Unholy: Blood Thorns: +40% Damage Multiplier Holy: Holy Fire Deals 4.5% of Strength as More Total Damage (max 250%) | OK |
| 259 | item_crush_damage_percent | 3 | Charges: 12 | Charges: 12 | Charges: 12 | OK |
| 260 | item_thrust_damage | 3 | ÿc8Thunderfuryÿc0 | Thunderfury | Thunderfury | OK |
| 261 | item_thrust_damage_percent | 1 | +12 Extra Totems | +12 Extra Totems | +12 Extra Totems | OK |
| 265 | item_absorb_slash_percent | 1 | +12 Life after each Kill | +12 %+d Life after each Kill | +12 Life after each Kill | OK |
| 266 | item_absorb_crush_percent | 3 | ÿc8Area Effect Attackÿc0 | Area Effect Attack | Area Effect Attack | OK |
| 271 | item_mana_bytime | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 276 | item_energy_bytime | 3 | ÿc5Cannot be Disenchantedÿc3 | Cannot be Disenchanted | Cannot be Disenchanted | OK |
| 277 | item_vitality_bytime | 3 | ÿc8Area Effect Attackÿc0 | Area Effect Attack | Area Effect Attack | OK |
| 279 | item_tohitpercent_bytime | 2 | Lion Stance Damage Bonus 12% | Lion Stance Damage Bonus 12% | Lion Stance Damage Bonus 12% | OK |
| 282 | item_ltng_damagemax_bytime | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 283 | item_pois_damagemax_bytime | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 284 | item_resist_cold_bytime | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 288 | item_absorb_cold_bytime | 3 | ÿc8Blood Hatred does not cost you life | Blood Hatred does not cost you life | Blood Hatred does not cost you life | OK |
| 289 | item_absorb_fire_bytime | 36 | ÿc:Orb Effects Applied to this Item are Doubledÿc3 | _(no line)_ | Orb Effects Applied to this Item are Doubled | OK |
| 292 | item_find_gold_bytime | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 294 | item_regenstamina_bytime | 3 | ÿc:Allows Runewords up to Level 12 | Allows Runewords up to Level 12 | Allows Runewords up to Level 12 | OK |
| 295 | item_stamina_bytime | 1 | +12 Mana on Melee Attack | +12 Mana on Melee Attack | +12 Mana on Melee Attack | OK |
| 299 | item_tohit_undead_bytime | 2 | ÿc0Current Strength Damage Bonus: 12% | Current Strength Damage Bonus: 12% | Current Strength Damage Bonus: 12% | OK |
| 300 | item_crushingblow_bytime | 2 | Additional Dexterity Damage Bonus: 12% | Additional Dexterity Damage Bonus: 12% | Additional Dexterity Damage Bonus: 12% | OK |
| 309 | item_damage_vs_monster | 33 | [A-Za-z' ]+ Cooldown (Reduced\|Decreased) by [0-9.e+-]+ | _(no line)_ | Jitan's Gate Cooldown Reduced by 2 | OK |
| 316 | burningmin | 3 -> - | _(no line)_ | FLYING POLAR BUFFALO ERROR | _(no line)_ | OK (no line) |
| 317 | burningmax | 3 -> - | _(no line)_ | FLYING POLAR BUFFALO ERROR | _(no line)_ | OK (no line) |
| 318 | progressive_damage | 3 -> - | _(no line)_ | FLYING POLAR BUFFALO ERROR | _(no line)_ | OK (no line) |
| 319 | progressive_steal | 3 -> - | _(no line)_ | FLYING POLAR BUFFALO ERROR | _(no line)_ | OK (no line) |
| 329 | passive_fire_mastery | 4 | +12% to Fire Spell Damage | +12% to Fire Skill Damage | +12% to Fire Spell Damage | OK |
| 330 | passive_ltng_mastery | 4 | +12% to Lightning Spell Damage | +12% to Lightning Skill Damage | +12% to Lightning Spell Damage | OK |
| 331 | passive_cold_mastery | 4 | +12% to Cold Spell Damage | +12% to Cold Skill Damage | +12% to Cold Spell Damage | OK |
| 332 | passive_pois_mastery | 4 | +12% to Poison Spell Damage | +12% to Poison Skill Damage | +12% to Poison Spell Damage | OK |
| 333 | passive_fire_pierce | 20 | -12% to Enemy Fire Resistance | -12% to Enemy Fire Resistance | -12% to Enemy Fire Resistance | OK |
| 334 | passive_ltng_pierce | 20 | -12% to Enemy Lightning Resistance | -12% to Enemy Lightning Resistance | -12% to Enemy Lightning Resistance | OK |
| 335 | passive_cold_pierce | 20 | -12% to Enemy Cold Resistance | -12% to Enemy Cold Resistance | -12% to Enemy Cold Resistance | OK |
| 336 | passive_pois_pierce | 20 | -12% to Enemy Poison Resistance | -12% to Enemy Poison Resistance | -12% to Enemy Poison Resistance | OK |
| 338 | passive_dodge | 2 | 12% Avoid Damage | 12% Avoid Damage | 12% Avoid Damage | OK |
| 339 | passive_avoid | 2 | 12% Avoid Damage | 12% Avoid Damage | 12% Avoid Damage | OK |
| 340 | passive_evade | 2 | 12% Avoid Damage | 12% Avoid Damage | 12% Avoid Damage | OK |
| 357 | passive_mag_mastery | 4 | +12% to Physical/Magic Spell Damage | +12% to Physical/Magic Spell Damage | +12% to Physical/Magic Spell Damage | OK |
| 358 | passive_mag_pierce | 20 | -12% to Enemy Magic Resistance | -12% to Enemy Magic Resistance | -12% to Enemy Magic Resistance | OK |
| 370 | itemstatcost_185 | 3 -> - | _(no line)_ | FLYING POLAR BUFFALO ERROR 12 | _(no line)_ | OK (no line) |
| 371 | itemstatcost_186 | 3 -> - | _(no line)_ | FLYING POLAR BUFFALO ERROR 12 | _(no line)_ | OK (no line) |
| 376 | chance_to_cast_level_on_kil | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 377 | mega_impact | 3 | ÿc8Mega Impactÿc0 | Mega Impact | Mega Impact | OK |
| 379 | weapon_physical_damage | 8 | Weapon Physical Damage +12% (Based on Character Level) | Weapon Physical Damage +12% (Based on Character Level) | Weapon Physical Damage +12% (Based on Character Level) | OK |
| 380 | maximum_avoid_chance | 4 | Maximum Avoid Chance +12% | Maximum Avoid Chance +12% | Maximum Avoid Chance +12% | OK |
| 382 | life_on_striking | 1 | +12 Life on Striking | +12 Life on Striking | +12 Life on Striking | OK |
| 383 | mana_on_striking | 1 | +12 Mana on Striking | +12 Mana on Striking | +12 Mana on Striking | OK |
| 384 | life_on_melee_attack | 1 | +12 Life on Melee Attack | +12 Life on Melee Attack | +12 Life on Melee Attack | OK |
| 387 | maximum_block_chance | 4 | Maximum Block Chance +12% | Maximum Block Chance +12% | Maximum Block Chance +12% | OK |
| 388 | to_strength | 2 | 12% to Strength | 12% to Strength | 12% to Strength | OK |
| 389 | to_dexterity | 2 | 12% to Dexterity | 12% to Dexterity | 12% to Dexterity | OK |
| 390 | to_energy | 2 | 12% to Energy | 12% to Energy | 12% to Energy | OK |
| 391 | to_vitality | 2 | 12% to Vitality | 12% to Vitality | 12% to Vitality | OK |
| 392 | slows_attacker_by | 2 | Slows Attacker by 12% | Slows Attacker by 12% | Slows Attacker by 12% | OK |
| 393 | attacker_flees_after_striki | 2 | Attacker Flees after Striking 12% | Attacker Flees after Striking 12% | Attacker Flees after Striking 12% | OK |
| 394 | freezes_attacker | 12 | Freezes attacker +12 | Freezes attacker +12 | Freezes attacker +12 | OK |
| 395 | blinds_attacker | 12 | Blinds Attacker +12 | Blinds Attacker +12 | Blinds Attacker +12 | OK |
| 396 | attacker_takes_cold_damage | 3 | Attacker Takes Cold Damage of 12 | Attacker Takes Cold Damage of 12 | Attacker Takes Cold Damage of 12 | OK |
| 397 | attacker_takes_fire_damage | 3 | Attacker Takes Fire Damage of 12 | Attacker Takes Fire Damage of 12 | Attacker Takes Fire Damage of 12 | OK |
| 400 | amazing_grace | 3 | ÿc8Amazing Graceÿc0 | Amazing Grace | Amazing Grace | OK |
| 404 | itemstatcost_375 | 31 | You Cannot Deal Fire, Lightning or Poison Damage Max Life gained per point of Vitality reduced by 0.9 100% of Vitality Added as Cold Damage to Weapon | +12 to a Random Class' Skill Levels | You Cannot Deal Fire, Lightning or Poison Damage Max Life gained per point of Vitality reduced by 0.9 100% of Vitality Added as Cold Damage to Weapon | OK |
| 406 | itemstatcost_377 | 31 | ÿc5Reward can be used to upgrade Paragon's Hammer Transmute with a Xith rune to receive two quest entrance scrolls Difficulty Level: 125 ÿc4Infernal Machine (Heroic) | +12 to a Random Class' Skill Levels | Reward can be used to upgrade Paragon's Hammer Transmute with a Xith rune to receive two quest entrance scrolls Difficulty Level: 125 Infernal Machine (Heroic) | OK |
| 407 | itemstatcost_378 | 31 | Can be upgraded to unlock a Paragon reward skill | +12 to a Random Class' Skill Levels | Can be upgraded to unlock a Paragon reward skill | OK |
| 408 | itemstatcost_379 | 3 -> - | _(no line)_ | FLYING POLAR BUFFALO ERROR | _(no line)_ | OK (no line) |
| 409 | bonus_damage_to_bloodlust | 4 | +12% Bonus Damage to Bloodlust | +12% Bonus Damage to Bloodlust | +12% Bonus Damage to Bloodlust | OK |
| 410 | bonus_elemental_damage_to_b | 4 | +12% Bonus Elemental Damage to Bloodlust | +12% Bonus Elemental Damage to Bloodlust | +12% Bonus Elemental Damage to Bloodlust | OK |
| 414 | itemstatcost_385 | 34 | You Cannot Deal Fire, Lightning or Poison Damage Max Life gained per point of Vitality reduced by 0.9 100% of Vitality Added as Cold Damage to Weapon | _(no line)_ | You Cannot Deal Fire, Lightning or Poison Damage Max Life gained per point of Vitality reduced by 0.9 100% of Vitality Added as Cold Damage to Weapon | OK |
| 416 | bonus_damage_to_mark_of_the | 4 | +12% Bonus Damage to Mark of the Wild | +12% Bonus Damage to Mark of the Wild | +12% Bonus Damage to Mark of the Wild | OK |
| 417 | bonus_elemental_damage_to_m | 4 | +12% Bonus Elemental Damage to Mark of the Wild | +12% Bonus Elemental Damage to Mark of the Wild | +12% Bonus Elemental Damage to Mark of the Wild | OK |
| 418 | duration_bonus_to_mark_of_t | 2 | 12% Duration Bonus to Mark of the Wild | 12% Duration Bonus to Mark of the Wild | 12% Duration Bonus to Mark of the Wild | OK |
| 419 | already_enchanted | 3 | ÿc2Already Enchanted | Already Enchanted | Already Enchanted | OK |
| 420 | mirrored | 3 | ÿc2Mirrored | Mirrored | Mirrored | OK |
| 433 | to_fire_spell_damage | 7 | 12% to Fire Spell Damage (Based on Character Level) | 12% %+d%% to Fire Skill Damage (Based on Character Level) | 12% to Fire Spell Damage (Based on Character Level) | OK |
| 434 | to_lightning_spell_damage | 7 | 12% to Lightning Spell Damage (Based on Character Level) | 12% %+d%% to Lightning Skill Damage (Based on Character Level) | 12% to Lightning Spell Damage (Based on Character Level) | OK |
| 435 | to_cold_spell_damage | 7 | 12% to Cold Spell Damage (Based on Character Level) | 12% %+d%% to Cold Skill Damage (Based on Character Level) | 12% to Cold Spell Damage (Based on Character Level) | OK |
| 436 | to_poison_spell_damage | 7 | 12% to Poison Spell Damage (Based on Character Level) | 12% %+d%% to Poison Skill Damage (Based on Character Level) | 12% to Poison Spell Damage (Based on Character Level) | OK |
| 438 | skill_duration | 2 | 12% Skill Duration | 12% Skill Duration | 12% Skill Duration | OK |
| 439 | prefixes | 3 | ÿc9Prefixes: 12 | Prefixes: 12 | Prefixes: 12 | OK |
| 440 | suffixes | 3 | ÿc9Suffixes: 12 | Suffixes: 12 | Suffixes: 12 | OK |
| 443 | bonus_to_summoned_edyrem_li | 4 | +12% Bonus to Summoned Edyrem Life | +12% Bonus to Summoned Edyrem Life | +12% Bonus to Summoned Edyrem Life | OK |
| 444 | innate_fire_damage_1f_of | 35 | Innate \w+ Damage: \d+ \(\d+\.\d% of \w+\) | _(no line)_ | Innate Cold Damage: 13 (45.0% of Vitality) | OK |
| 447 | can_be_upgraded_with_black | 3 | ÿc8Can Be Upgraded with Black Road Challenge | Can Be Upgraded with Black Road Challenge | Can Be Upgraded with Black Road Challenge | OK |
| 451 | current_bonus | 2 | Current Bonus: 12% | Current Bonus: 12% | Current Bonus: 12% | OK |
| 456 | activation_frequency | 38 | Activation Frequency +12 | _(no line)_ | Activation Frequency +12 | OK |
| 457 | to_maximum_exodia_minions | 1 | +12 to Maximum Exodia Minions | +12 to Maximum Exodia Minions | +12 to Maximum Exodia Minions | OK |
| 458 | to_maximum_paladin_minions | 1 | +12 to Maximum Paladin Minions | +12 to Maximum Paladin Minions | +12 to Maximum Paladin Minions | OK |
| 460 | bonus_to_poison_skill_durat | 4 | +12% Bonus to Poison Skill Duration | +12% Bonus to Poison Skill Duration | +12% Bonus to Poison Skill Duration | OK |
| 468 | life_when_struck_by_an_enem | 1 | +12 Life when Struck by an Enemy | +12 Life when Struck by an Enemy | +12 Life when Struck by an Enemy | OK |
| 469 | mana_when_struck_by_an_enem | 1 | +12 Mana when Struck by an Enemy | +12 Mana when Struck by an Enemy | +12 Mana when Struck by an Enemy | OK |
| 470 | to_maximum_necromancer_mini | 1 | +12 to Maximum Necromancer Minions | +12 to Maximum Necromancer Minions | +12 to Maximum Necromancer Minions | OK |
| 471 | additional_strength_damage | 2 | Additional Strength Damage Bonus: 12% | Additional Strength Damage Bonus: 12% | Additional Strength Damage Bonus: 12% | OK |
| 472 | already_upgraded | 3 | ÿc2Already Upgraded | Already Upgraded | Already Upgraded | OK |
| 473 | to_summon_life | 4 | +12% to Summon Life | +12% to Summon Life | +12% to Summon Life | OK |
| 474 | to_maximum_barbarian_minion | 1 | +12 to Maximum Barbarian Minions | +12 to Maximum Barbarian Minions | +12 to Maximum Barbarian Minions | OK |
| 475 | damage_to_protector_spirit | 4 | +12% Damage to Protector Spirit Bloodlust | +12% Damage to Protector Spirit Bloodlust | +12% Damage to Protector Spirit Bloodlust | OK |
| 476 | restored | 7 | Restored: 12% | Restored: 12% | Restored: 12% | OK |
| 477 | potion_effectiveness | 2 | 12% Potion Effectiveness | 12% Potion Effectiveness | 12% Potion Effectiveness | OK |
| 478 | maximum_skill_level_increas | 37 | [A-Za-z' ]+ Maximum Skill Level Increased by 12 | _(no line)_ | Jitan's Gate Maximum Skill Level Increased by 12 | OK |
| 491 | is462 | 4 | +12% Damage to Vessel of Justice | +12% Damage to Vessel of Justice | +12% Damage to Vessel of Justice | OK |
| 495 | essence_slots | 3 | Essence Slots: 12 | Essence Slots: 12 | Essence Slots: 12 | OK |
| 497 | is468 | 4 | +12% Damage to Vessel of Retribution | +12% Damage to Vessel of Retribution | +12% Damage to Vessel of Retribution | OK |
| 498 | extra_targets_to_paladin_ve | 1 | +12 Extra Targets to Paladin Vessels | +12 Extra Targets to Paladin Vessels | +12 Extra Targets to Paladin Vessels | OK |
| 499 | to_summon_damage | 4 | +12% to Summon Damage | +12% to Summon Damage | +12% to Summon Damage | OK |
| 502 | unlocks_your_ennead_skill | 3 | ÿc2Unlocks your Ennead Skill | Unlocks your Ennead Skill | Unlocks your Ennead Skill | OK |
| 503 | unlocks_your_black_road_ski | 3 | ÿc2Unlocks your Black Road Skill | Unlocks your Black Road Skill | Unlocks your Black Road Skill | OK |
| 509 | to_vitality_per_socketed_ge | 1 | +12 to Vitality per Socketed Gem | +12 to Vitality per Socketed Gem | +12 to Vitality per Socketed Gem | OK |
| 510 | to_runemaster_defense_bonus | 4 | +12% to Runemaster Defense Bonus | +12% to Runemaster Defense Bonus | +12% to Runemaster Defense Bonus | OK |
| 513 | innate_elemental_damage | 2 | 12% Innate Elemental Damage | 12% Innate Elemental Damage | 12% Innate Elemental Damage | OK |
| 514 | is485 | 1 | +12 Spell Focus | +12 Spell Focus | +12 Spell Focus | OK |
| 516 | to_summon_elemental_resista | 4 | +12% to Summon Elemental Resistances | +12% to Summon Elemental Resistances | +12% to Summon Elemental Resistances | OK |
| 517 | bonus_to_spell_focus | 4 | +12% Bonus to Spell Focus | +12% Bonus to Spell Focus | +12% Bonus to Spell Focus | OK |
| 518 | target_takes_additional_dam | 3 | Target Takes Additional Damage of 12 | Target Takes Additional Damage of 12 | Target Takes Additional Damage of 12 | OK |
| 525 | cube_with_oil_of_craft_to_r | 3 | ÿc1(Cube with Oil of Craft to Reveal) | (Cube with Oil of Craft to Reveal) | (Cube with Oil of Craft to Reveal) | OK |
| 526 | corrupted | 3 | ÿc1Corrupted | Corrupted | Corrupted | OK |
| 528 | to_energy_2 | 6 | +12 to Energy (Based on Character Level) | +12 %+d to Energy (Based on Character Level) | +12 to Energy (Based on Character Level) | OK |
| 529 | to_summon_attack_rating | 4 | +12% to Summon Attack Rating | +12% to Summon Attack Rating | +12% to Summon Attack Rating | OK |
| 530 | quantity_2 | 3 | ÿc3Quantity: 12 | Quantity: 12 | Quantity: 12 | OK |
| 534 | life_regenerated_per_second | 32 | +1 Life Regenerated per Second (Based on Character Level) | _(no line)_ | +1 Life Regenerated per Second (Based on Character Level) | OK |
| 539 | chance_to_cast_level_on_blo | 15 | _(item)_ | _(no line)_ | _(no line)_ | ITEM (no text at this sample; real items) |
| 492 | itemstatcost_463 | - | (Paladin Only) |  | (Paladin Only) | OK |
| 0 (group) | strength | 1 | +12 to all Attributes |  | +12 to all Attributes | OK |
| 1 (group) | energy | 1 | +12 to all Attributes |  | +12 to all Attributes | OK |
| 2 (group) | dexterity | 1 | +12 to all Attributes |  | +12 to all Attributes | OK |
| 3 (group) | vitality | 1 | +12 to all Attributes |  | +12 to all Attributes | OK |
| 7 (group) | maxhp | 1 | +12 to Life and Mana |  | +12 to Life and Mana | OK |
| 9 (group) | maxmana | 1 | +12 to Life and Mana |  | +12 to Life and Mana | OK |
| 39 (group) | fireresist | 4 | Elemental Resists +12% |  | Elemental Resists +12% | OK |
| 40 (group) | maxfireresist | 4 | Maximum Elemental Resists +12% |  | Maximum Elemental Resists +12% | OK |
| 41 (group) | lightresist | 4 | Elemental Resists +12% |  | Elemental Resists +12% | OK |
| 42 (group) | maxlightresist | 4 | Maximum Elemental Resists +12% |  | Maximum Elemental Resists +12% | OK |
| 43 (group) | coldresist | 4 | Elemental Resists +12% |  | Elemental Resists +12% | OK |
| 44 (group) | maxcoldresist | 4 | Maximum Elemental Resists +12% |  | Maximum Elemental Resists +12% | OK |
| 45 (group) | poisonresist | 4 | Elemental Resists +12% |  | Elemental Resists +12% | OK |
| 46 (group) | maxpoisonresist | 4 | Maximum Elemental Resists +12% |  | Maximum Elemental Resists +12% | OK |
| 60 (group) | lifedrainmindam | 2 | 12% Life and Mana stolen per Hit |  | 12% Life and Mana stolen per Hit | OK |
| 62 (group) | manadrainmindam | 2 | 12% Life and Mana stolen per Hit |  | 12% Life and Mana stolen per Hit | OK |
| 76 (group) | item_maxhp_percent | 4 | Maximum Life and Mana +12% |  | Maximum Life and Mana +12% | OK |
| 77 (group) | item_maxmana_percent | 4 | Maximum Life and Mana +12% |  | Maximum Life and Mana +12% | OK |
| 86 (group) | item_healafterkill | 1 | +12 Life and Mana after each Kill |  | +12 Life and Mana after each Kill | OK |
| 93 (group) | item_fasterattackrate | 2 | 12% Combat Speeds |  | 12% Combat Speeds | OK |
| 99 (group) | item_fastergethitrate | 2 | 12% Combat Speeds |  | 12% Combat Speeds | OK |
| 102 (group) | item_fasterblockrate | 2 | 12% Combat Speeds |  | 12% Combat Speeds | OK |
| 105 (group) | item_fastercastrate | 2 | 12% Combat Speeds |  | 12% Combat Speeds | OK |
| 128 (group) | item_attackertakeslightdamage | 3 | Attacker Takes Elemental Damage of 12 |  | Attacker Takes Elemental Damage of 12 | OK |
| 138 (group) | item_manaafterkill | 1 | +12 Life and Mana after each Kill |  | +12 Life and Mana after each Kill | OK |
| 329 (group) | passive_fire_mastery | 4 | +12% to Spell Damage |  | +12% to Spell Damage | OK |
| 330 (group) | passive_ltng_mastery | 4 | +12% to Spell Damage |  | +12% to Spell Damage | OK |
| 331 (group) | passive_cold_mastery | 4 | +12% to Spell Damage |  | +12% to Spell Damage | OK |
| 332 (group) | passive_pois_mastery | 4 | +12% to Spell Damage |  | +12% to Spell Damage | OK |
| 333 (group) | passive_fire_pierce | 20 | -12% to Enemy Elemental Resistances |  | -12% to Enemy Elemental Resistances | OK |
| 334 (group) | passive_ltng_pierce | 20 | -12% to Enemy Elemental Resistances |  | -12% to Enemy Elemental Resistances | OK |
| 335 (group) | passive_cold_pierce | 20 | -12% to Enemy Elemental Resistances |  | -12% to Enemy Elemental Resistances | OK |
| 336 (group) | passive_pois_pierce | 20 | -12% to Enemy Elemental Resistances |  | -12% to Enemy Elemental Resistances | OK |
| 338 (group) | passive_dodge | 2 | 12% Chance to Avoid Damage |  | 12% Chance to Avoid Damage | OK |
| 339 (group) | passive_avoid | 2 | 12% Chance to Avoid Damage |  | 12% Chance to Avoid Damage | OK |
| 340 (group) | passive_evade | 2 | 12% Chance to Avoid Damage |  | 12% Chance to Avoid Damage | OK |
| 343 (group) | passive_mastery_melee_dmg | 3 -> - | _(no line)_ |  | _(no line)_ | OK (no line) |
| 357 (group) | passive_mag_mastery | 4 | +12% to Spell Damage |  | +12% to Spell Damage | OK |
| 382 (group) | life_on_striking | 1 | +12 Life and Mana on Striking |  | +12 Life and Mana on Striking | OK |
| 383 (group) | mana_on_striking | 1 | +12 Life and Mana on Striking |  | +12 Life and Mana on Striking | OK |
| 388 (group) | to_strength | 2 | 12% to All Attributes |  | 12% to All Attributes | OK |
| 389 (group) | to_dexterity | 2 | 12% to All Attributes |  | 12% to All Attributes | OK |
| 390 (group) | to_energy | 2 | 12% to All Attributes |  | 12% to All Attributes | OK |
| 391 (group) | to_vitality | 2 | 12% to All Attributes |  | 12% to All Attributes | OK |
| 396 (group) | attacker_takes_cold_damage | 3 | Attacker Takes Elemental Damage of 12 |  | Attacker Takes Elemental Damage of 12 | OK |
| 397 (group) | attacker_takes_fire_damage | 3 | Attacker Takes Elemental Damage of 12 |  | Attacker Takes Elemental Damage of 12 | OK |
<!-- table:end -->

## Layered special text follow-up (special-text, 2026-10-25)

The earlier audit incorrectly treated empty descstrpos columns for functions 31 / 34 as proof of an empty line. Sigma
`0x10077720` and `0x10077cb0` read the low layer word as a Median numeric string ID. Function 31 selects colour value minus
one for values 1..13, otherwise colour 3; function 34 always selects orange (8). Sigma `0x100b5530` prefixes the colour and
copies the text without interpolating the stat value. The migration had erased the functions, and the plugin also explicitly
suppressed them. Both are corrected; original priorities (241 / 3 / 232 / 241) are preserved.

`tools/item_special_data.py` scans every source property slot and generates a sorted ID-to-key mapping, checking each text
against Median's TBL. The 812 IDs cover 2027 references: 585 unique properties, 2 set-item properties, 17 set bonuses,
75 runeword properties, 19 automagic, 3 magic suffixes and 1326 cube recipe properties. Namespace keys are resolved through
the localization service, including `_mxl` aliases. Numeric IDs remain unchanged in the stat layer and saved items.

Azure Wrath (unique 1485, `14@`) uses stat 404, layer 11278, value 9. Before: its three special lines were absent and it
printed `+2 to FLYING POLAR BUFFALO ERROR`. After: the expected orange paragraph is:

> You Cannot Deal Fire, Lightning or Poison Damage
> Max Life gained per point of Vitality reduced by 0.9
> 100% of Vitality Added as Cold Damage to Weapon

The spurious +skill line is omitted: Sigma's descfunc 27 (`0x100771a0`) and 28 (`0x100774c0`) test skills2 AltTooltip through
`0x100af8f0` before naming a skill. Azure's hidden passive is skill 1651 (migrated 1723), AltTooltip=1, without a skilldesc.
The generated header includes the complete AltTooltip skill set rather than a one-item exception.

The `specialtext` game check reads all 812 IDs through D2R's formatter, probes all four stats with 16 colour values, and
reads Azure Wrath, Staff of Herding, unique 30 and Optical Detector through the actual item builder. `tipfmt` now emits an
additional `tipcolour` record so the colour is checked before the existing plain-text probe strips escape codes. An
`MXL_CONTROL=itemtext` run expects the old behavior. Long paragraphs remain limited by the native 256-byte output buffer;
see the precise list in [unported.md](unported.md). Full set/runeword assembly is outside this check; their referenced strings
are covered through the same stat description path.

The original baseline was read on the lane's `SpecialTxt` character in slot 2 (`checks-20261025-182658-s2`).
The itemtext-off control passed in slot 3 (`checks-20261025-184439-s3`): all four unique paragraphs disappeared;
Azure Wrath again printed the missing passive name, and D2R's descfunc 31 emitted `+9 to a Random Class' Skill Levels`.
The initial on run (`checks-20261025-184050-s3`) passed the 313-line regression and restored all four unique tooltips,
but caught one dangling colour introducer at the long-paragraph boundary (12383); truncation now drops incomplete colour
escapes as well as incomplete UTF-8 characters. One earlier intermediate install (`checks-20261025-183809-s3`) could not
start because a partial table regeneration had dropped build-time namespace aliases; full regeneration restored them,
and final data validation has zero errors in both banks. These are build artifacts, not committed table changes.

Final run: `checks-20261025-185239-s3`, `tooltiptext specialtext` **PASS**. The regression reads 313 property lines
(294 OK, 19 item-dependent) and 54 lines from five named uniques plus six rare/magic bases. The special check reads
812 IDs (778 complete short strings, 34 bounded long paragraphs), all four layered stats at 16 colour values, and four
real uniques. No crash files, zero cursor or foreground changes, no watchdog abort. The plugin-off control above is PASS.
Both use only the lane-created `SpTextThree` character; slot 1 was untouched. Existing engine/harness assertions remain
in logscan (the assertion inventory is in testing.md); no plugin error/fail/assert lines were added by these checks.

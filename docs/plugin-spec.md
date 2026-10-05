# mxl-core plugin: what it must do (from D2Sigma)

What D2Sigma changes in the engine, as found in `D2Sigma.dll` (1.10, base 0x10000000; decompiled in
`mxl_extracted/3_ghidra/D2Sigma_decompiled.c`). Where to hook in D2R comes from the Ghidra analysis of the
dumped D2R image (`tools/dump_image.py`, `tools/ghidra_analyze.ps1`); this file is the *what*.

## Skill server functions (skills.txt `srvdofunc`)

D2Sigma keeps its own table of skill server functions at **0x101df698** (4-byte pointers, index = srvdofunc).
Zero entries are the original D2Game functions, filled in at start-up; non-zero entries are D2Sigma's. Found by
locating the run of pointers to functions that assert in `D2SkillDo.cpp`: the 19 functions D2Sigma adds
(153-171) are contiguous at 0x101df8fc-0x101df944, which fixes the table start.

**Added (153-171): D2R has no such function; skills with these values do nothing until the plugin adds them.**

| srvdofunc | D2Sigma function | Median skills | Examples |
|---|---|---|---|
| 153 | 0x100ad720 | 40 | blood_skeleton, ice_elementals, fire_elementals, summon_familiars |
| 154 | 0x100aafa0 | 3 | mysterious_companion_skill, judgementwander_2_skill |
| 155 | 0x100ab090 | 1 | **town_portal** (see below) |
| 156 | 0x100aaee0 | 1 | skills_374 |
| 157 | 0x100aca50 | 25 | catapult_shot, overkill, diseased_cattle, hail_of_stones |
| 158 | 0x100acc50 | 74 | DeathMaul, MegademonInferno, CountessFirewall, egg_trap |
| 159 | 0x100aa7e0 | 15 | spiral_dance, heaven_s_fury, crucify_2, barrier_strike |
| 160 | 0x100a97e0 | 4 | thundergod, armageddon_2, frigid_domain, overheat |
| 161 | 0x100ad0f0 | 2 | hyena_strike, exorcism |
| 162 | 0x100ac7c0 | 1 | skills_667 |
| 163, 164, 168-170 | 0x100a9ac0, 0x100ac430, 0x100aabf0, 0x100ac200, 0x100aa9e0 | 0 | unused by the data |
| 165 | 0x100ab190 | 10 | Holy Freeze, skills_1907 |
| 166 | 0x100abff0 | 7 | flamestrike, flamestrike_2 |
| 167 | 0x100ad4a0 | 1 | resurrect_2 |
| 171 | 0x100acee0 | 1 | queen_of_blades |

**Replaced originals: D2R runs the vanilla behaviour for these today, which may differ from Median.**

| srvdofunc | D2Sigma function | Median skills | Examples |
|---|---|---|---|
| 11 | 0x100aa610 | 57 | bladestorm, retaliate, backstab |
| 16 | 0x100adb60 | 0 | |
| 18 | 0x100ab710 | 122 | Fire Ball, CatapultPlague, RogueMissile |
| 24 | 0x100ab400 | 17 | Fire Wall, Charged Bolt Sentry |
| 27 | 0x100ad9d0 | 53 | Teleport, jitan_s_gate |
| 41 | 0x100a9290 | 4 | vampiric_strike, deathgaze |
| 54 | 0x100aa050 | 1 | queen_of_blades_2 |
| 68 | 0x100a9f50 | 39 | Shout, lightning_shield |
| 73 | 0x100aa120 | 2 | hammer_of_zerae, power_shift |
| 77 | 0x100abb40 | 2 | Leap |
| 78 | 0x100abd10 | 1 | Leap Attack |
| 112 | 0x100ac550 | 1 | MonCurseCast |
| 114 | 0x100ad460 | 9 | jinn, blade_spirits |
| 120 | 0x100aa320 | 1 | takedown |
| 122 | 0x100ab920 | 0 | |
| 124 | 0x100a9510 | 38 | Hurricane, war_spirit |

### 155: town portal (`FUN_100ab090`)

```
if (level > 0) {
    player->flags(+0xc4) |= 0x40;
    if (Ordinal_10786(player, skillRecord+0x138, skillId, level)   // original-game call, see below
        && FUN_10097a80())
        { FUN_10049c00(1); return 1; }
    if (FUN_100bf920()) FUN_100b1420(0x13);                     // failure message (0x13)
}
return 0;
```

It does the town-portal work without the scroll/book item the original function 113 ("Book of Townportal")
needs, which is why setting srvdofunc 113 did not help (bug #6). To do: name `Ordinal_10786` (which module and
function), `FUN_10097a80` and `FUN_10049c00`, then find D2R's portal-creation function and call it the same way.

## Still to specify

- Skill calc codes D2Sigma adds (`D2SkillCalc.cpp`): `mlvl`, `skcp`, `skcn` (skill level cap), `syn1`-`syn6`,
  `wdm`, `skcd`, `aura`, `bl12..`, `area`, ... (docs/validation.md lists counts).
- Formula functions: `hasstate`, `tabpoints`, `statlayer`, `itemstat`, items `skilllevel`, missiles `stat`
  (docs/formulas.md).
- Skill tree rules (`ui/SkillTree.cpp`): devotions lock each other, "pick up to three masteries".
- Skill descriptions (`D2SkillDesc.cpp`).
- Engine limits: states (1,014 > MAX_STATES), level types (Median 36-51).
- Atma/Geglash sacred-item service (npcstores +40/+41, docs/vendors.md).

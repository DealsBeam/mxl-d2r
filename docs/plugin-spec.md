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
| 153 (-> 172) | 0x100ad720 | 40 | blood_skeleton, ice_elementals, fire_elementals, summon_familiars — **plugin** (multi-summon) |
| 154 (-> 173) | 0x100aafa0 | 3 | mysterious_companion_skill, judgementwander_2_skill — **plugin** (red portal to the Median level calc1, the keys' routine; 0.21.0) |
| 155 (-> D2R 109) | 0x100ab090 | 1 | **town_portal** (see below) |
| 156 (-> 175) | 0x100aaee0 | 1 | skills_374 — not ported (two D2Game calls; hidden skill) |
| 157 (-> 176) | 0x100aca50 | 25 | catapult_shot, overkill, diseased_cattle, hail_of_stones — **plugin** (barrage) |
| 158 (-> 177) | 0x100acc50 | 74 | DeathMaul, MegademonInferno, CountessFirewall, egg_trap — **plugin** (missile row) |
| 159 (-> 178) | 0x100aa7e0 | 15 | spiral_dance, heaven_s_fury, crucify_2, barrier_strike — **plugin** (strike burst) |
| 160 (-> D2R 124) | 0x100a97e0 | 4 | thundergod, armageddon_2, frigid_domain, overheat — **data** (D2R's Armageddon) |
| 161 (-> D2R 121) | 0x100ad0f0 | 2 | hyena_strike, exorcism — **data** (D2R's Rabies, see below) |
| 162 (-> 181) | 0x100ac7c0 | 1 | skills_667 — not ported (casts a random one of Param1-8's skills) |
| 163, 164, 168-170 | 0x100a9ac0, 0x100ac430, 0x100aabf0, 0x100ac200, 0x100aa9e0 | 0 | unused by the data |
| 165 (-> 184) | 0x100ab190 | 10 | Holy Freeze, skills_1907, item auras (Penumbra, Majesty, ...) — **plugin** (aura with layers) |
| 166 (-> 185) | 0x100abff0 | 7 | flamestrike, flamestrike_2 — **plugin** (missile at target) |
| 167 (-> D2R 58) | 0x100ad4a0 | 1 | resurrect_2 — **data** (D2R's Revive; D2Sigma sets life to calc1 outright, D2R reads it as a life bonus) |
| 171 (-> 190) | 0x100acee0 | 1 | queen_of_blades — **plugin** (area damage, see below) |

**Replaced originals: D2Sigma's own versions of vanilla functions.** Their plugin slots use the ones 153-171's +19 shift
leaves free (174, 179, 180). Compared with D2R's (mxl-core 0.18.0): most
are vanilla's routine re-implemented, the Median additions being the skills2 layers (as in 165) and a range limit.

| srvdofunc | D2Sigma function | Median skills | Examples | Port |
|---|---|---|---|---|
| 11 | 0x100aa610 | 57 | bladestorm, retaliate, backstab | same as D2R (0x5562b0): melee, then calc1 missiles (flags 0x21) |
| 16 | 0x100adb60 | 0 | | unused |
| 18 | 0x100ab710 | 122 | Fire Ball, CatapultPlague, RogueMissile | self buff: D2R 0x553e60 + layers; the 6 rows with layers -> **slot 174** |
| 24 | 0x100ab400 | 17 | Fire Wall, Charged Bolt Sentry | D2R 0x553950 (D2R also caps active walls); D2Sigma clamps the spot to aurarangecalc (only burning_veil sets one: not ported) |
| 27 | 0x100ad9d0 | 53 | Teleport, jitan_s_gate | D2R 0x554c40 + distance limit aurarangecalc (-1 refuses) for players -> **slot 179** (47 rows with a range) |
| 41 | 0x100a9290 | 4 | vampiric_strike, deathgaze | not compared (4 skills) |
| 54 | 0x100aa050 | 1 | queen_of_blades_2 | not compared (queen_of_blades_2) |
| 68 | 0x100a9f50 | 39 | Shout, lightning_shield | same as D2R (0x567500): nova + buff; layers unused by its rows |
| 73 | 0x100aa120 | 2 | hammer_of_zerae, power_shift | not compared (2 skills) |
| 77 | 0x100abb40 | 2 | Leap | aurarangecalc like vanilla Leap |
| 78 | 0x100abd10 | 1 | Leap Attack | not compared (Leap Attack) |
| 112 | 0x100ac550 | 1 | MonCurseCast | not compared (MonCurseCast) |
| 114 | 0x100ad460 | 9 | jinn, blade_spirits | calls the original (Raven) |
| 120 | 0x100aa320 | 1 | takedown | not compared (takedown) |
| 122 | 0x100ab920 | 0 | | unused |
| 124 | 0x100a9510 | 38 | Hurricane, war_spirit | storm: D2R 0x575de0 + layers; land_of_the_dead -> **slot 180** |

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
needs, which is why setting srvdofunc 113 did not help (bug #6).

**Resolved without plugin code:** D2R has its own itemless town portal, srvdofunc **109** (skill 411 "Townportal O
Skill", `0x5822b0`): `0x46fa70` finds the destination town (Act start level), creates the portal object 0x3b with
the portal-creation routine `0x432ce0` (`D2Game\src\Items\ItemMode.cpp`), and `0x470ae0` creates and links the
town-side portal. `txt_migrate.py` maps town_portal to 109 (and sets rightskill 1). D2Sigma's extra
`player+0xc4 |= 0x40` flag is not reproduced (unknown purpose; revisit if portals behave differently).

### Reading D2Sigma

D2Sigma is built with whole-program optimisation: its functions take arguments in registers (ECX game, EDX unit,
stack skill id and level) and the decompiler loses track of them, so the functions are read from the disassembly
(`tools/ghidra_query.ps1 Disasm.java -Project D2Sigma <address>`; the D2Sigma project is `..\ghidra\sigma`). It
calls the vanilla 1.10 D2Common by ordinal: 10786 evaluates a skill formula (unit, calc, skill, level), 10769 is the
squared distance (x1, y1, x2, y2). 1.10 skill-record offsets: calc1-4 +0x138-0x144, prgcalc1 +0x38, aurarangecalc
+0x64, srvmissile +0x46 (a/b/c +0x48/+0x4a/+0x4c), petmax +0xc0.

### 157: barrage (`0x100aca50`)

prgcalc1 missiles of srvmissilea (flags 0x420), each aimed at a random point within aurarangecalc of the skill
target (rolled twice on the caster's seed, which is first reseeded: lo = next roll, hi = 0x29a); points within
squared distance 4 of the caster are skipped. One missile, or a radius below 2: a single missile at the target.

### 158: missile row (`0x100acc50`)

calc1 missiles (flags 0x820) from the caster towards points on a line through the target, perpendicular to the aim,
spaced by the aim direction rotated 90° and shrunk to length² <= 3, centred on the target. Missile: srvmissilea/b/c
by calc3 clamped to 1-3. calc2 goes to the missile's attack-bonus field (1.10 params +0x44).

### 153: multi-summon (`0x100ad720`)

min(calc2, petmax) pets (summon, summode, pettype at 1.10 +0xbc/+0xbf/+0xbe; petmax +0xc0), each via 1.10's
summon-pet call. Position: TargetCorpse skills (flag bit 24 at +4) use the target corpse (no skill on 153 has it);
otherwise, when aurarangecalc is non-zero and the skill target lies within it (squared distance), the target, else the
caster. After each pet: `0x100683d0` (D2Hirelings.cpp, Median's minion scaling; below), `0x10067940` (the skill's aura/passive stats on the pet; D2R `0x55ef80`), `0x10042740` (party/AI;
D2R `0x58d950`), `0x10049c00(owner, 0x200b, 1)` (D2Achievements.cpp counter; skipped). The plugin also sets the pet
level with D2R's `0x55fb50` as D2R's Raise Skeleton does (level = the owner's level, as D2Sigma's step 2).

**Minion scaling** (`0x100683d0` (game, owner, pet, skill, level); mxl-core 0.15.0, `pet_scaling.cpp`). 1.13c skills
record: calc1 +0x138, Param3/4/5 +0x150/+0x154/+0x158 (D2R +0x1c0/+0x1c4/+0x1c8). D2Common 10887 sets a unit stat
(D2R `0x2f7d10`), 10551 adds (D2R `0x2f34f0`).

| Step | Function | Does | Port |
|---|---|---|---|
| 1 | 0x10067b30 | new stat list on the pet, state 307, stats 350/351 = skill, level (modifierlist) | no |
| 2 | 0x10068520 | level = owner's level (at most the class's experience.txt MaxLvl) | D2R 0x55fb50 |
| 3 | 0x10068120 | tohit (19) set to mnar: (level-1)*Param5, plus that * owner stat 500 / 100 | yes |
| 4 | 0x10068190 | maxhp and hp set to mnhp: (Param3 + (level-1)*Param4) * (pet level/2 + 100) * (calc1 + 100) / 3333, << 8 (unsigned 64-bit, clamped) | yes |
| 5 | 0x10068250 | add owner stat 487 to fire/cold/lightning res (<= 105) and poison res (<= 80); owner stat 229 to damage resist (<= 50) | yes |
| 6 | 0x10067db0 | add the owner's pierce stats 333-336 when not 0 | yes |
| 7 | 0x10067e20 | pettypeext byte 2: the owner's skill-on-event stats 196, 292 (and 195 if 1) with chance / byte 13, layers re-encoded | 0.20.0 (layers kept as they are; the owner's layers read by `stat_list.cpp`: unit +0x88 StatListEx, totals array found at run time and checked against `0x2f5020`) |
| 8 | 0x10068020 | pettypeext byte 0: add the owner's 1, 0, 485, 278, 86 in full and 329-332, 357 halved | yes |
| 9 | 0x10068320 | the skill's aurastat/passivestat (1.13c +0x54/+0x68, +0x98/+0xa4) on the pet | D2R 0x55ef80 |
| 10 | 0x100681e0 | sumumod (+0xe4) on the pet | empty on all 153 skills |

pettypeext: 14 bytes per pettype row (work/sigma/pettypeext.txt), indexed by migrated pettype in `mxl_pets.h`.
Calc codes `mnhp`/`mnar` call the same formulas with the unit's own level (`0x100a6340`, EDX = 0); mnhp stays in 1/256.

### 159: strike burst (`0x100aa7e0`)

Like vanilla Charged Strike (D2R `0x5562b0`): the skill target unit, if any, takes the queued melee hit (1.13c pointer
`0x101f1b04`; D2R `0x44b2b0`); calc1 missiles (srvmissilea/b/c by calc2 clamped 1-3, flags 0x21) start at the skill
target and fly to 2*target - caster; each gets 1.13c's charged-strike init function (`D2Game+0x41f50`, through
`0x101f1a40`) with its index. D2R replaced the init-function pointer with a kind code at params +0x70 (1 =
charged-strike bolt, 4 = multishot) and its argument at +0x74. D2Sigma first calls `0x10041f30(game, unit)` (through
`0x101f1d78` = D2Game `+0xe07a0`): vanilla's skill-target check, which clears the path target when it no longer
matches its GUID or is an item on the ground/equipped (97 call sites in 1.13c). D2R's is `0x491b50`, run by the target
getter `0x48fe20` that the plugin calls, so it is covered. D2Sigma resolves D2Game/D2Common addresses as (module index, RVA) through
`0x10042870`; module 8 is D2Game.dll (table at `0x101d4014`). Median runs on 1.13c.

### 160: storm state (`0x100a97e0`)

A timed aurastate for auralencalc frames carrying the skill's aura stats and stats 350/351 (skill, level), plus a
periodic type-5 event: D2R's Armageddon/Hurricane function 124 (`0x575de0`) does the same, and the Median rows use
vanilla Armageddon's columns (cltprgfunc1 92, srvmissilea-c controllers, Param4/6/7/8). `txt_migrate.py` maps 160 to
124.

### 166: missile at target (`0x100abff0`)

One missile (srvmissilea/b/c by calc3 clamped 1-3, flags 1) starting on the skill target; refused when the target spot
is blocked for the missile's size (D2Common 10303, mask 5; not ported) or beyond D2Common 10080's approximate
distance (max + min/2) of 100.

### 161, 171: Median's damage block

**161 is D2R's Rabies (121, `0x576bb0`)**: both do a melee roll; on a hit, auratargetstate on the target (ELen),
a missile at the target with flags 0x8000, the skill's elemental damage (EType/EMin/EMax) in the damage block and
the melee hit. Only the missile choice differs: D2Sigma takes srvmissilea/b/c by calc2 (clamped 1-3), D2R
(`0x571700`) by a state/stat rule of its own, else srvmissilea. Mapped in data; hyena_strike's srvmissilec
(when Median stat 191 > 0) is lost.

**165 (auras; ported, see above):** `0x100ab190` is vanilla's aura routine (aurastate on the caster, auratargetstate on
units found by aurarangecalc/aurafilter through D2Game's finder, callback `0x100bbbe0`), plus one Median addition:
each aurastat carries a layer from D2Sigma's skills2 (first six dwords per row), e.g. aurastat `state` with layer 940
(a Median state) or `item_skillonattack` with a packed skill/level. 6 of the 10 skills use layers, so D2R's aura 65
alone is not enough: needs D2R's aura routine with layered stats (migrated state/skill ids).

Both fill a 0x70/0x6c-byte damage block themselves (Median's damage: elemental type switch on the 1.13c record's
EType, min/max from Median's own helpers `0x100a6020`/`0x100a5ec0`, `0x10041e80`), then 161 applies it to the melee
target and 171 to every unit in aurarangecalc chosen by aurafilter (`0x10042600`, callback `0x100bb740`). Port
together with D2R's damage application.

### 171: area damage (`0x100acee0`) — mxl-core 0.17.0, slot 190 (`area_damage.cpp`)

queen_of_blades is periodic (srvstfunc 28 puts its buff state on; the periodic event calls 171 every perdelay frames).
D2Sigma builds one damage block (hit flags 2 | HitFlags, HitClass, the skill's elemental damage) and hits every unit in
aurarangecalc chosen by aurafilter (`0x10042600`, callback `0x100bb740`), adding the caster's weapon damage scaled by
SrcDam (D2Game `0x6fcfd450`, SrcDam/128) per target. Ported with D2R's pieces the way its damage auras (srvdofunc 66,
`0x5630e0`, callback `0x561280`) use them:

| D2R | What |
|---|---|
| `0x437d00` (unit, &damage, skill, level) | skill's elemental damage into the block |
| `0x4327d0` (game, unit, 0, 0, range, filter, callback, context, 0, file, line) | unit finder; callback (finder ctx {game, caster, count, context}, target) -> counted if non-zero |
| `0x4494b0` (&to, &from) | copy a damage block |
| `0x44c030` (game, attacker, target, &damage, 0, srcDam byte) | weapon damage, srcDam/128 (0 = 128) |
| `0x44ce80` (game, attacker, target, 1, &damage), `0x44a9b0` (game, attacker, target, &damage) | hit and apply |

D2R's damage block is 0x180 bytes: +0 hit flags, +4 result flags (u16), +0x40/+0x48/+0x50 a small list (data, count,
capacity with bit 63 = inline storage at +0x58, 16 x 12 bytes), +0x144 hit class, +0x158 a tagged callback pointer
(1 = empty); the destruction D2R inlines is in `DestroyDamage`. SrcDam comes from `mxl_damage.h` (txt_migrate
damage_tables). Not ported: the hit roll when ResultFlags bit 0 is clear (`0x10057690`; every unit found is hit),
Median's physical damage helpers (no MinDam/MaxDam on queen_of_blades) and `0x10056d20`.

### 165: aura (`0x100ab190`) — mxl-core 0.16.0, slot 184 (`auras.cpp`)

Ported as D2R's aura (srvdofunc 65, `0x563d90`) followed by a rewrite of the caster's aura stat lists
(`0x2f5b80` (unit, state, 0) for aurastate and auratargetstate; `0x2f7c00` (dataFlag, list, stat, value, layer)):
each layered aurastat gets its own aurastatcalc (D2R record +0x88 + 4k; aurastat ids +0x74) at its layer, layer 0 of
that stat the last unlayered value or 0. Layers come from `mxl_auras.h` (txt_migrate aura_tables: skills2 dwords 0-5,
`state` layers -> migrated states, Encode 2/3 -> (skill << 6) | level with the migrated skill, +skill/synergy stats ->
migrated skill). Targets other than the caster keep D2R's layer-0 stats (no layered Median aura has a range).
The notes below are the original analysis.

Median's own aura pulse built from C++ callbacks: aura stats onto units with aurastate (1.13c +0x80), auratargetstate
(+0x82), units in aurarangecalc (+0x64) chosen by aurafilter (+0x50) through `0x10042600` with callback `0x100bbbe0`.
D2R's generic stat aura is srvdofunc 65, but Median's auras use aurafilter 1 where vanilla uses e.g. 73731, and
D2Sigma's callback does its own target selection: a plain remap to 65 could put an enemy aura (Holy Freeze) on the
caster's party. Needs the callback's rules first.

## Client skill functions (skills.txt `cltdofunc`)

D2Sigma's client table is at **0x101df428** (4-byte pointers; entries other than D2Sigma's are filled with D2Client's
at start-up). D2Sigma's own entries: replaced vanilla 11, 15, 22, 31, 93, 94, 95 and added 96-99. D2R's table is **0x235f7d0** (8-byte pointers, bound 130 at `0x235f7c4`, dispatcher `0x216d20` calls
(unit, skill, level)); D2R 3.x uses 96-109 for its own skills (97 Volcano, 98 Sigil Death Explosion, 99 Ring of Fire),
so Median's rows ran those. 110-129 are free.

| D2Sigma | Function | Median skills | Pairs with | Port |
|---|---|---|---|---|
| 97 | 0x100a7690 | 74 | server 158 (missile row) | **110, 0.23.0**: D2Sigma's evaluates aurarangecalc and three calcs, reads the target and returns 0 without creating anything; ported as a function returning 0 |
| 98 | 0x100a84b0 | 15: whirlpool, torment, spiral_dance, crucify_2, ravage, mana_pulse, ... | server 159 (strike burst) | **111, mxl-core 0.22.0**: cltmissilea/b/c by calc3 (1-3) on the target, skipped beyond distance 100 |
| 99 | 0x100a7cb0 | 7: flamestrike, dragon_jaws, havoc | server 166 (missile at target) | **112, 0.23.0**: calc1 client missiles, cltmissilea/b/c by calc2 (1-3), on the skill's target point (`0x214c80`) aimed past it, flags 0x21, index as init argument (initKind 1). calc1 is empty on flamestrike/dragon_jaws, so they draw no client missile in Median either |

Replaced vanilla client functions (mxl-core 0.24.0), compared with D2R's:

| D2Sigma | Function | Median skills | D2Sigma does | Port |
|---|---|---|---|---|
| 11 | 0x100a8910 | flamefront_15 | random spray (±20) of the picked missile around the target | data -> D2R 15 (`0x2b3750`, the same spray) |
| 15 | 0x100a7920 | vampiric_strike, batstrike_2, deathgaze, skills_2759 | Charged-Strike fan: calc1 missiles on the target aimed past it, index as init argument | data -> D2R 19 (`0x2ae540`, client Charged Strike: the same) |
| 22 | 0x100a7fa0 | parasite_2, parasite_3, rock_shock | wall: two cltmissilea on the target point (clamped to aurarangecalc) flying out perpendicular | **plugin 113** (`CltDoWall`); D2R's 22 is now an Amazon chain-lightning client. Not ported: the spot's collision test, town check |
| 31 | 0x100a7780 | Raise Skeleton, Raise Skeletal Mage | cltmissilea/b/c by calc2 on the corpse | keeps D2R 31 (`0x2b0ac0`): calc2 is empty, so both pick cltmissilea |
| 93 | 0x100a86d0 | none | | |
| 94 | 0x100a7ad0 | doomknight2_skill | Charged-Strike fan by calc2/calc1 (calc1 empty: draws nothing) | keeps D2R 94 (vanilla Doom Knight missile) |
| 95 | 0x100a82a0 | necromage_skill | same as 98 | data -> plugin 111 |

D2R client pieces used: data flag `0x34a0e0` (unit), skill target `0x9a570` (unit), client missile `0x1b7760`
(&params, 0; same parameter block as the server's `0x5371a0`), record cltmissile +0x13e, a/b/c +0x140/+0x142/+0x144.

## Missile functions

D2Sigma's missile tables (filled at start-up with the vanilla D2Client/D2Game functions, its own entries left in):
client do **0x101dc4b0** (72), server do 0x101dc5d0, client hit **0x101dc6c8** (64), server hit **0x101dc7c8**. D2R's:
server do `0x2390e80` (index = record +0x2c), client do `0x2358430`, server hit **`0x2391030`** (record +0x2e,
bound 71 at `0x2391404`, dispatcher `0x464ac0`: (game, missile, target or null); 61-70 empty), client hit
**`0x2358710`** (record +0x2a, bound 81 at `0x2358a64`). The 1.13 D2Game that D2Sigma calls into is decompiled in
`mxl_extracted/3_ghidra/D2Game.dll.c` (and a Ghidra project of the launcher's copy, `ghidra_query.ps1 -Project D2Game`).

**Empty table slots (bug #24b, mxl-core 0.48.0+):** the client missile tick `0x1ba040` calls client do `table[record +0x28]` for any index below the bound with no null check (slots 0 and 78-90 are empty), and the server hit dispatcher `0x464ac0` does the same for 1 <= index < 71 (slot 70 is D2Sigma's unported hit 42). A Median missile with an empty pCltDoFunc (45 rows in the main bank, 95 in the base bank; row 5507 `warp_crash_missile` is cast by level 251's monsters) crashed the client with a call to address 0 outside a town. `InstallMissileHits` fills every empty slot of both tables (after the ported functions) with a function that does nothing and logs the first missile row per table; the client hit dispatcher (`0x1b8e60`) and the server do dispatcher (`0x466ce0`, index > 0) check or exclude the empty case and are left alone. Harness: `missilescan` lists the rows that point at an empty slot, `missile <row>` fires one.

| Function | D2Sigma | Median missiles | Port |
|---|---|---|---|
| client do 19 / server do 15 | 0x100892f0 / 0x10089930 | 1,110 / 1,017 (Frozen Orb style spawners) | same as D2R's (`0x1a8880` / `0x459380`) |
| server do 4 -> **46**, client do 69 -> **14** | 0x10089cc0, 0x10089660 | 2 (phalanx_missile, phalanx_2_missile; Amazon phalanx_2) | **0.32.0**: every Param1 frames (client CltParam1) SubMissile1 (record +0x3a; client CltSubMissile1 +0x40) aimed along direction step & 63, range Param3 (CltParam3) else the skill's calc3, flags 0x8002; the step (missile data +0x30 = 1.13c +0x28, D2Common 11018/10637; D2R setter `0x3bd8a0`, set by Fire Wall's skill function) then turns by Param2; then do 1 (server slot 1 `0x4550e0`, client `0x1b5380`). D2R missile data: +0x0c skill, +0x0e level, +0x10 total frames (int16, `0x3bdbc0`), +0x14 frames left (float x 40 per frame, getter `0x3bb1e0` = 1.13c 10737, setter `0x3bd450`), elapsed `0x3bb0f0` (10985). Server do table `0x2390e80` (dispatcher `0x466ce0`, no bound; 46-54 empty); client do `0x2358430` (77 slots, called (missile, float part-frame) from `0x1ba040`; slot 14 used by no missile). Aimed with the nova table instead of D2Game's (the tiles do not move). The client drops on reaching each multiple of CltParam1 (it runs part-frames) |
| server hit 21 | 0x1008cb20 | 57 | same as D2R's (`0x45ef10`: auratargetstate + aurastats on the target); D2Sigma adds skills2 layers, which none of these 57 missiles' skills use |
| server hit 72 -> **64** | 0x1008d400 | 55 (egg_trap, wintertide, phoenix_wave, ...) | **0.25.0**: the owner casts skill sHitPar1 (Median row, translated) at level sHitPar2 / the missile's level on the unit hit or the missile's spot, through D2R's event-skill caster `0x589930` (1.13c `0x6fd114f0`) |
| server hit 61 -> **61** | 0x1008d2d0 | 3 | **0.25.0**: the owner runs server skill function sHitPar1 (D2Sigma numbering, translated) for the record's Skill (else the missile's) at the missile's level |
| server hit 69 -> **1** | 0x1008eeb0 | 21 | **data**: vanilla hit 1 (explosion: missile damage with HitFlags/ResultFlags to all units within sHitPar1, else calc1; D2R `0x45bc40`) with weapon damage recomputed at the hit and an optional to-hit roll (record +0x18b). The missile's SrcDamage is already in its stats from creation, so D2R's hit 1 runs it |
| server hit 77 -> **66** (fixed in 0.32.0: it read the total frames, +0x10, as the frames left, so the nova never fired) | 0x1008d960 | 14 (frigid_nova, frigid_domain, coronation, hurricane_2, hammertime) | **0.26.0**: once the missile's range is used up (missile data +0x10; D2Common 10737 in 1.13c), HitSubMissile1 (+0x46) in every sHitPar1-th of 64 directions (radius-30 table at D2Sigma `0x1018b140`), range sHitPar2 or the skill's calc3, flags 0x8002 (offset target, range at params +0x5c); returns 3. Not ported: D2Common 10637/10019 on each new missile |
| server hit 71 -> **63** | 0x1008e6f0 | 12 | **0.27.0**: state Param4 on the missile's owner. SrvCalc1 > 0 puts it on for CltCalc1 frames (< 1: for good), < 0 takes it off; stat sHitPar1 (param Param5) = SHitCalc1, stat sHitPar2 (param CltParam5) = CHitCalc1 (missile formulas, D2R `0x3b4a90`). State list through D2R's self-buff path (`0x433d20`, expiry `0x5551f0`, state `0x3355a0`). txt_migrate translates state, stats and skill/state params (encode 2/3 as skill<<6\|level). Not ported: the client state message (`0x438470`) and D2Common 10158 |
| server hit 41 -> **68** | 0x1008f210 | 14 | **0.27.0**: monstats row = the missile skill's calc1 spawned at the missile in summode (else NU) as an ordinary monster (1.13c `0x6fc66f70` -> D2R `0x492cf0`, retry with search 4). txt_migrate rewrites Median monster rows in those calc1 formulas; ones through ast2/syn1 (other columns) are not translated yet. Not ported: the skill-Param1 superunique path (unused) and sumumod |
| server hit 30 -> **69** | 0x1008d220 (-> D2Objects.cpp 0x10097870 / 0x100976d0) | 8 (ancient_gate, judgementwander) | **0.28.0**: portal at the missile to Median level sHitPar1 (item_spells.cpp `HitLevelPortal`, LevelIds); sHitPar2 set: the one-way portal (Median object 586) once per level and game (the keys' game counter), else a red portal. D2Sigma uses the level's own portal object (1.13c levels +0x156) for the red one: not ported |
| server hit 34 -> **67** | 0x1008df40 | 6 | **0.28.0**: the unit hit, when an ally of the owner (D2R `0x492790` hostile check, 1.13c D2Game 0x6fd00570), gets the skill's calc1 % of maximum life (100+: full) |
| server hit 62 -> **62**, client 6 -> **6** | 0x1008dd70, 0x1008c970 | 2 | **0.28.0**: (Clt)HitSubMissile1 from the missile at the owner's skill target, range (c)HitPar1 else calc1, flags 0x8001 |
| server hit 40 | 0x1008cfa0 | 2 (catapult spike ball) | **data**: vanilla's catapult spikes; D2R's own catapult spike ball uses hit 40 too, so Median's runs D2R's 40 |
| server hit 73 -> **65** | 0x1008f490 | 1 (habeas_corpus) | **0.30.0**: a monster at the missile in monster mode sHitPar2 (0-15, else 1; habeas_corpus: 12 = DD, a corpse) through 1.13c D2Game 0x6fc67cf0 (EAX monster, ECX room; game, x, y, mode) -> 0x6fc66f70 = D2R `0x492cf0`. Monster: Param4 set -> the owner's stat 88 param 0x200f (the class it last revived; D2Sigma's resurrect 0x100ad4a0 sets it), else sHitPar1. The port's resurrect_2 runs D2R's Revive (srvdo 58, `0x55e7e0`), so that slot is wrapped to record the target corpse's class (`0x48fe20`). Stat 0 (nothing revived yet) spawns nothing |
| server hit 42 -> 70 | 0x1008e9a0 | 1 (missiles_4373, skill parasite_2) | **not needed**: no skill, missile, item or D2Sigma code fires missiles_4373 (Parasite fires parasite_2_missile, hit 12), so the bounce is dead content. What it would do: while the missile data +0x28 counter is 2+, a new missile of the same row from the hit spot to another unit within sHitPar1 (else calc1; filter 0x88583), sHitPar2 splash, counter - 1 |
| client hit 7 -> **7** | 0x1008c770 | 14 | **0.28.0**: client half of hit 77: CltHitSubMissile1 (client record +0x4e) in every cHitPar1-th of 64 directions, range cHitPar2 else calc3; D2R's own client spawner (0x1b9840) only does 16 directions. Client: owner `0x9a7b0`, create `0x1b7760` (flag 0x8000 / range +0x5c like the server) |
| client hit 5 -> 20 | 0x1008c660 | 1 (missiles_4677) | **not needed**: nothing fires missiles_4677 (the 4677 in skills2 row 812 is a formula offset). Would run client function cHitPar1 of D2Sigma's table 0x101df418 |

## D2R side (dump of image 0x6ab3782c; RVAs relative to the image base)

| What | RVA | Notes |
|---|---|---|
| Server skill dispatcher | 0x43acb0 | `D2Game\src\Skills\Skills.cpp`; reads `srvdofunc` (u16 at skill record +0x4e), rejects > 0xbe, calls the table entry if non-null with (game, unit, skillId, skillLevel) |
| srvdofunc table | 0x238ea00 | 191 slots of 8-byte pointers (.data). D2R fills 0-171; **172-190 are free** |
| srvstfunc table | 0x238e710 | indexed by `srvstfunc` (u16 at +0x4c) |
| Skill record flags | +0x24..+0x28 | e.g. +0x24 bit0 decquant, bit1 lob; +0x25 bit0 InTown; +0x28 bit5 usemanaondo, bit7 rightskill (mapped from skills.bin vs skills.txt) |
| D2R scroll/book skills (113) | 0x584b60 | only sends packet 0x3f (skill id) to the client (`0x47f520`); the scroll/book use goes through the item code |
| D2R itemless town portal (109) | 0x5822b0 | skill record lookup `0x97790`, then `0x46fa70` (make portal) and `0x470ae0` (link town side) |
| Skill record | 0x97790 | (game+0x106 byte, skillId); records are 748 bytes: srvmissile a/b/c +0x68/+0x6a/+0x6c, prgcalc1 +0x58, aurarangecalc +0x84, calc1-4 +0x190-0x19c, Param1-8 +0x1b8.. (`research/bin_layout.py skills`) |
| Evaluate skill formula | 0x3b5160 | (game+0x106 byte, unit, calc, skillId, level) |
| Skill target position | 0x432b70 | (game, unit, &x, &y); unit position: path at unit+0x38, x/y u16 at +0x2/+0x6 (players, monsters, missiles) or int at +0x10/+0x14 |
| Create missile | 0x5371a0 | (game, &params); params are 1.10's block with 8-byte pointers: flags +0, owner +8, origin +0x10, target +0x18, missile +0x20, x/y +0x24, target x/y +0x2c, skill +0x3c, level +0x40, attack bonus +0x54 (see D2R Multiple Shot, 0x557840) |
| Missile count | 0x1b89a0 | (game+0x106 byte) |
| Unit seed | 0x34a1e0 | (unit) -> unit+0x28 {lo, hi}; the RNG is inlined everywhere (lo*0x6ac690c5 + hi) |
| Unit "skill used" flag | 0x34e790 | (unit, on): unit+0x124 bit 0x40 (1.10 unit+0xc4) |
| Skill target unit | 0x48fe20 | (game, unit) |
| Apply queued melee hit | 0x44b2b0 | (game, attacker, target, 0, 1) (SUnitDmg.cpp) |
| Item spells (misc pSpell) | 0x236fe30 | 20 entries {check, use, flag} (u32 indexes): check handlers at 0x1d3ea30 (11), use handlers at 0x1d3ea90 (16); 16-19 are empty (index 0), 20+ does nothing. Flag 1 = not usable (0x308e80) |
| Item spell "used up" | 0x308e80 | (item) -> 1 when the item's slot flag is 0. The client puts this in the use-item message (0x1c7a30 -> 0x1400ed1e0) and the server (0x4f40c0) refuses the message unless they agree; when set, the server frees the item after the use handler. mxl-core sets slot 17's flag to 1 for the items D2Sigma keeps |
| Remove an inventory item | 0x4712a0 pattern | location 0x382d20 (int[4], item); 0x472590 (player, 0, &location, 0, &unit+8, 0 adds, 1 remove, 0) validates and applies; 0x36dea0 (item, 0); 0x535f60 (game, player, item, 0x20, 4, 0, 0, 0, location[2]); free 0x43ec10 (game, item). Inventory walk: 0x34a360 (unit) -> inventory, 0x388c10 first, 0x38aba0 next; page = item data +0x55 (0x36cfe0: 0 inventory, 3 cube, 4 stash) |
| Item spell dispatchers | 0x5817f0, 0x581680 | check (game, player, item, target) -> bool and use (same args), called from the player's use-item message (0x4f40c0). When the item's spellicon (record +0x144, char) is not -1 the handler gets the clicked target item instead of the item; the caller removes the used item afterwards |
| Item helpers | see item_spells.cpp | class id 0x349860 (unit, file, line); items record 0x314110 (dataFlag, class) with 1.13c's layout to +0x94 (code +0x80, pSpell +0x94, calc1 +0xa4); is-type 0x373890 (item, itemtype); quality 0x36cf60 (item data +0); level requirement 0x376de0 (item, player), -1 none; item update packet 0x535f60 as after identifying (0x46e8c0) |
| Item properties | 0x3d4620 | (type, unit, item, mods, index, set, &property, state, statlist flags, 0): 1.13c's D2Common 10665. Property {int prop, param, min, max}; funcs at 0x2386ab0. Item stat list 0x3d5230 (owner, item, state, flags), created if missing; stat list add 0x2f3330 (dataFlag, list, stat, value, layer) |
| Summon pet | 0x4350e0 | (game, &params) -> pet: 1.10's block with an 8-byte owner: flags +0 (1 = at x/y), owner +8, monster u16 +0x10, AI state +0x14, mode +0x18, x/y +0x1c, pettype +0x24, petmax +0x28. Registers the pet (enforces petmax) |
| Pet setup (D2R Raise Skeleton, 0x55e440) | 0x55fb50, 0x55ef80, 0x58d950 | level from the owner (game, owner, pet, 0, level); the skill's passive/aura stats and aura state on the pet (game, owner, pet, skill, level, owner+0x12c, 0); party/AI (game, pet, 0, owner+0x130). Skills record: summon +0x110, pettype +0x112, summode +0x113, petmax +0x114 |
| Create portal object | 0x432ce0 | (game, unit, room, x, y, destLevel, &object, objectId 0x3b, 0, 0) |

**Collision:** D2R's newer skills use srvdofunc 153-171, the same numbers as D2Sigma's added functions, so Median's
skills ran unrelated D2R functions. `txt_migrate.py` now moves D2Sigma's 153-171 to the free 172-190 (+19);
The plugin writes its functions into those slots (a data write into .data, guarded by the
expected value 0).

Other tables to check for the same problem: Median uses cltdofunc 98-99 (D2R base max 97) and cltstfunc 82
(D2R base max 55).

## Mana costs (mxl-core 0.29.0, `mana_costs.cpp`)

D2Sigma's cost (0x100b0af0, D2Resource.cpp 0x100a1dc0) is `((level - 1) * lvlmana + mana) << manashift`; mana and
lvlmana are skills2 formulas (skills2 +0x36/+0x3a) and lvlmana is multiplied by `(100 + stat 228) / 100`. txt_migrate
`skill_costs` writes the 2,053 constant values into skills.txt mana/lvlmana and the 17 other formulas into calc7
(lvlmana) / calc8 (mana), unused by Median. D2R's cost function `0x33aa00` (dataFlag, skillId, level; minmana << 8 as
the floor) has no unit, so it is hooked through a stub that passes the caller's callee-saved register as a fourth
argument: RDI at the server charge site (call 0x4369fb inside `0x436830`, whose entry is already hooked by the loader)
and RBX at the client charge site (`0x2188b0`, call 0x218937), picked by return address (0x436a00 / 0x21893c). Other
callers (`0x338110` / `0x33d480` tooltips and checks) keep D2R's value. Skills record: minmana +0x226, manashift
+0x228, mana +0x22a, lvlmana +0x22c, calc7 +0x1a8, calc8 +0x1ac.

## Skill restrictions (`skill_restrict.cpp`, off word `restrict`)

Median's skills.txt has a `restrict` column (0-7) with `State1`-`State3`; D2Sigma's usability check
(D2SkillValidation.cpp `FUN_100b0c30`) knows modes 0-7, D2R's `0x33f360` (skill usable: 0 usable, 1 mana or cooldown, 2
requirements, 3 not a skill, 4 restricted, 5 no weapon, 7 no level, 8 charges) only 0-2. The plugin detours `0x33f360`
(entry `40 56 57 48 83 ec 28 48 8b fa 48 8b f1 48 85 d2`, 16 bytes), calls the original, and for a usable skill whose
record (+0x2d0 mode byte, +0x2d2/+0x2d4/+0x2d6 states) has mode 3-7 applies the rule (`skill_restrict_logic.h`, `Allowed`),
returning 4 when it fails. The migrated record carries the column; the mode 7 rule needs two state ids that
`txt_migrate.py` writes into `mxl_ids.h` (`StateMeleeDevotion`, `StateProgressiveCold`). Hunter Heartseeker is mode 5 with
the states `sk1296_state` (Steady Aim) / `progressive_cold`: "Requires Steady Aim". Checked by `game_checks steadyaim`.

## Steady Aim (Hunter, audit SK10): data plus the restrict module

Steady Aim is stat 220 layer 5 on the state `sk1296_state` (D2R 558), held by the layered self buff (`auras.cpp`, slot
174). **Build:** each Steady Shot (D2R 1368) adds one charge, `min(mxf_4 + clc3, clc2)` with a cap of 6, and turns the proc
state on at the cap. **Spend:** Heartseeker (D2R 352) removes one charge on the missile hit (missile hit 71, `HitOwnerState`
slot 63; the state goes with the last charge), Cascade (351) removes them all. **Gate:** Heartseeker and Cascade are
unusable without the state (restrict mode 5, above). The charge counter and the missile spend already worked from the data
once `tools/formula.py` turned missile `stat(x, n>2)` into the `mxl_statlayer` proxy and `txt_migrate.py formula_proxies`
looked the `ParamN` columns up by name; only the gate was missing.

## Heat (Fire Sorceress, audit SK11): `heat.cpp`, off word `heat`

Heat is the mana bar. The Molten Core passive (D2R 1682) sets stat 297 to the heat loss rate, `100 + level / 3 - mana
regeneration / 10`; D2Sigma's mana regeneration (D2Resource.cpp `0x100a1b50`) turns the regeneration into a drain of that
rate whenever stat 297 is not 0 (no drain in a town); the fire skills cost 0 mana in the data; Emberstep's aura and the
hit states of the fire skills put a **negative** 297 on the caster for a while, which makes the drain a build-up. D2R's
regeneration is `0x42e350` (game, unit; entry `48 89 5c 24 10 48 89 6c 24 18 56 57 41 57 48 83 ec 20`, 18 bytes): the
plugin detours it. With stat 297 zero it calls the original; in a town (`0x34b440` room, `0x2f0750`) it does nothing;
otherwise `heat_logic.h` `HeatDelta` takes the regeneration D2R would give (max mana / the class's regeneration frames
x (stat 27 + 140)%, plus stat 26), scales it by -stat 297% and adds the result to mana (stat 8), clamped to 0 and max
mana. Not ported: the UI relabel of the mana bar as Heat. Checked by `game_checks heat` (drain, floor at 0, Flamefront
build-up, town hold). Emberstep was not cast in the test (the harness cannot select it).

## Trophy timer (`trophy_timer.cpp`, off word `trophy`)

The 20 trophy recipes (class-split cubemain rows 6369-6501, rows 9, 10, 64-75) check player stat 202 (D2R 378) against a
per-trophy challenge code (op 6, value e.g. 167166 = 167 x 1000 + 166 for Azmodan's Heart, 176000 for the Book of Lies).
In the data the code comes from 15 aura-style skills (`srvdofunc` 30; `aurastat` 202 with calc `area` or `area*1000`, an
`auralencalc` of 250 frames = 10 s for the kill states, 750, 1500 and 15000 for the longer ones). So **the timer is the
state duration** and D2R runs it natively; but Median's second rule (patch notes P2.10.0: dying or entering another zone
voids the challenge) is in no table, and which skill is cast on a boss kill is not in the data either (only the chimaera
traps 2-4, judgementwander and `skills_556` through missile 5087 are referenced; the other ten are not named by any
monster, missile or state, so D2Sigma's server code, not txt, fires them).

The plugin therefore adds the window itself (`trophy_timer_logic.h`, `Tracker`): the game-frame hook (`goblins.cpp`
`FrameHook`) calls `TrophyTimerFrame`, which every 5 frames reads each player's stat 378, level and mode. A window opens
when the stat takes a value (a changed total, e.g. the second Tran Athulua buff, opens a new one), is **voided** when the
player dies (mode 0 / 17) or the area changes (the player gets a red chat line), and closes when the stat goes to 0.
`cube_conditions.cpp` op 6 on stat 378 also asks `TrophyWindowOpen`: false when the window is voided or older than the
trophy's limit (`trophy::Limits`: 10 s for most trophies, 15 s Legacy of Blood, 25 s Vizjun, 60 s Judgement Day, 4 min
Nephalem's Sacrifice, 5 min 30 s Yshari; counted from the code appearing, never stricter than the trophy text; codes not in
the table have no plugin limit). A player the tracker has not seen with that code passes. The `mxl` line is `trophy
timer on: ...` with the counts of windows opened, voided and cube checks refused. Checked by `game_checks trophytimer`.

## Item spells (misc.txt `pSpell`)

D2Sigma replaced pSpell 12-22 with its own handlers: table 0x101d95b0 of {client start, use} pairs, dispatcher
0x1007f140 (D2ItemSpell.cpp). Two-step spells (dyes 14, mystic orbs 20) first run 0x1007e840, which puts the item on
the cursor; the click on another item then runs the use handler with (game, player, item, target).

| pSpell | Handler | Items | Does | Port |
|---|---|---|---|---|
| 12 | 0x1007fa10 | home_cherry | calc1/calc2 -> warp | refused |
| 13 | 0x1007f910 | 58: arcane shards/crystal, catalysts, oils | shows the item's help text: packet 0x27 (unit type 4, item id, string spelldescstr); not used up. D2R has no packet 0x27 | text in the tooltip (data), right-click refused |
| 14 | 0x1007eff0 | 22 mystic dyes | target weapon/armour: stat 183 = calc1 + 1 (1-21) | plugin |
| 15 | 0x1007ebb0 | Perfect Ruby, askari_call | | refused |
| 16 | 0x1007f720 | 40 keys, riftstones, summoning scrolls | portal to level calc1; misc2 +0x26 = difficulty + 1 (0 any); calc2 0: red portal (0x10097870: object 60 or the level's own, plus the return portal), else one-way portal object 586 (0x100976d0); once per level and game (DRLG level +0x230 bit 4); calc3 non-zero: the use "fails", so the item is kept | plugin (0.13.0) |
| 17 | 0x1007faf0 | cherry | | refused |
| 18 | 0x1007f300 -> 0x1006f240 | 103 containers (gem/rune/shrine clusters) | takes the items it holds (misc.bin +0x8c, ultracode) from inventory, cube (in town or with a cube) and stash (in town), adding calc1 to stat1 each up to len; the absorbed items are destroyed, the container kept | plugin (0.14.0) |
| 19 | 0x1007f8d0 | toa ("Not used") | | refused |
| 20 | 0x1007f4b0 | 101 mystic orbs | below | plugin |
| 21 | 0x1007eef0 | catalyst_of_transference | as 13 | as 13 |
| 22 | 0x1007e900 | 12 potions | timed potion | D2R's 3 (data) |

`txt_migrate.py` (item_spells) moves 12-21 to pSpell 16 (used up) or 17 (kept: 13, 18, 21, keys with calc3), empty
but usable slots of D2R's table, gives 14/20 D2R's
Identify cursor (spellicon 0) and lists the items by code in `plugin/src/mxl_items.h`. mxl-core hooks D2R's two
dispatchers (0x5817f0 check, 0x581680 use) and handles pSpell-16 items; the ones not implemented are refused, so
they are not used up.

**Mystic orbs** (0x1007f4b0). Row = calc1 - 1 in D2Sigma's mysticorbs table (143 bytes; `work/sigma/mysticorbs.txt`):

| Offset | Field |
|---|---|
| +0x00 | name (32) |
| +0x20 | int8 level requirement added to the item |
| +0x21 | u16 description string |
| +0x23, +0x25 | u16 item types the orb fits (`armo`, `weap`, `myor`, `riam`, `helm`, `shie`, `tors`) |
| +0x27, +0x29 | u16 excluded item types (`nomo`) |
| +0x2b | int16 maximum of this orb per item |
| +0x2d | 6 properties {int prop, param, min, max} (prop -1: none) |
| +0x8d, +0x8e | u8 quality for the included / excluded types (0 any; else D2Common 10106 + 1, 7 = rare) |

Check (0x1008f840, then the handler): an excluded type matches -> allowed only if its quality is set and differs;
else an included type must match (with its quality, if set); the item holds fewer than the maximum (stat 290, layer
= row); the item's level requirement (D2Common 11015) + the orb's <= the player's level. Apply: on the item's stat
list with flags 0x40 (created if missing) add item_levelreq, apply the six properties (D2Common 10665) stat 289 times
(at least once), add 1 to stat 290 at the row's layer; the orb is used up.

**Keys** (pSpell 16). calc1 gives a Median level number (`(stat(...)>0)?0:215`), translated by `LevelIds` (Median
levels from 1 on moved by +1). mxl-core opens the portal like D2R's cube "Red Portal" output (`0x5da800`): free spot
`0x517f90` (game, room, xy, 3, 0x400, &room, 5, 100), portal routine `0x432ce0` with object 60 (or one_way_portal,
600) and last argument 1 (allowed outside town too), `0x34e9d0` (portal, level byte); the routine also places the
return portal (`0x435dd0`). Limits from D2R: the destination must be in the player's current act (`0x3e2b5c8`
(dataFlag, level) -> act; checked first so the key is kept). Destinations are 16 bits in D2R (object data +0x08 low, +0x78 high, read by `0x34ad40`); the byte setter clears the
high one, which mxl-core then writes (0.19.0; the 12 summoning scrolls to levels 263-280 open). "Once per game" uses D2R's per-game counter map
(`0x5d88b0` on game+0x1aac8, the cube Red Portal's), key 0x6d780000 + level.

## Still to specify

- Skill calc codes D2Sigma adds (`D2SkillCalc.cpp`; codes 73-110 go through `0x100a6340(unit, code, skill, level)`,
  jump table `0x100a67b0`; Median's skillcalc rows 73+ differ from D2R's, so these names need D2R rows + an evaluator
  hook). **Skill caps done in data:** `mlvl` (87) = skills2 u16 at +0x2f; `skcn` (110) = the skills2code formula at
  skills2 +0x3e (Specialization's clc1 raising the tab's caps, `*-999`/`-666` locks between skills), evaluated via
  `0x100a7620`; both written into each skill's skpoints by txt_migrate.py (work/formulas/skill_caps.txt). `skcp` (89) =
  the unit's stat 449 with the skill id as its parameter (items' "+N to maximum level of <skill>").
  **Calc codes in the plugin (mxl-core 0.8.0, `calc_codes.cpp`):** txt_migrate.py rebuilds skillcalc.txt in both banks
  as D2R's rows, placeholders up to 127, then Median's 37 missing codes (rows 127-163, `plugin/src/mxl_ids.h`), so
  every formula compiles. The hook on D2R's code switch `0x33b3b0(dataFlag, unit, code, skill, level)` (unknown rows
  -> 0) answers `syn1`-`syn6` (cases 74-77/104-105: the unit's stats 383/393/396/397/399/400 with the skill as
  parameter, D2Common 10973; D2R `0x2f5020(unit, stat, layer)`), `skcp` (stat 449), `aura`/`pass` (1 if the unit has
  the skill's aurastate +0xa0 / passivestate +0xb4; D2R `0x3351b0`) and, in 0.9.0, `bl12`-`bl78`/`blz1`-`blz7`
  (base-level linear on Param pairs, `0x100a5c40`: blvl>0 ? a+(blvl-1)*b : (z ? a : 0); blvl from D2R's own code 41),
  `bd12`-`bd78`/`bdz1`-`bdz7` (base-level diminishing, `0x100a5be0`: a + (110*blvl/(blvl+6))*(b-a)/100), `wdm`
  (SrcDam*100/128; D2R SrcDam +0x24d, Param1 +0x1b8) and `area` (room -> level id; D2R `0x2efc10(0x34b440(unit))`;
  Median's level ids are unchanged by the migration) and `skcd` (case 73, `0x100b0ed0`: the delay formula, D2R's
  compiled localdelay at record +0x238, minus the unit's stat 309 for the skill), `bmax` (case 88, `0x100a6e00`: max(0, min(mlvl + skcn + skcp, clvl + 1 -
  reqlevel)); mlvl/skcn are stored by txt_migrate.py in calc9/calc10, D2R-only columns at +0x1b0/+0x1b4; reqlevel
  +0x214) and `maxe` (case 92: base level > 0 and equal to bmax). Still 0 (rare): `mode` (1 skill; 1.13c unit+0x10),
  `pdmn`/`pdmx` (2 skills; `0x100a6260`/`0x100a6180`, skills2-based damage), `pdmn`, `pdmx`, `mode` (unit+0x10 in 1.13c)
  (cases in `0x100a6340`, jump table `0x100a67b0`). Open as before:
  `wdm`, `skcd`, `aura`, `bl12..`, `area`, ... (docs/validation.md lists counts).
- Missile calc codes (mxl-core 0.10.0): Median's misscalc adds `msyn` (43) and `blvl` (44) after D2R's 42
  (`0x10088fd0`; misscalc.txt rows 47/48 in both banks). `msyn` = the owner's stat 382 with the missile's skill
  (1.13c missile +0x194; D2R `Skill` +0x1b6) as parameter; `blvl` = that skill's base level on the owner, capped by
  `0x100bdff0` (1 for units other than players/hirelings; the plugin caps every non-player at 1). Hooked on D2R's
  missile code switch `0x3b9dc0(dataFlag, missile, owner, code, missileId, level)` (missile formulas: `0x3b4a90`,
  callback `0x3b5540`).
- Formula functions (mxl-core 0.11.0): each distinct call of D2Sigma's hasstate/statlayer/itemstat/tabpoints (all
  arguments are constants) becomes a hidden proxy skill `mxf_N` (57; Param1 = function, Param2..4 = migrated
  arguments; work/formulas/proxies.txt) and the call `skill('mxf_N'.mxfn)`: D2R's skill() evaluates calc code
  `mxfn` (skillcalc row 164) for that skill id, and the plugin answers hasstate (`0x3351b0`) and statlayer (mode
  0/1/2 = D2R `0x2f5020`/`0x2f48c0`/`0x2f5c60`, as D2R's stat()). **0.31.0:** itemstat (D2Sigma table `0x101df3a8`
  entry 13, `0x100a68c0`: the stat on the item in body location 1-12, D2Common 11139; D2R `0x3886d0(inventory,
  bodyloc)` with the inventory from `0x34a360`; 6 calls in hidden passives skills_2062/2063/2066/2069/3001) and
  tabpoints (entry 10, `0x100a6a70` -> D2Skills.cpp `0x100aefe0`: sum of base levels, D2Common 10306 with no bonus,
  of the class skills, D2Common 10044, whose skilldesc SkillPage is the tab; skills_1473) - the class/page lists are
  generated into plugin/src/mxl_tabs.h by txt_migrate tab_tables, base levels from D2R's calc code 41. Earlier notes: `hasstate`, `tabpoints`, `statlayer`, `itemstat`, items `skilllevel`, missiles `stat`
  (docs/formulas.md).
- Skill tree rules: **in data, nothing for the plugin.** `ui/SkillTree.cpp` is only layout (panels, arrows, sockets).
  The rules live in each skill's cap formula (skcn, written into skpoints): devotions lock each other
  (`skill('<other devotion>'.blvl)*-666` / `-500`), and each mastery is locked once the hidden counter
  `skills_1091` (one per class: 1180, 1273, 1359, 1457, 1543, 1619; aurastatcalc1 `clc1+clc2+clc3`) passes 2 unless
  it already has points, and until its unlock item stat (skills_1835/1836 passives) is present.
- **Cube conditions (D2Craft.cpp; confirmed 2026-10-20, ported in mxl-core 0.37.0, `cube_conditions.cpp`):**
  - **The checker** `0x10059d00` (thiscall: ecx = game, edx = the player; stack: op, param, value, layer) decides one
    condition for a player. Ops 1-14 are D2R's; 29-42 are Median's. All stat reads go through D2Common 10973 (total)
    and `0x100bd7b0` (base). "Value" below is cubemain's `value` (for stat compares `value >> ValShift`, the stat's
    ItemStatCost record +0x18).
    - 1/2 date and weekday (D2R has them); 3-14 player stat compares: 3 `>=`, 4 `<=`, 5 `!=`, 6 `==` on the total,
      7-10 the same on the base value, 11-14 the same on total minus base (layer & 0xffff). The stat is `param`, its
      layer is the fourth value.
    - **29** current month (1-12) == param (unused by Median's data).
    - **30 / 31** the player's level of skill `param` >= value; 30 with item bonuses, 31 without (D2Common 10306).
    - **32 / 33** the player has / does not have state `param` (D2Common 10494).
    - **34** the player's area == param. **35** the area's act (levels.txt +3) == param.
    - **36** the area is a town (levels.txt record +0x14b is nonzero for nine areas: the five act towns and four Median towns); Wirt's leg plus
      Tome of Town Portal only opens the Cow Level portal in town.
    - **37** random: passes value% of the time (clamped 0-100; rolled again at each check).
    - **38 / 39** player craft flag `param` (0-255) is set / clear. **40 / 41** the same for the game-wide flags.
      The flags live in a structure at game +0x1b30 (created by `0x594e0` at game start, freed by `0x59bf0`): the game, a
      32-byte bit array for the game-wide flags, and a name-keyed map (key = the player's name) to one 32-byte bit array
      per character. The checker tests them with `0x5abd0` (character) and `0x5ab60` (game). **They are never saved**:
      nothing else reads or writes the structure, so a flag lives for one game only (leave the game and it is gone).
    - **Who sets them (found 2026-10-21): the recipes' output slots.** Each of a cubemain record's three output slots
      (record +0x4c, +0xa0, +0xf4; 0x54 bytes each) has a word at +6 (the parameter) and a **special-output byte at +8**.
      Vanilla uses 1-3 (Cow / Pandemonium portals; the parameter is the level) and D2Game's transmute loop (1.13c
      D2Game `0x72260..0x72439`) dispatches the byte through a small function-pointer table, calling (ecx = game,
      edx = player, stack = the output slot). Median's loader patch list (records in D2Sigma's .rdata at `0x1712f0`:
      D2Game address, new value) edits that code: bound 4 -> 11 (D2Game `0x72418`), table -> D2Sigma `0x101d90b0`
      (`0x72421`), and a jump (`0x72429`) to the trampoline `0x1009b850`. Table index = the byte:

      | byte | handler | effect (parameter = slot word at +6) | recipes |
      |---|---|---|---|
      | 1 | `0x58d20` | portal to level `param` (Cow Level 39; also 163, 192, 214, 242) | 5 (rows 2, 3, 85, 4962, 7453) |
      | 2-5 | `0x58e60`, `0x58eb0`, `0x58f00`, `0x59030` | other portal / level effects, not identified | 0 |
      | 6 | `0x59120` | **pay `param` x 100 gold**: from carried gold (stat 14), the rest from the bank (stat 15); `cubemainext`'s cost is only the check in the search | 65 (all gold-cost recipes) |
      | 7 | `0x591b0` | **set player flag `param`** | 135 |
      | 8 | `0x59260` | clear player flag `param` | 0 |
      | 9 | `0x59300` | **set game flag `param`** | 56 |
      | 10 | `0x59390` | clear game flag `param` | 0 |

      The byte is not in the decoded cubemain.txt (its output cell keeps only the item code); it is in
      `extract/mxl/data/global/excel/cubemain.bin` (slot byte +8, word +6), which is how the rows below were found.
      A flag number is 0-255.
    - **Median's flag uses.** Every setter recipe also checks the same flag first, so a flag is a once-per-game guard;
      no item use, kill, quest or area event sets one.
      - Player flags 1-19: 133 class-split "upgrade a quest item" recipes (item e.g. `RathmasSupremacy2`; one recipe per
        flag and class: rows 6369-6387 assassin, 6388-6406 paladin, 6407-6425 druid, 6426-6444 necromancer, 6445-6463
        sorceress, 6464-6482 barbarian, 6483-6501 amazon; main op 6 = player stat 202 equals a per-item code, ext op 16
        = item stat 450; output `useitem` plus the class's property bundle) set flag N. Flag N then opens op-38 recipe
        `6350 + N - 1` (same input item, `useitem`, item level 100, no mods). **Rows 6350-6368 are the only readers of
        flags 1-19, so they stay locked until a setter runs.**
      - Player flags 99 / 100: rows 7383 (`UniqueDemonsbane`) / 6317 (`ZakarumsEar`): op 39 (flag clear) and set the same
        flag, so once per character per game.
      - Game flags (cubemainext op 41 = flag clear, output byte 9 sets it): 50-61 and 70-77 (the dungeon-key crafts, rows
        7100-7163 and 7340-7351: `UpgradedBossKillerRing` / `erun` + `f700` -> `useitem` / `du01`..`du21`), 101-105 (rows
        6324, 5656, 5667, 5670) and 102 (rows 4749-4755, `ElderVial` per class, min difficulty 2). Each key or reward
        is craftable **once per game**, not per character. These are anti-repeat guards; they do not open areas
        (the keys and the dungeon lockout do), so the flags do not block the area progression, only the repeat crafts.
      - Row 2 (Wirt's leg -> Cow portal) checks *player* flag 69 clear (ext op 39) but sets *game* flag 69; nothing
        reads game flag 69 and nothing sets player flag 69, so the check always passes (a Median quirk, no effect).
    - **42** strength + energy (stats 0 and 1) > 9000.
  - **Median's data** (cubemain op column): 38 x19 (flags 1-19 gate the 19 "useitem" quest-item recipes), 37 x9 (the
    chance recipes), 33 x6 (not state 893), 34 x2 (area 39, 241), 39 x2, 32 x1, 36 x1, 42 x1.
  - **The search** (`0x1005a9f5` area, per cubemain record of 0x148 bytes with its cubemainext record of 24 bytes): the
    record's player condition passes, and if the ext op is nonzero the ext condition passes too, and carried plus
    banked gold (stats 14 + 15) >= ext +0x00 x 100. Then each input item must pass the record's item condition **and**
    the ext one (`0x1005a400` returns true for "item fails": ops 15-18 compare the item's total stat to value: `<`, `>`,
    `==`, `!=` reject, so the item needs `>=`, `<=`, `!=`, `==`; 19-22 the same on the base value, 23-26 on total minus
    base, 27 compares a per-item property, D2Common 10620, with the param).
  - **cubemainext record** (24 bytes, one per cubemain row, same index; work/sigma/cubemainext.txt): `+0x00` gold cost in
    units of 100 gold (65 recipes, 50-10000); `+0x04` op, `+0x08` param, `+0x0c` value of the second condition (ops 3
    x174, 6 x20, 30 x18, 39 x1, 41 x55 player conditions; 15-18 item conditions on 1,212 recipes); `+0x10` the stat
    layer of that second condition; `+0x14` the stat layer of cubemain's own condition. Layers are Median's numbers
    (class/skill/state ids; not migrated).
  - **D2R side** (found 2026-10-14): cubemain records are **0x16c** bytes at data tables +0x1570 (`0x3e3f60(?, index)`
    returns record `index`), +0 enabled, +1 min diff, +2 class (-1 any), +3 op (byte), +4 param, +8 value, +0xc
    numinputs, +0xe version, +0x10/+0x12 ladder seasons. **Recipe search** `0x529090(int *result, game, player, out)`
    (D2Game PlrTrade.cpp): counts the cube items, walks every record (enabled, version, ladder, difficulty, class,
    numinputs), then a player condition in a switch on op **1-14 only** (any other op counts as passed), then
    `0x529d50(game, inventory, record, input#)` for inputs 0..6 (all seven nonzero = found; result = record index).
    `0x529d50` matches one input and checks the item condition ops on each item. D2R has no layer in its record.
  - **Port (0.37.0).** `tools/cube_tables.py` (build.py step after txt_migrate) reads cubemain and cubemainext and
    writes `plugin/src/mxl_cube.h`: 542 recipes with plugin conditions or a cost (main 29-42; ext player ops; main
    stat compares 3-14 whose record slot an ext item condition takes; costs), params migrated through idmap (skills,
    states, areas, stats), stat-compare values pre-shifted. It also (1) **moves an ext item condition (ops 15-18) into
    the cubemain record's op/param/value when the record's own slot is free or holds a player op** (176 recipes), so
    D2R's matcher checks it, and (2) **remaps the record's stat ids** (ops 3-26: Median row numbers were left as they
    were, but D2R's stats from 359 on shifted by +29: 2,197 recipes pointed at the wrong stat). Not done: **1,036
    recipes with an item condition in both slots** (main 15-18 and ext 15-18) keep only the main one; D2R's record has
    one slot, applying both needs the per-item check inside `0x529d50`.
    Plugin: `SearchHook` remembers game and player, `MatchHook` answers 0 for input 0 when `Passes()` fails (record
    index = (record - `0x3e3f60(dataFlag, 0)`) / 0x16c, verified against the getter, else the hook logs once and passes
    through). Skill level: 31 uses D2R's base-level calc code 41; 30 (0.38.0) adds the +skills: it walks the skill list (`0x34b6e0(unit)` -> first node; node +0 -> record, word id; +8 next; +0x40 level; +0x4c, +0x54) the way calc code blvl does and adds `0x3400a0(unit, node, 0)` (the bonus function the skill-level callers `0x337a70` / `0x338fd8` use when node +0x4c is -1; D2RLoader has a `jmp [rip+x]` at its entry, calling the address is fine). Craft flags are set and the gold is paid by the recipe-executor hook
    (below, `craft_flags.cpp`). **Hooks on since 0.38.0**, entry bytes verified by disassembling a memory dump of the running image (build 93847, timestamp 0x6ab3782c):
    - `0x529090` search, (rcx = int* result {index at +0, found byte at +4}, rdx = game, r8 = player, r9 = out); 18 bytes `40 55 53 56 57 41 54 41 55 41 56 41 57 48 8d 6c 24 88` (push rbp/rbx/rsi/rdi/r12-r15, `lea rbp,[rsp-0x78]`). It loops the records with `0x3e3f60(game+0x106, i)`, tests the player condition by a jump table (ops 0-13, stat reads `0x2f5020` total / `0x2f48c0` base), then runs `0x529d50` for inputs 0-6.
    - `0x529d50` input match, (rcx = game, rdx = player inventory, r8 = record, r9d = input#, **plus two stack arguments** (out, scratch): the hook forwards all six); 17 bytes `44 89 4c 24 20 55 53 56 41 55 48 8b ec 48 83 ec 68` (`mov [rsp+0x20],r9d` / push rbp, rbx, rsi, r13 / `mov rbp,rsp` / `sub rsp,0x68`). Returns eax non-zero on match; input record at record + 0x14 + 10 * input.
    - `0x3e3f60(ecx = byte game+0x106, edx = index)` returns table base + index * 0x16c (bounds-checked), as assumed.
    `mxl-core-off.txt` word `cube`.
  - **Output effects: implemented (craft_flags.cpp, option 2, the post-hook).** `tools/cube_effects.py` reads
    `cubemain.bin` and writes `plugin/src/mxl_cube_effects.h` (256 effects, sorted by row: 65 x byte 6, 135 x byte 7,
    56 x byte 9; bytes 8 and 10 are accepted but absent). The plugin detours the recipe executor `0x5269c0` (entry
    bytes `40 55 53 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 08 ea ff ff`, 21 bytes, checked against the dump); after
    the original returns an output item (non-null) it finds the record's row (same formula as the match hook) and runs
    the row's effects in slot order: byte 6 pays `param x 100` gold (`craft::Pay`: carried gold, stat 14, first; the rest
    from the bank, stat 15; set with `0x2f7d10`, nothing goes below 0), 7 / 9 set a player / game flag. Flags are
    memory only (`craft_state.h`): game flags and player flags are cleared when the game pointer seen by the search or
    the executor changes (no game-destroy hook; a new game that reuses the same address would keep the old flags, which
    only means a repeat guard stays closed), and player flags are keyed by the player's unit id (`0x34ab60`), not
    Median's name key, so a player who leaves and rejoins the same game gets a new id and fresh flags. D2R's own bytes
    1-4 (portals) are untouched. The gold change is made with the stat setter and no extra packet: if the client does
    not show the new gold until something else updates it, a stat-update send is needed (to verify in game). Switch:
    `mxl-core-off.txt` word `craftflags` (flags then never set: the 19 op-38 recipes stay locked, gold is not paid).
  - **Original port spec (kept for reference).**
    - *Storage.* Nothing to save: Median keeps the flags in game memory only (see above), so no itemstatcost stat or
      save field is needed (and none would match Median: the flags reset when the game ends). The plugin's maps
      (`g_playerFlags`, `g_gameFlags`) are right in kind; change them to be keyed per game (a game pointer, cleared when the
      game is created or freed: hook the game-create / game-free path, address to find, or reset when `SearchHook` sees a
      new game pointer) and per player **name** (as Median does; the unit pointer changes when a player re-enters), and
      each flag set 0-255.
    - *Data.* D2R's cubemain.txt cannot express bytes 6-10: its parser only knows the portal outputs, and the migrated
      output cell has no place for Median's byte. So the setters need data outside the txt: extend `tools/cube_tables.py`
      to read `extract/mxl/data/global/excel/cubemain.bin` (record 0x148 bytes after a 4-byte count; slots at +0x4c,
      +0xa0, +0xf4; byte +8, word +6) and emit, into `mxl_cube.h`, a list `{recipe row, kind (6 pay / 7 set player / 9 set
      game), parameter}` (256 entries: 65 + 135 + 56; kinds 8 and 10 are unused but cost nothing to accept; the
      5 portal outputs stay D2R's, via the txt). Row numbers are the cubemain.bin row numbers, equal to the migrated cubemain.txt rows used by `Entries`.
    - *Hook (D2R, to find).* D2R's transmute runs the same loop as 1.13c's: per output slot it reads the slot byte at +8
      and, when it is 1-3 (4 for D2R's Red Portal), calls a handler out of a four-entry pointer table (the D2R record is
      0x16c bytes, 0x24 more than Median's 0x148, so the slot offsets are probably shifted too: read them from the loop).
      To find: in the D2R Ghidra project take the caller of the Red Portal opener `0x5da800` (the cube's portal output) or
      the cubemain record walker `0x529090` and look for the function that reads the record's three output slots
      (stride 0x54 or the D2R equivalent) and indexes a table by a slot byte; the signature is likely (game, player,
      slot). Two ways to use it:
      1. *Like Median:* patch the bound and the table (copy the original entries, append the plugin's handlers at indexes
         6-10) and set the slot byte of the recipes in the table above when the plugin loads (write through
         `0x3e3f60(dataFlag, row)` + slot offset). The handlers then run in D2R's own flow, in slot order, after the
         recipe has been accepted. Needs: the dispatcher's bound and table-pointer patch sites, the slot layout.
      2. *Post-hook:* hook the transmute entry (the function that calls the search and then consumes the inputs; the
         search result is the record index that `SearchHook` already receives) and, after it returns success, run the
         recipe's list from the generated table. Needs only that function's entry bytes. Simpler; the effects then run
         once per transmute, as Median does (it runs them per slot, in the same call).
      Handlers: set / clear a bit (`SetCraftFlag(playerName-key, flag, game, on)`), and pay gold: take `param * 100` from
      stat 14 and the rest from stat 15 (D2R stat getters/setters from `cube_conditions.cpp`'s `GetStatRva` family and the
      add-stat call used elsewhere in the plugin; carried gold first, as Median does) which also closes the "gold cost is
      checked, not paid" gap for the 65 gold recipes.
    - *Effort and risk.* S once the transmute function is found (data emitter about 40 lines, handlers about 40). Without it
      the 19 op-38 recipes (6350-6368) stay locked and the once-per-game guards (ext op 41 recipes) never close, so those
      recipes can be crafted repeatedly (benign); until gold is paid the 65 gold recipes are free. Test: craft 6369-style
      item upgrade, then 6350+ must appear; craft a dungeon key twice in one game (second must be refused), leave and
      re-enter the game (allowed again).
- Skill descriptions (`D2SkillDesc.cpp`).
- Engine limits: states (below), level types (below).

## Level types

Median has 52 level types (36-51 its own, used by 44 areas). D2R loads LvlTypes.txt into a vector (`0x32db30`), so
the tile sets load. Only AutoMap.txt is fixed at 36: the loader `0x32cae0` matches each row's LevelName against 36
names (table `0x1cfe110`, 0x10 bytes each; unmatched rows assert "ii < NUM_LEVEL_TYPES" and become type 0) and keeps
each type's row range in a 36-entry array at `0x2a9a8e0` (8 bytes each, followed by another object at `0x2a9aa00`).
The automap tile lookup `0x32bdf0(levelType, tileType, style, sequence)` reads that array unchecked.
**mxl-core 0.7.0** hooks the lookup: types >= 36 answer -1 ("no automap tile"), so Median's areas have no automap
tiles instead of reading past the array. **mxl-core 0.33.0:** no relocation needed. The loader files Median's rows
(LevelName `mxl_lvltype_<n>`, unmatched, so type 0 after a soft assert) but keeps all rows in file order in its vector
(`0x2a9a8c8` data, +8 count; 0x20 bytes: +0 type, +4 tile type, +8 style (0xff any), +9/+0xa start/end sequence
(start 0xff any), +0xc four cels, +0x1c cel count). txt_migrate `automap_tables` writes each Median type's row range
(975 rows, types 36-44 and 51; 45-50 have none) to plugin/src/mxl_automap.h, and the lookup hook searches them with
D2R's rules, picking among several cels with the automap seed (`0x2372a40`, x 0x6ac690c5 + carry). If the loaded
row count differs from the generated table (5086), types >= 36 keep answering -1.

## States limit (engine; in progress)

D2R: MAX_STATES 511, MAX_STATE_BLOCKS 16 (data load `0x3074d0` asserts `size <= 0x1ff` and `< 0x200`, then keeps
going with blocks = (count + 31) >> 5; Median: 1,014 states = 32 blocks). Data-side tables are sized from the live
count, but each unit's state bits live inline in its extended stat list (unit+0x88): two arrays of `blocks` dwords at
+0xaf0 (current states, then "changed" flags at +0xaf0 + blocks*4), with room for 16 blocks each (+0xaf0..+0xb70;
fields follow at +0xb70/+0xb78, see the copy routine `0x2f0d80`). With 32 blocks the "changed" array starts at +0xb70,
so **every state change by any unit writes into the following fields**: memory corruption, independent of which
state. Accessors: check `0x2f6220`, set/clear `0x2f7ea0` (3 accesses), array pointer `0x2f6370`, copy `0x2f0d80`
(32 dwords spelled out). Pools: plain stat lists `0x2a9a430` (element 0x90), `0x2a9a4d0` (element 0x2b00; used by
`0x2f65d0`/`0x2f9eb0`, to confirm as the extended list).

Allocation: pool `0x2a9a4d0` (element 0x2b00, 0x200 per chunk; static constructor `0x3eb60`) serves extended stat
lists only: `0x2f65d0` takes one and zero-fills 0x2b00 bytes, `0x2f9eb0` returns it when +0x1c < 0 (the "extended"
marker the state functions test). A chunk records its element size when created (`0x1230260`), so the size must
change before the first chunk exists.

**Fix (mxl-core 0.6.0, `states_fix.cpp`):** element 0x2c00 (constructor immediate, live pool field, creation memset);
both arrays move to the new tail, current states at +0x2b00 and "changed" flags at +0x2b00 + blocks*4 (room for 64
blocks in all); displacements patched in the check (`0x2f6220`), set/clear (`0x2f7ea0`, 3), array pointer
(`0x2f6370`), "changed" pointer (`0x2f62d0`, dword index 0x2bc -> 0xac0) (the copy routine `0x2f0d80` is NOT patched: its only caller `0x2f65d0` copies from a stack template
whose +0xaf0..+0xb70 it zeroes; redirecting the reads took 0x80 bytes of other stack data as states, bug #21;
the creation memset, grown to 0x2c00, zeroes the new tail). 8 sites (+1 data; 0.6.0-0.33.2 had 72), checked
byte for byte, one D2RLoader transaction, skipped if the pool already has chunks. Callers of the array pointers
(`0x336440`, `0x3356c0`, `0x336330`) size their loops from the live block count. Open: the client/server state
packets' state-id width (if narrower than 10 bits, states above it misbehave in multiplayer independently of this fix);
the startup assertions remain (harmless).

**Bug #22 (monsters cannot be hovered or attacked, 2026-10-07):** bisected to the states fix (plugin all off: works;
only states off: works). The state trace (0.34.0, below) showed only ordinary states (105 alignment on monsters and
NPCs as in D2R; 186 sharedstash, which D2R itself sets on the player at join, `0x52f9d5`; Median's 906 and 247 on the
player from skills) and no state-mask check (`0x336440`) answering yes, so no state blocks targeting. Byte scans found no
other user of the dword index 0x2bc and no other 0x2b00-sized allocation; the extended list has its own fields at
+0x2af0/+0x2af8 (creation args), below the new tail. Test in progress, 0.34.1 "keepcurrent": 32 blocks of current
states fit exactly in D2R's 16+16-dword area (+0xaf0..+0xb70), so the current array stays where every reader expects it
and only the "changed" flags move (+0x2b00 + blocks*4; sites 0x2f7fff and 0x2f634f, plus pool size and memset: 5
sites). If hovering works with it, it becomes the default (it also keeps the copy routine's zeroing of +0xaf0..+0xb70
correct); room is then exactly 1,024 states (Median: 1,014).

**Turned off by default (0.35.0, 2026-10-14):** D2RLoader 1.3.1 already supports more than 512 states. D2RCore
replaces D2R's state senders (0x53a210 changed, 0x539890 full, both ending in `jmp [rip+...]`) with its exports
`SendChangedWideStateStats` / `SendAllWideStateStats` (and `ReceiveWideStateStatPacket` on the client), which take
the block count from the live table and read the arrays at D2R's own offsets (`lea rax,[rcx+0xaf0]`, `lea rbx,
[rcx+rdx*4+0xaf0]`, D2RCore 0x3e5a92). Moving the arrays therefore stopped all state sync to the client (bug #22,
the missing Whirlwind overlay). With 32 blocks the "changed" flags cover +0xb70..+0xbf0; D2R's code only copies
+0xb70/+0xb78 from the creation template (zeroed) and nothing in the stat code reads +0xb70..+0xbf0, so leaving
the layout alone is safe. The fix stays available with the word `statesfix`.

## Whirlwind (mxl-core 0.36.0, `whirlwind.cpp`)

Median keeps 1.13c's Whirlwind (D2Sigma's srvdofunc table leaves 76 at D2Game's own; no D2Sigma code refers to the
skill). Compared with D2R's (do `0x568f40`, start `0x56a420`): same start, movement, one target per hit, two hits
with two weapons, calc1 as the damage %, HitFlags/HitClass, aurastate with aurastats. The difference is the hit
rate. D2R 2.4 waits `0x56ac00(unit, weapon)` frames between hits: the weapon's attack animation length at D2R's
speed, clamp(stat 68 + 120*IAS/(120+IAS), 15, 175). 1.13c (D2Game 0x6fce1470, D2Common 10564) takes the same
animation length at 100 + stat 68 + IAS (stat 93, no diminishing returns, no cap) and maps it through a table:
< 12 frames -> a hit every 4, < 15 -> 6, < 18 -> 8, < 20 -> 10, < 23 -> 12, < 26 -> 14, else 16 (no weapon: 10).
The hook on 0x56ac00 (only Whirlwind calls it) rescales D2R's length to 1.13c's speed and applies the table; it
logs stat 68 / IAS / both speeds for the first hits, to confirm whether D2R's stat 68 carries the base 100 that
1.13c adds itself (the code assumes so when stat 68 >= 100). Word `whirlwind` in mxl-core-off.txt turns it off.
Data side: anim/seqtrans SQ, plrmode SQ/KB, D2R's stop functions, the overlay Character column and the HD state
machine copies (txt-migrate.md, tools/hd_skill_anims.py).

## Compiled-in Sigma tables

Three D2Sigma tables have no D2R counterpart and no D2R address to patch, so `tools/sigma_data.py` (build.py step,
after cube_tables.py) compiles them into the plugin as constexpr data. Nothing calls them yet (no hook, no behaviour
change, no version bump); the hook lanes add code only. Sources are the raw `.bin` files in `extract/mxl` (the
sigma decode has no names for these fields), ids go through `work/named/idmap.json`: Median level and monster rows
become D2R ids (levels from 137 up are +1, monsters per the monstats map). Headers are written the way
`mxl_cube.h` is: `// Generated by ...; do not edit.`, `#pragma once`, a namespace, a plain struct and an
`inline constexpr` array with a count and a trailing `// name` comment per row.

### requirements (`mxl_requirements.h`, `requirements.cpp/.h`)

33 rows x 44 bytes (D2Requirements.cpp, docs/subsystems/quests-requirements.md). `mxl_requirements::Row`
`{type, a, b, c, child[10], code[5]}`: type 1 OR / 2 AND (children are row indexes, 0 = unused; `a b c` zero),
9 quest bit set (a quest id, b bit, c difficulty: -1 the game's, else 0/1/2), 30 player class == a, 35 player holds
no item with the 4-character `code` (stored negated, as D2Sigma returns it), 36 always false; row 0 type 0. Quest
ids, classes and item codes are the same in D2R (Median keeps the vanilla quest array and its item codes), so no
remapping. The generator warns if a row uses a type outside {0, 1, 2, 9, 30, 35, 36}.

`mxl_requirements::Evaluate(row, Queries)` is pure: `Queries` holds a `user` pointer and three callbacks
(`questBit(user, quest, bit, difficulty)`, `playerClass(user)`, `holdsItem(user, code)`). Row 0 and negative indexes
allow; an index past the table, an unsupported type, a missing callback or a cycle (depth > 16) deny. An empty AND
allows and an empty OR denies (unused slots are skipped; neither occurs in the data). The hooks (NPC shop / gamble /
hire gates and the 16 action slots) supply the callbacks.

Test: `plugin/tests/requirements_test.cpp`, target `mxl_tests` (EXCLUDE_FROM_ALL, not in the DLL):
`cmake --build build/plugin --config Release --target mxl_tests` then `build/plugin/Release/mxl_tests.exe`. It checks
the class rows, quest bits in the game and in a fixed difficulty, the no-item rows, Cain's AND/OR chain and Akara's row.

### Dungeon lockout and entry requirements (`mxl_dungeons.h`)

levels.bin rows are 0x220 bytes; the generator reads the extension +0x151..+0x15f (docs/subsystems/ubers-events.md,
section 3). `mxl_dungeons::Entry` `{level (D2R id), lockSeconds, lockGroup, minDifficulty, levelReq[3], classLock,
unk152, unk155, portalObject, unk15b, questLock}`, one entry per level where any of them is non-zero (162 levels; 141 without questLock).

**Used by** plugin/src/dungeon_lockout.cpp + dungeon_logic.h (lane dungeon-lockout): the clock starts when a player enters a level with a lock time
(stairs, tile or portal), seals it for the rest of the game when it runs out; entry also needs minDifficulty, levelReq[difficulty] and the class lock.
Status and what is left: docs/subsystems/ubers-events.md section 3a.

| Field | Offset | Meaning |
|---|---|---|
| lockSeconds | +0x153 u16 | seconds until the level seals after a portal into it is used (0 = no clock). +0x154, listed separately in the research doc, is its high byte (values up to 1200 need it) |
| lockGroup | +0x15f | levels with the same non-zero group lock together |
| minDifficulty | +0x151 | 0 Normal, 1 Nightmare, 2 Hell |
| levelReq[3] | +0x158..+0x15a | player level needed in Normal / Nightmare / Hell |
| classLock | +0x15e | 0 none, else only class (value - 1) may enter (Mirror Temple) |
| unk152, unk155, unk15b | +0x152, +0x155, +0x15b | raw, meaning not established (+0x155 is set on harder areas) |
| questLock | leveldefs.bin +4 of the 0x9c-byte row (QuestFlagEx) | quest id that must have bit 1, 0, 0xe or 0xf set in the player's record of the game's difficulty (0 = none); 23 levels. Refusal is the third verdict (`dungeon::Verdict::Quest`) |
| portalObject | +0x156 u16 | raw: 585 only on level 247 (rainbow portal); a Median objects row, not remapped |

### Treasure goblins (`mxl_goblins.h`)

`Chance {level (D2R id), chance, monLevel[3]}` for the 161 levels with GoblinChance (levels +0x15c u16, 4-16; rolled
against 1024 after a 10%-per-extra-player reduction; monLevel = levels +0x16 + 2 x difficulty, u16 x3), `Towns[]` (levels
with the town flag +0x14b) and `NoKill[]` (monstats3 +0x20 set: kills that do not stamp the player). Logic: plugin/src/goblins_logic.h. `Goblin {monster (D2R monstats id), flags, rarity,
minLevel, maxLevel, levelRestrict}` for the 21 monstats3 rows (44 bytes) whose flag word (+0) has bit 0x400 (goblin)
or 0x1000 (rainbow, Halloween only): rarity u16 at +0x17, min / max monster level u16 at +0x19 / +0x1b (0 = no bound),
level restriction u16 at +0x1d (0xffff -> -1 any, else the level row mapped to its D2R id). `flags` keeps the whole
word; its other bits are not understood. Rarity is the row's weight in the draw (confirmed, docs/subsystems/ubers-events.md section 4).

## Skill tree pages (`skill_tree.cpp`, off word `skilltree`)

D2R's `SkillsTreePanel` (D2Client SkillsTreePanel.cpp, one class for the HD and the classic panel) handles three pages
and an item-skills "tab 3"; Median has eight pages per class (nine for the Sorceress: Coven and Reward). Findings (dump RVAs; Ghidra `FUN_1414c4520` etc.):
- Build function `0x14c4520`: walks the pages 3, 2, 1 (`r13d`) and gives page p the widget `Tab<n>` with n counting up from 0
  (`0x14c5f60`), so Tab0 held page 3 and Tab2 page 1, the reverse of the `SkillPage` numbers. It creates a
  `SkillButton<page> <col>,<row>` for every class skill (the class skill list, `0x33cb30` count / `0x33dde0` id) whose
  skilldesc record (stride 300: +2 SkillPage, +3 row, +4 column, +6 IconCel) has that page. Pages above 3 never got buttons.
- Tab setter `0x14c3b10` (the `SkillsTreePanelMessage:ActivateTab:<n>` handler) clamps the index to 0..3 (tab 5-8 clicks
  all selected tab 4); the current tab is the global `0x3bbd8bc`; controller bumpers `0x14c6810` wrap 0..3; the tab styling
  loop `0x14c789b` covers tabs 0..3; seven `cmp [0x3bbd8bc], 3` mark the item-skills tab (no skill counter, no skill button
  draw hook, `ItemHeader`).
Patches (13 sites + 1 inline hook, byte-checked, one mutation transaction): clamp, controller wrap and styling loop 3/4 -> 8/9; the seven
compares test 127 (no item-skills tab); the build function starts at page 9 and counts the tab widget index down
(`push 3; pop r14` / `push 9; pop r13` / `mov eax, 8` in place of the loop set-up at `0x14c488a`, 38 bytes, and `inc eax` ->
`dec eax` at `0x14c531a`), so page p lives in `Tab<p-1>`, which is what `tools/ui_layouts.py` (Tab n, background frame n) and
`tools/skill_tree_art.py` (frame n = page n+1) always assumed. `mxl` prints `skill tree on: 13 sites + tab setter hook, 9 pages ...`.
Nine tabs for the Sorceress only: the layout has 9 `Tab<n>` widgets for every class (one JSON shared by all), the art draws the 9th cell
only for a class with 9 titles (blank otherwise). The tab setter `0x14c3b10` is hooked (entry bytes `48 89 5c 24 08 57 48 83 ec 30`): for
an index >= 8 it asks the local player's class (`0x8b2d0` local slot -> `0x9a480` player unit -> u32 at +4) for its tab count
(`mxl_tabs::ClassTabs`, from the highest skilldesc page of the class's skills, at least 8; 8 when the unit is unknown) and ignores
a mouse click on a tab the class lacks; from the controller bumper (return addresses `0x14c6bd3 / 0x14c6be6 / 0x14c6c07`) it wraps
instead (last -> first, first -> last). Checked in game 2026-10-22 (HD and G, Sorceress and Barbarian).
Data side (`tools/skill_icons.py`): a skilldesc row has one IconCel, but Median shares rows between classes (the Mastery
pages of all classes, many item skills): the row is now split per sheet ("row~class" copies, skills.txt repointed), so every
tree skill finds its own icon in its class sheet (bug #3b: empty sockets on masteries, wrong icons where a class-less
skill came first). Classes with more than 62 tree icons get smaller HD frames (Amazon 113 px, Barbarian 116 px).

## Cooldowns (`cooldowns.cpp`, off word `cooldowns`)

Median's skill cooldown: delay = skill delay formula - stat 309 (layer = skill id), started only for a player when the delay is above 0, and skills of one
skills2 cooldown group share one timer (D2Sigma `0x100b0f40` / `0x100b1140` / `0x100b0ab0`; details in d2r-addresses.md section 17). The plugin hooks D2R's two
cooldown starts (server `0x439500`, client `0x217b90`; entry bytes are verified before the hook is installed): for a player it subtracts stat 309 and starts the D2R timer
(state `0xb9`, stat 359 at layer = skill id) of every member of the skill's group with the same delay; other units keep D2R's behaviour. The group table is generated
(`mxl_cooldowns.h`, `txt_migrate cooldown_tables`); the pure rules and a model of Median's tables are in `cooldowns_logic.h` (`cooldown_tests`). `mxl` prints the line
`cooldowns` with the counters starts / reduced / shared / dropped (also in `h state`). Median's removal of monster cooldowns and the client packet `0x2b` are not ported.

## Runewords and kill experience (`items_ext.cpp`, off words `runewords`, `xp`)

Details, addresses and evidence in [subsystems/items-xp.md](subsystems/items-xp.md). Three hooks, each checked against
the entry bytes of build 93847:

| Hook | D2R | What |
|---|---|---|
| runeword properties | `0x3d44c0(item, owner)` | the seven properties of D2R's record, then up to six more from `mxl_runewords.h` (Median's runes.txt slots 8-13, `tools/items_data.py`) through the same call `0x3d4620(6, item, owner, 0, index, 0, &prop, 0xab, 0x40, 0)`; only for the row whose index and first rune code match the generated table |
| runeword matcher | `0x372260(item)` | the original first; if it finds nothing, Median's padded match: Jewels at the front of the rune list up to the socket count (never for rows made only of Jewels) |
| kill experience | `0x44fef0(plvl, mlvl, xp)` | Median's level-gap formula instead of 1.13c's tables |

## Stat columns (mxl-core 0.52.0, `stat_columns.cpp`, off word `statcols`)

Hooks the Advanced Stats panel's row value (`0x14eb1b0`) and row text (`0x14eb250`) functions: the Str / Dex damage bonus rows
(stats 299 / 300: total Str x StrBonus / 100, total Dex x DexBonus / 100 of the right-hand weapon) and the " (+N% bonus, M base)"
suffix of the attribute, life, mana and stamina rows. Groups come from the generated `mxl_statcols.h`
(tools/stat_screen.py `column_groups()`); the arithmetic is `stat_columns_logic.h` (unit tests `stat_columns_test.cpp`).
Details and the game check: [subsystems/stat-screen.md](subsystems/stat-screen.md) section 3b.

### Median's section order (`stat_columns.cpp`, `stat_order_logic.h`, generated `mxl_statorder.h`)

Replaces the Advanced Stats panel's list rebuild (`0x14eaf40`): all 103 entries of Median's two stat pages (11 section labels,
92 stats) go into the panel's group 2 in the order of the sheet; the row hooks above list a stat when `advdisplay` is 1 or its
value is not 0, and a section label while one of its stats is listed ("== <name> =="). Needs the entry bytes of `0x14eaf40`,
`0x14edd50` (push a record) and `0x14ead50` (add a row) to match, else D2R's order stays. Same off word `statcols`. Harness verb
`statorder [open|adv|close]` (client side). [subsystems/stat-screen.md](subsystems/stat-screen.md) section 3c.

## Help panels (`info_panels.cpp`, generated `mxl_infographics.h`, off word `infopanels`)

Median's `Infographic` items (misc2; the catalyst of disenchantment, `3o[`) open a help panel on a right click (item event 13,
packet 0x16). The plugin registers one PluginSDK panel per infographics row (layout JSON as a resource
`data/global/ui/layouts/mxl-core/<id>hd.json`, `PanelService::registerPanel`, `CloseOnEscape`; text from
`tools/infographics.py`: D2R's modal frame, the title, the text blocks in two columns in the picture's order, colour codes
dropped). The item's use handler (`item_spells.cpp`) accepts the right click for such an item and sends the plugin message
`I` (one byte: the panel index; `client_messages_logic.h`); the client handler queues `openPanel` on the UI thread
(`ThreadService`). `mxl` console: "help panels ... (asked, opened, failed)". Harness verb `infopanel [close <n>]` (client side).

## Quest reward sounds (`client_messages.cpp`)

The unit messages 0x5f-0x61 (`SendRewardMessage`, server `0x491960`) travel in D2R's unit packet 0x2c; the client hook of its
handler (`0x12c870`) plays Median's sounds at the player's unit (217 item_gem, 4715 shrine_boink with D2R's holy shrine sound
2608 as stand-in, 230 item_lightarmor); the chat line is the fallback when the hook is off. Harness verbs `rewardmsg`,
`rewardsnd`, `goblinsay` (the goblin line with the variant's own monstats name).

## Travel: waypoint auto-activation, map reveal, hotkeys (`travel.cpp`, off words `wpauto`, `mapreveal`, `hotkeys`)

Pure rules in `travel_logic.h` (unit tests `tests/travel_test.cpp`); evidence and addresses in `docs/d2r-addresses.md` section 18.
No inline hook: the plugin calls D2R functions whose first bytes it checks with `memcmp` (a mismatch switches that part off and
the `mxl` line says why).

* **Waypoint auto-activation** (`wpauto`). `TravelGameFrame` runs from the goblins frame hook (`goblins.cpp`, after
  `HarnessGameFrame`), every 5th server frame. For each player in the game it reads the level (`0x2efc10(0x34b440(player))`), the
  waypoint number (`mxl_waypoints.h`), and tests the record bit (`0x3d6930`). Unknown, and a waypoint object (the 16 classes of
  objects.txt OperateFn 23) of the same level within 36 sub-tiles (squared integer distance): `0x3d6890` sets the bit. Median does
  the same after a walk order (`D2AutoPickup.cpp`, distance below `0x24`). The `goblins` word must be on because it owns the hook.
* **Map reveal** (`mapreveal`). D2R's debug "Reveal Automap" `0xd42b0` on the UI thread, then the debug flag byte `0x2a2add4` is put back.
  Starts from the player's room (the rooms joined to it); in game the automap layer list grew from 1 to 25 layers. Median's `0x887c0` walks every
  level of the act once. Not ported: the once-per-game limit (the reveal can be repeated).
* **Hotkeys** (`hotkeys`). Three InputService actions (category "Median XL", ids `mxl_pickup_toggle` X, `mxl_map_reveal` L,
  `mxl_transmute` K, in the Controls menu, rebindable; none on D2R's own keys: R is its run toggle, C the Character screen, V another action). X flips the switch that gates the auto-activation; L queues the reveal on
  the UI thread; K queues the cube panel's Transmute `0x2cd720` on the UI thread when a UiMessageListener has seen the Horadric
  Cube panel open. Counters and the last UI messages are in the `mxl` command (`travel` lines) and the harness verb `travel`.
* **Stash pages** are not code: `tools/install_mod.py` writes `d2rcore.stash.add_shared_tabs = 7` into the mod's `d2rloader.toml`
  (3 normal tabs + 7 = Median's 10 pages), only when the key is not set by hand.

## Diagnosis switch and state trace

`<game>/mods/MedianXL/d2rloader/plugins/mxl-core-off.txt` (read at launch, next to the DLL) lists words separated by
spaces, commas or lines. `statesfix` turns the (default-off) states fix on. Parts left out: `states levels calc skills items missiles mana itemstat whirlwind cube craftflags quests quests2 waypoints wpauto mapreveal hotkeys lockout goblins npc reqgates hirelings skilltree runewords xp mapmem`. Never leave out
`calc` alone while skills or missiles are on (the game hangs on load). Extra words: `trace` turns on the state trace
(`state_trace.cpp`, 0.34.0: hooks set/clear state `0x2f7ea0` and the state-mask check `0x336440`, logs each distinct
state x unit kind x on/off, and each mask check that says yes with its caller, to `mxl-core.log`; at most 1,500
lines); `keepcurrent` selects the 0.34.1 states fix variant above. Delete the file for normal play.

Data cannot fix it: Median's own data references 639 of its 782 added states (skills aurastate/passivestate/
auratargetstate/State1-3, misc state/cstate, states overlays), so even compacted it needs 871 > 512. Plan: move both
arrays to a larger area (enlarge the pool element, redirect the four accessors and any inlined copies, check network
state sync and saves for fixed 16-block assumptions).
- Atma/Geglash sacred-item service (npcstores +40/+41, docs/vendors.md).

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
      The flags live in a structure at game +0x1b30 (a bit array for the game, and a name-keyed map to one bit array per
      character). Setters are four small output effects at `0x591b0` / `0x59260` (player set / clear) and `0x59300` /
      `0x59390` (game set / clear), each taking an effect record whose word at +6 is the flag number; they sit in a
      pointer table at `0x101d90cc`. The caller (which recipe output runs them) is not found, so the port has no
      setters yet. `0x59120` is the neighbouring effect that takes (word at +6) x 100 gold from carried gold (stat 14)
      and then the bank (stat 15).
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
    through). Skill level (30/31) uses D2R's base-level calc code 41 for both (no +skills yet). Craft flags exist
    in the plugin (`SetCraftFlag`) but nothing sets them, so ops 38/40 never pass (the 19 flag recipes stay locked) and
    39/41 always pass. The gold cost is checked, not paid. **Blocked:** D2R's entry bytes of `0x529090` / `0x529d50`
    are not recorded (arrays empty in cube_conditions.cpp), so the hooks refuse to install and report why in `mxl`.
    `mxl-core-off.txt` word `cube`.
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

## Diagnosis switch and state trace

`<game>/mods/MedianXL/d2rloader/plugins/mxl-core-off.txt` (read at launch, next to the DLL) lists words separated by
spaces, commas or lines. `statesfix` turns the (default-off) states fix on. Parts left out: `states levels calc skills items missiles mana itemstat`. Never leave out
`calc` alone while skills or missiles are on (the game hangs on load). Extra words: `trace` turns on the state trace
(`state_trace.cpp`, 0.34.0: hooks set/clear state `0x2f7ea0` and the state-mask check `0x336440`, logs each distinct
state x unit kind x on/off, and each mask check that says yes with its caller, to `mxl-core.log`; at most 1,500
lines); `keepcurrent` selects the 0.34.1 states fix variant above. Delete the file for normal play.

Data cannot fix it: Median's own data references 639 of its 782 added states (skills aurastate/passivestate/
auratargetstate/State1-3, misc state/cstate, states overlays), so even compacted it needs 871 > 512. Plan: move both
arrays to a larger area (enlarge the pool element, redirect the four accessors and any inlined copies, check network
state sync and saves for fixed 16-block assumptions).
- Atma/Geglash sacred-item service (npcstores +40/+41, docs/vendors.md).

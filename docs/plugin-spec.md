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
| 154 | 0x100aafa0 | 3 | mysterious_companion_skill, judgementwander_2_skill |
| 155 (-> D2R 109) | 0x100ab090 | 1 | **town_portal** (see below) |
| 156 | 0x100aaee0 | 1 | skills_374 |
| 157 (-> 176) | 0x100aca50 | 25 | catapult_shot, overkill, diseased_cattle, hail_of_stones — **plugin** (barrage) |
| 158 (-> 177) | 0x100acc50 | 74 | DeathMaul, MegademonInferno, CountessFirewall, egg_trap — **plugin** (missile row) |
| 159 (-> 178) | 0x100aa7e0 | 15 | spiral_dance, heaven_s_fury, crucify_2, barrier_strike — **plugin** (strike burst) |
| 160 (-> D2R 124) | 0x100a97e0 | 4 | thundergod, armageddon_2, frigid_domain, overheat — **data** (D2R's Armageddon) |
| 161 (-> 180) | 0x100ad0f0 | 2 | hyena_strike, exorcism — needs Median's damage block (see below) |
| 162 | 0x100ac7c0 | 1 | skills_667 |
| 163, 164, 168-170 | 0x100a9ac0, 0x100ac430, 0x100aabf0, 0x100ac200, 0x100aa9e0 | 0 | unused by the data |
| 165 | 0x100ab190 | 10 | Holy Freeze, skills_1907 |
| 166 (-> 185) | 0x100abff0 | 7 | flamestrike, flamestrike_2 — **plugin** (missile at target) |
| 167 | 0x100ad4a0 | 1 | resurrect_2 |
| 171 (-> 190) | 0x100acee0 | 1 | queen_of_blades — needs Median's damage block (see below) |

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
caster. After each pet: `0x100683d0` (D2Hirelings.cpp, Median's minion scaling by pettype: level, stats, ~10 steps;
**not ported**), `0x10067940` (the skill's aura/passive stats on the pet; D2R `0x55ef80`), `0x10042740` (party/AI;
D2R `0x58d950`), `0x10049c00(owner, 0x200b, 1)` (D2Achievements.cpp counter; skipped). The plugin also sets the pet
level with D2R's `0x55fb50` as D2R's Raise Skeleton does, until Median's scaling is ported.

### 159: strike burst (`0x100aa7e0`)

Like vanilla Charged Strike (D2R `0x5562b0`): the skill target unit, if any, takes the queued melee hit (1.13c pointer
`0x101f1b04`; D2R `0x44b2b0`); calc1 missiles (srvmissilea/b/c by calc2 clamped 1-3, flags 0x21) start at the skill
target and fly to 2*target - caster; each gets 1.13c's charged-strike init function (`D2Game+0x41f50`, through
`0x101f1a40`) with its index. D2R replaced the init-function pointer with a kind code at params +0x70 (1 =
charged-strike bolt, 4 = multishot) and its argument at +0x74. Not ported: `0x10041f30(game, unit)` (a D2Game call
through `0x101f1d78`, unidentified). D2Sigma resolves D2Game/D2Common addresses as (module index, RVA) through
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

Both fill a 0x70/0x6c-byte damage block themselves (Median's damage: elemental type switch on the 1.13c record's
EType, min/max from Median's own helpers `0x100a6020`/`0x100a5ec0`, `0x10041e80`), then 161 applies it to the melee
target and 171 to every unit in aurarangecalc chosen by aurafilter (`0x10042600`, callback `0x100bb740`). Port
together with D2R's damage application.

### 165: aura (`0x100ab190`) — not ported yet

Median's own aura pulse built from C++ callbacks: aura stats onto units with aurastate (1.13c +0x80), auratargetstate
(+0x82), units in aurarangecalc (+0x64) chosen by aurafilter (+0x50) through `0x10042600` with callback `0x100bbbe0`.
D2R's generic stat aura is srvdofunc 65, but Median's auras use aurafilter 1 where vanilla uses e.g. 73731, and
D2Sigma's callback does its own target selection: a plain remap to 65 could put an enemy aura (Holy Freeze) on the
caster's party. Needs the callback's rules first.

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
| Item spells (misc pSpell) | 0x236fe30 | 20 entries {check, use, flag}: check handlers at 0x1d3ea30 (11), use handlers at 0x1d3ea90 (16); pSpell 20+ does nothing |
| Summon pet | 0x4350e0 | (game, &params) -> pet: 1.10's block with an 8-byte owner: flags +0 (1 = at x/y), owner +8, monster u16 +0x10, AI state +0x14, mode +0x18, x/y +0x1c, pettype +0x24, petmax +0x28. Registers the pet (enforces petmax) |
| Pet setup (D2R Raise Skeleton, 0x55e440) | 0x55fb50, 0x55ef80, 0x58d950 | level from the owner (game, owner, pet, 0, level); the skill's passive/aura stats and aura state on the pet (game, owner, pet, skill, level, owner+0x12c, 0); party/AI (game, pet, 0, owner+0x130). Skills record: summon +0x110, pettype +0x112, summode +0x113, petmax +0x114 |
| Create portal object | 0x432ce0 | (game, unit, room, x, y, destLevel, &object, objectId 0x3b, 0, 0) |

**Collision:** D2R's newer skills use srvdofunc 153-171, the same numbers as D2Sigma's added functions, so Median's
skills ran unrelated D2R functions. `txt_migrate.py` now moves D2Sigma's 153-171 to the free 172-190 (+19);
The plugin writes its functions into those slots (a data write into .data, guarded by the
expected value 0).

Other tables to check for the same problem: Median uses cltdofunc 98-99 (D2R base max 97) and cltstfunc 82
(D2R base max 55).

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
  compiled localdelay at record +0x238, minus the unit's stat 309 for the skill). Still 0: `bmax`, `maxe`, `mnhp`/`mnar` (Median's minion scaling, `0x10067c60`/`0x10067bd0`), `pdmn`, `pdmx`, `mode` (unit+0x10 in 1.13c)
  (cases in `0x100a6340`, jump table `0x100a67b0`). Open as before:
  `wdm`, `skcd`, `aura`, `bl12..`, `area`, ... (docs/validation.md lists counts).
- Formula functions: `hasstate`, `tabpoints`, `statlayer`, `itemstat`, items `skilllevel`, missiles `stat`
  (docs/formulas.md).
- Skill tree rules (`ui/SkillTree.cpp`): devotions lock each other, "pick up to three masteries".
- Skill descriptions (`D2SkillDesc.cpp`).
- Engine limits: states (below), level types (below).

## Level types

Median has 52 level types (36-51 its own, used by 44 areas). D2R loads LvlTypes.txt into a vector (`0x32db30`), so
the tile sets load. Only AutoMap.txt is fixed at 36: the loader `0x32cae0` matches each row's LevelName against 36
names (table `0x1cfe110`, 0x10 bytes each; unmatched rows assert "ii < NUM_LEVEL_TYPES" and become type 0) and keeps
each type's row range in a 36-entry array at `0x2a9a8e0` (8 bytes each, followed by another object at `0x2a9aa00`).
The automap tile lookup `0x32bdf0(levelType, tileType, style, sequence)` reads that array unchecked.
**mxl-core 0.7.0** hooks the lookup: types >= 36 answer -1 ("no automap tile"), so Median's areas have no automap
tiles instead of reading past the array. Full support: relocate the name table (+16 names) and the range array (52
entries), raise the loader's bound 0x24 -> 0x34.

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
(`0x2f6370`), "changed" pointer (`0x2f62d0`, dword index 0x2bc -> 0xac0) and the copy routine's 64 accesses
(`0x2f0d80`, which now copies the 32-block state array and leaves the "changed" flags). 72 sites (+1 data), checked
byte for byte, one D2RLoader transaction, skipped if the pool already has chunks. Callers of the array pointers
(`0x336440`, `0x3356c0`, `0x336330`) size their loops from the live block count. Open: the client/server state
packets' state-id width (if narrower than 10 bits, states above it misbehave in multiplayer independently of this fix);
the startup assertions remain (harmless).

Data cannot fix it: Median's own data references 639 of its 782 added states (skills aurastate/passivestate/
auratargetstate/State1-3, misc state/cstate, states overlays), so even compacted it needs 871 > 512. Plan: move both
arrays to a larger area (enlarge the pool element, redirect the four accessors and any inlined copies, check network
state sync and saves for fixed 16-block assumptions).
- Atma/Geglash sacred-item service (npcstores +40/+41, docs/vendors.md).

# Monster AI, unique mods and monster tooltips (lane monster-ai, 2026-10-21)

Source: `D2Sigma.dll` (1.10, base 0x10000000, Ghidra read-only copy; every function below decompiled, the two big ones also read in
assembly), tables from `work/named`, `work/mod`; D2R side from the analysed dump (`docs/d2r-addresses.md` section 12). Addresses are D2Sigma
RVAs-as-VAs (`0x1004a470`) unless marked D2R. Buckets as in [README.md](README.md): (a) data, (b) SDK, (c) native hook, (d) UI, (e) cut.

## Short version

- **Median's monster code is almost all vanilla, recompiled.** D2Sigma carries its own copy of D2Game's AI, unique-mod and sound code (it
  replaced the game DLL), so most of the 15 functions in these four modules have an exact twin in D2R. Only **three** change behaviour:
  1. **`andariel_ai`** (`D2AI.cpp` `0x1004a820`), a new boss AI that is not in D2R's AI table: 47 monsters (Andariel, Maghda, Zoltun Kulle,
     Radament, Kentril Dumon, Lilith, the Scholars, Gharbad, Malthael x10, ...). **This was a real bug in the pipeline**: D2R's table has 155
     entries and txt_migrate appended the row as id 155, which the engine silently turns into "None" (a think function that is a bare `ret`), so all 47
     would have stood still. Fixed in data (section 5): they now use D2R's own `Andariel` AI with parameters in its meaning, and
     `validate.py` fails on any AI past the table. A native port of Median's AI is **not done** (section 4, ~3-5 days, cannot be tested without the game).
  2. **`Summoner`** (`0x1004a470`): the vanilla five-skill caster with the resistance test taken out (166 monsters). D2R's version picks the
     cold or the fire branch by the target's resistances; Median's picks by a plain `aip3`% roll. Cosmetic; left as D2R's.
  3. **`MosquitoNest`** (`0x1004a110`): an init that delays the first spawn by the nest skill's delay. 5 rows, 1 enabled (`harvester_of_souls`). Skipped.
- **Idle, the alt function (`0x1004a280`) and 7 tiny init functions are the vanilla ones** (D2R: the Idle think waits 200 frames, Median 250).
- **D2MonUMods and D2Monsters are rebuilds of D2Game's code**; Median's unique-mod ids 0-42 are D2R's 0-42 by name, so the mods are **data only**
  (two `enabled` flags differ: `thief` on, `stoneskin` off, already migrated).
- **D2MonDesc is the client's monster hover text** (type, immunities, flags, mod names, name colour). D2R draws its own (immunity icons
  `Controller/HUD/EffectIcons/Immune_*`, champion/unique colours and mod names). Bucket (d), nothing to port now.
- **No plugin code in this lane**: nothing in these modules is both different from D2R and cheap to port safely (section 6 says what would be).

## 1. How D2R dispatches AI (verified, details in d2r-addresses.md section 12)

Monster AI is a table, `monai.txt` row = index. The getter `0x4a36c0(unit, mode)` returns `0x2396e90 + ai * 0x20` for `ai` in 0..154 and entry 0
otherwise; entry `{kind, init, think, alt}`. The tick dispatcher is `0x4a2a00`, the set-state function `0x4a2690`. The monstats row holds the id
(`+0x52`), `aip1..8` (u16 per difficulty) and the five skills with modes; the think functions read them directly. D2Sigma's table is the same
thing at `0x101d8480` with 16-byte entries `{think, alt, kind, init}`; entries are filled at start from D2Game imports except where Median
supplies its own function:

| Sigma slot | AI (id) | Sigma function | D2R twin / status |
|---|---|---|---|
| think of 1 | Idle | `0x1004a210` | D2R `0x4a3680`, same |
| think of 53 | Summoner | `0x1004a470` | D2R `0x5b4d50`, differs (section 3) |
| alt of 61, 65, 128 | Hireable, FetishShaman, Nihlathak | `0x1004a280` | D2R `0x5af320`, identical |
| init of 83 | MosquitoNest | `0x1004a110` | D2R has no init for it, section 3 |
| think of 148 | `andariel_ai` | `0x1004a820` | **none** (D2R ends at 154) |
| init of 42, 44, 120 / 58 / 100, 103 / 109, 110 / 121 / 104 / 101 | various | `0x1004a330`, `a430`, `a370`, `a3f0`, `a3b0`, `a1a0`, `a2f0` | vanilla state resets (`ctl[5..7] = 0 / -1 / frame`), D2R has its own |

Median's monai.txt has 149 rows (0..148, `andariel_ai` last); D2R's has 155. Every other Median AI name exists in D2R's 0..147 under the same
name, so the monstats AI column resolves by name; D2R's seven additions (`GenericPet`, `Colossal*`, `ColBarb*`, ids 148-154) are D2R-only.

## 2. D2AI.cpp, the 16 functions

The nine with assert strings (lines in parentheses), then the seven without. "Users" = monstats rows (`work/named`) with that AI.

| Function | What it does | Users | Bucket / state |
|---|---|---|---|
| `0x1004b270` (:0xb4) | `AiWait(game, unit, frames)`: queue a wait action of N frames (min 1) | helper of all | D2R `0x4a6d10`; nothing to do |
| `0x1004b110` (:0xd3/:0xd7) | `AiMove(game, unit, ...)` with mode checks; mode 15 with state 0x3c becomes mode 2 and zeroes `ctl[6..8]`; optional follow-up bits (1 = flag set, 2 = chance wait, 4 = clear) | called by `andariel_ai` | helper; D2R has the family `0x4a7df0`, `0x4a78e0` |
| `0x1004b310` (:0x116/:0x11a) | `AiUseSkill(game, target, skill, mode, x, y)`: mode < 0x10, builds the action, wait 10 if refused | helper of Summoner, `andariel_ai` | D2R `0x4a7bc0`, same shape |
| `0x1004aef0` (:0x655, :0x65b, :0x666) | skill chooser used by `andariel_ai` when the target is far: if the distance is under `aip3` and the `aip2` roll passes, the skills selected by `aip5` bits 2 / 4 (with `ctl[7] == 0`, then `ctl[7] = 11`) and bit 1 (skill 1 or 4 at 50%, enemy within 20), else wait 15 | `andariel_ai` | goes with it, (c) later |
| `0x1004a110` (:0x5df) | MosquitoNest init: `ctl[5] = frame + skills.txt[Skill1].delay` (first spawn delayed); no owner skill -> `frame` | `suckernest1-4`, `harvester_of_souls` | c, tiny, **skipped** (one enabled monster) |
| `0x1004a210` (:0x181/:0x187) | Idle think: `wait(250)` | 257 rows (236 enabled): critters, totems, doors, Bone Prison, Target Dummies, injured Barbarians, `brute2`, ... | D2R `0x4a3680` waits 200; **data/no work** |
| `0x1004a280` (:0x63d) | alt think: drop state 0xc, `AiSet`, wait 1 | `roguehire`, `act2hire`, `act3hire`, `act5hire1/2` (Hireable); `fetishshaman1-8`, `flayer_shaman`; 3 Nihlathak | D2R `0x5af320` identical; **no work** |
| `0x1004a470` (:0x527) | Summoner think (five skills, cooldowns, flee, a2q5 init) | 166 monsters | section 3, **left as D2R's** |
| `0x1004a820` (:0x350) | `andariel_ai` think | 47 monsters | section 4, **replaced in data** |
| `0x1004a1a0`, `a2f0`, `a330`, `a370`, `a3b0`, `a3f0`, `a430` | init state resets: `ctl[5..7]` to 0 / -1 / frame / owner level | Vendor, Duriel, Overseer, Navi, Buffy, InvisoPet, Totem, Vines, MinionSpawner, DeathSentry, AssassinSentry | vanilla (`:0x5a3`, `:0x5b3`, `:0x5c1`, `:0x5cf`, `:0x600`, `:0x60e`, `:0x626`); **no work** |

Users in Acts 1-2 (enabled, level <= 40, normal): Summoner AI `summoner` (23), `fairy_witch` x3 (25), `scorcher_2` (35), `khazra_blood_caller` (9),
`manoruk` x2 (6), `death_projector_4`; `andariel_ai` `andariel` (17), `zoltun_kulle` (24), `maghda` (35), `vision_of_anguish` (15), `vision_of_hatred` (40);
Hireable the five hire classes; FetishShaman all nine.

## 3. Summoner (`0x1004a470`) against D2R's `0x5b4d50`

Same skeleton, line for line: init once (a2q5 hook) -> if distance < 5 and `aip6`% flee (mode 6) -> gate `aip1`% else wait 4 -> `aip2`% skill 5 on the
target -> nearest enemy, then a **first branch** (skill 2 with cooldown `aip4` while distance < `aip7`, else skill 1 on the enemy within `aip8`) or a **second
branch** (skill 4 with cooldown `aip5`, skill 3 on the enemy, skill 2 as above) -> default skill 5, else wait 4. Skill modes are `Sk1mode..5`.

The single difference is which branch runs first. D2R: `first = (target cold res > target fire res)`, inverted when a roll is `<= aip3`. Median: `first = roll < aip3`.
When the target's cold and fire resistances are equal the two are the same distribution; with unequal resistances D2R's AI is smarter (it casts the element the player resists
less), Median's is random. Not worth a hook (a port would be a 100-line copy of `0x5b4d50` with one comparison replaced). Median's data (`aip3`) is still a
valid probability in D2R's version.

## 4. `andariel_ai` (`0x1004a820`)

**What is known** (assembly read; unit tested nowhere): a multi-phase boss AI parameterised by `aip1..aip5` and `Skill1..4`:

- `aip5` is a bit mask: 1 = skill 1 allowed (ranged), 2 = skill 2 (cooldown), 4 = skill 3 (cooldown). In the data: 7 on 34 rows, 3 on 12, 1 on 1 (so skill 3 is
  off for 13 of them). `aip1` (3-99, mostly 25 / 33) gates the ranged attack, `aip2` (mostly 80-100) and `aip4` (mostly 15, 11 rows 95) the cooldown skills,
  `aip3` (40-100) is a distance. Skills 1-4 are set on all 47 rows (skill 3 on 34), skill 5 on none.
- `ctl[5]` is a phase (phase 2 is tested first: a charge in progress), `ctl[6]` a distance mark, `ctl[7]` a cooldown (counts down per think).
- Phase 2 runs while hit points are under 75%: it computes the monster's run-to-walk speed ratio from monstats `+0x32` / `+0x34`
  (`run * 100 / walk - 100`, capped at 120), writes it into the monster data (`+0x1c`) and issues the charge action (mode 8); with hit points back over 75% it returns to phase 1
  (walk towards the target, mode 7).
- Skills 2 and 3 are used with an 11-frame cooldown (`ctl[7] = 11`), skill 1 or 4 chosen by a 50% roll when an enemy is within 20, a chase action (mode 4) otherwise.
- A second path for monsters whose monster data `+0x54` is 3 or 0x13 (a controlled-monster state) queues the attack through the raw action queue instead.

**What is not known**: the semantics of several helper results (the "nearest enemy" distance out-parameter, `+0x54`, the D2Common call `0x10167150`), so a port
cannot be written from this alone. D2R equivalents exist for the primitives (wait `0x4a6d10`, use skill `0x4a7bc0`, flee `0x4a7df0`, nearest target `0x595f80`, seed
`0x34a1e0`) but not for the "enemy distance" and the hit-point percentage call; they would have to be found and the unit semantics checked in the game.

**Interim (done, section 5)**: D2R's `Andariel` AI (`0x5aba70`: skill 1 with `aip1`% when in reach, otherwise wait `aip2`% / skill by `aip3`% / walk) uses skills 1 and 2 and
`aip1..4`; skills 3 and 4 (summons, towers) are not used. Plan for the port: new `monster_ai.cpp` that writes a native think function into a table entry and a getter
hook that returns an entry for id 155 (the table cannot grow, d2r-addresses.md section 12), with the decision logic in `monster_ai_logic.h` so it is unit tested; 3-5 days.

## 5. What changed in the data pipeline

- `tools/monster_ai_data.py`, new step after `txt_migrate.py`: monsters with AI `andariel_ai` get AI `Andariel`; `aip1` and `aip4` keep Median's values (chances in both
  meanings), `aip2` = 3 (approach quickly; Median's own monsters on the vanilla Andariel AI use 2-25) and `aip3` = 25, `aip5-8` cleared; the `andariel_ai` monai row is removed so
  `monai.txt` has D2R's 155 rows. Both banks. Report: `work/mod/monster_ai_report.md`. Run against a copy: 47 monsters per bank.
- `tools/validate.py`: **error** when a monster's AI is not within the engine's first 155 monai rows (checked it fires: 47 errors on the unfixed tables, 0 after).
- Nothing else: `thief` / `stoneskin` unique-mod flags, monsounds, monai names all arrive already; D2R's monai has no column Median lacks.

## 6. The other modules

| Function | What it does | Bucket |
|---|---|---|
| D2MonUMods `0x1008c580` (:0x3d...) | `AddUMod(game, monster, mod id, flag)`: id < monumod count; counts a unique in the game's level record (`+0x2c8`), sets monster data flag 8, stores the id in the nine slots at monster data `+0x1c`, calls the mod's start handler from a table at `0x1033d610` (21 vanilla handlers, filled from D2Game imports at `0x100246b5`) | (a): D2R has the same, ids match by name (monumod 0-42 identical to D2R's first 43; D2R adds 43 `thinkonce_on_death`, 44 `hidedead`) |
| D2Monsters `0x1008c3f0` (:0x49) | monster attack sound: Attack1 / Weapon1 (`monsounds +6/+8`), or Attack2 / Weapon2 (`+0xa/+0xc`) for champions and uniques (monster data flag bits 2 / 8) | (e): D2R sounds (`sounds.txt`, D2R wins) |
| D2Monsters `0x1008c360` (:0x75) | monster init / taunt sound when the unit's mode equals the row's byte `+0xe` (ids at `+0xf`, `+0x11`) | (e) same |
| D2MonDesc `0x1008b450` (:0xc) | builds the monster hover text: type line `0x8c0b0`, immunity / resistance lines `0x8b650` (stats 0x27 fire, 0x2b cold, 0x29 lightning, 0x2d poison, 0x24 damage, 0x25 magic; >= 100 is "immune"), flags line `0x8bd10` (monster data flags 0x10 ...), unique mod names `0x8c180` (nine slots joined by ", "), then format `0x8b630` | (d) D2R draws immunity icons and mod names itself |
| D2MonDesc `0x1008b570` (:0x117) | text colour class of the monster name: 9 unique, 4 champion / minion variants, 0xc / 8 / 2 / 4 for monster data flag bits 0x10 / 0x20 / 0x40 / 0x80, else the monstats colour byte | (d) D2R colours champions and uniques; the extra flag colours are probably unused |
| D2MonDesc `0x1008b1a0` (:0x?) | ring of up to 64 client effects of radius 30 around a unit (cos / sin table x30 at `0x10189bf8` / `0x10189cf8`), no direct caller | (e) |

What a native port of the whole area would need: the `andariel_ai` think function (section 4), optionally the Summoner branch (section 3) and the nest init
(`0x1004a110`): about 5 days with in-game checks, none of it blocking Act 1-2 play now that the 47 monsters have a working AI.

## 7. In-game checks (for the first run)

1. Andariel (Act 1 boss, Median `andariel`): attacks and moves (not frozen), uses `andariel_skill` / `andariel_skill_2`; Maghda, Zoltun Kulle, Radament
   likewise. A monster that stands still means the AI id is out of range again (check `monster_ai_report.md`, `validate_report.md`).
2. Summoner in the Arcane Sanctuary: casts, flees when you are adjacent, the Act 2 quest still fires (init `0x5a0b80`).
3. Hire a Rogue / Desert Mercenary: fights and follows (AI Hireable alt `0x5af320`).
4. Spot check a Bone Prison / totem / Target Dummy (Idle) stays inert.

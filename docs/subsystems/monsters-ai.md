# Monster AI, unique mods and monster tooltips (lane monster-ai 2026-10-21; native ports lane median-ai 2026-10-22)

Source: `D2Sigma.dll` (1.10, base 0x10000000, Ghidra read-only copy; every function below decompiled, the two big ones also read in
assembly), tables from `work/named`, `work/mod`; D2R side from the analysed dump (`docs/d2r-addresses.md` section 12). Addresses are D2Sigma
RVAs-as-VAs (`0x1004a470`) unless marked D2R. Buckets as in [README.md](README.md): (a) data, (b) SDK, (c) native hook, (d) UI, (e) cut.

## Short version

- **Median's monster code is almost all vanilla, recompiled.** D2Sigma carries its own copy of D2Game's AI, unique-mod and sound code (it
  replaced the game DLL), so most of the 15 functions in these four modules have an exact twin in D2R. Only **three** change behaviour, and
  all three are now native (`plugin/src/monster_ai.cpp`, decision logic `monster_ai_logic.h`, unit tests `monster_ai_test.cpp`, off word `medianai`):
  1. **`andariel_ai`** (`D2AI.cpp` `0x1004a820`), a boss AI that is not in D2R's AI table: 47 monsters (Andariel, Maghda, Zoltun Kulle,
     Radament, Kentril Dumon, Lilith, the Scholars, Gharbad, Malthael x10, ...). Without help D2R's table (155 entries) turns id 155 into
     "None" (a bare `ret`), so all 47 would stand still (that was a pipeline bug, fixed first by mapping them to D2R's own Andariel AI). Now
     the table getter `0x4a36c0` is detoured and serves id 155 with a plugin entry whose think is Median's port (section 4). The data keeps
     the `andariel_ai` monai row at id 155 and Median's own `aip1-5`; `validate.py` accepts exactly that.
  2. **`Summoner`** (`0x1004a470`): Median's first-branch rule `roll < aip3` instead of D2R's resist-based choice (166 monsters): two byte patches in `0x5b4d50` (section 3).
  3. **`MosquitoNest`** (`0x1004a110`): an init at table entry 83 that delays the first spawn by the nest skill's delay (5 rows, 1 enabled).
- **With the `medianai` word of `mxl-core-off.txt`** the getter hook stays, but id 155 runs D2R's Andariel think on the row with its old mapping (`aip2` 3,
  `aip3` 25, `aip1` / `aip4` as Median has them), and Summoner and the nest are D2R's. Without the plugin at all id 155 is "None" (monsters stand still).
- **Idle, the alt function (`0x1004a280`) and 7 tiny init functions are the vanilla ones** (D2R: the Idle think waits 200 frames, Median 250).
- **D2MonUMods and D2Monsters are rebuilds of D2Game's code**; Median's unique-mod ids 0-42 are D2R's 0-42 by name, so the mods are **data only**
  (two `enabled` flags differ: `thief` on, `stoneskin` off, already migrated).
- **D2MonDesc is the client's monster hover text** (type, immunities, flags, mod names, name colour). D2R draws its own (immunity icons
  `Controller/HUD/EffectIcons/Immune_*`, champion/unique colours and mod names). Bucket (d), nothing to port now.
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
| think of 148 | `andariel_ai` | `0x1004a820` | **plugin entry** (D2R's table ends at 154; id 155 is served by the getter hook, section 4) |
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
| `0x1004aef0` (:0x655, :0x65b, :0x666) | skill chooser used by `andariel_ai` when the target is far: if the distance is under `aip3` and the `aip2` roll passes, the skills selected by `aip5` bits 2 / 4 (with `ctl[7] == 0`, then `ctl[7] = 11`) and bit 1 (skill 1 or 4 at 50%, enemy within 20), else wait 15 | `andariel_ai` | dead code (phase 2 only), not ported |
| `0x1004a110` (:0x5df) | MosquitoNest init: `ctl[5] = frame + skills.txt[Skill1].delay` (first spawn delayed); no owner skill -> `frame` | `suckernest1-4`, `harvester_of_souls` | c, tiny, **native init** |
| `0x1004a210` (:0x181/:0x187) | Idle think: `wait(250)` | 257 rows (236 enabled): critters, totems, doors, Bone Prison, Target Dummies, injured Barbarians, `brute2`, ... | D2R `0x4a3680` waits 200; **data/no work** |
| `0x1004a280` (:0x63d) | alt think: drop state 0xc, `AiSet`, wait 1 | `roguehire`, `act2hire`, `act3hire`, `act5hire1/2` (Hireable); `fetishshaman1-8`, `flayer_shaman`; 3 Nihlathak | D2R `0x5af320` identical; **no work** |
| `0x1004a470` (:0x527) | Summoner think (five skills, cooldowns, flee, a2q5 init) | 166 monsters | section 3, **native (two patches)** |
| `0x1004a820` (:0x350) | `andariel_ai` think | 47 monsters | section 4, **native port** |
| `0x1004a1a0`, `a2f0`, `a330`, `a370`, `a3b0`, `a3f0`, `a430` | init state resets: `ctl[5..7]` to 0 / -1 / frame / owner level | Vendor, Duriel, Overseer, Navi, Buffy, InvisoPet, Totem, Vines, MinionSpawner, DeathSentry, AssassinSentry | vanilla (`:0x5a3`, `:0x5b3`, `:0x5c1`, `:0x5cf`, `:0x600`, `:0x60e`, `:0x626`); **no work** |

Users in Acts 1-2 (enabled, level <= 40, normal): Summoner AI `summoner` (23), `fairy_witch` x3 (25), `scorcher_2` (35), `khazra_blood_caller` (9),
`manoruk` x2 (6), `death_projector_4`; `andariel_ai` `andariel` (17), `zoltun_kulle` (24), `maghda` (35), `vision_of_anguish` (15), `vision_of_hatred` (40);
Hireable the five hire classes; FetishShaman all nine.

## 3. Summoner (`0x1004a470`) against D2R's `0x5b4d50` (native: two byte patches)

Same skeleton, line for line: init once (a2q5 hook) -> if distance < 5 and `aip6`% flee (mode 6) -> gate `aip1`% else wait 4 -> `aip2`% skill 5 on the
target -> nearest enemy, then a **first branch** (skill 2 with cooldown `aip4` while distance < `aip7`, else skill 1 on the enemy within `aip8`) or a **second
branch** (skill 4 with cooldown `aip5`, skill 3 on the enemy, skill 2 as above) -> default skill 5, else wait 4. Skill modes are `Sk1mode..5`.

The single difference is which branch runs first. D2R: `first = X ^ 1`, or `X` when `roll <= aip3`, with `X = (target fire res >= target cold res)` (stat `0x27` against
`0x2b`, stored at `[rsp+0x44]`, read only at `0x5b4fa1` and `0x5b4feb`). Median: `first = roll < aip3`. The port patches two places and leaves everything else D2R's:
`0x5b4e49` `0f 9d c1` (`setge cl`) -> `b1 01 90` (`mov cl, 1`, so X = 1) and `0x5b4fed` `4e` -> `4c` (`cmovle` -> `cmovl`, so `X` is taken when `roll < aip3`, `X ^ 1 = 0` otherwise).
Pure rule: `SummonerFirstBranch(roll, aip3) = roll < aip3` (tested).

## 4. `andariel_ai` (`0x1004a820`), native port

Parameterised by `aip1..aip5` (current difficulty) and `Skill1..4`: `aip5` bit 1 = skill 1 allowed (the ranged skill, with skill 4 as its alternative), bit 2 = skill 2,
bit 4 = skill 3 (the last two are used with the cooldown below); `aip1` the in-reach skill chance, `aip2` the approach-skill gate, `aip3` the engage distance,
`aip4` the chance of skill 2 / 3. Three scratch ints in the control block (Sigma `ctl[5..7]` = D2R `ctl+0x44/+0x48/+0x4c`, zeroed whenever the AI is set): phase
(0 idle, 1 engaged), a mark (controlled-monster branch only), a cooldown counted down once per think. Read from the assembly (D2Sigma `0x1004a820`, 0x6a0 bytes), ported as `monster_ai::Andariel()` with an injected roll so every branch is unit tested in the order of Median's rolls:

- the cooldown (`ctl+0x4c`) counts down by one when it is not zero;
- **target in reach**: phase := 1, the unit seed is reseeded (`lo := lo * 0x6ac690c5 + hi; hi := 0x29a`, Sigma `0xc03f0`), then `aip1`%: pass -> with skill 1 allowed, an enemy
  (nearest-enemy search) and a failed 30% roll: enemy farther than 20 -> "attack or wait 33%", else a 50% roll picks skill 4 (pass) or skill 1 (fail) on the **enemy**; otherwise attack
  the enemy (the target when the search found none; Median queues a null target there). Fail -> attack or wait 33%.
- **out of reach**, distance >= `aip3`: wait 15 while the phase is 0, walk once it is 1 (so a monster stays idle until you come within `aip3`, then chases for good);
  distance < `aip3`: phase := 1 and `aip2`%: fail -> distance > 20 walk, 9..20 attack or wait 50%, under 9 a 50% roll charges (mode 8 action on the target) else attack or wait 50%;
  pass -> skill 2 (`aip5` bit 2), then skill 3 (bit 4), each needing cooldown <= 0 and an `aip4`% roll, cooldown := 11, on the target; else with skill 1 allowed and an enemy within 20: 75% fails -> attack,
  passes -> 50% picks skill 1 (pass) or skill 4 (fail) on the enemy; else walk.
- "attack" is the mode-4 action (`0x4a78e0(game, unit, 4, target)`), "wait" `0x4a6d10`, skills `0x4a7bc0(game, unit, mode, skill, target, 0, 0, 0)`, "walk" `0x4a7270(unit, 1, 0, 0)` + `0x4a8740(game, unit, target, 7)` (what D2R's own Andariel does),
  "charge" `0x4a78e0(game, unit, 8, target)` (D2R's AIs use mode 8 the same way, e.g. `0x5ae373`).
- A skill the row does not name (`Skill` < 0) turns into the plain attack on the same aim; its `aip5` bit counts as off.

**Decode items that were open, now resolved**
- *Nearest-enemy distance*: `0x595f80 (game, unit, int* outDistance, u32* aux) -> Unit*` (the unit is the return value, `outDistance` is `0x7fffffff` when none; the old table entry "u8 difficulty" was a misread of the first stack slot).
- *Monster data `+0x54`*: Median's first test `unit.data+0x54 in {3, 0x13}` (with the unit being a monster) selects a "controlled monster" copy of the logic that queues the attack through the raw action queue and tracks the nearest distance in `ctl[6]`. It looks like a
  pet / summon kind (not confirmed against a data table); none of the 47 `andariel_ai` rows is ever a pet, so the branch is **not ported**.
- *Hit-point percentage call* (Sigma import `0x1016714c`): only used in phase 2 (`ctl[5] == 2`, a charge while below 75% hit points, with the run/walk speed ratio written to monster data `+0x1c`). Nothing in the function ever sets the phase to 2, and no other code does (`ctl[5]` is zeroed whenever the AI is set), so phase 2 and the skill chooser `0x1004aef0`
  that only it calls are **dead code in Median too**. Not ported (a monster with the phase 0/1 logic above behaves exactly as Median's does).

**Plugin wiring**: the getter `0x4a36c0` is detoured; when the original returns entry 0 and the unit is a monster whose monstats `+0x52` is 155, the plugin's entry `{kind 1, init 0, think, alt 0}` is returned instead. The table cannot grow
(`0x2396e90 + 0x9b * 0x20` is the mode table), and the getter is the only reader (section 1), so redirecting it is the safe route; replacing D2R's Andariel think instead would change the 26 vanilla users.

## 5. What changed in the data pipeline

- `tools/monster_ai_data.py` (after `txt_migrate.py`) no longer rewrites anything: it checks that the `andariel_ai` monai row is exactly id 155 in both banks (the id the plugin serves), that monai does not grow further, and lists
  `andariel_ai` monsters without Skill 1 / 2 (the plain attack stands in). Median's `aip1-5` pass through untouched. Report `work/mod/monster_ai_report.md`.
- `tools/validate.py`: **error** when a monster's AI is not within the engine's first 155 monai rows **or** the plugin's `PLUGIN_AI` ids (`andariel_ai` = 155).
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

The three behaviours that differ from D2R (section 4 `andariel_ai`, section 3 Summoner, the nest init) are native in `plugin/src/monster_ai.cpp`; the rest of the area is data or D2R's own.

## 7. In-game checks

1. Andariel (Act 1 boss, Median `andariel`): attacks and moves (not frozen), uses `andariel_skill` / `andariel_skill_2`; Maghda, Zoltun Kulle, Radament
   likewise. A monster that stands still means the AI id is out of range again (check `monster_ai_report.md`, `validate_report.md`).
2. Summoner in the Arcane Sanctuary: casts, flees when you are adjacent, the Act 2 quest still fires (init `0x5a0b80`).
3. Hire a Rogue / Desert Mercenary: fights and follows (AI Hireable alt `0x5af320`).
4. Spot check a Bone Prison / totem / Target Dummy (Idle) stays inert.

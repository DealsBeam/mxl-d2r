# Median XL rules audit

Median XL 2.14.4 gameplay rules that differ from vanilla Diablo II (1.13c and D2R), each checked against the port (D2R 3.3 build 93847, plugin mxl-core).

The port's to-do list (`docs/unported.md`, `docs/subsystems/*.md`) came from D2Sigma's tables and engine functions. Behaviour that players notice but that never shows up as a table field slipped through. The example that started this audit: Median leaves no player corpse, the port still makes D2R's corpse. This file lists those rules and everything else Median documents as different, with a port status for each.

Research only: nothing here was run in game. Where a rule can be confirmed cheaply with the existing harness the last column gives the check idea.

## Method and sources

- Rules come from Median's official patch notes (sigma 1.0.0 to 2.13.x, read in full for the Game / Engine / Quality of Life sections), the Median wiki, the in-game strings and the installed classic Median at `C:\Games\median-xl`. The strings hold little general help text, so most rules come from the patch notes. Notes for 2.14 were taken from `changelog?ver=2.14` (the `sigma-2-14-0` page is a 404).
- Port status was decided by reading `docs/unported.md`, `docs/services.md`, `docs/plugin-spec.md`, `docs/subsystems/*.md`, `docs/testing.md`, `tools/game_checks.py`, `plugin/src` and the built tables in `work/mod/data/global/excel`. "No code" means a search of `plugin/src` and the docs found nothing; it does not mean D2R itself never does it.
- Source shorthand: **P1.0.0** = `https://www.median-xl.com/version/sigma-1-0-0.php` (same pattern for every version); **W:Page** = `https://wiki.median-xl.com/Page`; **D:table** = a built table in `work/mod/data/global/excel`.

### Status key

| Status | Meaning |
|---|---|
| ported | code or data exists and is cited. "(unverified)" when the docs still say `verify` or it was never seen in game. "(D2R native)" when D2R already behaves the same way |
| partly | part of the rule is ported, the rest is not |
| missing | nothing in the port does this |
| deviation | knowingly different; the `docs/unported.md` row is cited |
| unknown | cannot be told from the code or the docs; needs an in-game test |

Rules that `docs/unported.md` already tracks say "listed". Every **missing** or **unknown** rule that is not listed there gets a new row in `docs/unported.md` (status `open` or `verify`) pointing back to this file.

## Totals

| Category | ported | partly | missing | deviation | unknown | total |
|---|---|---|---|---|---|---|
| Death and corpse | 2 | 0 | 3 | 0 | 4 | 9 |
| Gold, stash and shared stash | 4 | 3 | 3 | 0 | 2 | 12 |
| Inventory, cube and belt sizes | 3 | 0 | 0 | 0 | 2 | 5 |
| Item rules | 10 | 3 | 1 | 2 | 2 | 18 |
| Crafting and the cube | 3 | 0 | 2 | 0 | 2 | 7 |
| Experience, party and level cap | 5 | 0 | 0 | 0 | 4 | 9 |
| Difficulty, area levels and monster scaling | 7 | 1 | 0 | 0 | 2 | 10 |
| Uber and quest content | 3 | 2 | 1 | 0 | 1 | 7 |
| Skills, respec and caps | 5 | 1 | 3 | 0 | 4 | 13 |
| Mercenaries | 3 | 1 | 0 | 1 | 0 | 5 |
| NPC services and town | 3 | 0 | 0 | 0 | 1 | 4 |
| Waypoints and travel | 1 | 1 | 1 | 0 | 1 | 4 |
| Maps and areas | 1 | 0 | 1 | 1 | 1 | 4 |
| Player-visible UI | 1 | 2 | 2 | 2 | 0 | 7 |
| Multiplayer | 0 | 0 | 0 | 1 | 3 | 4 |
| Hardcore | 1 | 0 | 1 | 0 | 0 | 2 |
| Anything else Median documents as different | 1 | 0 | 0 | 2 | 1 | 4 |
| **All** | **54** | **14** | **17** | **9** | **30** | **124** |

## Top 10 gaps (by player impact)

1. ~~**XP1, level cap 150**~~: fixed (lane gap-levelcap): the built `experience` table now carries Median's `MaxLvl 150` row, the engine reaches level 150 and stops there.
2. **DC1, no player corpse**: D2R still makes a corpse that must be retrieved; Median re-equips gear in town and keeps the belt. Lane `no-corpse`.
3. **SK10 and SK11, Hunter steady aim and Fire Sorceress Heat**: class-defining resources that the docs and plugin never mention. Unknown whether the tables alone carry them; if not, those builds do not work.
4. ~~**IT1 and IT2, durability and quantity**~~ ported and verified (lane gap-items): items no longer wear down and throwing stacks are not used up.
5. ~~**IT3, auto-identify on pickup**~~ ported and verified (lane gap-items): the pick-up identifies the item.
6. **DC4 and DC3, death rules**: no 5 second respawn wait, and gold on death is untested.
7. ~~**SK7, sprint and no stamina**~~: not a gap. Median's sprint is its charstats stamina (50 points, 5 seconds of running) and the port carries those columns; game check `sprint` confirms it.
8. **CR4 and DC6, trophy challenge timers**: no timer op in the cube, no void on death or zone change, so the cube cannot enforce the time limit.
9. **MU2, WP2, UI4, Map Reveal, waypoint auto-activation, X/R/C hotkeys**: daily quality-of-life rules that every Median player expects.
10. **GS5, stash pages** (D2R's tab count applies, no Median multi-page stash). ~~SK3, free mana in town~~ is ported and verified (lane gap-items).

Runners-up: GS2 (teleport picks up gold), SK8 (Shift attacks without a target), DC5 (Soulstealer heal rules), XP9 (Grit), HC1 (Hardcore to Softcore).

## 1. Death and corpse

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| DC1 | Dying leaves no player corpse to retrieve. Gear is re-equipped automatically in town and the belt keeps its potions. | P1.0.0 ("Gear is automatically re-equipped in town after death. Potions no longer vacate your belt") | ported (game check `nocorpse` PASS, lane no-corpse; docs/subsystems/death.md) | was: no corpse code on main; D2R still made the corpse. The corpse creator `0x42c110` is now a no-op, so gear and the belt are never taken off (the belt's potions were not tested: the harness cannot fill a belt). Check used: kill the player with `hit`, respawn, `state` shows no corpse unit and gear and belt are intact |
| DC2 | No experience loss on death. | P1.0.0 | ported (verified by the lane no-corpse check: no experience change on death with or without the hook; the hook also removes the penalty call for any later non-blank value) | D:difficultylevels `DeathExpPenalty` is blank in the built mod (D2R has 0/5/10). Check: kill the player on Nightmare, `stat player 13` before and after |
| DC3 | No gold loss on death. | P1.0.0 | ported (verified: D2R takes and drops ALL carried gold on death; with the lane no-corpse hooks carried gold and bank are unchanged) | was: no gold-on-death code or table value. Evidence docs/subsystems/death.md section 7. Check: `gold set 100000 0`, kill the player, `gold get` |
| DC4 | Respawning after death has a 5 second cooldown (replaces the loss penalties; discourages suicide-rushing). | P1.0.0 | ported (plugin part `respawnwait`, 125 frames; no Median code found for it, rule from the patch note) | was: no respawn delay in plugin or data; D2R revived at once. Check: Esc 45 frames after the death is held, respawn at 275 frames works. Check: time the revive after a kill |
| DC5 | Soulstealer bosses heal (10% of max life) only when they kill a player, not minions; fire teleport and Chaos Orb heal the boss only if they kill a player (40%). | P1.1.0, P2.1.0, P2.8.0, P2.13.0 | unknown | boss kill hooks are D2Sigma monster code the monsters-ai triage never listed; nothing in `plugin/src` besides the goblin kill hook. Check: let a spawned Soulstealer boss kill the harness player and read its life before and after |
| DC6 | Dying, or entering another zone, voids an active trophy challenge. | P2.10.0 ("Dying or entering another zone with a trophy challenge will void the one you've already started") | unknown | the dungeon lockout clock is ported (`lockout`, `lockclock`, `reenter`) but nothing ties death or zone change to a challenge. See CR4 |
| DC7 | Resetting skills and stats removes every active buff. | P1.6.0 | unknown | check `respec` covers stats and skills only. Check: buff, respec at Akara, `state` states |
| DC8 | Monsters flagged "no monster corpses" (labyrinth modifier, Twisted Clawstorms, Corrupted Barbarians) leave none; others do. | P1.0.0, P1.6.0, P2.11.0 | ported | monstats corpse data migrates unchanged |
| DC9 | A Hardcore character that dies converts to Softcore on Single Player. | P1.6.0 ("Fixed bugs with the Hardcore to Softcore conversion on Single Player") | missing | no hardcore code; D2R deletes or locks the character. See HC1 |

## 2. Gold, stash and shared stash

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| GS1 | Gold (and waypoints) are collected by walking over them; larger pickup range. | P1.0.0, P1.8.0 | partly | D2R picks up gold by walking too (not checked for range). Waypoint auto-activation is WP2 |
| GS2 | All teleport and warping skills pick gold up. | P1.6.0 | missing | `teleport.cpp` header: "Not ported: D2Sigma's gold auto-pickup after a player's teleport". Check: teleport onto a gold pile |
| GS3 | Gold drops and vendor prices were rebalanced (vendors buy cheap, potions and oils cost more). | P1.0.0, P2.0.0 | ported | D:monstats, D:treasureclassex replaced by Median's; D:npc buy mult 52 (D2R 512), sell mult 1024; stock in `docs/vendors.md` |
| GS4 | Larger inventory and stash. | P1.0.0 | ported (unverified) | `inventory.txt` gridX/gridY via `txt_migrate`; HD panels; in-game look pending (`docs/testing.md` "Median stash size") |
| GS5 | Shared stash and multi-page personal stash in Single Player. | P2.0.0 | partly | only the grid is set. `tools/install_mod.py` writes `d2rloader.toml` with `save_format` only; D2RLoader's `d2rcore.stash.add_shared_tabs` (`docs/services.md` L11) is not used, so D2R's own tab count applies. Check: open the stash, count tabs |
| GS6 | Shared stash on TCP/IP had stability problems (disabled in 2.0, "potential fix" in 2.9). | P2.0.0, P2.9.0 | unknown | D2R/D2RLoader share the stash file in TCP/IP; bug #20 (stash link check) is "mostly fixed" (`docs/bugs.md`). Check: TCP/IP game with two profiles |
| GS7 | A sort button in the stash; a button in the inventory that collects every item that fits a container (AC, runes, shrines). | P2.2.0, P2.9.0 | missing | container right-click is ported (`item_spells.cpp`, pSpell 18); the two buttons are Median UI with no panel code |
| GS8 | Containers: Greater Signets of Learning, Enchanted Runes, Corrupted Treasures; shrines and vessels are 1x1. | P2.11.0, P2.12.0 | partly | pSpell 18 logic ported; which container items exist in the built data was not counted. Check: `item` each code, `use` |
| GS9 | Ctrl+Click moves items between inventory, stash, cube and the ground; charms can be dropped into the stash/cube while another charm is carried. | P1.0.0, P1.2.0 | ported (D2R native) | D2R quick-move. Charm limit rule: unknown |
| GS10 | Signets of Learning give 1 stat point each, capped at 400; experience signets removed. | P1.0.0 | unknown | pSpell 3 with calc `min(1, 400 - stat 185)` (`ubers-events.md`) relies on D2R reading it the same way. Check: `item` a signet, `use`, `stat player 185` |
| GS11 | Gold costs on recipes (item designs, jewelcrafting, access scrolls). | P1.8.0, P2.1.0 | ported | `cube_conditions.cpp` gold payment; checks `goldcraft`, `goldclient` |
| GS12 | Realm services: web stash, vault, hibernated characters, bounties, Stash Stat Search, in-game ladders. | P2.13.0 | missing | realm only, Phase 7 (`docs/realm-plan.md`) |

## 3. Inventory, cube and belt sizes

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| IC1 | Backpack and Horadric Cube sizes enlarged (15x10). | P1.0.0 | ported | `inventory.txt`; HD panels by `ui_panels.py` / `ui_layouts.py` |
| IC2 | Trade window and NPC stores are larger. | P1.0.0 | ported (unverified) | `inventory.txt` Trade Page and Monster rows; listed (Trade window) |
| IC3 | Belt UI and the show-belt-rows hotkey work. | P1.2.0 | ported (D2R native) | D2R belts.txt carried |
| IC4 | Set bonuses with more than 6 pieces. | Median sets | unknown | `docs/services.md` L10: set size >6 unknown in D2R. Check: equip a 7-piece set |
| IC5 | Only one charm of a type may be carried (fixed bug: more than one charm). | P1.0.0 | unknown | D2R's rule applies; Median's fix not compared |

## 4. Item rules

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| IT1 | Items have no durability; repair, durability warnings and breakage are gone. | P2.4.0 ("Removed durability"), P2.7.0 | ported (verified) | plugin part `durability` (`item_rules.cpp`, off word `durability`): D2R's wear dispatcher `0x441b10` and the skill-use wear `0x5590c0` are no-ops, the equivalent of Median's NOP of the wear call (D2Game+0xdee28). `game_checks nodurability`: 600 wear steps, weapon 110 -> 110, armour 48 -> 48 (off word: 110 -> 101, 48 -> 16) |
| IT2 | Thrown/quantity items are no longer used up by attacking or throwing; ammunition warnings gone. | P2.7.0 ("Quantity is no longer reduced by normal attack and throw") | ported (verified) | plugin part `quantity`: D2R's quantity use `0x439130` leaves the stack alone, the equivalent of Median's NOP of the `dec ebp` (D2Game+0xa16be). `game_checks nodurability`: 4 Throws, quantity 58 -> 58 (off word: 110 -> 109) |
| IT3 | Items dropped by monsters are identified as soon as they are picked up; no identify scrolls. | P1.0.0 | ported (verified; mechanism differs) | plugin part `autoid`: after a successful pick-up `0x471950` the item goes through D2R's identify `0x46e8c0`. Median's exact mechanism is not in D2Sigma, so this is the same effect by D2R's own function. `game_checks autoid`: flags 0x82000 -> 0x82010 (off word: stays 0x82000) |
| IT4 | Repair All with a hotkey at the smith (before durability was removed). | P2.0.0 | deviation | superseded by IT1 |
| IT5 | Ethereal and socket rolls, ethereal drop chance, ethereal durability penalty. | P1.1.0, D2Sigma | partly | `itemrolls`; listed (Ethereal roll approx, Socket roll verified) |
| IT6 | Class-locked items. | D2Sigma | ported | verified, `classlock`; listed |
| IT7 | Resist cap (physical cap bug fixed, 75%/max) and absorb. | P1.6.0 | ported | verified, `resistcap`, `absorb`; listed |
| IT8 | Andariel catalyst and bonus drops. | D2Sigma | ported | verified, `bonusdrops`; listed |
| IT9 | Item colour dyes. | D2Sigma | ported | verified; listed (Dye) |
| IT10 | Gamble: more item types on the screen (rings, amulets), custom prices (rare 25000, unique 1000), refresh button (hotkey R) and a highlight filter, independent per player. | P1.0.0, P2.0.0, P2.9.0 | partly | prices from D:difficultylevels `GambleRare`/`GambleUnique`; stock in `docs/vendors.md`; refresh button and filter are Median UI with no hook. Check: open Gheed |
| IT11 | Enhanced Damage / Defense shown next to damage and defense on the item. | P2.9.0 | unknown | item tooltip text; listed (Item tooltip text) |
| IT12 | Item level line hidden on 10 item types. | itemtypesext `NoShowILvl` | ported (unverified) | listed (Item level line) |
| IT13 | Item event ids (use, drop, equip hooks). | D2Sigma | partly | listed (Item event ids; id 32 open) |
| IT14 | Loot filter lists and rules. | D2LootFilter | deviation | listed (Loot filter fields) |
| IT15 | Tiered uniques, sacred uniques with varying level requirements and ilvls, tiered runewords, superior items. | P1.0.0, P2.0.0 | ported | tables replaced by Median's |
| IT16 | Item drop level uses the area level the monster was found in, not the town's. | P2.3.0 | unknown | D2R's own drop code with Median's treasure classes; not compared. Check: kill a monster after a portal trip to town and read the dropped item level |
| IT17 | Shrines/runes/containers right-click use. | P2.11.0 | ported | pSpell 18 in `item_spells.cpp` |
| IT18 | Halloween chest treasure class (`Treasureclass_H`). | objects2 | missing | listed (Halloween chest `Treasureclass_H`) |

## 5. Crafting and the cube

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| CR1 | About 7500 recipes: item designs, upgrades, runeword and honorific rerolls, shrinecrafting. | P1.0.0, P2.0.0 | ported | D:cubemain from `txt_migrate`; check `cubemain` |
| CR2 | Median cube conditions (ops 29-42: gold payment, craft guards, class conditions). | D2Sigma | ported | `cube_conditions.cpp`; `docs/plugin-spec.md` |
| CR3 | Craft guard flags: once per game or per character. | D2Sigma | ported | ops 38, 39, 41 |
| CR4 | Trophy challenge charms: kill the boss and cube the charm within the limit (10 seconds unless stated, e.g. 25 s); death or zone change voids it. | P2.10.0 | missing | no timer op in the cube conditions (`docs/plugin-spec.md` "Cube conditions"). Check: kill the trophy boss, wait 15 s, cube |
| CR5 | Cube transmute hotkey 'C'. | P1.0.0 | unknown | D2R transmute button only; no hotkey code. Check: press C with the cube open |
| CR6 | The "Cow Level" portal can be made in any Act town. | P1.0.0 | unknown | recipe is in D:cubemain; the town restriction check was not looked at. Check: make the portal in Act 5 |
| CR7 | Imbue, socket and personalize rewards highlight the eligible items. | P2.0.0 | missing | Median UI |

## 6. Experience, party and level cap

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| XP1 | The maximum character level is 150. | P1.0.0 | ported | fixed in lane gap-levelcap: `tools/txt_migrate.py` relabels Median's `MaxLvl` row, so it replaces D2R's `MaxLvl 99` row (it was decoded as level -1 / `4294967295` and D2R's row was appended last, capping the engine at 99). Verified: `levelcap` check (levels 99, 120, 149, 150; the level stops at 150; stat and skill points per `charstats`; Save and Exit and Play keeps level 150; `docs/testing.md`) |
| XP2 | Reworked experience formula; level 100 by the start of Hell; no huge gaps at 121-125. | P1.0.0 | ported (unverified) | D:experience rows and ExpRatio |
| XP3 | Skill and stat points per level. | P1.0.0 | ported | D:charstats `SkillsPerLevel`, `StatPerLevel` |
| XP4 | Kill experience: reworked level-difference penalty. | P2.9.0 | ported (unverified) | inline hook `0x44fef0`, check `xp`; listed (`docs/services.md` I3) |
| XP5 | Party kill experience is awarded without a split. | D2Sigma | unknown | listed (Party experience award, multiplayer) |
| XP6 | Shift+Click on a stat point adds 25. | P1.1.0 | unknown | D2R adds 5 or all. Check: shift-click a stat |
| XP7 | Unlimited respecs until level 50 after Den of Evil on Normal. | P2.0.0 | unknown | `respec` check covers the Akara respec; the free-until-50 rule is a quest flag. Check: respec twice at level 20 after the quest |
| XP8 | Extended stats screen: sum of all stats, damage, poison length reduction by difficulty penalty. | P2.0.0, P2.9.0 | ported (unverified) | `docs/subsystems/stat-screen.md`; listed (Character sheet approx) |
| XP9 | Grit: Strength grants a stat that reduces damage taken. | P2.11.0 | unknown | no mention of Grit in docs or plugin; may be table driven. Check: raise Strength and read the stat |

## 7. Difficulty, area levels and monster scaling

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| DF1 | Resist penalty -30 (Nightmare) and -70 (Hell) for players. | difficultylevels | ported | D:difficultylevels |
| DF2 | Life and mana steal divisors 1/4/9 and 6/24/48. | difficultylevels | ported | D:difficultylevels |
| DF3 | Area levels: Nightmare 51-100, Hell 105-120, endgame 105-150; rifts and dungeons start at 105. | P1.0.0 | ported | D:levels, D:monstats, D:monlvl |
| DF4 | Monster immunities replaced by absorb (25/50/75% by difficulty); monster resists 0% Normal, 15% elemental Nightmare, 10%/35% Hell. | P1.0.0 | ported | D:monstats; verified (`absorb`); listed |
| DF5 | Monster unique affixes, champion/minion density, Median-only mods. | W:Monsters | ported | D:monumod, D:levels |
| DF6 | Elite and uber boss behaviours (summons, phases). | W:Bosses | partly | boss AI is D2Sigma's AI table module (`docs/subsystems/monsters-ai.md`) |
| DF7 | Players X: monster count and life scale with party size. | D2Sigma | unknown | D2R's own scaling is used; Median's not compared |
| DF8 | Hell difficulty needs the previous difficulty completed; difficulty names are vanilla. | P1.0.0 | ported | D2R |
| DF9 | Monsters do not drop guaranteed uniques on Nightmare/Hell act bosses. | P1.0.0 | ported | treasure classes replaced by Median's |
| DF10 | Monster level for skill/hit formulas uses the area level the monster was found in. | P2.3.0 | unknown | not compared |

## 8. Uber and quest content

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| UQ1 | Dungeon entry lockouts and the in-game lock timer. | P2.0.0 | ported | checks `lockout`, `lockclock`, `lockquest`; listed (item-teleport clock start open) |
| UQ2 | Access scrolls, keys, signets, oils, riftwalks, rune and crystal systems. | W:Items | ported | tables and `item_spells.cpp` |
| UQ3 | Goblins and treasure goblins. | P2.2.0 | ported | `goblins` check; listed (sighted packet approx) |
| UQ4 | Quest reward notices. | P2.0.0 | partly | listed (Quest reward notices, approx) |
| UQ5 | Some zones disable town portals (Edge of Nothingness corridor). | P1.6.0 | unknown | only the cooldown is ported. Check: open a portal in the corridor |
| UQ6 | The ubers and endgame events work without a dedicated module. | `docs/subsystems/ubers-events.md` | partly | data + item spell 16 + lockout + goblins |
| UQ7 | Seasonal/Halloween/Christmas events. | P2.4.0, P2.13.0 | missing | listed (Halloween chest) |

## 9. Skills, respec and caps

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| SK1 | Skill points cap (max base level) and soft levels. | P1.0.0 | ported | D:skills |
| SK2 | Akara respec with a confirmation dialog. | P1.6.0 | ported | check `respec`; listed (Akara respec confirm, done) |
| SK3 | Skills cost no mana in town. | P2.1.0 | ported (verified; mechanism differs) | plugin part `townmana`: the mana cost hook returns 0 at the charge sites when the caster is in a town; it needed a caster hook on `0x43acb0` because D2RLoader re-assembles the server charge function (docs/plugin-spec.md, Mana costs). `game_checks townmana`: town 5000 -> 5000, field -> 4993 (off word: 4993 in town) |
| SK4 | Many skills castable in town (buffs, totems, warp, Shadow Refuge, Acid aura). | P1.1.0, P1.2.0, P1.7.0, P2.1.0 | unknown | skills.txt `town` column carried from Median; not tested. Check: `cast` each in town |
| SK5 | Town Portal is an innate skill (no scrolls) with a 10 s cooldown, and a 1 second delay when opening. | P1.0.0, P2.2.0 | partly | D:skills town_portal srvdofunc 109, localdelay 250; the 1 s delay is not checked |
| SK6 | Skill cooldowns are computed dynamically; skill groups no longer share cooldowns. | P1.0.0 | unknown | D2Sigma engine feature; D2R's cooldown. Check: two groups' cooldowns |
| SK7 | Run/walk toggle is renamed "Toggle Sprint"; a sprint lasts about 5 seconds and is much faster than walking. The patch note's "stamina removed" means the vitality and level stamina gains, not the stamina bar. | P1.0.0, patch strings, D:charstats | ported (verified) | Median's sprint is data: `charstats` stamina 50, WalkVelocity 10, RunVelocity 15 (Barbarian 14), RunDrain 60, StaminaPerLevel and StaminaPerVitality blank, built identical to Median's. D2Game's drain (RunDrain x2 per frame, forced to walk at 0) is vanilla 1.13c code in both games; there is no sprint state or skill and no cooldown timer (the bar refilling is the cooldown). Game check `sprint`: run from full drains 100 raw per frame = 5.1 s, run/walk speed 1.44 (data 1.40), walking from empty refills, a run starts at 2 points of stamina, R toggles run and walk. Not comparable: Median's stamina regen rate (unpublished) |
| SK8 | Most melee skills can be used with Shift without a target. | P2.3.0 | missing | no code. Check: shift-click a Great Hunt with no target |
| SK9 | Skill tree UI: base/max levels, Alt shows soft level, prerequisite arrows, Shift+Click maxes a skill. | P2.0.0 | missing | Median UI, no panel code |
| SK10 | Druid bow "steady aim" pseudo-resource. | P1.7.0 | unknown | not mentioned in docs; may be table driven. Check: `cast` a Hunter skill, read the stat |
| SK11 | Fire Sorceress "Heat" replaces mana (degenerates outside town). | P2.13.0 | unknown | not mentioned in docs. Check: cast a Fire skill, read mana and the heat stat |
| SK12 | Skill synergies, attack rating, block and speed formulas. | W:Skills | ported | `docs/formulas.md`, D:skills calcs |
| SK13 | Frame rate caps (FCR/IAS/FRW values that crashed fixed). | P1.0.0 | ported (unverified) | `docs/subsystems/stat-screen.md` |

## 10. Mercenaries

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| MC1 | Hireling gates (who is hirable and where). | W:Mercenaries | ported | verified, `hiregates`; listed |
| MC2 | Hireling starting gear. | W:Mercenaries | deviation | listed |
| MC3 | Mercenary level caps follow the player. | W:Mercenaries | ported | verified; listed (Mercenary caps) |
| MC4 | The Median mercenary changes list (sections 2.3 and 2.4 of the NPC doc). | `docs/subsystems/npc-hirelings.md` | partly | per-change statuses in that file |
| MC5 | Mercenaries have their own skill trees. | W:Mercenaries | ported | D:hireling, D:skills |

## 11. NPC services and town

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| NPC1 | All NPCs can heal you. | P2.2.0 | unknown | no NPC-heal code. Check: damage the player and talk to a vendor |
| NPC2 | Vendor stock and refresh rules. | P2.0.0 | ported | `docs/vendors.md` |
| NPC3 | Gheed gamble, Akara respec, Larzuk sockets, Charsi. | W:Vendors | ported | `npc.txt`, `docs/vendors.md` |
| NPC4 | New towns: vendors and stash in the new town areas; dying or town portal in some places lead to the new towns. | P1.1.0 | ported (unverified) | D:levels, DS1 files |

## 12. Waypoints and travel

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| WP1 | The waypoint UI shows more waypoints; acts are reachable with number keys. | P1.1.0, P2.0.0 | partly | listed (Waypoint panel, done); number-key navigation unknown |
| WP2 | Waypoints activate automatically as you walk over them. | P1.0.0 | missing | no auto-activation code. Check: walk onto a fresh waypoint and open the panel |
| WP3 | Waypoint activation applies to every player in multiplayer (bug fix). | P1.8.0 | unknown | multiplayer only |
| WP4 | Extra waypoints in endgame areas (Fauztinville etc.). | P1.1.0, P2.0.0 | ported | levels / objects data |

## 13. Maps and areas

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| MU1 | Median adds new areas (Rifts, Dungeons, Labyrinth, the new towns). | W:Areas | ported | `docs/maps-hd.md`, levels, DS1 files |
| MU2 | Map Reveal: accessible from the UI or the R key (shows Cairn Stones, shrines, Staff altar, Tal Rasha tomb, Act 5 waypoints). | P1.0.0 | missing | no map reveal code. Check: press R in a new area |
| MU3 | Area level can be displayed in the corner of the screen (optional). | P1.0.0 | unknown | no area-level HUD code. Check: look for the option in settings |
| MU4 | Terror zones removed. | docs/PLAN.md | deviation | removed in the port plan |

## 14. Player-visible UI

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| UI1 | Character sheet shows Median's stat layout (physical/magic resistance, extended stats). | P1.0.0 | partly | listed (Character sheet, approx) |
| UI2 | Infographic help panel. | D2Sigma | partly | listed (Infographic help panel, approx) |
| UI3 | New configuration menu: hotkeys, notifications, interface (always show life/mana, game name/password), video and sound. | P1.0.0 | deviation | D2R options instead; no port planned |
| UI4 | Hotkeys: nopickup (X), Map Reveal (R), transmute (C). | P1.0.0 | missing | no code (see CR5, MU2). Check: press X and walk over items |
| UI5 | Dangerous-monster alert above the life globe. | P2.10.0 | missing | Median UI |
| UI6 | Custom GUI themes (Tartarus), fonts, classic panels, 16:9. | P2.0.0 | deviation | deferred in `docs/PLAN.md` |
| UI7 | Trade window layout. | D2Sigma | ported (unverified) | listed (Trade window) |

## 15. Multiplayer

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| MP1 | Party experience award. | D2Sigma | unknown | listed (Party experience award) |
| MP2 | Shared stash on TCP/IP. | P2.0.0, P2.9.0 | unknown | see GS6 |
| MP3 | Players X scaling. | D2Sigma | unknown | see DF7 |
| MP4 | PvP arena. | D2Sigma | deviation | the arena table has no D2R equivalent and was skipped (`docs/txt-migrate.md`) |

## 16. Hardcore

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| HC1 | On Single Player a dead Hardcore character converts to Softcore. | P1.6.0 | missing | no hardcore code; see DC9 |
| HC3 | Hardcore players cannot join softcore games. | D2R | ported (D2R native) | D2R rule |

## 17. Anything else Median documents as different

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| OT1 | Higher resolution and field of view (1024x768) and glide settings. | P1.0.0 | deviation | D2R HD instead |
| OT2 | Median's strings replace D2R's. | `docs/strings-namespace.md` | ported | strings contract |
| OT3 | Custom HD art and the classic graphics mode. | `docs/PLAN.md` | deviation | deferred |
| OT4 | D2 bugfixes: overflow percentages, damage cap, str/dex glitch, 32,767 rollover of life/mana, druid immortal while morphed, ghost state after death, whirlwind chance-to-cast. | P1.0.0 | unknown | D2R fixed some of these itself; none compared. Check: raise life above 32767, cast whirlwind with a chance-to-cast item |

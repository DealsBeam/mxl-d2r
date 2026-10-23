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
| Death and corpse | 6 | 0 | 1 | 0 | 2 | 9 |
| Gold, stash and shared stash | 4 | 3 | 3 | 0 | 2 | 12 |
| Inventory, cube and belt sizes | 3 | 0 | 0 | 0 | 2 | 5 |
| Item rules | 10 | 3 | 1 | 2 | 2 | 18 |
| Crafting and the cube | 4 | 0 | 1 | 0 | 2 | 7 |
| Experience, party and level cap | 5 | 0 | 0 | 0 | 4 | 9 |
| Difficulty, area levels and monster scaling | 7 | 1 | 0 | 0 | 2 | 10 |
| Uber and quest content | 3 | 2 | 1 | 0 | 1 | 7 |
| Skills, respec and caps | 8 | 1 | 2 | 0 | 2 | 13 |
| Mercenaries | 3 | 1 | 0 | 1 | 0 | 5 |
| NPC services and town | 3 | 0 | 0 | 0 | 1 | 4 |
| Waypoints and travel | 1 | 1 | 1 | 0 | 1 | 4 |
| Maps and areas | 1 | 0 | 1 | 1 | 1 | 4 |
| Player-visible UI | 1 | 2 | 2 | 2 | 0 | 7 |
| Multiplayer | 0 | 0 | 0 | 1 | 3 | 4 |
| Hardcore | 1 | 0 | 1 | 0 | 0 | 2 |
| Anything else Median documents as different | 1 | 0 | 0 | 2 | 1 | 4 |
| **All** | **61** | **14** | **14** | **9** | **26** | **124** |

## Top 10 gaps (by player impact)

1. ~~**XP1, level cap 150**~~: fixed (lane gap-levelcap): the built `experience` table now carries Median's `MaxLvl 150` row, the engine reaches level 150 and stops there.
2. **DC1, no player corpse**: D2R still makes a corpse that must be retrieved; Median re-equips gear in town and keeps the belt. Lane `no-corpse`.
3. **SK10 and SK11, Hunter steady aim and Fire Sorceress Heat** (lane gap-classres, done): both are table driven and worked in the port except for two engine rules, now in the plugin. Steady Aim is a layered stat on a state (Steady Shot builds, Heartseeker and Cascade spend); the missing part was the "requires Steady Aim" gate (skill `restrict` modes 3-7, `skill_restrict.cpp`). Heat is the mana bar, drained by stat 297 through D2Sigma's regeneration; D2R's regeneration was missing that (`heat.cpp`). `docs/plugin-spec.md` "Skill restrictions", "Steady Aim", "Heat".
4. ~~**IT1 and IT2, durability and quantity**~~ ported and verified (lane gap-items): items no longer wear down and throwing stacks are not used up.
5. ~~**IT3, items drop identified**~~ ported and verified (lane gap-items): the generator pushes the identified flag for magic, set, rare and unique items, as Median does.
6. **DC4 and DC3, death rules**: no 5 second respawn wait, and gold on death is untested.
7. ~~**SK7, sprint and no stamina**~~: not a gap. Median's sprint is its charstats stamina (50 points, 5 seconds of running) and the port carries those columns; game check `sprint` confirms it.
8. **CR4 and DC6, trophy challenge timers** (lane gap-classres, done): Median's timer is the duration of the states that carry the challenge stat (data, native in D2R); the plugin adds the void on death or zone change and a per-trophy cube window (`trophy_timer.cpp`). Open: nothing in the data says which skill is cast on a boss kill (see CR4).
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
| DC5 | Soulstealer bosses heal (10% of max life) only when they kill a player, not minions; fire teleport and Chaos Orb heal the boss only if they kill a player (40%). | P1.1.0, P2.1.0, P2.8.0, P2.13.0 | partial | The boss rule is ported in lane p4-leftovers: plugin part `soulstealer`, 34 Soulstealer classes heal 10% of max life on the player-death call `0x424ac0` (killer argument). `game_checks soulstealer` PASS, run `checks-20261023-221828-s2` (Zagraal and Kabraxis +10.0%, an ordinary monster +0%), control with the part off PASS, run `checks-20261023-221613-s2`. Not done: the 40% fire teleport / Chaos Orb heal, Deimoss' 25%, Uldyssian's flat amount, and The Leoric (heals to full by itself); see `docs/unported.md` |
| DC6 | Dying, or entering another zone, voids an active trophy challenge. | P2.10.0 ("Dying or entering another zone with a trophy challenge will void the one you've already started") | ported | `trophy_timer.cpp`: the stat 202 (D2R 378) window is voided on death (mode 0 / 17) or an area change, with a chat line, and the cube refuses the recipe. `game_checks trophytimer` (zone change, death). See CR4 |
| DC7 | Resetting skills and stats removes every active buff. | P1.6.0 | verified | plugin part `respecbuffs` (`ClearTimedBuffs` in the respec bookkeeping hook). `game_checks respecbuffs` PASS, run `checks-20261023-185038-s3`: states 32 / 101 / 26 on before the Akara reset, off after; with the part off the states stay (run `checks-20261023-182242-s2`) |
| DC8 | Monsters flagged "no monster corpses" (labyrinth modifier, Twisted Clawstorms, Corrupted Barbarians) leave none; others do. | P1.0.0, P1.6.0, P2.11.0 | ported | monstats corpse data migrates unchanged |
| DC9 | A Hardcore character that dies converts to Softcore on Single Player. | P1.6.0 ("Fixed bugs with the Hardcore to Softcore conversion on Single Player") | ported (verified) | `hardcore_death.cpp` (check `hcdeath`, docs/unported.md). See HC1 |

## 2. Gold, stash and shared stash

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| GS1 | Gold (and waypoints) are collected by walking over them; larger pickup range. | P1.0.0, P1.8.0 | partly | D2R picks up gold by walking too (not checked for range). Waypoint auto-activation is WP2 |
| GS2 | All teleport and warping skills pick gold up. | P1.6.0 | ported (verified) | `teleport.cpp` picks up the gold within 36 sub-tiles of the arrival point after a player's Teleport; check `teleportgold` |
| GS3 | Gold drops and vendor prices were rebalanced (vendors buy cheap, potions and oils cost more). | P1.0.0, P2.0.0 | ported | D:monstats, D:treasureclassex replaced by Median's; D:npc buy mult 52 (D2R 512), sell mult 1024; stock in `docs/vendors.md` |
| GS4 | Larger inventory and stash. | P1.0.0 | ported (unverified) | `inventory.txt` gridX/gridY via `txt_migrate`; HD panels; in-game look pending (`docs/testing.md` "Median stash size") |
| GS5 | Shared stash and multi-page personal stash in Single Player. | P2.0.0 | partly | Median's panel counts "n / 10" pages in both modes (D2Sigma `0x1130a0`). `tools/install_mod.py` writes `add_shared_tabs = 7` (D2R's 3 normal shared tabs + 7 = 10) into `d2rloader.toml`; `game_checks stashpages` PASS: ItemService puts an item into tabs 0-9, tab 10 is refused. Personal stash: 2 pages (stock page + D2RLoader's custom page, "1 / 2" strip; `game_checks stashpage2` PASS; `docs/stash-pages.md`); pages 3-10 and Median's Mode / Sort buttons are not ported (`docs/unported.md` "Stash pages") |
| GS6 | Shared stash on TCP/IP had stability problems (disabled in 2.0, "potential fix" in 2.9). | P2.0.0, P2.9.0 | unknown | D2R/D2RLoader share the stash file in TCP/IP; bug #20 (stash link check) is "mostly fixed" (`docs/bugs.md`). Check: TCP/IP game with two profiles |
| GS7 | A sort button in the stash; a button in the inventory that collects every item that fits a container (AC, runes, shrines). | P2.2.0, P2.9.0 | missing | container right-click is ported (`item_spells.cpp`, pSpell 18); the two buttons are Median UI with no panel code; not built in lane stash-pages (no item size source for a sort, see `docs/stash-pages.md` section 7) |
| GS8 | Containers: Greater Signets of Learning, Enchanted Runes, Corrupted Treasures; shrines and vessels are 1x1. | P2.11.0, P2.12.0 | partly | pSpell 18 logic ported; which container items exist in the built data was not counted. Check: `item` each code, `use` |
| GS9 | Ctrl+Click moves items between inventory, stash, cube and the ground; charms can be dropped into the stash/cube while another charm is carried. | P1.0.0, P1.2.0 | ported (D2R native) | D2R quick-move. Charm limit rule: unknown |
| GS10 | Signets of Learning give 1 stat point each, capped at 400; experience signets removed. | P1.0.0 | verified | pSpell 3 with calc `min(1, 400 - stat 185)` works as written in D2R. `game_checks signet` PASS, run `checks-20261023-182242-s2`: used at 0 and 399 (+1 point each), refused at 400, stacks of 5 and 6 clip at 400 |
| GS11 | Gold costs on recipes (item designs, jewelcrafting, access scrolls). | P1.8.0, P2.1.0 | ported | `cube_conditions.cpp` gold payment; checks `goldcraft`, `goldclient` |
| GS12 | Realm services: web stash, vault, hibernated characters, bounties, Stash Stat Search, in-game ladders. | P2.13.0 | missing | realm only, Phase 7 (`docs/realm-plan.md`) |

## 3. Inventory, cube and belt sizes

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| IC1 | Backpack and Horadric Cube sizes enlarged (15x10). | P1.0.0 | ported | `inventory.txt`; HD panels by `ui_panels.py` / `ui_layouts.py` |
| IC2 | Trade window and NPC stores are larger. | P1.0.0 | ported (unverified) | `inventory.txt` Trade Page and Monster rows; listed (Trade window) |
| IC3 | Belt UI and the show-belt-rows hotkey work. | P1.2.0 | ported (D2R native) | D2R belts.txt carried |
| IC4 | Set bonuses with more than 6 pieces. | Median sets | unknown | `docs/services.md` L10: set size >6 unknown in D2R. Check: equip a 7-piece set |
| IC5 | Only one charm of a type may be carried (fixed bug: more than one charm). | P1.0.0 | ported and verified (lane p4-verify-b: check `charmlimit`, run `checks-20261023-195127-s1`) | D2R's `carry1` column plus the plugin's unique limits; the second copy of a carry1 / limited charm is refused, an unlimited charm takes two |

## 4. Item rules

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| IT1 | Items have no durability; repair, durability warnings and breakage are gone. | P2.4.0 ("Removed durability"), P2.7.0 | ported (verified; the UI half reopened and fixed by lane `no-durability`, see below) | **Wear** (plugin part `durability`, `item_rules.cpp`): D2R's wear dispatcher `0x441b10` and the skill-use wear `0x5590c0` are no-ops, the equivalent of Median's NOP of the wear call (D2Game+0xdee28). `game_checks nodurability`: 600 wear steps, weapon 110 -> 110, armour 48 -> 48 (off word: 110 -> 101, 48 -> 16). **UI** (parts `durabilitytip`, `repairui`, data `ID_PAIRS` in `tools/tbl2json.py`; check `nodurabilityui`): the tooltip line, the Charsi/Larzuk repair mode and the `trade/repair` line are gone, an ethereal item reads `Ethereal`; see "IT1 in detail" |
| IT2 | Thrown/quantity items are no longer used up by attacking or throwing; ammunition warnings gone. | P2.7.0 ("Quantity is no longer reduced by normal attack and throw") | ported (verified) | plugin part `quantity`: D2R's quantity use `0x439130` leaves the stack alone, the equivalent of Median's NOP of the `dec ebp` (D2Game+0xa16be). `game_checks nodurability`: 4 Throws, quantity 58 -> 58 (off word: 110 -> 109) |
| IT3 | Items dropped by monsters are identified (Median: no identify scrolls). | P1.0.0 | ported (verified) | plugin part `autoid`: the magic, set, rare and unique generators of Median push 1 into `SetItemFlag(item, 0x10)` (D2Game `0x104c1`, `0x15443`, `0x13daa`, the unique picker; sigma-patch-audit.md item 1); the plugin calls D2R's `SetItemFlag` `0x36d8f0` after the item generator `0x442d60` for qualities 4-7. `game_checks autoid` (Andariel kills): 6 of 6 drops identified (off word: 0 of 6) |
| IT4 | Repair All with a hotkey at the smith (before durability was removed). | P2.0.0 | deviation | superseded by IT1: the repair buttons, repair cost and the `trade/repair` menu line are removed in the port as in Median (IT1 in detail) |
| IT5 | Ethereal and socket rolls, ethereal drop chance, ethereal durability penalty. | P1.1.0, D2Sigma | partly | `itemrolls`; listed (Ethereal roll approx, Socket roll verified). The durability penalty stays (Median's make-ethereal function multiplies the maximum by 0.75; it is invisible without the durability line, `itemrolls` still asserts it). The ethereal text is `Ethereal`, not `Ethereal (Cannot be Repaired)` (IT1 in detail) |
| IT6 | Class-locked items. | D2Sigma | ported | verified, `classlock`; listed |
| IT7 | Resist cap (physical cap bug fixed, 75%/max) and absorb. | P1.6.0 | ported | verified, `resistcap`, `absorb`; listed |
| IT8 | Andariel catalyst and bonus drops. | D2Sigma | ported | verified, `bonusdrops`; listed |
| IT9 | Item colour dyes. | D2Sigma | ported | verified; listed (Dye) |
| IT10 | Gamble: more item types on the screen (rings, amulets), custom prices (rare 25000, unique 1000), refresh button (hotkey R) and a highlight filter, independent per player. | P1.0.0, P2.0.0, P2.9.0 | mostly ported | engine part ported (lane pa2-gamble, check `gamble`): 32 offers (byte patch `0x541a7e`, off word `gamble`), ring and amulet first, chances rare 25000 / set 1000 / unique 1000 from D:difficultylevels `GambleRare` / `GambleSet` / `GambleUnique`, one list per player, redrawn on every request; stock in `docs/vendors.md`. Not ported: refresh button, hotkey R and highlight filter (Median UI, custom art, last; `unported.md`) |
| IT11 | Enhanced Damage / Defense shown next to damage and defense on the item. | P2.9.0 | unknown | item tooltip text; listed (Item tooltip text) |
| IT12 | Item level line hidden on 10 item types. | itemtypesext `NoShowILvl` | ported (unverified) | listed (Item level line) |
| IT13 | Item event ids (use, drop, equip hooks). | D2Sigma | partly | listed (Item event ids; id 32 open) |
| IT14 | Loot filter lists and rules. | D2LootFilter | deviation | listed (Loot filter fields) |
| IT15 | Tiered uniques, sacred uniques with varying level requirements and ilvls, tiered runewords, superior items. | P1.0.0, P2.0.0 | ported | tables replaced by Median's |
| IT16 | Item drop level uses the area level the monster was found in, not the town's. | P2.3.0 | verified | D2R's default item level of a monster is the monster's own level (`0x43e8a0`), set from the area; a trip to town does not touch it. `game_checks dropilvl` PASS, run `checks-20261023-221828-s2`: drops in areas 2 and 76 in Normal and Hell read item level = area level (1, 29, 100, 105), with and without a town trip. The earlier PARTIAL was a wrong reader in the harness (`0x3730d0`, always 101), now `0x36ccf0` |
| IT17 | Shrines/runes/containers right-click use. | P2.11.0 | ported | pSpell 18 in `item_spells.cpp` |
| IT18 | Halloween chest treasure class (`Treasureclass_H`). | objects2 | missing | listed (Halloween chest `Treasureclass_H`) |

### IT1 in detail: durability in the UI (lane `no-durability`)

**What a Median player sees: no durability anywhere.** The item has the stats (a base has `durability`, an item has stat 72 current
and 73 maximum, ethereal items are made x0.75), nothing shows them and nothing uses them.

| Where | Median | The port before this lane | The port now |
|---|---|---|---|
| Wear | the wear call is NOPed (D2Game+0xdee28); the stats stay | `0x441b10` / `0x5590c0` no-ops (PR #114, part `durability`) | same |
| Item description | no `Durability: x of y` line: `engine/D2ItemDesc.cpp` never loads string `ItemStats1d` (id 0xd81); stats 72 / 73 have no description in `itemstatcost`, stat 152 (indestructible) has no `descfunc` | the line was drawn by D2R's `0x2c1640` | part `durabilitytip`: `0x2c1640` is a no-op |
| Repair at Charsi / Larzuk | `ui/Merchant.cpp` has no repair button, no repair-all button, no repair cost; `ui/NpcMenu.cpp` never loads the `trade/repair` line (0xd06), the smiths' menus read `trade` (0xd44) | repair panel mode (dword `+0x168` = 1 of the vendor panel, set at `0x2412de`) with `button_repair`, `button_repair_all`, `background_repair`; menu line `trade/repair` | part `repairui`: the mode byte is patched 1 -> 0 (the smiths open the plain trade panel) and the 0xd06 ids of the NPC menu records are rewritten to 0xd44 before each menu is built |
| Ethereal | "Ethereal" (the item is not repairable anyway, there is nothing to say about repair); the durability x0.75 stays | `Ethereal (Cannot be Repaired)` (string 22745) | data: `tools/tbl2json.py` `ID_PAIRS` overrides `strethereal` with `{}` and `strItemModEtherealSocketed` with `{}, Socketed (%i)` |
| Indestructible, breakage warning | no line, no icon (nothing can break) | no durability to lose, so nothing breaks; D2R's own warning icon has nothing to show | nothing to do |

What Median **keeps**: the base durability columns of `armor` / `weapons`, stat 72 / 73 on items, the ethereal x0.75 (`itemrolls`
keeps asserting 75%), mercenary items. None of it is visible, it only costs the player nothing.

**Why the port missed it.** The port was built from [sigma-patch-audit.md](sigma-patch-audit.md), which lists the patches D2Sigma
applies to the *original* 1.13c DLLs. The wear call is such a patch (item 12, game6-016), so wear was ported and verified. The
tooltip line, the repair button and the menu line are not patches: they are things Median's **own** code
(`engine/D2ItemDesc.cpp`, `ui/Merchant.cpp`, `ui/NpcMenu.cpp`) *does not draw*. A thing a replacement leaves out leaves no row in
any table, so no checklist derived from the tables could contain it. The buff icon bar (lane `buff-icons`) was the same blind
spot. The note "Limits of this audit" in sigma-patch-audit.md says so and says how to look for the next one (strings Median's
code never loads, widget names per panel, Median's own screenshots panel by panel).

**How the port does it** (plugin `item_rules.cpp`, pure logic `RetitleRepairLines` in `item_rules_logic.h` with unit tests; off
words `durabilitytip` and `repairui`, the `mxl` line reports `tooltip line on (0x2c1640), repair ui on (0x2412de), N menu lines now
read trade (0x2a48830)`). The tooltip hook is an inline hook of 13 bytes at `0x2c1640` (`48 89 5c 24 20 56 48 81 ec 40 04 00 00`, whole
instructions, no rip-relative operand), the vendor mode is a 4-byte immediate patch with original-bytes verification, and the NPC menu
records (table `0x2a48830`, stride 0x48, ids `u16[8]` at `+0x08`) are rewritten in place so the click handler `0x112f30` still
opens the same panel.

**Evidence** (`py -3 tools/game_checks.py --slot 2 nodurabilityui`, harness verb `itemdesc <id> [eth]` builds the item description with
D2R's own `0x2be560` and prints it): stats 72 / 73 > 0 on a Field Plate, a Battle Sword, a magic Field Plate and an ethereal one;
none of the four descriptions mention durability, repair or indestructible; the ethereal one reads `Ethereal`; Charsi's menu has
0xd44 and no 0xd06; the strip of the panel below the item grid differs from Gheed's trade panel by 0.13 (mean pixel difference), where
the control run with both parts off shows `Durability: 48 of 48`, 0xd06 in Charsi's menu and a diff of 5.97. Median's own tooltips
for comparison: screenshots of Median 2.14.4 inventory tooltips taken by this lane show no durability line on armour or weapon.

## 5. Crafting and the cube

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| CR1 | About 7500 recipes: item designs, upgrades, runeword and honorific rerolls, shrinecrafting. | P1.0.0, P2.0.0 | ported | D:cubemain from `txt_migrate`; check `cubemain` |
| CR2 | Median cube conditions (ops 29-42: gold payment, craft guards, class conditions). | D2Sigma | ported | `cube_conditions.cpp`; `docs/plugin-spec.md` |
| CR3 | Craft guard flags: once per game or per character. | D2Sigma | ported | ops 38, 39, 41 |
| CR4 | Trophy challenge charms: kill the boss and cube the charm within the limit (10 seconds unless stated, e.g. 25 s); death or zone change voids it. | P2.10.0 | ported (kill trigger open) | Median has no cube timer op: the recipes (op 6, param 202) compare the stat that 15 timed aura states put on the player (`auralencalc` 250 frames = 10 s), so the timer is the state duration. Ported: the cube condition on stat 378 also needs the plugin's window (in time per trophy, same area, alive; `docs/plugin-spec.md` "Trophy timer"), `game_checks trophytimer` (in time passes, 12 s later refused). Open: which skill a boss kill casts is in no table, so the real kill path is untested (the check sets the stat) |
| CR5 | Cube transmute hotkey (Median's default V; here K). | P1.0.0 | verified | D2Sigma `0x69820` transmutes while the cube panel is open. `travel.cpp` InputService action (default K) calls the panel's Transmute `0x2cd720` only while the panel is open. `game_checks cubehotkey` PASS, run `checks-20261023-182242-s2`: cube closed -> press counted, nothing transmuted; cube open -> transmuted (Wirt's Leg portal). The check runs the plugin's handler through the `hotkey` harness verb; the key delivery is `game_checks hotkeys` |
| CR6 | The "Cow Level" portal can be made in any Act town. | P1.0.0 | verified | D2R's cow handler is act 1 only; the plugin makes the portal in the other towns (`CubeCowPortal`), turns the creator's act compare (`0x432dd5`) into a no-op and takes cross-act portals through the NPC travel `0x4f2e40`. Check `cowtowns` (run `checks-20261023-210200-s3`): portal, walk-in (area 39, Hell, level 110 as Median's table says) and way back in acts 2-5 |
| CR7 | Imbue, socket and personalize rewards highlight the eligible items. | P2.0.0 | missing | Median UI |

## 6. Experience, party and level cap

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| XP1 | The maximum character level is 150. | P1.0.0 | ported | fixed in lane gap-levelcap: `tools/txt_migrate.py` relabels Median's `MaxLvl` row, so it replaces D2R's `MaxLvl 99` row (it was decoded as level -1 / `4294967295` and D2R's row was appended last, capping the engine at 99). Verified: `levelcap` check (levels 99, 120, 149, 150; the level stops at 150; stat and skill points per `charstats`; Save and Exit and Play keeps level 150; `docs/testing.md`) |
| XP2 | Reworked experience formula; level 100 by the start of Hell; no huge gaps at 121-125. | P1.0.0 | ported (unverified) | D:experience rows and ExpRatio |
| XP3 | Skill and stat points per level. | P1.0.0 | ported | D:charstats `SkillsPerLevel`, `StatPerLevel` |
| XP4 | Kill experience: reworked level-difference penalty. | P2.9.0 | ported (unverified) | inline hook `0x44fef0`, check `xp`; listed (`docs/services.md` I3) |
| XP5 | Party kill experience is awarded without a split. | D2Sigma | unknown | listed (Party experience award, multiplayer) |
| XP6 | Shift+Click on a stat point adds 25. | P1.1.0 | verified | plugin part `statclick` (`stat_rules.cpp`, hook on the packet 0x3a handler `0x4b3c70`: count 4 -> 24). `game_checks statclick` PASS, run `checks-20261023-185038-s3`: count 0 spends 1, count 4 spends 25 on all four stats, 7 points left spends 7. The client's own shift-click was not driven (the harness sends the packet) |
| XP7 | Unlimited respecs until level 50 after Den of Evil on Normal. | P2.0.0 | verified (free respec); limit above 50 not found | `0x5d9ae0` replaced as D2Sigma's `0x9e600`: at 50 or below the token is not taken, above 50 it is put back. `game_checks freerespec` PASS, run `checks-20261023-185038-s3`: 3 respecs at level 20, 2 each at 50, 51, 60, all offered. Nothing in the port or in D2Sigma's tables limits a respec above level 50 (see `docs/unported.md`) |
| XP8 | Extended stats screen: sum of all stats, damage, poison length reduction by difficulty penalty. | P2.0.0, P2.9.0 | ported (unverified) | `docs/subsystems/stat-screen.md`; listed (Character sheet approx) |
| XP9 | Grit: Strength grants a stat that reduces damage taken. | P2.11.0 | verified | the class passive's data formula plus a passive refresh after a Strength point (`StatClickHook`, `stat_rules.cpp`). `game_checks grit` PASS, run `checks-20261023-194315-s1`: Strength 30 / 100 / 150 / 200 / 300 at level 20 -> Grit 4 / 14 / 21 / 25 / 25 (the formula); a shift-click on Strength moves it 14 -> 17. See `docs/unported.md` |

## 7. Difficulty, area levels and monster scaling

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| DF1 | Resist penalty -30 (Nightmare) and -70 (Hell) for players. | difficultylevels | ported | D:difficultylevels |
| DF2 | Life and mana steal divisors 1/4/9 and 6/24/48. | difficultylevels | ported | D:difficultylevels |
| DF3 | Area levels: Nightmare 51-100, Hell 105-120, endgame 105-150; rifts and dungeons start at 105. | P1.0.0 | ported | D:levels, D:monstats, D:monlvl |
| DF4 | Monster immunities replaced by absorb (25/50/75% by difficulty); monster resists 0% Normal, 15% elemental Nightmare, 10%/35% Hell. | P1.0.0 | ported | D:monstats; verified (`absorb`); listed |
| DF5 | Monster unique affixes, champion/minion density, Median-only mods. | W:Monsters | ported | D:monumod, D:levels |
| DF6 | Elite and uber boss behaviours (summons, phases). | W:Bosses | partly | boss AI is D2Sigma's AI table module (`docs/subsystems/monsters-ai.md`) |
| DF7 | Players X: monster count and life scale with party size. | D2Sigma | ported | lane pa2-monsters: hook `0x543e90`, off word `playersx`; check `monlevel`: 8 players vs 1 in Hell, life x4.49, experience equal |
| DF8 | Hell difficulty needs the previous difficulty completed; difficulty names are vanilla. | P1.0.0 | ported | D2R |
| DF9 | Monsters do not drop guaranteed uniques on Nightmare/Hell act bosses. | P1.0.0 | ported | treasure classes replaced by Median's |
| DF10 | Monster level for skill/hit formulas uses the area level the monster was found in. | P2.3.0 | verified | the plugin's `monlevel` part (monsters take the area level in every difficulty); `game_checks dropilvl` reads monster level 1 / 29 (Normal) and 100 / 105 (Hell) in areas 2 and 76, with and without a town trip, and the drops of those monsters take the same item level, run `checks-20261023-221828-s2`. The skill and hit formulas that read it were not run |

## 8. Uber and quest content

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| UQ1 | Dungeon entry lockouts and the in-game lock timer. | P2.0.0 | ported | checks `lockout`, `lockclock`, `lockquest`; listed (item-teleport clock start open) |
| UQ2 | Access scrolls, keys, signets, oils, riftwalks, rune and crystal systems. | W:Items | ported | tables and `item_spells.cpp` |
| UQ3 | Goblins and treasure goblins. | P2.2.0 | ported | `goblins` check; listed (sighted packet approx) |
| UQ4 | Quest reward notices. | P2.0.0 | partly | listed (Quest reward notices, approx) |
| UQ5 | Some zones disable town portals (Edge of Nothingness corridor). | P1.6.0 | ported and verified (lane p4-verify-b: check `tplock`, run `checks-20261023-195127-s1`; control `checks-20261023-200935-s3`) | Tplock column of levels.txt (22 levels) read by a hook on the Town Portal skill; off word `tplock` |
| UQ6 | The ubers and endgame events work without a dedicated module. | `docs/subsystems/ubers-events.md` | partly | data + item spell 16 + lockout + goblins |
| UQ7 | Seasonal/Halloween/Christmas events. | P2.4.0, P2.13.0 | missing | listed (Halloween chest) |

## 9. Skills, respec and caps

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| SK1 | Skill points cap (max base level) and soft levels. | P1.0.0 | ported | D:skills |
| SK2 | Akara respec with a confirmation dialog. | P1.6.0 | ported | check `respec`; listed (Akara respec confirm, done) |
| SK3 | Skills cost no mana in town. | P2.1.0 | ported (verified; mechanism differs) | plugin part `townmana`: the mana cost hook returns 0 at the charge sites when the caster is in a town; it needed a caster hook on `0x43acb0` because D2RLoader re-assembles the server charge function (docs/plugin-spec.md, Mana costs). `game_checks townmana`: town 5000 -> 5000, field -> 4993 (off word: 4993 in town) |
| SK4 | Many skills castable in town (buffs, totems, warp, Shadow Refuge, Acid aura). | P1.1.0, P1.2.0, P1.7.0, P2.1.0 | ported and verified (lane p4-verify-b: check `towncast`, run `checks-20261023-195127-s1`) | `InTown` column carried from Median; 3 class-free InTown skills start in town, 12 others are refused and start in the field. The 8 Warlock InTown skills are not judged (need a Warlock character) |
| SK5 | Town Portal is an innate skill (no scrolls) with a 10 s cooldown, and a 1 second delay when opening. | P1.0.0, P2.2.0 | partly | D:skills town_portal srvdofunc 109, localdelay 250; the 1 s delay is not checked |
| SK6 | Skill cooldowns are computed dynamically; skill groups no longer share cooldowns. | P1.0.0 | ported | D2Sigma's cooldown start: one timer per skill or per skills2 cooldown group, no unit-wide delay state; `cooldowns.cpp` ports it on D2R's cooldown starts. Check `cooldowns` (PASS 2026-10-22, Blast Wave (group 4, localdelay 500): 494 frames without and 390 with stat 309 = 100, the six other group skills got the same end frame; server start only, the client twin is not reached by the harness cast) |
| SK7 | Run/walk toggle is renamed "Toggle Sprint"; a sprint lasts about 5 seconds and is much faster than walking. The patch note's "stamina removed" means the vitality and level stamina gains, not the stamina bar. | P1.0.0, patch strings, D:charstats | ported (verified) | Median's sprint is data: `charstats` stamina 50, WalkVelocity 10, RunVelocity 15 (Barbarian 14), RunDrain 60, StaminaPerLevel and StaminaPerVitality blank, built identical to Median's. D2Game's drain (RunDrain x2 per frame, forced to walk at 0) is vanilla 1.13c code in both games; there is no sprint state or skill and no cooldown timer (the bar refilling is the cooldown). Game check `sprint` (exact, per frame): run from full drains exactly 120 raw per frame with no regeneration = 107 frames (4.3 s), walking from empty gains exactly 37 raw per frame, standing exactly 50, run/walk speed about 1.3 (data 1.40), a run starts at 2 points of stamina, R toggles run and walk. Median's stamina regen is D2Sigma's `0x100a1f90` (decompiled), ported by `stat_rules.cpp` (docs/sigma-patch-audit.md item 15; game check `manarules`): standing 100% of max / 256 raw per frame, walking and other modes 75%, running 0, state 136 (Stamina Shrine) fills the bar |
| SK8 | Most melee skills can be used with Shift without a target. | P2.3.0 | ported (verified) | native in D2R 3.3 (Shift + click runs the skill at the ground point); check `meleenotarget` for three melee skills |
| SK9 | Skill tree UI: base/max levels, Alt shows soft level, prerequisite arrows, Shift+Click maxes a skill. | P2.0.0 | missing | Median UI, no panel code |
| SK10 | Druid bow "steady aim" pseudo-resource. | P1.7.0 | ported | stat 220 layer 5 on state `sk1296_state`: Steady Shot +1 (cap 6), Heartseeker -1 per hit, Cascade all; the "requires" gate is the new restrict mode 5 (`skill_restrict.cpp`). `game_checks steadyaim` (4 shots read 1-2-3-4, Heartseeker usable only with charges, spends to 0) |
| SK11 | Fire Sorceress "Heat" replaces mana (degenerates outside town). | P2.13.0 | ported (no UI relabel) | stat 297 (Molten Core's loss rate) turns D2R's mana regeneration into a drain, negative 297 from fire skills builds it, none in town (`heat.cpp`). `game_checks heat`. The bar is still labelled mana; Emberstep was not cast in the test |
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
| NPC1 | All NPCs can heal you. | P2.2.0 | ported and verified (lane p4-verify-b: check `npcheal`, run `checks-20261023-195127-s1`; control `checks-20261023-200935-s3`) | the heal flag of all 43 NPC table entries is set by the plugin (off word `npcheal`) |
| NPC2 | Vendor stock and refresh rules. | P2.0.0 | ported | `docs/vendors.md` |
| NPC3 | Gheed gamble, Akara respec, Larzuk sockets, Charsi. | W:Vendors | ported | `npc.txt`, `docs/vendors.md` |
| NPC4 | New towns: vendors and stash in the new town areas; dying or town portal in some places lead to the new towns. | P1.1.0 | ported (unverified) | D:levels, DS1 files |

## 12. Waypoints and travel

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| WP1 | The waypoint UI shows more waypoints; acts are reachable with number keys. | P1.1.0, P2.0.0 | partly | listed (Waypoint panel, done); number-key navigation unknown |
| WP2 | Waypoints activate automatically as you walk over them. | P1.0.0 | ported | D2Sigma `D2AutoPickup.cpp` `0x4c7e0`: used when the distance is below 36 sub-tiles after a walk. `travel.cpp`: server scan every 5th frame, bit set with `0x3d6890`; `game_checks wpauto` PASS (distance 3 and 25 set it, 47 does not) |
| WP3 | Waypoint activation applies to every player in multiplayer (bug fix). | P1.8.0 | ported (unverified) | the scan walks the game's client list and handles every player in range; no two-player run yet |
| WP4 | Extra waypoints in endgame areas (Fauztinville etc.). | P1.1.0, P2.0.0 | ported | levels / objects data |

## 13. Maps and areas

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| MU1 | Median adds new areas (Rifts, Dungeons, Labyrinth, the new towns). | W:Areas | ported | `docs/maps-hd.md`, levels, DS1 files |
| MU2 | Map Reveal: accessible from the UI or the L key (Median's default; the first port's R is D2R's run toggle) (shows Cairn Stones, shrines, Staff altar, Tal Rasha tomb, Act 5 waypoints). | P1.0.0 | partly (verified) | D2Sigma `0x887c0` reveals the act once per game. `travel.cpp`: L calls D2R's debug Reveal Automap `0xd42b0` on the current level (preset objects included). `game_checks mapreveal` PASS, run `checks-20261023-191456-s1`: Harrogath entered without debug mode, current-layer cells 15 -> 2269, a second press +0. The earlier PARTIAL was the check's (town maps arrive whole, the all-layer count mixed in layers the call creates). Not Median's act-wide walk; no UI button |
| MU3 | Area level can be displayed in the corner of the screen (optional). | P1.0.0 | not ported (client drawing); the data it shows is verified (lane p4-verify-b: check `arealevel`, run `checks-20261023-195127-s1`) | the corner text is UI, listed in docs/unported.md |
| MU4 | Terror zones removed. | docs/PLAN.md | deviation | removed in the port plan |

## 14. Player-visible UI

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| UI1 | Character sheet shows Median's stat layout (physical/magic resistance, extended stats). | P1.0.0 | partly | listed (Character sheet, approx) |
| UI2 | Infographic help panel. | D2Sigma | partly | listed (Infographic help panel, approx) |
| UI3 | New configuration menu: hotkeys, notifications, interface (always show life/mana, game name/password), video and sound. | P1.0.0 | deviation | D2R options instead; no port planned |
| UI4 | Hotkeys: nopickup (X), Map Reveal (L), transmute (V). | P1.0.0 | partly | Median's table at `0x1d9110`; the three are InputService actions (Controls menu, rebindable); defaults X, L and K (V is a D2R action; R and C, the first port's keys, are D2R's run toggle and Character screen: a press did both); `game_checks hotkeys` checks that each press moves only its own counters and that no plugin key is in the character's D2R key file. X gates the plugin's waypoint auto-activation (D2R has no auto-pickup to switch) |
| UI5 | Dangerous-monster alert above the life globe. | P2.10.0 | missing | Median UI |
| UI6 | Custom GUI themes (Tartarus), fonts, classic panels, 16:9. | P2.0.0 | deviation | deferred in `docs/PLAN.md` |
| UI7 | Trade window layout. | D2Sigma | ported (unverified) | listed (Trade window) |

## 15. Multiplayer

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| MP1 | Party experience award. | D2Sigma | unknown | listed (Party experience award) |
| MP2 | Shared stash on TCP/IP. | P2.0.0, P2.9.0 | unknown | see GS6 |
| MP3 | Players X scaling. | D2Sigma | ported | see DF7 |
| MP4 | PvP arena. | D2Sigma | deviation | the arena table has no D2R equivalent and was skipped (`docs/txt-migrate.md`) |

## 16. Hardcore

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| HC1 | On Single Player a dead Hardcore character converts to Softcore. | P1.6.0 | ported (verified) | see DC9 |
| HC3 | Hardcore players cannot join softcore games. | D2R | ported (D2R native) | D2R rule |

## 17. Anything else Median documents as different

| ID | Rule | Source | Status | Port evidence / check idea |
|---|---|---|---|---|
| OT1 | Higher resolution and field of view (1024x768) and glide settings. | P1.0.0 | deviation | D2R HD instead |
| OT2 | Median's strings replace D2R's. | `docs/strings-namespace.md` | ported | strings contract |
| OT3 | Custom HD art and the classic graphics mode. | `docs/PLAN.md` | deviation | deferred |
| OT4 | D2 bugfixes: overflow percentages, damage cap, str/dex glitch, 32,767 rollover of life/mana, druid immortal while morphed, ghost state after death, whirlwind chance-to-cast. | P1.0.0 | verified for the engine parts (lane p4-verify-b: check `bugfixes`, runs `checks-20261023-195127-s1` and `checks-20261023-200935-s3`) | percent overflow, damage cap, 32,767 rollover, death in a shapeshift state, ghost state and aura after death are fine in D2R; the str/dex glitch is absent in D2R (lane p4-quests-verify: check `strdex`); whirlwind chance-to-cast and real shapeshift immortality could not be reached with the harness: D2R refuses Median's class skills with usable code 5 (docs/unported.md) |

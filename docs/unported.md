# Unported, approximated and different from Median

Rule (owner, 2026-10-21): the port follows Median XL, not D2R, wherever the two differ (respecs, drops, caps, ...).
Anything that cannot be done yet is listed here instead of being dropped. Each lane appends its gaps. Things removed or
deferred on purpose (classic mode G, terror zones) are in [PLAN.md](PLAN.md) "Removed or deferred for now".

Status: **open** (not ported), **approx** (works, but not exactly as Median), **deviation** (deliberately different,
with the reason), **verify** (ported, Median behaviour not yet confirmed in game).

## Engine (plugin)

| Area | Median | Port now | Status | Needs / notes |
|---|---|---|---|---|
| Monster AI `andariel_ai` (47 monsters: Andariel, Maghda, Radament, Zoltun Kulle, Lilith, Scholars, Malthaels, ...) | own AI routine | native port as AI id 155 (`monster_ai.cpp`, getter hook); "medianai" off word = D2R's Andariel AI | verify | in-game check pending; not ported: the controlled-monster branch and the dead phase 2 ([monsters-ai.md](subsystems/monsters-ai.md)) |
| Summoner AI | first skill branch by an `aip3`% roll | same rule, two byte patches (`0x5b4e49`, `0x5b4fed`) | verify | [monsters-ai.md](subsystems/monsters-ai.md) |
| MosquitoNest init | Median init | native init written into the AI table entry 83 | verify | one enabled monster |
| Max resistance cap | 90 | 90 (immediate of D2R's clamp patched in the per-hit resist step 0x4523e0, the character sheet and the hireling panel; mercenaries use the same branch) | verify | verified in game on the player (fire resist 300: 75 / 85 / 90 caps took 25 / 15 / 10% of the damage); mercenary not exercised ([combat-stats.md](subsystems/combat-stats.md) section 2) |
| Absorb stats 184/189/417 (D2R 369/373/446) | each absorbed amount that heals the defender is cut by 189 (divisor), 184 (percent) and 417 (percent per damage type) | same, hook on D2R's absorb step 0x450670 | verify | verified in game (player with absorb 40%: the credit drops with 446 layer 1 = 50). Not rolled by items in general: monster stats from monprop (`time` / `stam` rows, 439 / 965 uses) and skills; stat 184 also on two runewords and the Barbarian hireling. Median's second helper 0x10057c10 (the same stats on the damage fields) has no caller and is not ported |
| Mercenary caps | resist cap 90, avoid / dodge / evade caps (not the block cap) | resist: yes (same branch as players); avoid / dodge / evade: the 0x44ef70 hook accepts hirelings | verify | no mercenary was hired in the test; build and logic verified only ([combat-stats.md](subsystems/combat-stats.md) section 2) |
| Andariel catalyst / Act 2-5 bonus drops | treasure class dropped once per player on the ground | same: D2R's TC executor `0x441300` once per player in the game, at the victim (monstats word +0xc0 + difficulty * 0x10); the inventory item stays as the fallback when the class cannot be resolved | verify | Andariel checked in game (catalyst on the ground, still one after a second kill); Summoner / Izual use the same helper, not run in game; multiplayer count not run |
| Quest reward notices (client messages 0x5f-0x61) | Median's popup / message | a line in the message log ("You have received: ...", neutral wording) | approx | Median's text is not in its string tables: the client handler of the pending message (D2Game `0xb15d0` stores id and subject at `+0x6e` / `+0x70`, flag `0x400` at `+0xc4`) was not found and no "received / reward / crystal" string fits, so the client probably shows an item popup (art, last); a runtime trace of message 0x61 would settle it; sender: [d2r-addresses.md](d2r-addresses.md) section 13 |
| Quest 5 repeat guard (`0x9e8b0`) | Median guard | not ported | open | purpose unclear in D2R ([quests-requirements.md](subsystems/quests-requirements.md) 5b) |
| Quest 8 event-2 handler (`0x5a2830`) | Median handler | not read | open | |
| Cain crafting materials | per-item submenu (4 entries) | a submenu: the "crafting materials" click asks the server which entries' requirements hold (plugin message `N`), the client closes the list and builds it again with those entries in Cain's record (record restored afterwards); each entry runs its own slot | done | checked in game: the `cainsub` check passes (first Cain talk: Talk / crafting materials / Cancel; the click shows Deckard Cain / Horadric Cube / Cancel; `menu 0` gives the cube); [npc-hirelings.md](subsystems/npc-hirelings.md) 1.5, [d2r-addresses.md](d2r-addresses.md) sections 9 and 13 |
| Cain menu line visibility (bug #23) | shown | added in code, never seen on screen | verify | look at Cain's menu in game |
| Craft player flags | keyed by player name | keyed by player name (character name at player data +0, fallback unit id) | verify | checked in game (flags survive a unit id change); a second character of the same name in one game would share them, as in Median |
| Craft per-game flags reset | game end | reset on the game-end event (`game_end.cpp`, client teardown `0x486be0`), plus the old game pointer / frame guard | verify | event fires once per left game (checked: games ended 0 -> 1); `0x4057c0` has a rip-relative prologue and is only called by the idle sweep |
| Gold payment client update | Median sends the update | assumed D2R stat sync | verify | stat-update packet sender not located |
| Dungeon lockout client timer and popup (packet 0x88, message 0x13) | DungeonWidget with a running timer, popup | a live countdown "Closing in: mm:ss" + "ONCE YOU EXIT YOU CANNOT GO BACK" drawn with D2RLoader's OverlayService from the plugin message `T` (level, ms left) while the player stands in the clock's level; log lines in Median's words stay (refusal reasons, "This area is locked", the clock at its start); no popup art | verify | the overlay calls run and succeed (plugin log), but the text was not seen in the harness's window captures (PrintWindow of the background window shows only the chat line, not the overlay): look at it in a normal game window; text only, vanilla look: Median's DungeonWidget frame and the popup are custom art, last; [ubers-events.md](subsystems/ubers-events.md) 3a |
| Dungeon lockout: item-teleport clock start (`0x7fb70`), quest-table condition | yes | no | open | |
| Goblin "sighted" packet | client notice (packet 0x5a type 0x15) | a message-log line "Treasure Goblin appears!" in Median's wording: the client stores the unit id (handler `0x91a50`) and `0x90f00` (D2Notifications.cpp) shows the unit name + string 12320 `StrPushNotifGoblinAppear` " appears!" as a push notification (icon type 10) | approx | the line carries the generic name (the server has no client-side unit name; the variants' own monstats names are not resolved); Median's notification HUD entry (`HUD:PushNotification*` layouts, art `game\hud\push-notifications`) is custom art, last |
| Hireling starting gear (Head/Torso/Weapon/Shield/Share columns) | per hireling row | D2R's own look; a fresh hire has no items | deviation | checked in game: a new Rogue carries 0 items in its inventory and still draws a bow, so D2R's gear is a visual taken from its own class data, not items to create; the columns have no D2R counterpart. Share is converted |
| Hireling hire/equip gates (Median ids 0x4c/0x50/0x51 and the hireclasses lists) | item type ids: potion types and weapon / shield types the class may wield | ported: weapon / shield types by class refused in the `0x36bc50` hook (`MercClasses`, `item_gen.cpp`); class-locked items check the merc's `equivalentcharclass` | verify | checked in game on the Rogue (bow allowed, crossbow, spears, swords, polearms, scythes, axes, maces, shield refused); the other three classes are unit-tested only; potion branch (`0x672a0`) not ported |
| Tyrael travel quest gate | quest 28 | D2R quest 26 | approx | harmless (portal, no menu line) |
| Item event ids 11/12/32/33/34, immunity checks on open wounds / crushing blow (monster stat 373) | attacker-takes-fire/cold/magic damage, flat life/cast on event (33/34), monsters with stat 0xbd immune to 15/16 | none | open | D2R dispatcher of itemeventfunc not found (stat record +0x48/+0x4a; the registration is not in the main image's word reads or in D2RCore.dll); see [items-xp.md](subsystems/items-xp.md) section 5 for the search done and the candidates ruled out; reproducible in-game test: `py -3 tools/game_checks.py equipevents` (`h equip` with stat 396 / 397: the owner's stat stays 0) |
| Class-locked items (stat 492) | requirement check fails for other classes (players and mercenaries) | players and mercenaries (hook 0x36bc50; a merc counts as its `equivalentcharclass`) | verify | equip a locked unique on two classes and on a mercenary |
| Ethereal roll (item level 100+) | `rand(1000) <= qualities word2` (2.6%, quality 1 included), +25% stats, 75% durability | same rate/effect, but D2R's gate (5%, quality 1 and 5 excluded) stays in front; low quality never ethereal | approx | replace the `0x442d60` block fully to allow quality 1 |
| Socket roll (quality 1-3) | `rand() % 7` sockets, 6 of 7 items | same, through D2R's setter (size clipping); gen-forced sockets keep D2R's code | verify | compare socket rates in game; Median's quality 5-9 branch (6 sockets) is dead in the 1.13c call sites |
| Dye colour | stat 183 paints the item | stat 368 through the item colour hook 0x3710d0 | verify | look in game; dyes 16-21 may need Send Bits 6 on stat 368 |
| Party experience award (`0x5e830`) | every member in range gets the full award, range about 179 units | same: hook of the party iterator `0x506f90` pays each collected member through `0x44ece0` / `0x44f2e0` and zeroes the count so the kill function `0x44a3b0` skips its pool-and-split; range constant `0x44fe7b` 0x1900 -> 0x7d00 (off word `partyxp`) | verify | multiplayer only; not run with two players ([items-xp.md](subsystems/items-xp.md) section 5); the collect callback's member skip flag (`+0x124 & 0x10000` vs 1.13c `+0xc6 & 1`) not compared |

## Data and UI

| Area | Median | Port now | Status | Needs / notes |
|---|---|---|---|---|
| Waypoint panel: every Median waypoint under its own act (up to 27 per tab) | 10/11/12/4/10 per act | done (plugin hook of the tab fill + relocated button table, HD layout stride 100) | done | [areas-drlg.md](subsystems/areas-drlg.md) section 3b; a travel click from a real waypoint object is not yet checked in game; the panel patch is HD only (SD layout not touched) |
| Character sheet | portrait, pages 2-3 art | page 0 art; pages 2-3 as rows of D2R's Advanced Stats panel | approx | plugin panel for Median's own page art |
| Stat pages: Str/Dex damage bonus, total / % bonus / base columns | computed by code | missing | open | plugin ([stat-screen.md](subsystems/stat-screen.md)) |
| Skill tree: Sorceress 9th tab | 9 tabs | 9 for the Sorceress (Coven, Reward), 8 for the others; the layout has 9 tab cells, the 9th is blank for an 8-tab class and the plugin ignores a click on it (HD and classic checked in game 2026-10-22) | done | plugin hooks the tab setter ([plugin-spec.md](plugin-spec.md) "Skill tree pages") |
| Skill tree: `RogueMissile` row at Warmonger 6,1 | Fortress there | Fortress (the row is Median's Barbarian Fortress, skill 336, named `RogueMissile` only by the vanilla-name rule); class-less skills sharing its skilldesc row get no tree slot; validate.py fails on two class skills in one slot | done | |
| Item tooltip text after the descfunc change (65 stats) | Median text | D2R descfunc 19 | verify | read a few item tooltips in game |
| Trade window | Median layout | built, never seen with two players | verify | TCP/IP test |

## Graphics (HD)

| Area | Median | Port now | Status | Needs / notes |
|---|---|---|---|---|
| Skill effects: Median-only missile art (926 rows, 251 animations) | own art | **custom art, last**: closest D2R HD effect per row by element/shape words, colour and size of the DCC frames, moving/hit/loop role; scaled to the art size (tools/effects_hd.py, work/mod/effects_hd.md lists row -> effect, confidence) | approx | custom HD art (last step); a real recolour needs particle editing ([hd-effects.md](hd-effects.md) section 6) |
| Skill effects: Median-only overlay art (192 rows, 172 animations) | own art | **custom art, last**: closest D2R HD overlay (cast/aura/hit), same scoring; overlay.txt `Character` points at its folder | approx | custom HD art (last step) |
| Skill effects: Median rows that reuse D2R art (4,029 missile incl. 2,684 without art, 127 overlay) | D2R art with Median's own light, speed, range, transparency per row | the D2R HD effect of the D2R row with the same art and the most alike behaviour; per-row light colour/radius, transparency, speed and animation timing are not carried (an HD definition only has transform scale, one spot light, fade-in alpha) | approx | engine may still read light from missiles.txt; verify in game |
| Median-only monsters (1,193) | own art | family/size/animation-matched D2R donor + scale + colour tint per monster (tools/hd_monsters.py, docs/hd-monsters.md); 23 fully borrow a donor | approx | real models (mesh, skeleton, animations) |
| Median monster looks, low confidence (85) | own art | donor chosen without a family hint | open | review work/mod/hd_monsters.md; real models |
| Missing missile/overlay CelFiles (12 + 12 names, 74 rows in both banks) | n/a | tools/art_fix.py points each at the closest art that exists (D2R's file wherever one fits, so the HD effect maps find its exact model); work/mod/art_fix.md lists row, old name, new name; validate.py fails on any new animation name found nowhere | approx | custom art (last step) |

## Bug #24 leftovers

| Area | Median | Port now | Status | Needs / notes |
|---|---|---|---|---|
| HD scene of Median-only presets (`data/hd/env/preset/mxl/...json`, baked terrain model + biome) | own tiles | none: the HD client logs `empty biome filename` once and the area still loads | open | HD terrain per preset cannot be authored; a preset-to-D2R-preset redirect would need a plugin hook on the scene loader (`0x6a0e20`) |
| Automap cels 1499/1500 of Median (one level-type-36 row, filler cels of nine town rows) | own cel | -1 (no tile) | approx | atlas of D2R ends at 1498 |
| Per-level map memory of Median levels with Layer above 99 | own layer per level | D2R's layer of the same id or 0 (shared records) | approx | D2R's table has 100 buckets (needs a plugin patch of 0x1400d5fe0 / 0x1400d5710 to hold more) |

## Sigma table fields found in the phase 1 leftover

Behaviour of fields that were unnamed until 2026-10-22 ([sigma-tables.md](sigma-tables.md)); none of them is read by the port.

| Area | Median | Port now | Status | Needs / notes |
|---|---|---|---|---|
| Akara respec confirm prompt (npcext `Slot01PromptConfirm` = 1, `PromptLabel`) | asks before spending the respec | respec runs at once from the menu line (`npc_actions.cpp`) | open | client dialog (or a second menu step) before the action |
| Monster spawn overlays (monstats3 `Overlay1/2`, `BossOverlay1/2`, `OverlayType`; `FUN_1008c380`) | an overlay plays on the monster when it spawns (treasure goblins, rift inspectors, Mephisto, Azgar) | none | open | needs a spawn hook that starts an overlay on the unit |
| Monster name colour (monstats3 `Flags` rank bits Elite / Veteran / Uber / Heroic / Boss, `NameColor`; `FUN_1008b470`) | name colour 9 / 12 / 8 / 2 / 4 by rank, else `NameColor` | D2R's own name colours | approx | name text colour is drawn by D2R's UI |
| Monster automap markers (monstats3 `Automap`, `AutomapShowName`, `AutomapNameColor`, `AutomapVectorColor`; `FUN_1004b620`) | marker and name on the automap for flagged monsters | none | open | D2R automap has no per-monster hook found |
| Item hover text in interact mode (misc2 `InteractString`; `FUN_1007c930`) | "Right click to reveal trophy challenge" on the 20 trophy items | D2R's default | open | string 12325; needs the item-interact mode hover |
| Infographic help panel (misc2 `Infographic`, item event 13, packet 0x16, `FUN_1009a9e0`; infographics table) | the client opens a help panel (catalyst of disenchantment) | none | open | panel art and text are in the infographics table; client UI |
| Hide the item level line (itemtypesext `NoShowILvl`; `FUN_10078fb0`) | 10 item types (rune, thng, cyc1-3, trph, erun, tent, embl, cyng) do not show "Item Level" | D2R shows it when its option is on | open | D2R item description hook |
| Palette shift of missiles (missiles2 `PalShift`, 25 rows; `FUN_1008b100`) | the missile gets a palette shift when created | none (D2R's HD missile art is not palette based) | open | probably no equivalent in HD; check on the 25 rows (Countess firewall, thunderstone, megademon inferno, ...) |
| Loot filter fields (misc2/armor2/weapons2 `Flags` bit 0 `Filterable`, `Classifications`; `D2LootFilter.cpp`) | Median's own loot filter lists and rules | D2R has its own loot filter | deviation | no port planned |
| Hell treasure class of objects (objects2 `Treasureclass_H`) | the Halloween chest (row 587) drops class 1106 on Hell | D2R's class for the object's difficulty | open | only that chest sets it; event content |

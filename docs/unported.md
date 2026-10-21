# Unported, approximated and different from Median

Rule (owner, 2026-10-21): the port follows Median XL, not D2R, wherever the two differ (respecs, drops, caps, ...).
Anything that cannot be done yet is listed here instead of being dropped. Each lane appends its gaps. Things removed or
deferred on purpose (classic mode G, terror zones) are in [PLAN.md](PLAN.md) "Removed or deferred for now".

Status: **open** (not ported), **approx** (works, but not exactly as Median), **deviation** (deliberately different,
with the reason), **verify** (ported, Median behaviour not yet confirmed in game).

## Engine (plugin)

| Area | Median | Port now | Status | Needs / notes |
|---|---|---|---|---|
| Monster AI `andariel_ai` (47 monsters: Andariel, Maghda, Radament, Zoltun Kulle, Lilith, Scholars, Malthaels, ...) | own AI routine | D2R's Andariel AI (moves, attacks; skills 1-2 only, no summons/towers) | approx | native port, ~3-5 days + in-game testing ([monsters-ai.md](subsystems/monsters-ai.md)) |
| Summoner AI | first skill branch by an `aip3`% roll | D2R's (by target cold/fire resist) | approx | small native hook |
| MosquitoNest init | Median init | D2R's | open | one enabled monster |
| Max resistance cap | 90 | D2R 95 | open | D2R's resist read is behind a protection thunk; find another hook point ([combat-stats.md](subsystems/combat-stats.md)) |
| Absorb stats 184/189/417 | Median absorb | none | open | same hook point as the resist cap; which items roll them |
| Mercenary caps | Median caps | D2R's | open | [combat-stats.md](subsystems/combat-stats.md) |
| Andariel catalyst / Act 2-5 bonus drops | treasure class dropped once per player on the ground | same: D2R's TC executor `0x441300` once per player in the game, at the victim (monstats word +0xc0 + difficulty * 0x10); the inventory item stays as the fallback when the class cannot be resolved | verify | Andariel checked in game (catalyst on the ground, still one after a second kill); Summoner / Izual use the same helper, not run in game; multiplayer count not run |
| Quest reward notices (client messages 0x5f-0x61) | shown | not shown | open | client message sender |
| Quest 5 repeat guard (`0x9e8b0`) | Median guard | not ported | open | purpose unclear in D2R ([quests-requirements.md](subsystems/quests-requirements.md) 5b) |
| Quest 8 event-2 handler (`0x5a2830`) | Median handler | not read | open | |
| Cain crafting materials | per-item submenu (4 entries) | one line, gives everything owed | approx | D2R menu record holds 5 lines; submenu = plugin-owned record |
| Cain menu line visibility (bug #23) | shown | added in code, never seen on screen | verify | look at Cain's menu in game |
| Craft player flags | keyed by player name | keyed by player name (character name at player data +0, fallback unit id) | verify | checked in game (flags survive a unit id change); a second character of the same name in one game would share them, as in Median |
| Craft per-game flags reset | game end | reset on the game-end event (`game_end.cpp`, client teardown `0x486be0`), plus the old game pointer / frame guard | verify | event fires once per left game (checked: games ended 0 -> 1); `0x4057c0` has a rip-relative prologue and is only called by the idle sweep |
| Gold payment client update | Median sends the update | assumed D2R stat sync | verify | stat-update packet sender not located |
| Dungeon lockout client timer and popup (packet 0x88, message 0x13) | shown | refusal only ("can't do that") | open | client widget + packet |
| Dungeon lockout: item-teleport clock start (`0x7fb70`), quest-table condition | yes | no | open | |
| Goblin "sighted" packet | client notice | none | open | |
| Hireling starting gear (Head/Torso/Weapon/Shield/Share columns) | per hireling row | D2R's own look; a fresh hire has no items | deviation | checked in game: a new Rogue carries 0 items in its inventory and still draws a bow, so D2R's gear is a visual taken from its own class data, not items to create; the columns have no D2R counterpart. Share is converted |
| Hireling hire/equip gates (Median ids 0x4c/0x50/0x51 and the hireclasses lists) | item type ids: potion types and weapon / shield types the class may wield | ported: weapon / shield types by class refused in the `0x36bc50` hook (`MercClasses`, `item_gen.cpp`); class-locked items check the merc's `equivalentcharclass` | verify | checked in game on the Rogue (bow allowed, crossbow, spears, swords, polearms, scythes, axes, maces, shield refused); the other three classes are unit-tested only; potion branch (`0x672a0`) not ported |
| Tyrael travel quest gate | quest 28 | D2R quest 26 | approx | harmless (portal, no menu line) |
| Item event ids 11/12/32/33/34, immunity checks on open wounds / crushing blow (monster stat 373) | attacker-takes-fire/cold/magic damage, flat life/cast on event (33/34), monsters with stat 0xbd immune to 15/16 | none | open | D2R dispatcher of itemeventfunc not found (stat record +0x48/+0x4a); see [items-xp.md](subsystems/items-xp.md) section 5 for the search done |
| Class-locked items (stat 492) | requirement check fails for other classes (players and mercenaries) | players and mercenaries (hook 0x36bc50; a merc counts as its `equivalentcharclass`) | verify | equip a locked unique on two classes and on a mercenary |
| Ethereal roll (item level 100+) | `rand(1000) <= qualities word2` (2.6%, quality 1 included), +25% stats, 75% durability | same rate/effect, but D2R's gate (5%, quality 1 and 5 excluded) stays in front; low quality never ethereal | approx | replace the `0x442d60` block fully to allow quality 1 |
| Socket roll (quality 1-3) | `rand() % 7` sockets, 6 of 7 items | same, through D2R's setter (size clipping); gen-forced sockets keep D2R's code | verify | compare socket rates in game; Median's quality 5-9 branch (6 sockets) is dead in the 1.13c call sites |
| Dye colour | stat 183 paints the item | stat 368 through the item colour hook 0x3710d0 | verify | look in game; dyes 16-21 may need Send Bits 6 on stat 368 |
| Party experience award (`0x5e830`) | every member in range gets the full award | D2R's | open | multiplayer only; share loop of 0x44ece0 not read |

## Data and UI

| Area | Median | Port now | Status | Needs / notes |
|---|---|---|---|---|
| Waypoints Vizjun (155) and level 242 | in the panel | saved, no panel button | open | waypoint panel code patch (more than 9 per tab or a 6th tab) |
| Median waypoints in the panel (6) | own acts | listed in Act 4's free slots, usable after reaching Act 4 | approx | same panel patch |
| Character sheet | portrait, pages 2-3 art | page 0 art; pages 2-3 as rows of D2R's Advanced Stats panel | approx | plugin panel for Median's own page art |
| Stat pages: Str/Dex damage bonus, total / % bonus / base columns | computed by code | missing | open | plugin ([stat-screen.md](subsystems/stat-screen.md)) |
| Skill tree: Sorceress 9th tab | 9 tabs | 8 | open | |
| Skill tree: `RogueMissile` row at Warmonger 6,1 | Fortress there | monster skill shares the row | open | data migration fix |
| Item tooltip text after the descfunc change (65 stats) | Median text | D2R descfunc 19 | verify | read a few item tooltips in game |
| Trade window | Median layout | built, never seen with two players | verify | TCP/IP test |

## Graphics (HD)

| Area | Median | Port now | Status | Needs / notes |
|---|---|---|---|---|
| Skill effects (missiles, overlays): 217 missile + 113 overlay animations | own art | D2R look-alikes | open | flipbook path built (tools/missile_art.py) but no effect confirmed in game ([hd-effects.md](hd-effects.md)) |
| Median-only monsters (1,193) | own art | family/size/animation-matched D2R donor + scale + colour tint per monster (tools/hd_monsters.py, docs/hd-monsters.md); 23 fully borrow a donor | approx | real models (mesh, skeleton, animations) |
| Median monster looks, low confidence (85) | own art | donor chosen without a family hint | open | review work/mod/hd_monsters.md; real models |
| Missing missile/overlay CelFiles (11 + 15) | n/a | none | open | [PLAN.md](PLAN.md) Phase 5 |

## Bug #24 leftovers

| Area | Median | Port now | Status | Needs / notes |
|---|---|---|---|---|
| HD scene of Median-only presets (`data/hd/env/preset/mxl/...json`, baked terrain model + biome) | own tiles | none: the HD client logs `empty biome filename` once and the area still loads | open | HD terrain per preset cannot be authored; a preset-to-D2R-preset redirect would need a plugin hook on the scene loader (`0x6a0e20`) |
| Automap cels 1499/1500 of Median (one level-type-36 row, filler cels of nine town rows) | own cel | -1 (no tile) | approx | atlas of D2R ends at 1498 |
| Per-level map memory of Median levels with Layer above 99 | own layer per level | D2R's layer of the same id or 0 (shared records) | approx | D2R's table has 100 buckets (needs a plugin patch of 0x1400d5fe0 / 0x1400d5710 to hold more) |

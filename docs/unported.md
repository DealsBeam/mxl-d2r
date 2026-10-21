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
| Andariel catalyst / Act 2-5 bonus drops | treasure class dropped once per player on the ground | item created in the killer's inventory | deviation | D2R treasure-class drop per player not wired; multiplayer gives only the killer one |
| Quest reward notices (client messages 0x5f-0x61) | shown | not shown | open | client message sender |
| Quest 5 repeat guard (`0x9e8b0`) | Median guard | not ported | open | purpose unclear in D2R ([quests-requirements.md](subsystems/quests-requirements.md) 5b) |
| Quest 8 event-2 handler (`0x5a2830`) | Median handler | not read | open | |
| Cain crafting materials | per-item submenu (4 entries) | one line, gives everything owed | approx | D2R menu record holds 5 lines; submenu = plugin-owned record |
| Cain menu line visibility (bug #23) | shown | added in code, never seen on screen | verify | look at Cain's menu in game |
| Craft player flags | keyed by player name | keyed by unit id (rejoin = new flags) | approx | name lookup |
| Craft per-game flags reset | game end | game pointer change / frame counter | approx | game-destroy hook (`0x4057c0` has a rip-relative prologue) |
| Gold payment client update | Median sends the update | assumed D2R stat sync | verify | stat-update packet sender not located |
| Dungeon lockout client timer and popup (packet 0x88, message 0x13) | shown | refusal only ("can't do that") | open | client widget + packet |
| Dungeon lockout: item-teleport clock start (`0x7fb70`), quest-table condition | yes | no | open | |
| Goblin "sighted" packet | client notice | none | open | |
| Hireling starting gear (Head/Torso/Weapon/Shield/Share columns) | per hireling row | none | open | how D2R gives a new hire its gear |
| Hireling hire/equip state gates (states 0x4c/0x50/0x51) | yes | no | open | state ids unresolved |
| Tyrael travel quest gate | quest 28 | D2R quest 26 | approx | harmless (portal, no menu line) |
| Item event ids 11/12/32/33/34, immunity checks on open wounds / crushing blow | yes | in progress (lane item-gen) | open | |
| Class-locked items, ethereal and socket rolls, dye colours | yes | in progress (lane item-gen) | open | |
| Party experience award (`0x5e830`) | yes | D2R's | open | multiplayer only |

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
| Median-only monsters (1,193) | own art | D2R look-alike models; 24 fully borrow a donor | approx | lane hd-monsters (tints/scale/better donors); real models later |
| Missing missile/overlay CelFiles (11 + 15) | n/a | none | open | [PLAN.md](PLAN.md) Phase 5 |

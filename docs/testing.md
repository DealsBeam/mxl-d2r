# In-game tests waiting to be run

Changes that are built and installed but not yet confirmed in the game. Restart D2R after an install. Move an item to
"Confirmed" (or into docs/bugs.md) once tested. Console command `mxl` shows the plugin version and how many skill
functions it installed.

## Waiting

| Since | What | How to test | Expected |
|---|---|---|---|
| 0.7.0 | Plugin loads; engine fixes applied | Console: `mxl` | `mxl-core 0.7.0`, `states fix on: 73 sites ...` (or 72), `level types on: ...`, `skill functions 5/5` |
| 0.7.0 | Median-only areas (level types 36-51) | Enter one (e.g. an uber/Median-only zone) and open the automap | No crash or freeze; the automap stays blank there (expected for now) |
| 0.6.0 | States fix (bug #14) | Play a while with buffs, auras, curses, summons; enter/leave towns and areas | No crashes; buffs show and expire normally |
| 0.6.0 | High-numbered Median states | A Median buff skill late in the list (e.g. a class aura/shout) and its icon/overlay | Effect applies and ends; no crash |
| data | Town Portal (bug #6) | Put Town Portal on the right mouse button, cast outside town | Blue portal opens, leads to town and back |
| data | Death no longer crashes (bug #12) | Die to anything | "... was slain by ..." message, no crash |
| data | Tooltip number lines (296 strings kept from D2R) | Hover items and skills | "Durability: x of y", "Required Strength: n" etc. show their numbers |
| 0.3.0 | Multi-summon, slot 172 | Necromancer: blood_skeleton (or Barbarian guardian_spirit) | Several pets per cast, up to the cap; at the cursor when in range |
| 0.2.0 | Barrage, slot 176 | Necromancer: catapult_shot | Missiles land at random spots around the target |
| 0.2.0 | Missile row, slot 177 | Necromancer: death_ripple_2 (or Assassin CountessFirewall) | A row of missiles across the aim line |
| 0.4.0 | Strike burst, slot 178 | Necromancer: torment (or Assassin crucify_2, Barbarian earthquake_2) | Target is hit, then missiles burst from it and fly past |
| 0.5.0 | Missile at target, slot 185 | A flamestrike skill | One missile appears on the cursor spot |
| data | Storm skills -> D2R Armageddon | Druid frigid_domain, Sorceress overheat, Barbarian thundergod | Timed effect around the caster that keeps firing |
| later | Multiplayer (TCP/IP through D2RLoader) | Host + join with the same mod and plugin version | Both in game; new skills work for the joiner |

## Confirmed

| What | Notes |
|---|---|
| Vendors stock Median's items (bug #8) | |
| Skill points can be spent; 8-tab skill tree; skill icons | |
| Potions (bug #10) | |
| Flamefront needs Molten Core first (bug #13) | Median's rule, not a bug |

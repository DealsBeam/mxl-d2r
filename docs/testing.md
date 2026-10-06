# In-game tests waiting to be run

Changes that are built and installed but not yet confirmed in the game. Restart D2R after an install. Move an item to
"Confirmed" (or into docs/bugs.md) once tested. Console command `mxl` shows the plugin version and how many skill
functions it installed.

## Waiting

| Since | What | How to test | Expected |
|---|---|---|---|
| data | **Whirlpool, final pass** on a new Paladin (`paladin`): HD looks (missile_hd.py), sounds (Median's sound rows) | New Paladin; get Whirlpool (axe/item written into the save, or Leather of Aut), any melee weapon, hit a monster; also press G for classic graphics | HD: a blue tornado-style swirl on the target (placeholder for Median's ColdFuse), cold bolts flying out (Frozen Orb's), a bone-spirit burst when each swirl ends; the sewer-water sound plays with the swirl. Classic mode: the bolts and burst show, the swirl itself is invisible (ColdFuse art not ported yet). Tooltip: Whirlpool, % weapon damage, "item granted skill"; no crash |
| 0.26.0 | Plugin loads; engine fixes applied | Console: `mxl` | `mxl-core 0.26.0`, `states fix on: 73 sites ...` (or 72), `level types on: ...`, `calc codes on: syn1-6, skcp, aura, pass, bl/blz/bd/bdz, wdm, area, skcd, bmax, maxe, mnhp, mnar, mxfn (hasstate, statlayer); missiles msyn, blvl`, `skill functions 15/15`, `missile hits 3/3`, `item spells on: 330 items on pSpell 16; items on pSpell 16/17; mystic orbs (101 rows), dyes, keys, containers; ...` |
| 0.26.0 | Missile hit nova (srvhit 66, D2Sigma 77) and explosion (D2Sigma 69 -> D2R hit 1) | Nova: skills frigid_nova, frigid_domain, coronation, hammertime, hurricane_2. Explosion: skills gorefest, wychwind, flamestrike, earthquake_2, mana_pulse, batstrike, warp_crash | Nova: when the missile ends, a ring of sub-missiles flies out (frigid nova: a ring of cold bolts); no crash. Explosion: the hit damages every monster in a small radius around the impact, not just the one hit |
| 0.12.0 | Mystic orbs (bug #11) | Buy an orb from Gheed (e.g. a stat orb), right-click it, click an armour or weapon in the inventory | Cursor changes like Identify; the item gains the orb's bonus and +4 level requirement; the orb is used up. Applying the same orb beyond its maximum (5 for normal orbs), or to an item it doesn't fit, does nothing and keeps the orb. Also try one on an equipped item |
| 0.12.0 | Dyes (bug #11) | Right-click a mystic dye, click an armour | Dye used up, no crash (the colour won't show: D2R draws items without Median's dye) |
| 0.14.0 | Arcane shard help | Hover an arcane shard, Arcane Crystal, Oil of Craft, a catalyst | The recipe help text shows in the tooltip (check the line order); right-click does nothing and the item stays |
| 0.14.0 | Containers (gem/rune clusters) | Cube Oil of Craft + a perfect gem -> gem cluster; carry 2-3 more of that perfect gem (inventory and cube; stash too while in town); right-click the cluster | The gems disappear, the cluster's count goes up by one each (up to 50), the cluster stays. No duplicates or ghost items after moving things around / relogging |
| 0.13.0 | Keys open portals | In Harrogath (Act V): right-click dimensional_link (`ktox`) or mind_warp (`kfau`). In Act IV town: key_of_lies (`kbel`) or key_of_sin (`kazm`). Riftstones `cr01` (Act I), `cr02` (Act II), Hell only | A red portal opens next to you, leads to the level and back. A second use of the same key in that game does nothing. `ktox`, `ksco`, `ktpx` and riftstones are used up; `kbel`/`kazm`/`kfau`/`tbr` stay (0.14.0) |
| 0.13.0 | Key in the wrong act | Right-click key_of_lies in Act I town | "Can't do that" message, key kept |
| 0.13.0 | One-way portals | charged_riftstone_16 (`cr21`, Act V) or scroll_of_summoning (`b60X`, Act I) | A portal (Median's one-way portal object) opens; check it can be entered |
| data | Item skill procs/oskills (bug #16) | A unique with "+N to <Median skill>" or "chance to cast", e.g. Book of Triune (Mind Control) | Tooltip and proc name the right skill |
| 0.8.0 | Synergies (syn1-syn6) | An item with "+X% to <skill>"-style synergy bonuses; compare the skill's damage/tooltip with and without it | Damage/tooltip changes with the item |
| 0.8.0 | Item max-level bonus (skcp) | An item with "+N to maximum level of <skill>" | That skill can take N more points |
| 0.9.0 | Base-level skill values (bl12/bd12...) and weapon damage % (wdm) | Skill tooltips that show per-level values or "% weapon damage"; raise the skill by a point and check the value moves | Non-zero values that grow with points spent (not with +skill items) |
| 0.11.0 | State-dependent formulas (hasstate) | Fire Ball's poison interaction, or any skill that changes while a buff/curse is on (e.g. Meditation, shrine effects) | Values change when the state is active |
| 0.11.0 | Layered stat formulas (statlayer) | Holy Shock tooltip with "activation frequency" items | Values include the per-skill item bonus |
| 0.10.0 | Missile formulas (msyn, blvl) | A skill whose missile damage/size scales with the skill's points or synergy items (32 missiles use blvl) | Missile effect grows with points spent |
| 0.9.0 | "Maxed skill" effects (bmax/maxe) | Skills whose tooltip/bonus changes when maxed (e.g. "max this skill ... to remove cooldown") | The bonus appears once the skill reaches its current cap |
| 0.9.0 | Cooldown in skill tooltips (skcd) | Tooltip of a skill with a cooldown | Shows the cooldown (not 0) |
| 0.9.0 | Area-dependent skills (area) | The class "slow missiles" passives (paragon_of_sanc...) in their level range | Active only in the areas their Param7..Param8 range covers |
| data | Formulas with other Median codes (aura, area, bl12...) | Skills whose tooltips previously showed odd or zero values | Values show (codes not yet implemented count as 0) |
| 0.7.0 | Median-only areas (level types 36-51) | Enter one (e.g. an uber/Median-only zone) and open the automap | No crash or freeze; the automap stays blank there (expected for now) |
| 0.6.0 | States fix (bug #14) | Play a while with buffs, auras, curses, summons; enter/leave towns and areas | No crashes; buffs show and expire normally |
| 0.6.0 | High-numbered Median states | A Median buff skill late in the list (e.g. a class aura/shout) and its icon/overlay | Effect applies and ends; no crash |
| data | Skill caps (Median's real rules) | Spend points in one tab | Each skill stops at its own cap (e.g. 20 or 25; Molten Core 1); a point in the tab's Specialization raises the caps; locked pairs (e.g. Molten Core vs Entanglement) can't both be taken |
| data | Town Portal (bug #6) | Put Town Portal on the right mouse button, cast outside town | Blue portal opens, leads to town and back |
| data | Death no longer crashes (bug #12) | Die to anything | "... was slain by ..." message, no crash |
| data | Tooltip number lines (296 strings kept from D2R) | Hover items and skills | "Durability: x of y", "Required Strength: n" etc. show their numbers |
| 0.15.0 | Minion scaling (Median's rules) | Necromancer blood_skeleton or Barbarian guardian_spirit at two different character levels / skill levels; hover the pet | Pet level = your level; life grows with skill level and your level (and "+% summon life" items); attack rating grows with skill level; "+summon resist" items raise its resists. Compare with the skill tooltip's life/AR lines (mnhp/mnar, now non-zero) |
| data | hyena_strike / exorcism (D2Sigma 161 -> D2R Rabies) | Amazon hyena_strike on a monster | Melee hit with poison damage, the hyena state on the target, hyena_strike_missile appears |
| 0.25.0 | Missile hit "cast skill" (srvhit 64) | Druid **Egg Trap**: let an egg hatch/hit; Sorceress wintertide, phoenix_wave | When the missile lands, the follow-up skill fires (e.g. Egg Trap's hatch effect) on the target or spot; no crash |
| 0.24.0 | Replaced client functions | vampiric_strike / deathgaze (fan of missiles on the target), parasite_2/3 or rock_shock (two missiles forming a wall at the target), Raise Skeleton (corpse effect), flamefront_15 | Missiles appear as described, no Amazon chain-lightning visuals on parasite/rock_shock, no crash |
| 0.23.0 | Client missile row / missile at target (client 110/112) | Druid Egg Trap, Sorceress flamefront (missile row); havoc or heaven_s_fury_2 (missile at target) | Missile-row skills show their row of missiles (server-drawn) with no Volcano effects; havoc / heaven_s_fury_2 show their missiles at the target; no crash |
| 0.22.0 | **Whirlpool** (server slot 178 + client slot 111) | Wear Leather of Aut (low-level unique; also Black Ice, Waterwalk) which grants Whirlpool; put it on a mouse button with a melee weapon and hit a monster | The hit lands, then a spinning blue whirlpool appears on the target and throws out growing rings of bolts (magic damage); no Warlock/sigil effects, no crash. Also try torment / crucify_2 / ravage (same functions) |
| 0.21.0 | Level portal skill (slot 173) | Whatever grants mysterious_companion_skill / judgementwander_2_skill (item or NPC skill) | A red portal opens next to you to that level; nothing when the level is in another act |
| data | resurrect_2 (-> D2R Revive) | Paladin level-90 resurrect_2 on a corpse | Monster revives as a pet; note its life (Median set it outright, D2R treats the value as a bonus) |
| 0.20.0 | Pets inherit the owner's procs | Wear an item with "X% chance to cast level Y <skill> when you kill an enemy" (or on attack), then cast a multi-summon (blood_skeleton, Druid Harvesters/Bear Companion) | The pets cast that skill on kill (chance divided by the pet type's divisor); no crash on summoning. If nothing ever procs, note it (the stat array may not have been found) |
| 0.19.0 | Levels above 255 (bug #17) | A summoning scroll to the Heroic Library / Heroic Duncraig / an arena (Act matching the scroll), or a Mirror Temple key | Portal opens and leads into the area (not a wrong low-numbered level); area loads with no crash; cast town portal there and come back through it |
| data | Cow Portal (bug #19) | Wirt's Leg + Tome of Town Portal in the cube in Act 1 town | Red portal to the Moo Moo Farm |
| data | Skill tooltips (bug #9) | Hover skills in every tree tab (and with synergies) | No assert/crash; synergy block starts "<Skill> Receives Bonuses From:" with the skill's name; values show. Note any skill whose tooltip looks empty or wrong |
| 0.21.0 | Median teleport range (slot 179; was on 173, which mysterious_companion_skill also used) | Druid harbinger (or Sorceress Teleport, Assassin blink): click far away, then close by | Lands at most the skill's range away along the line to the click (harbinger ~25); close clicks land on the spot; no teleport where Median forbids it; monsters with teleport skills still blink |
| 0.18.0 | Layered self buffs (slot 174) | Druid Steady Shot (sk1296), frigid_nova, lionheart_2, brace_for_impact, lunar_assault_2 | Buff state and its bonuses show; Steady Shot's stacking counter grows per cast and its proc state turns on at the cap; no crash |
| 0.21.0 | land_of_the_dead (slot 180) | The skill (or an item granting it) | Timed effect around the caster keeps firing; its bonuses apply |
| 0.17.0 | queen_of_blades (slot 190) | Assassin: queen_of_blades (level 25) near a pack of monsters | Buff state on; every few frames all enemies within range take lightning plus weapon damage; damage numbers grow with the weapon; buff and pulses end when the state runs out; no crash |
| 0.16.0 | Median item auras (slot 184) | Equip Majesty, Penumbra, Clockwork Slippers or The Pathless | Their aura's state icon shows; its bonuses apply (character sheet); no crash when equipping/unequipping or changing areas |
| 0.3.0 | Multi-summon, slot 172 | Necromancer: blood_skeleton (or Barbarian guardian_spirit) | Several pets per cast, up to the cap; at the cursor when in range |
| 0.2.0 | Barrage, slot 176 | Necromancer: catapult_shot | Missiles land at random spots around the target |
| 0.2.0 | Missile row, slot 177 | Necromancer: death_ripple_2 (or Assassin CountessFirewall) | A row of missiles across the aim line |
| 0.4.0 | Strike burst, slot 178 | Necromancer: torment (or Assassin crucify_2, Barbarian earthquake_2) | Target is hit, then missiles burst from it and fly past |
| 0.5.0 | Missile at target, slot 185 | A flamestrike skill | One missile appears on the cursor spot |
| data | Storm skills -> D2R Armageddon | Druid frigid_domain, Sorceress overheat, Barbarian thundergod | Timed effect around the caster that keeps firing |
| later | Item images / vendor crash (bug #18) | After the HD item table stopgap: open Charsi/Gheed with Median items on sale, buy a quiver, open the stash | Items show a D2R look-alike picture; no `[default_unit]` spam in blz-log.txt; no crash |
| later | Multiplayer (TCP/IP through D2RLoader) | Host + join with the same mod and plugin version | Both in game; new skills work for the joiner |

## Confirmed

| What | Notes |
|---|---|
| Vendors stock Median's items (bug #8) | |
| Skill points can be spent; 8-tab skill tree; skill icons | |
| Potions (bug #10) | |
| Flamefront needs Molten Core first (bug #13) | Median's rule, not a bug |

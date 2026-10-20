Restart D2R after installing a build. Use `mxl` in the console to check the plugin version.
Run the sessions with the listed character, and mark each result by item.
Report results by item so confirmed checks can move to Confirmed and failures to docs/bugs.md.

## barby (Barbarian, level 10)

- [ ] **Skill tree look: Median titles, stone, arrows** (data) — Open the skill tree (T) in HD, click all 8 tabs; press G and repeat; try another class too (Sorceress, Paladin). **Expected:** Median's tab titles and arrows per tab, brown stone, sockets lined up with the icons, classic mode with a titled tab column; report any tab that shows no art or the wrong class.
- [ ] **Median stash size: 14 x 14** (data) — Open personal and shared stash tabs; place items in far corners, switch tabs, leave and reload, then check the shared stash on another character. **Expected:** Median art and grid line up; gold, arrows, tabs and stored items work; D2R material tabs are unchanged.
- [ ] **Median inventory and cube sizes: 15 x 10** (data) — In HD, open inventory and cube; use new rows, equip each slot, swap weapons, transmute, save/reload, and press G. **Expected:** Both grids and controls line up with Median art; items persist. Classic mode keeps D2R art with the larger grid.
- [ ] **States fix off by default** (0.35.0) — In Blood Moor, hover and attack monsters, cast Whirlwind, and play with buffs/auras. **Expected:** Monsters are targetable, Whirlwind spins, buffs work, no random states or crash.
- [ ] **Whirlwind hit rate as in classic Median** (0.36.0) — Spin through a pack with the axe; try a faster weapon or IAS item if available. **Expected:** More hits per spin than before; compare feel with classic Median.
- [ ] **Whirlwind and other sequence skills** (data) — Outside town, cast Whirlwind across monsters several times and release mid-spin; try Zeal-like strikes, Strafe-like volleys or Fend. Press G. **Expected:** Whirlwind moves, hits, spins and stops cleanly; other skills play their sequence.
- [ ] **Cold effect on monsters** (data) — Chill monsters with a cold skill or cold damage item. **Expected:** Chilled monsters slow by Median's -20% instead of speeding up.
- [ ] **Monster sequence skills** (data) — Fight Fallen Shamans, Greater Mummies, skeleton-raising casters, chargers and Sand Maggots. **Expected:** They revive, raise, charge, burrow and lay eggs as appropriate.
- [ ] **Median monsters** (data) — Fight in Act 1, press G, then visit a Median-only area such as a Trial. **Expected:** HD models use Median timing; classic mode shows Median art; monsters stay visible and targetable.
- [ ] **HD body animations for sequence skills** (data) — Cast Whirlwind outside town; try leap_2 if learnable. Also try Assassin smg or hurricane_2 / virulence if available. **Expected:** Barbarian spins like D2R Whirlwind; leap_2 uses Leap; other supported skills animate.
- [ ] **Classic graphics** (data) — Press G; inspect Median items in inventory and vendor, cast Whirlpool on a Paladin and other missile skills. **Expected:** Median item pictures and missile/overlay art show in classic mode; note missing art or crashes.

## Paladin

- [ ] **Whirlpool, final pass** (data) — On a new Paladin, obtain Whirlpool and a melee weapon; hit a monster in HD and press G. **Expected:** HD swirl, cold bolts, bone-spirit burst and sewer-water sound; classic shows bolts and burst; tooltip and item-granted skill are correct.
- [ ] **Whirlpool skill function** (0.22.0) — Wear Leather of Aut (or Black Ice/Waterwalk), bind Whirlpool and hit a monster; try torment, crucify_2 or ravage. **Expected:** Hit lands, blue whirlpool and growing bolt rings appear; no unrelated effects or crash.
- [ ] **Habeas Corpus and Resurrect** (0.30.0) — With both skills, resurrect a corpse then cast Habeas Corpus elsewhere; also try Habeas Corpus before resurrecting. **Expected:** It leaves corpses of the raised monster type after Resurrect, does nothing before, and Resurrect still works.
- [ ] **Phalanx and nova** (0.32.0) — Amazon: cast Phalanx at a pack. Any class: cast frigid_nova, coronation and hammertime. **Expected:** Phalanx leaves arrow-shooting tiles; novas emit a ring of sub-missiles; no crash.
- [ ] **Mana costs** (data) — Hover a skill in the tree, then cast repeatedly until mana runs out. **Expected:** Tooltip shows Mana Cost and casting drains the listed amount.
- [ ] **Mana costs from plugin** (0.29.0) — Cast Blood Skeleton or Summon Sinner several times; equip a mana-cost modifier and cast with/without it. **Expected:** Summons use a share of max mana; modifier changes drain by its percentage; no casting errors.
- [ ] **Keys open portals** (0.13.0) — Use dimensional_link/mind_warp in Act V, key_of_lies/key_of_sin in Act IV, and riftstones in Hell. **Expected:** Correct portal opens once, leads to the level and back; used keys and riftstones are consumed as listed.
- [ ] **Levels above 255** (0.19.0) — Use a scroll to Heroic Library, Heroic Duncraig or an arena in the matching act, or a Mirror Temple key; cast Town Portal there. **Expected:** Correct area loads without a crash and return portal works.
- [ ] **Item auras** (0.16.0) — Equip and remove Majesty, Penumbra, Clockwork Slippers or The Pathless; change areas. **Expected:** Aura icon and bonuses work; no crash.
- [ ] **resurrect_2** (data) — Use level-90 resurrect_2 on a corpse. **Expected:** Monster revives as a pet; note its life.

## New Necromancer, Druid and other class skills

- [ ] **Multi-summon** (0.3.0) — Necromancer: cast blood_skeleton (or Barbarian guardian_spirit). **Expected:** Several pets appear at the cursor, up to the cap.
- [ ] **Barrage** (0.2.0) — Necromancer: cast catapult_shot. **Expected:** Missiles land at random spots around the target.
- [ ] **Missile row** (0.2.0) — Necromancer: cast death_ripple_2 (or Assassin CountessFirewall). **Expected:** A row of missiles crosses the aim line.
- [ ] **Strike burst** (0.4.0) — Necromancer torment, Assassin crucify_2 or Barbarian earthquake_2. **Expected:** Target is hit, then missiles burst from it and fly past.
- [ ] **Missile at target** (0.5.0) — Use a flamestrike skill. **Expected:** One missile appears at the cursor spot.
- [ ] **Missile hit owner state and summon** (0.27.0) — Cast Fire Ball, vanquish, brace_for_impact or inoculation; test corrupted_necromancer, councillor_ormac or sinew_shaper. **Expected:** Caster gets the buff; monster minion appears at missile impact; no crash.
- [ ] **Pets inherit owner procs** (0.20.0) — Wear an on-kill/on-attack proc item, then summon blood_skeleton or Druid Harvesters/Bear Companion. **Expected:** Pets can proc the skill at the pet type's divided chance; no summon crash.
- [ ] **Minion scaling** (0.15.0) — Compare Necromancer blood_skeleton or Barbarian guardian_spirit at two character and skill levels; hover pet and test summon life/resist items. **Expected:** Pet level follows yours; life, attack rating and resists scale as described by tooltip and items.
- [ ] **Egg Trap cast skill** (0.25.0) — Druid: let Egg Trap hatch and hit; Sorceress: try wintertide or phoenix_wave. **Expected:** Missile landing fires the follow-up skill on target or spot; no crash.
- [ ] **Missile row and target placement** (0.23.0) — Druid Egg Trap or Sorceress flamefront for row; havoc or heaven_s_fury_2 for target. **Expected:** Row appears without Volcano effects; target missiles appear at target; no crash.
- [ ] **Replaced client functions** (0.24.0) — Try vampiric_strike/deathgaze, parasite_2/3 or rock_shock, Raise Skeleton and flamefront_15. **Expected:** Their missile fan, wall, corpse effect or missile appears as described; no chain-lightning visuals on wall skills.
- [ ] **Missile hits: portal, heal and nova** (0.28.0) — Use ancient_gate/judgementwander; hurt a pack with sinew_shaper nearby; cast frigid_nova, coronation or hammertime. **Expected:** Portal opens to its level, healer restores allies, nova ring is visible; no crash.
- [ ] **Nova and explosion hits** (0.26.0, fixed 0.32.0) — Try frigid_nova, frigid_domain, coronation, hammertime or hurricane_2; then gorefest, wychwind, flamestrike, earthquake_2, mana_pulse, batstrike or warp_crash. **Expected:** Nova emits sub-missiles; explosion damages nearby monsters; no crash.
- [ ] **Storm skills** (data) — Druid frigid_domain, Sorceress overheat or Barbarian thundergod. **Expected:** Timed effect around caster keeps firing.
- [ ] **Median teleport range** (0.21.0) — Druid harbinger, Sorceress Teleport or Assassin blink; click far away and nearby. **Expected:** Lands within skill range along click line; close clicks land there; forbidden areas block player teleport; monsters still blink.
- [ ] **Layered self buffs** (0.18.0) — Cast Druid Steady Shot, frigid_nova, lionheart_2, brace_for_impact or lunar_assault_2. **Expected:** Buff and bonuses show; Steady Shot counter stacks and proc turns on at cap.
- [ ] **land_of_the_dead** (0.21.0) — Use the skill or an item granting it. **Expected:** Timed effect around caster keeps firing and bonuses apply.
- [ ] **queen_of_blades** (0.17.0) — Assassin: use level-25 queen_of_blades near a pack. **Expected:** Buff pulses lightning and weapon damage to nearby enemies; pulses end with buff.
- [ ] **hyena_strike / exorcism** (data) — Amazon: use hyena_strike on a monster. **Expected:** Poison melee hit, hyena state and hyena_strike_missile appear.

## Any character: portals, items and town

- [ ] **Bug #7 strings** (data) — Make a new character; first-talk to Gheed, Akara, Kashya, Charsi and Warriv; read item and skill tooltips. **Expected:** Gheed's own greeting appears, not “Arcane Ape”; other text is intact.
- [ ] **Bug #18 vendor item pictures** (data) — Browse every vendor tab, buy a quiver, and scroll Item Spawner lists. **Expected:** Every item has a picture; no vendor crash or freeze.
- [ ] **HD item pictures: Median's own art** (data) — In HD, inspect Median-only charms, crafting items, armor and quest items at Gheed/Charsi/Akara and in stash; compare with G. **Expected:** Median's real, slightly soft art appears in the correct grid positions.
- [ ] **Mystic orbs** (0.12.0) — Buy an orb, apply it to armor/weapon, test max count, invalid item and equipped item. **Expected:** Valid item gains bonus and +4 level requirement; orb is consumed. Invalid or over-limit use keeps orb.
- [ ] **Dyes** (0.12.0) — Right-click dye and click armor. **Expected:** Dye is consumed without crash; color need not show.
- [ ] **Arcane shard help** (0.14.0) — Hover shard, Arcane Crystal, Oil of Craft and catalyst; right-click. **Expected:** Recipe help is ordered correctly; right-click does nothing and item stays.
- [ ] **Containers** (0.14.0) — Cube Oil of Craft and perfect gem; add more gems in inventory/cube and stash, then right-click cluster. **Expected:** Gems are consumed and count rises to 50 max; no duplicates or ghost items after moving/relog.
- [ ] **Item skill procs and oskills** (data) — Check a unique with +Median skill or chance to cast, such as Book of Triune. **Expected:** Tooltip and proc name the correct skill.
- [ ] **Cow Portal** (data) — Cube Wirt's Leg and Tome of Town Portal in Act 1 town. **Expected:** Red portal leads to Moo Moo Farm.
- [ ] **Town Portal** (data) — Put Town Portal on right mouse button and cast outside town. **Expected:** Blue portal leads to town and back.
- [ ] **One-way portal skill** (0.21.0) — Use mysterious_companion_skill or judgementwander_2_skill. **Expected:** Red portal opens to that level; nothing happens if level is in another act.
- [ ] **Key in the wrong act** (0.13.0) — Right-click key_of_lies in Act I town. **Expected:** “Can't do that” message; key stays.
- [ ] **One-way portals** (0.13.0) — Use charged_riftstone_16 in Act V or scroll_of_summoning in Act I. **Expected:** One-way portal opens and can be entered.
- [ ] **Automap in Median-only areas** (0.33.0) — Enter Caldeum, Caldeum Library, Ruins of Ureh, Silver City, Corrupted Abbey, a Trial or Dimensional Labyrinth; open Tab and explore. **Expected:** Walls and floors appear; no crash. Heart of Darkness/End of Time stay blank.

## Any character: skills, tooltips and general play

- [ ] **Skill tree rules** (data) — Amazon: spend in one devotion then try another; on a class with masteries, learn three and try a fourth. **Expected:** Second devotion and fourth mastery refuse points.
- [ ] **Skill caps** (data) — Spend points in one tab, add a point to its Specialization, and try locked pairs such as Molten Core/Entanglement. **Expected:** Skills stop at their own caps; specialization raises caps; locked pair cannot both be taken.
- [ ] **Skill tooltip assert** (data) — Hover skills in every tree tab and inspect synergies. **Expected:** No assert/crash; synergy heading includes skill name and values show.
- [ ] **Tooltip number lines** (data) — Hover items and skills. **Expected:** Durability, required strength and other number lines show values.
- [ ] **Synergies** (0.8.0) — Equip an item with +% to a skill synergy; compare damage/tooltip. **Expected:** Damage or tooltip changes.
- [ ] **Item max-level bonus** (0.8.0) — Equip item with +maximum skill level. **Expected:** Skill accepts the extra points.
- [ ] **Base-level values and weapon damage** (0.9.0) — Check per-level or weapon-damage tooltips, add a skill point. **Expected:** Values are nonzero and grow with points, not +skills.
- [ ] **State-dependent formulas** (0.11.0) — Check Fire Ball poison interaction or a skill affected by buff/curse, such as Meditation or shrine effect. **Expected:** Values change while state is active.
- [ ] **Layered stat formulas** (0.11.0) — Check Holy Shock tooltip with activation-frequency items. **Expected:** Values include per-skill item bonus.
- [ ] **Missile formulas** (0.10.0) — Add skill points or synergy item to a missile skill whose damage/size scales. **Expected:** Missile effect grows.
- [ ] **Maxed-skill effects** (0.9.0) — Check a skill whose bonus changes at cap. **Expected:** Bonus appears at current cap.
- [ ] **Cooldown tooltips** (0.9.0) — Hover a skill with cooldown. **Expected:** Tooltip shows its cooldown, not 0.
- [ ] **Area-dependent skills** (0.9.0) — Check class slow-missile passives in and out of their Param7..Param8 level range. **Expected:** Active only in covered areas.
- [ ] **Other Median formula codes** (data) — Check skills whose tooltip showed odd or zero values. **Expected:** Values show; unimplemented codes count as 0.
- [ ] **Formula itemstat/tabpoints** (0.31.0) — Compare character Defense with/without helmet or shield; equip, swap, drop items and swap weapons. **Expected:** Equipment dependent bonuses update; no crash.
- [ ] **Death message** (data) — Die to anything. **Expected:** “... was slain by ...” message appears; no crash.
- [ ] **States fix** (0.6.0) — Play with buffs, auras, curses and summons; enter and leave towns and areas. **Expected:** No crash; buffs show and expire normally.
- [ ] **High-numbered Median states** (0.6.0) — Use a late-list Median buff such as a class aura or shout; check its icon/overlay. **Expected:** Effect applies and ends; no crash.
- [ ] **Multiplayer** (later) — Host and join through D2RLoader with same mod and plugin version. **Expected:** Both enter game; new skills work for joiner.

## Keys and portals, one by one

Use each item in the listed act; try a second copy in the same game to check the once-per-game limit.
Level numbers are Median IDs (D2R IDs are one higher here). "Any" means any difficulty.

- [ ] `ktox` Dimensional Link — Act V (Harrogath), any difficulty: level 215 Edge of Nothingness; consumed.
- [ ] `kbel` Key of Lies — Act IV (Pandemonium Fortress), any difficulty: level 176 Realm of Lies; kept.
- [ ] `kazm` Key of Sin — Act IV (Pandemonium Fortress), any difficulty: level 166 Realm of Sin; kept.
- [ ] `kfau` Mind Warp — Act V (Harrogath), any difficulty: level 155 Fauztinville; kept.
- [ ] `ksco` Gaya Wand — Act V (Harrogath), any difficulty: level 242 The Frozen Crest; consumed.
- [ ] `tbr` the Black Road — Act V (Harrogath), any difficulty: level 155 Fauztinville; kept.
- [ ] `cr01` Charged Riftstone 1 — Act II (Lut Gholein), Hell: level 224 The Storm Oasis; consumed.
- [ ] `cr02` Charged Riftstone 2 — Act III (Kurast Docks), Hell: level 225 The Chamber of Offering; consumed.
- [ ] `cr03` Charged Riftstone 3 — Act IV (Pandemonium Fortress), Hell: level 226 The Storm Cave; consumed.
- [ ] `cr04` Charged Riftstone 4 — Act IV (Pandemonium Fortress), Hell: level 227 Court of the Damned; consumed.
- [ ] `cr05` Charged Riftstone 5 — Act IV (Pandemonium Fortress), Hell: level 228 The Storm Pit; consumed.
- [ ] `cr06` Charged Riftstone 6 — Act V (Harrogath), Hell: level 229 Dimensional Labyrinth 1; consumed.
- [ ] `cr07` Charged Riftstone 7 — Act V (Harrogath), Hell: level 230 Dimensional Labyrinth 2; consumed.
- [ ] `cr08` Charged Riftstone 8 — Act V (Harrogath), Hell: level 231 Dimensional Labyrinth 3; consumed.
- [ ] `cr09` Charged Riftstone 9 — Act V (Harrogath), Hell: level 232 Dimensional Labyrinth 4; consumed.
- [ ] `cr10` Charged Riftstone 10 — Act V (Harrogath), Hell: level 233 Dimensional Labyrinth 5; consumed.
- [ ] `cr11` Charged Riftstone 11 — Act V (Harrogath), Hell: level 234 Dimensional Labyrinth 6; consumed.
- [ ] `cr12` Charged Riftstone 12 — Act V (Harrogath), Hell: level 235 Dimensional Labyrinth 7; consumed.
- [ ] `cr13` Charged Riftstone 13 — Act V (Harrogath), Hell: level 236 Dimensional Labyrinth 8; consumed.
- [ ] `cr14` Charged Riftstone 14 — Act V (Harrogath), Hell: level 237 Dimensional Labyrinth 9; consumed.
- [ ] `cr15` Charged Riftstone 15 — Act V (Harrogath), Hell: level 238 Dimensional Labyrinth 10; consumed.
- [ ] `ktpx` Otherworldly Link — Act V (Harrogath), any difficulty: level 240 Dimensional Plane; consumed.
- [ ] `ir75` Call of the Damned — Act IV (Pandemonium Fortress), any difficulty: level 227 Court of the Damned; consumed.
- [ ] `cr21` Charged Riftstone 16 — Act V (Harrogath), any difficulty: level 247 Heart of Darkness; consumed.
- [ ] `b60X` Scroll of Summoning 1 — Act I (Rogue Encampment), any difficulty: level 250 Butcher's Lair 2; consumed.
- [ ] `b61X` Scroll of Summoning 2 — Act III (Kurast Docks), any difficulty: level 251 Proving Grounds 2; consumed.
- [ ] `b62X` Scroll of Summoning 3 — Act I (Rogue Encampment), any difficulty: level 252 Infernal Cave 2; consumed.
- [ ] `b63X` Scroll of Summoning 4 — Act IV (Pandemonium Fortress), any difficulty: level 262 Heroic Library of Fate; consumed.
- [ ] `b64X` Scroll of Summoning 5 — Act IV (Pandemonium Fortress), any difficulty: level 263 Heroic Library of Fate 2; consumed.
- [ ] `b65X` Scroll of Summoning 6 — Act IV (Pandemonium Fortress), any difficulty: level 264 Heroic Library of Fate 3; consumed.
- [ ] `b66X` Scroll of Summoning 7 — Act IV (Pandemonium Fortress), any difficulty: level 265 Heroic Library of Fate 4; consumed.
- [ ] `b67X` Scroll of Summoning 8 — Act IV (Pandemonium Fortress), any difficulty: level 266 Heroic Library of Fate 5; consumed.
- [ ] `b68X` Scroll of Summoning 9 — Act IV (Pandemonium Fortress), any difficulty: level 267 Heroic Library of Fate 6; consumed.
- [ ] `b70X` Scroll of Summoning 10 — Act I (Rogue Encampment), any difficulty: level 273 Heroic Duncraig; consumed.
- [ ] `b71X` Scroll of Summoning 11 — Act I (Rogue Encampment), any difficulty: level 274 Heroic Duncraig 2; consumed.
- [ ] `b72X` Scroll of Summoning 12 — Act I (Rogue Encampment), any difficulty: level 275 Heroic Duncraig 3; consumed.
- [ ] `b73X` Scroll of Summoning 13 — Act I (Rogue Encampment), any difficulty: level 276 Heroic Duncraig 4; consumed; current calc1 formula evaluates to 0, so flag a failure.
- [ ] `b80X` Scroll of Summoning 14 — Act I (Rogue Encampment), any difficulty: level 277 Mindflayer's Arena; consumed.
- [ ] `b81X` Scroll of Summoning 15 — Act I (Rogue Encampment), any difficulty: level 279 Eyegouger's Arena; consumed.
- [ ] `b82X` Scroll of Summoning 16 — Act I (Rogue Encampment), any difficulty: level 278 Bonehexer's Arena; consumed.

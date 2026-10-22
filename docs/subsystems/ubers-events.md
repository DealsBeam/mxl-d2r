# Ubers, endgame areas, goblins and events (research lane, 2026-10-21)

Source: `D2Sigma.dll` (1.10, base 0x10000000, Ghidra read-only copy), tables from `extract/mxl`, `work/named`, `work/sigma`.
Addresses are D2Sigma RVAs-as-VAs (0x1005xxxx) unless marked otherwise; **D2R addresses: "to find"** throughout.
Median level numbers are the `levels.bin` row numbers (what Median's formulas use); from 137 up the D2R id is the
Median number + 1 (`LevelIds` in `plugin/src/mxl_items.h`; 137 is skipped). Lists below use Median numbers.
Format follows [quests-requirements.md](quests-requirements.md) and [npc-hirelings.md](npc-hirelings.md).

## Short version

- **There is no "uber module" in D2Sigma.** Median's endgame is almost entirely **data**: levels, DS1 map presets (574
  files already extracted), monsters, treasure classes, items, and cube recipes whose conditions are item stats and
  player craft flags. The only engine code behind it is small and generic:
  1. **Keys, scrolls, riftstones** open the areas: item spell 16 = "portal to level calc1" (already ported,
     `item_spells.cpp`; 40+ items, map in section 2).
  2. **The dungeon lockout** (D2Dungeon.cpp + a few lines in D2DrlgWarp.cpp, the warp/portal object functions and a client
     `DungeonWidget`): a per-level clock that starts when a portal into the level is used and seals the level (and its
     lock group) for the rest of the game when it runs out; plus per-level entry requirements (minimum difficulty, level
     requirement per difficulty, class lock). **Not ported, nothing in D2R does it.** This is the one real engine job.
  3. **Treasure goblins** (D2Goblins.cpp): a 15-second per-game tick that may spawn a goblin next to an active player.
     20 goblin rows exist in data (Treasure Goblin ... Rift Inspector 12). **Not ported.**
  4. **Halloween event** (objects 587/588 `halloween_chest`/`halloween_exit` (D2R 601/602) with object functions 36/37, the kill-currency
     system D2Currency.cpp, level `whimsyshire_invaded`, rainbow portal): small code, event-only. **Defer.**
  5. D2SpecialEvents.cpp is not an event module: it is a per-game **join filter** (minimum level, name black/white lists) for
     the D2GS server; D2Events.cpp is a tiny "call this at game frame N" queue. D2Achievements.cpp only bumps a counter.
     All **cut/defer**.
- **The vanilla ubers (rows 704-709, `ubermephisto` ... `uberbaal`) are dead leftovers.** No level, superunique, code or
  treasure class references them, and no Median key opens Uber Tristram. What Median calls ubers are **Uber Butcher /
  Uber Laser (Death Projector) / Uber Infernal (Infernal Machine)**, plain monsters in levels 250-252 reached with
  Scrolls of Summoning. (Open question: make sure D2R's own Pandemonium/uber code stays inert with this data.)
- **Trophies, signets, sigils, marks, relics are items, not code.** Signets use the vanilla "add stat" potion spell
  (pSpell 3, data). Trophies/sigils/marks/riftstones are inert `thng` items (help text only); progression is the cube:
  99 recipes feed trophies into the unique `TrophyCarrier2`, others turn riftstones/dimensional keys into flags, all
  through cube conditions that mxl-core 0.37.0 already evaluates. The flags are set by recipe output slots (byte 7 player / 9 game flag; plugin-spec.md "Cube conditions"); they are
  once-per-game guards on crafts, not area gates, and live in game memory only.
- Rough total: data/converter ~3-4 days, lockout ~1.5-2 weeks (hook + state + UI), goblins ~3-5 days, Halloween ~1 week,
  everything else cut. Details in section 6.

## 1. Where each piece lives

| Piece | D2Sigma code | Tables / data | State today |
|---|---|---|---|
| Keys, scrolls of summoning, riftstones, links (open an area) | item spell 16: handler 0x1007f720, red portal 0x10097870 -> 0x10097750 (object 0x24a = 586 one-way portal, once per level and game via DRLG level +0x230 bit 4), object fns 15/35 (0x96fa0/0x96e80) | misc.txt `pSpell 16`, calc1 = level, misc2 | **ported** (plugin 0.13.0, 0.19.0) |
| Dungeon lockout clock and sealing | D2Dungeon.cpp 0x5da80..0x5e2d0 (state array at game +0x1b24, 12 bytes per level), start 0x5d9d0 -> 0x5df20, warp check 0x5d810 / 0x5cee0 / 0x5d150 (D2DrlgWarp.cpp) | levels.bin extension fields (section 3) | **server rules ported** (plugin/src/dungeon_lockout.cpp, lane dungeon-lockout; section 3a); timer UI open |
| Lockout UI | client `DungeonWidget:*` (0xf4bb0, 0xf4fc0), packet 0x88 | UI layout (not in our extract) | **open** |
| Treasure goblins | D2Goblins.cpp 0x66120 (tick), 0x66070 (chance), 0x65de0 (pick), 0x65be0 (rebuild candidates); tick table 0x101d90ec run by 0x66230 (D2GS.cpp) | levels `GoblinChance` (+0x15c), monstats3 goblin fields (section 4) | **open** (monsters exist) |
| Trophies, sigils, marks, relics, riftstones | none (items); cube checker 0x10059d00 | misc.txt, cubemain/cubemainext, uniqueitems `TrophyCarrier2`, `Fist *`/`Lab *` treasure classes | cube conditions **ported** 0.37.0; flag setters = cubemain output slots, spec in plugin-spec.md (needs a D2R hook) |
| Signets | vanilla pSpell 3 | misc.txt (about 30 rows: gold, experience, the learning family, skill) | **data** (D2R handles pSpell 3; to verify) |
| Boss rewards (Marks of Victory, trophies, keys) | none | treasureclassex, monstats `TreasureClass1(H)`, superuniques `TC(H)` | **data** |
| Halloween event | object fns 36 (0x96b40) and 37 (0x969e0), D2SpecialEvents map at game +0x1b2c, currency sync tick 0x5b800 (packet 0x86), HUD `HalloweenCurrencyBalance*` | objects2 `OperateCurrency*` (chest costs 2300 of currency 102024), monstats3 `Currency*` (68 monsters), level 246 | **open / defer** |
| Join filter ("special events") | D2SpecialEvents.cpp 0xb16a0 (create), 0xb1830 (check), 0xb1a10 (send, packet 0x87), 0xb1940 (free) | none | cut |
| Achievements | D2Achievements.cpp 0x49c00 / 0x49c40 (counter stat 0x58 on kills) | monstats3 `Achievement` (479 monsters) | cut |
| Monster sounds on spawn/death, unique mod counter | D2Monsters.cpp 0x8c360 / 0x8c3f0, D2MonUMods.cpp 0x8c580 | monstats3 sound fields | defer (audio / bookkeeping) |
| Shrines | D2Shrines.cpp 0xa5a30 / 0xa5a90 (restore function, random pick) | shrines.txt | vanilla behaviour, nothing Median-specific found |
| Town table | D2Town.cpp 0xb6090 (act -> town level 1/40/75/103/109) | levels `+0x14b` town flag (9 rows) | read: vanilla |

## 2. The content, in the order a player meets it

All "opened by" statements are from the migrated tables; "drops" from `treasureclassex.txt`. Bosses are placed by DS1
presets (not in the text tables), so the exact spawn spots are an asset question, not a code one.

1. **Treasure goblins** (any act, from the first area). See section 4. Roster: treasure_goblin, shiny_hoarder,
   voided_horror (level 100+), voided_spawn, arcane_collector, glowing_baron (up to level 99), relic_thief (100-119) and
   relic_thief_2 (120+), twelve rift_inspectors (each tied to one level), and the rainbow_goblin (Halloween only).
2. **Act boss keys / Pandemonium Keys 1-3 (`gc06`-`gc08`)**: containers (pSpell 18, ported). Mephisto Key `mss` is a
   quest item. Pandemonium levels 133-136 still exist in levels.txt but no Median item opens them.
3. **Realm of Sin (166) and Realm of Lies (176)**: opened by `key_of_sin` (Ghom, Hell drop `Ghom (H)`) and `key_of_lies`
   (Maghda, `Maghda (H)`); keys are pSpell 16 with level requirement 125. **Fauztinville (155)** by `mind_warp` or the
   black road. Ported (item spell) plus data.
4. **Uber Butcher / Laser / Infernal**: Scrolls of Summoning 1-3 (`b60X`-`b62X`) open levels 250 `butcher_s_lair_2`,
   251 `proving_grounds_2`, 252 `infernal_cave_2` (lock times 60 / 120 / 60 s). Bosses `the_butcher_2` (lvl 60),
   `death_projector_3` (lvl 70), `infernal_machine_6` (lvl 65) drop `Uber Butcher/Laser/Infernal Reward` in Hell =
   **Marks of Victory 1-3** (`vrx1`-`vrx3`). `mark_of_fate` (`vrx4`) is made by a 5x `f667` recipe;
   the marks (and `mark_of_sanctity` `vrx5`) are cube ingredients for upgrades of the `HotTJ` item (cube recipes with
   op 17/18 checks on stats 453/454).
5. **Dimensional Labyrinth (229-238, ten levels), Twisted Sanctum (239), Dimensional Plane (240)**: one charged
   riftstone per level (`cr06`-`cr15` -> 229-238), `otherworldly_link` (`ktpx`) -> 240. Level requirements 126-135
   (Hell), lock 20 s each (sealed after the clock). Trash drops depleted/charged riftstones (`Lab Trash 6-15`,
   `Navigator 2-4`, `Riftwalker 3`), `Lab UMO` drops `sigil_of_absolution` (`ir82`); recipes use the Dimensional Key
   (`eby`/`ebw`, item stats 450-454 as counters) with riftstones. Storm areas 224-228 use `cr01`-`cr05`;
   Court of the Damned 227 also `call_of_the_damned`. Edge of Nothingness 215 by `dimensional_link`. **Frozen Crest
   (242)** by `gaya_wand`; **Trials of Vasily (243) and Bul-Kathos (244)** (lock 20 s, level req 154) and **Heart of
   Darkness (247, `cr21`)**, End of Time (248).
6. **Heroic Library of Fate (262-268)** (`b63X`-`b68X`, level req 120-147, the last two share lock group 1),
   **Heroic Duncraig (273-276)** (`b70X`-`b73X`, req 127-145) and the **three arenas** 277 mindflayer / 279 eyegouger /
   278 bonehexer (`b80X`/`b81X`/`b82X`, lock 300 s, req 139). Bosses `emissary_of_assur_*` drop `Boss 1-5 Reward` =
   the five sigils (`l@l`, `m@m`, `o@o`, `p@p`, `q@q`), the arena bosses drop `heroic_duncraig_boss_trophy` 1-5
   (`b74X`-`b78X`) and `Mindflayer/Eyegouger/Bonehexer Chest`s drop the next scrolls. The sigils feed the
   `BossKillerRing` cube recipes. **Mirror Temple (255-261)**: seven areas, each class-locked (class lock +0x15e = 1..7).
7. **Fist bosses and trophies**: the multi-phase bosses (`kabraxis_the_thief_of_hope_3..7`, `malthael_4..8`,
   `bartuc_2..6`, `azmodan_lord_of_sin_2..6`, `quov_tsin_3..12`, `assur_s_avatar_2..6`, Hell treasure classes
   `Fist <name> 1/2`) drop the named trophies (`archbishop_lazarus`, `banisher_of_light`, `judgement_day`,
   `legacy_of_blood`, `heart_of_sin`, `quov_tsin`, `duncraig`). 20 trophies exist (`[01`-`[20`). Cube recipes with
   input `ths,uni` (the unique **TrophyCarrier2**, level req 125, carry limit 1) record them in the carrier's item
   stats 450-453 (99 recipes, ops 15/18 compare, "useitem" outputs).
8. **Signets**: pSpell 3 items (`signet_of_gold`, `signet_of_experience`, the `*_of_learning` family with calc1
   `min(1, 400 - stat 185)` = a lifetime cap, `signet_of_skill` capped by stat 186). Pure data if D2R's pSpell 3 reads
   stat1/calc1 the same way (it does for the vanilla potions of gold/experience; to verify).
9. **Halloween** (`whimsyshire_invaded` 246, rainbow portal, `herder_goblin`s, candy currency): see section 5.

## 3. The lockout and entry requirements (D2Dungeon.cpp, D2DrlgWarp.cpp)

### Level record extension (levels.bin rows are 0x220 bytes; the vanilla layout ends at 0x14a)
Names come from the table loader strings (QuestLock, QuestLockGroup, PortalLevel, PortalObject, LevelReq (N/NM/H),
AlwaysDisplayName, GoblinChance); the **meaning below is from how the code uses each offset**:

| Offset | Type | Use (confirmed from code unless marked) |
|---|---|---|
| +0x14b | u8 | town flag (cube op 36; 9 rows) |
| +0x151 | u8 | minimum difficulty to enter (0 N, 1 NM, 2 H); 0x5cee0 |
| +0x153 | u16 | **lock time**, seconds (QuestLock; 66 levels: 10-1200); read by 0x5dc40 for the client and by 0x5d9d0 |
| +0x154, +0x156 | u8, u16 | portal level / portal object (not confirmed; +0x156 = 585 only on level 246 = the rainbow portal) |
| +0x155 | u8 | flag set on harder areas (use not found) |
| +0x158..0x15a | u8 x3 | **level requirement** Normal / Nightmare / Hell; 0x5cee0 compares the player's level against byte +0x158 + difficulty |
| +0x15c | u16 | **GoblinChance** (0x66070; 161 levels, 4-16) |
| +0x15e | u8 | **class lock**: only class (value - 1) may take the warp (Mirror Temple, values 1-7); 0x5cee0 |
| +0x15f | u8 | **lock group** (QuestLockGroup): levels with the same non-zero group are locked together (Heroic Library 6 and 7) |

### State and flow
- Per game, a 12-byte record per level at game +0x1b24: u32 flags, u32 end time (GetTickCount), u32 spare. Flag bits:
  1 = clock running, 2 = **sealed** (checked by the warp test 0x5d810), 4 = in progress (cleared by the second timed
  event 0x5e2d0). (Exact bit 1/4 roles inferred; bit 2 is certain.)
- **Start** (0x5d9d0, called from the red portal and one-way portal object functions after the portal is used): if the
  player is a real player and the level's lock time is non-zero, 0x5df20 starts the clock: registers two timed game
  events (0x5e180 at the end, 0x5e2d0), end = now + lock x 1000 ms, and sets the flags on the level (or every level of
  its lock group). Nothing happens if the level is already running or sealed.
- **Expire** (0x5e180): seals the level (all levels of the group) and broadcasts the state.
- **Warp test** (0x5d810, from the DRLG warp code 0x5d150): the player must have seen the source level, and a sealed
  level refuses entry with a message (0x13); 0x5cee0 adds the difficulty / level / class checks. Three vanilla special
  cases (levels 73 Duriel's Lair, 100 Mephisto 1, 132 World Stone Keep) keep their quest checks.
- **To the client**: packet 0x88, 60 levels per packet: type 1 = the flags, type 2 = milliseconds left. Functions 0x5dc90
  (flags) and 0x5ddc0 (times) run when a player joins and on every change (0x65770 / 0x65780).
- **Client widget**: `DungeonWidget` (caption title, caption, "on entering level" text, timer text, lockout popup and
  its primer). The timer is the visible product of the clock.
- **The 20 s levels** (Labyrinth, Trials) look like an "entry window" after which the level is sealed for latecomers;
  the 600-1200 s levels (World Stone 1200, Dimensional Plane 600, Uldyssian's Tomb 900, Heart of Darkness 840) look like
  time limits. Same mechanism, different values; the unit is seconds in the code (checked in 0x5df20).

### 3a. Port status (lane dungeon-lockout)

Corrections found while reading the code for the port (all from the D2Sigma decompile):
- **The clock starts on every successful entry**, not only on portals: the tile warp function 0x5cf90 (calls the warp test
  0x5d810, then moves the player, then 0x5d9d0), the portal object function 0x96e80 (fn 35; test 0x5d810, set the move
  message, 0x5d9d0) and the item teleport 0x7fb70 (no test) all call 0x5d9d0 with the destination level. A level with a lock
  time therefore counts down from the first player who walks in, by stairs, tile or portal.
- **Per game, not per player.** The 12-byte records are at game +0x1b24; any player entering starts the clock for everybody.
  Sealed = flag 2 set when the first timed event (0x5e180) fires; the second event (0x5e2d0) only clears the "starting" bit 4.
  Players already inside stay; the clock does not eject anybody. It only refuses entry (stairs, tiles, portals) afterwards.
  Leaving and coming back during the countdown is allowed.
- **Test 0x5d810 (player entering level `dst`)** refuses when: (1) 0x5cee0 fails: player level (stat 12, capped by an engine value)
  below the level's requirement for the game difficulty (+0x158 + difficulty), difficulty below +0x151, or class lock (+0x15e)
  not equal to class + 1; (2) the level has a lock time (+0x153 non-zero) and its flag 2 is set; (3) a quest-table condition:
  the 0x9c-byte table at TxtRecords +0xc60 (vanilla LevelDefBin, row = level) has a non-zero quest id at +4 and the player
  has none of quest bits 1, 0, 0xe, 0xf for it. (3) **is ported** (`Entry::questLock`, `dungeon::QuestAllows`): the table is
  `leveldefs.bin` (QuestFlagEx at +4 of each 0x9c-byte row, read by tools/sigma_data.py): 23 levels name a quest (46 and 66-73: quest 13;
  50-54 and 74: quest 11; 100-102: quest 21; 128-132: quest 39). D2R's own check (0x516a50: levels 73 / 100 / 118 / 128 / 132) still runs
  in front; the plugin adds the rest. The three Sigma special cases (73, 100, 132) are D2R's own.
- **What the player sees**: a refusal sets the player's pending unit message 0x13 (0x100b15d0 stores it at +0x6e / +0x70 and
  sets flag 0x400), which the client turns into its lockout popup; the clock and its title text are the client
  `DungeonWidget` fed by packet 0x88 (type 1 flags, type 2 milliseconds left, 60 levels per packet, sent on join and on
  every change). The 20-second levels are real timers with the same widget.

Implemented (mxl-core, switch word `lockout`, report on the `mxl` command and the log):
- Hook 0x491ff0 (warp action, entry `48 8b c4 53 55 57 41 56 48 83 ec 68 49 8b d8`, 15 bytes): works out the destination level
  from the linked tile, refuses (returns without the original) when Median's test refuses, and starts the clock when the
  player's level changed afterwards.
- Hook 0x58f680 (portal object operate function, entry `48 89 4c 24 08 55 53 56 57 41 56 41 57 48 8b ec`, 16 bytes): same
  test on the portal's destination word (0x34ad40); refusal = D2R's failure exit (unit message 0x14, return 0); clock starts
  after a successful move. Town portals and red portals use it. The one-way portal (object 600) uses it only if its operate
  function in objects.txt is the same one: to confirm in game.
- Rules in plugin/src/dungeon_logic.h (162 levels from mxl_dungeons.h): requirements, seal, group locking (levels with the same
  non-zero group take the starter's end time), per-game reset by game pointer or frame counter going backwards. The seal is
  checked lazily against GetTickCount64 (Median: game events, same wall clock; only difference: Median's events are frame
  based, so a paused server would differ). Unit tests: plugin/tests/dungeon_test.cpp.
- Message: the generic failure message 0x14 (`0x491960`, D2R shows "Impossible.") and, since lane client-ui, text lines in the
  player's message log through D2R's chat packet 0x26 (d2r-addresses.md section 13): refusal for requirements "(Required Level: N)"
  (string 11189) / "Requires Hell difficulty" / "Only for the <class>"; refusal for a sealed level "This area is locked" + "Join another
  game to enter again" (11192); at the clock start, to every player in the game, "Closing in: mm:ss" (11191) and "ONCE YOU EXIT YOU
  CANNOT GO BACK" (11193). Median's 0x13 and packet 0x88 have no D2R counterpart: no packet is invented. Verified in game
  (slot 2, Hell rules, level 253: "Closing in: 01:00", then after 68 s "This area is locked / Join another game to enter again").

Left:
- Live timer and popup art (packet 0x88 DungeonWidget): the clock is a single log line at the start. A running countdown needs the
  PluginSDK OverlayService (drawText from a frame callback) fed by a NetworkService channel (`sendToClient`, 224 bytes); popup art is
  custom art (last).
- The 0x7fb70 item teleport (clock start only): it is the pSpell 17 item spell ("cherry" `kpac`, table 0x101d95b0), which the plugin
  refuses (item_spells.cpp); the level it targets (218) has no lock time, so there is nothing to start yet. Port with the spell.
- Per-game reset: the seals are dropped on the game-end event (`game_end.cpp`, client teardown `0x486be0`; `0x4057c0` itself
  has a rip-relative prologue). The game pointer / frame regress reset stays as a second guard.
- Confirmed in game (lane client-ui): 0x14 shows "Impossible." in the log. Still open: the one-way portal object.

## 4. Treasure goblins (D2Goblins.cpp)

- **Tick**: D2GS.cpp 0x66230 runs a table of four periodic functions at 0x101d90ec once per game frame:
  entries are (period in seconds x 25 frames, function): 15 s Guard no-op, 5 s currency sync (0x5b800; sends packet 0x86
  with each changed currency balance), 15 s Guard check, **15 s goblins (0x66120)**. D2Guard is anti-cheat (cut).
- **0x66120 (per player, every 15 s)**: skips dead or dying players (0xbf440), players in a town (0xbf8a0) and players who have not killed anything in the last 375 frames (15 s; the player's last-kill frame sits in the player data +0x28, `NoKillTimestampUpdate` in monstats opts a monster out of updating it). If the chance roll passes it picks
  a goblin, finds a spot near the player (radius 8), spawns it (`0x10042630`, monster id, x, y) and sends the
  client a 0x28-byte packet 0x5a type 0x15 with the new unit id (the "goblin sighted" notice).
- **Chance (0x66070)** (decoded 2026-10-21): `chance - chance * (10 * players - 10) / 100` (0x88e50 is `a * b / 100`; `players` = game +0x8c,
  the player count), at least 1; the roll is `rand < chance` out of 0x400 (1024). So 10% less per extra player (the tick is per player).
  Area monster level for the pick = levels +0x16 + 2 x difficulty (u16 x3: Blood Moor 1 / 51 / 100; copied into `mxl_goblins.h` as `monLevel`).
- **Pick (0x65de0)**: from a candidate list of monstats rows (rebuilt by 0x65be0) whose monstats3 flag word has bit 0x400
  set (and not bit 0x1000 = rainbow/Halloween only), with the area's monster level (levels record +0x16 + 2 x
  difficulty) within [min +0x19, max +0x1b] (0 = no bound) and level restrict (+0x1d: -1 any, else that level id).
  Each row appears `rarity` (+0x17) times in the draw list. The monster must be a valid row (0x8c500).
- **Rows** (monstats3, 20 with flag 0x400): treasure_goblin (rarity 2325), shiny_hoarder (184), voided_horror (184, min 100),
  voided_spawn (0), arcane_collector (300), glowing_baron (553, max 99), relic_thief (4, 100-119), relic_thief_2 (10, 120+),
  rainbow_goblin (flag 0x1000), rift_inspector 1-12 (restrict to levels 139, 39, 140, 141, 146, 148, 150, 151, 270, 154,
  155; the 5th at monster levels 116-117). The goblin treasure classes are `Goblin`, `Gob Rainbow`, `SoH Design`.
- Rarity confirmed from 0x65de0: the u16 at +0x17 is the number of times the row is added to the draw list (a plain weight; 2325 vs 184 / 300 / 553
  makes the treasure goblin about 69% of the draws below monster level 100). The pick is uniform over that list.
- **Last-kill stamp** (D2Achievements.cpp 0x49c40, called per kill): when a player (or the owner of a minion, walked up the owner chain) kills a
  monster whose monstats3 byte +0x20 (`NoKillTimestampUpdate`, 177 rows incl. the goblins themselves and summons) is 0, the frame goes into
  the player data +0x28 (0xc04c0). The tick needs `frame - stamp < 0x177`.
- Drops and behaviour after the spawn are pure data (monstats `TreasureClass1` "Goblin", "Goblin (N)/(H)", AI `OblivionKnight`, velocity 7);
  D2Goblins.cpp has no despawn, flee or death code. The client notice (packet 0x5a type 0x15, "goblin sighted") is Median-client only.

### D2R port (plugin 0.40.0, `plugin/src/goblins.cpp`, switch word `goblins`)

- Tick: detour of the server game frame `0x406550` (14 entry bytes `48 89 74 24 20 55 57 41 57 48 8d 6c 24 b9`), original first, then
  `game+0x170 % 375 == 0` runs the per-player turn. Chosen over a kill-wrapper roll because it is the exact twin of Median's table entry (a
  15 s clock independent of kills, all players of the game) and the hook sits at a plain prologue; if the install fails the kill wrapper runs
  the same tick once per 375-frame bucket (implemented as the fallback).
- Stamp: pre-hook of the kill wrapper `0x515640` (15 bytes `48 89 5c 24 10 48 89 6c 24 18 57 48 83 ec 70`); the owner of a minion killer is
  found with `0x4a5480(unit, &id, &err)` + `0x48fe80(game, 0, id)` exactly as the wrapper does.
- Turn per player: not dead (unit mode `+0xc` is 0 / 17; `0x34ab60` reads the mode, **not** the unit id), room `0x34b440`, level `0x2efc10`, not in
  a town (levels with the town flag: 1, 40, 75, 103, 109, 138, 139, 242, 243), recent kill, chance roll, pick, spot = player position + (8, 8)
  in `0x38dfb0(room, x, y)`, then `0x4929e0(game, room, x, y, class, 1, 2, 0, 0)` (radius arg 2 lets `0x493cc0` search rings of 3 and 6 sub-tiles
  for a free spot; Median's call is the exact point). Drops: the monster's own monstats treasure classes, nothing hooked.
- State: last-kill frames per player id (16 slots), reset when the game pointer changes or the frame counter goes backwards (a new game on a
  reused address). The game-end event of `game_end.cpp` (`0x486be0`) is not wired in here yet; `0x4057c0` has a rip-relative
  load inside its first 14 bytes.
- Not ported: the "goblin sighted" packet. Testing aid: environment variable `MXL_GOBLINS_FORCE=1` (every roll passes) or `=2` (also no
  last-kill requirement).

## 5. Halloween event and currency (D2Currency.cpp, object functions 36/37)

- **Currency**: monstats3 gives 68 monsters (zombie, temptress, ravager and forsaken-one families) 1-14 units of
  currency type 102024 on kill (`CurrencyType/Amount/Unowned`); balances sit in a per-game map (+0x1b34), synced to the
  client every 5 s (packet 0x86) and shown by the HUD (`HalloweenCurrencyBalance*`). `KillCurrencyBank/Add/Shared` and
  `OperateCurrencyBank/Cost/Shared` are the table headers; D2Currency.cpp is "partial" in the triage (containers only).
- **halloween_chest (object 587 / D2R 601, fn 36 = 0x96b40)**: costs 2300 units (objects2). Operating it sets the object to
  its open state (packet 0x2c), schedules two game events (+150 frames and a second one) and queues a callback through
  D2Events (0x5e470).
- **halloween_exit (object 588 / D2R 602, fn 37 = 0x969e0)**: sets a per-player flag in the D2SpecialEvents map (key 0x134da05) the
  first time, opens the exit and schedules an event 375 frames later.
- **Level 246 whimsyshire_invaded**: level record +0x156 = 585 (rainbow portal, fn 15 = 0x96fa0), `mon1` treasure_goblin.
  How the event is switched on and off (date? realm setting?) was not found; cube ops 1/2 (date, weekday) exist in D2R and
  are not used for it.
- Verdict: a closed mini-feature. Defer until a realm exists.

## 6. Port plan

Sizes: S <= 1 day, M 2-4 days, L about 1-2 weeks.

| # | Item | Bucket | Size | Notes |
|---|---|---|---|---|
| 1 | Keep keys/scrolls/riftstones as item spells (done); add a **test list** of the 40 pSpell-16 items -> levels (section 2) | a | S | gives the area-by-area smoke test; levels 215+ need LevelIds |
| 2 | Converter step `tools/level_ext.py`: levels.bin -> `plugin/src/mxl_levels_ext.h` {lock seconds, lock group, min difficulty, level req x3, class lock, goblin chance} keyed by D2R level id (like `cube_tables.py`) | a | S-M | no data hits D2R's own levels.txt; only the plugin reads it |
| 3 | Same for goblins: monstats3 -> `mxl_goblins.h` {monster id, rarity, min, max, level restrict, flags} | a | S | uses the monster id map |
| 4 | Craft-flag setters: the recipe output slots (slot byte 7 = player flag, 9 = game flag, 6 = pay gold) need a D2R hook on the cube output dispatcher or transmute entry plus a generated `{recipe, kind, param}` table (cube_tables.py reads cubemain.bin); without it the 19 op-38 recipes (6350-6368) stay locked and the once-per-game key crafts can repeat. Area gating is **not** affected | b/c | S-M | spec in plugin-spec "Cube conditions" (Port spec); D2R address to find |
| 5 | Verify signets (pSpell 3), trophies in `TrophyCarrier2`, "useitem" cube outputs on D2R (the 99 trophy recipes) | a (test) | S | likely works; needs an in-game check |
| 6 | **Lockout server logic**: per-game state (12 bytes x levels), clock start on portal use, expiry events (D2R timed-event API: to find), sealing, warp test + level / difficulty / class requirements | c | L | hooks: portal-object operate for the red and one-way portals (same hooks as item_spells), the level-entry / warp function (to find), a D2R per-game storage slot (to find; D2Sigma used game +0x1b24) |
| 7 | **Lockout client**: DungeonWidget (timer, title/caption, lockout popup) + the state packet; D2RLoader packet service or a game-message path (to find) | b + d | L | can ship after 6 with a chat/system message instead of the widget (S) |
| 8 | **Treasure goblins**: 15 s per-player tick (D2R game tick hook, to find), kill-timestamp (monster-killed time per player), chance/pick (small), spawn next to a player (D2R spawn monster at x/y, to find), "goblin sighted" notice (message/sound) | c | M | pure plugin logic once the tick and spawn APIs are found; data from item 3 |
| 9 | Halloween event: currency system (kill rewards, per-game balances, object cost), chest/exit object functions, event switch | b/c + d | L | start only with the realm plan (docs/realm-plan.md); needs a decision on whether Median's event ships at all |
| 10 | Achievements counter, monster spawn/death sounds, unique mod counter | e | - | no D2R equivalent worth the work (sounds are an audio-asset job) |
| 11 | D2SpecialEvents join filter, D2Guard ticks, D2Events queue | e | - | D2GS/admin features; D2R has its own scheduler (D2Events' role) |
| 12 | Vanilla uber rows 704-709 / Pandemonium levels 133-136 | e | S | check D2R's native code stays inert (open question 1), then ignore |

Order for a player: 1 (keys) -> 4 (flags) -> 6 (lockout, needed from the first Hell area with a lock time) -> 8 (goblins
appear from Act 1, so players notice their absence early) -> 7 -> 9. Item 2 is a prerequisite of 6 and 8.

## 7. Open questions

1. **D2R's native uber/Pandemonium code**: D2R spawns its own Uber Tristram bosses from its own keys. With Median's data
   (`pk` codes replaced, rows 704-709 kept, levels 133-136 kept) is any of it reachable or does it misfire? Needs an
   in-game check, and a look at D2R's Pandemonium handler (to find).
2. ~~Who sets the craft flags~~ answered: recipe output slots (plugin-spec "Cube conditions"). Remaining: the D2R address of the cube output dispatcher / transmute entry.
3. The unit and meaning of the lock time (seconds in code; are 20 s levels an entry window or a real time limit?) and
   the roles of lock-flag bits 1 and 4: confirm with a test game, or read the DungeonWidget layout strings and the
   `OnEnteringLevelText` string.
4. Level extension +0x154/+0x155/+0x156: portal level / portal object / flags (which one the red portal uses; plugin
   uses object 60 or the one-way portal today).
5. (answered, see section 4) Goblin chance reduction term and rarity field.
6. What switches the Halloween event on (date, realm setting, quest flag?) and what the currency 102024 item is.
7. How the three ubers and the Fist bosses are placed and phase-chained (DS1 presets and `BossEventMode` /
   `BossEventMissile1/2` fields in monstats; not read). Probably asset work, but the phase change of
   `kabraxis_*_3..7` etc. may need code (those rows are separate monsters chained by AI or by `SpecialEvent` tricks).
8. Are trophy effects applied by the carrier's item stats only (cube `useitem` mods), or does any code read the `trph`
   type (nothing found in D2Sigma by name)? The 99 recipes suggest data only.
9. D2R equivalents to find: per-game state slot, timed game events, game tick hook, spawn-monster-at-position,
   player kill timestamp, packet/message path for the lockout timer.

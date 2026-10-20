# NPC services and hirelings: what D2Sigma does and how to port it

Research lane, 2026-10-20. Source: `D2Sigma.dll` (1.10 build, base 0x10000000), read with the Ghidra scripts in
`tools/ghidra_scripts`; tables from `work/sigma` and `extract/mxl`. Modules: D2NPC.cpp (22 functions),
D2NpcActions.cpp (39), npc_speech.cpp (7), D2Interact.cpp (6), D2Hirelings.cpp (23), plus the client file
`ui\NpcMenu.cpp` and the packet handlers in D2PacketServer.cpp that call into them. No plugin code in this lane.
"Confirmed" means read from the code and matched against table data; "likely" means inferred; unresolved items are
under Open questions. D2R addresses are not known yet and are marked "to find".

Summary: Median has no special "Atma/Geglash service". Their rows in npcstores are ordinary shop rows for a
quality D2R's vendor columns cannot express (rare items), and their shops are switched off by a requirement that
is always false. The real NPC extension is a **data-driven menu**: npcext flags choose which of the standard
entries an NPC shows (shop, gamble, ...), and 16 menu slots per NPC run actions from npcactions (travel,
imbue/sockets/personalize, Cain's replacement materials, respec, grants of quest flags). Hirelings are mostly
data (hireling.txt is already converted); the code part is a handful of passive-stat bonuses applied on hire,
and the shared minion scaling that is already ported.

## 1. NPC side

### 1.1 Function map (D2Sigma)

| Address | Role | Confidence |
|---|---|---|
| 0x10093710 | builds the runtime NPC store records (0x44 bytes each) from npcext (49 rows), one per NPC, at game start; calls 0x10093fb0 for every NPC with the shop flag | confirmed |
| 0x10093fb0 | copies one NPC's npcstores rows (52-byte rows, filtered by the game difficulty byte at +9..+11) into two lists: always-in-stock (+8 set) and random | confirmed |
| 0x10093a30 | restock: first the always-in-stock items, then for each random row, per quality group: roll a count in [min,max], check the group's level window and requirement, create that many items of that quality; item level capped by the act's last area (levels 35, 46, 101, 107) plus 5; gives up after 32 failed creations | confirmed |
| 0x100933d0, 0x10093e50 | gamble: picks 10 random distinct candidates out of the NPC's gamble range, flags them, regenerates the ones that were bought | likely (the range lookup is a D2Common call, ordinal 10427) |
| 0x10093de0 / 0x10048730 | find the runtime store record / the npcext row by monster id | confirmed |
| 0x10094650, 0x100946c0 | "can trade" (flag bit 0, requirement at +0x0c), no-player and player forms | confirmed |
| 0x100943e0, 0x10094450 | "can gamble" (flag bit 1, requirement at +0x10) | confirmed |
| 0x100944e0, 0x10094550 | flag bit 2, requirement at +0x14 (set on the four mercenary hirers); entry label unidentified | confirmed flag, likely meaning |
| 0x100945e0, 0x10094630 | flag bit 3 (mercenary hirers plus Tyrael of act 4) | confirmed flag, unidentified use |
| 0x10094300, 0x10094370 | open the trade (0) or gamble (1) page for the player (D2Game side) | likely |
| 0x10092d20 / 0x10092e00 | allocate / free a per-game NPC pointer table (game +0x1b28) | confirmed |
| 0x10092f30, 0x10092fe0, 0x10093260 | per-NPC clean-up when the player leaves the NPC (run through a stored callback) | likely |
| 0x100948d0, 0x100947d0, 0x10094740 | menu slot "visible" test: reads the slot's first value as a requirement row and evaluates it (D2Requirements.cpp, 0x100a10f0) | confirmed |
| 0x10095a90, 0x100959c0 | menu slot "run": reads the slot's second value as an npcactions row and runs it through 0x100949d0 | confirmed |
| 0x100949d0 | action dispatcher: checks the action's own requirement, then calls the handler for its type from one of three tables (below) | confirmed |
| 0x1009a850 | server packet handler (size 13): kind 1 = open trade (bit 0), kind 2 = open gamble (bit 1), kind 10 = menu slot (slot number at +9: visible test, then run) | confirmed |
| 0x1009a9d0 | server packet handler (size 5): bit 3 gate, then 0x10042320 | confirmed gate |
| 0x10107b40 | client menu builder (`ui\NpcMenu.cpp`): entries and their order: Talk (10), Trade (20), Gamble (30), the bit-3 entry (40), the bit-2 entry (50), then the 16 slots | confirmed |
| 0x10107400 | client menu page (page 0 = the menu, page 1 = the slot list; slot callbacks send packet kind 10) | likely |
| 0x100484a0, 0x10048660, 0x10048780 and 4 small ones | NPC speech (below) | confirmed |
| 0x1006aef0 and 5 small ones | D2Interact: server click handler by target type (below) | confirmed |

### 1.2 Tables

**npcext** (49 rows, 336 bytes, row n extends npc.txt row n). Offsets:

| Offset | Meaning |
|---|---|
| +0x00 | monstats id |
| +0x04 | flags: bit 0 shop, bit 1 gamble, bit 2 (hirers, see above), bit 3 (hirers, Tyrael2) |
| +0x0c, +0x10, +0x14 | requirement row (`-1` or 0 = none) gating bit 0, 1, 2 |
| +0x18 u16 | default greeting string id (5382 = none) |
| +0x1a..0x27 | 7 u16: greeting per player class (5382 = fall back to the default) |
| +0x28 u16, +0x2a..0x37 | sound ids matching the greeting (0 = fall back to +0x28) |
| +0x38..0x43 | 6 u16 "idle" lines (5382 = empty slot); +0x44..0x4f the matching sounds |
| +0x50 | 16 menu slots of 16 bytes: value 1 = requirement row that shows the entry (-1 = always), value 2 = npcactions row to run, then two u16 (label string id, 5382), a u16 count/flag (0 or 10) and a u16 (60..63, probably a sort or icon value) |

Flags seen: shop on 21 NPCs, gamble on Gheed, Elzix, Alkor, Jamella, Drehya (Anya) and Nihlathak (Nihlathak is
gamble-only), bit 2 on Kashya, Greiz, Asheara, Qual-Kehk, bit 3 on those plus Tyrael of act 4.

**npcactions** (31 rows, 52 bytes): +0 type (1-21), +4 requirement row, +8 flag byte (1 = also send the "action
done" notice to the client UI; set on the travel rows and on the compound rows), +0x0c.. up to ten values.
Dispatch order in 0x100949d0: the "both" table first, then the server table (context flag byte +1), then the
client table (flag byte +5):

| Type | Handler (server / client) | What it does | Rows |
|---|---|---|---|
| 1 | 0x10095140 | compound: runs each non-zero value as another npcactions row | 23-25 (travel packages), 27-30 (Cain) |
| 3 | 0x10095200 (client) | show a text page (value 1 = page, value 2 = string id) | 26 (Cain: "crafting materials", string 28640) |
| 4 / 5 | 0x10095550, 0x100958f0 (server); 0x100954e0, 0x10095880 (client) | set / clear the listed quest-state bits (value 1 = quest, values 3.. = bits; skips bits already set/clear) | none in this data |
| 6, 7 | 0x10095620, 0x10094b60 | add / remove an item-stat style value (calls D2Game 0x100c05d0 / 0x100bd200) | none |
| 8 | 0x100957b0 (server) | travel: send the player to a town level (value 1 = level id 1, 40, 75, 103, 109) | 4-8 |
| 9 | 0x100953e0 | open / close a UI state (value 2 selects) | none |
| 10 | 0x10095710 (client) | set a UI tab or mode value | none |
| 11, 12, 13 | 0x10095470, 0x10095490, 0x100954b0 (client) | open a D2Client service page: mode 1 (Charsi, "imbue"), 3 (Drehya, "personalize"), 2 (Larzuk, "add sockets"); each calls one client function then stores the mode | 20, 21, 22 |
| 14 | 0x100954d0 (client) | forward to a D2Client function (Akara's second value: respec dialog) | 19 |
| 15 | 0x10094ab0 (server) | activate the waypoint of a town level (value 1) | 9-12 |
| 16 | 0x10095830 | internal "continue" step that passes the third context value on (value 1 = 2, 3 or 5) | 1-3 |
| 17 | 0x10095080 (server), 0x10095000 (client) | create an item by 4-letter code at the player (value 2 = quality 1-9, default 2; value 3 = item level, default area level); client prints the item's message | 15-18 (box, 3o[, 0o[, jcat) |
| 18, 19 | 0x100952e0, 0x10095360 (client) | confirm / check step with a string-id style value (row 13: 321) | 13 |
| 20 | 0x100956a0 (client) | show a system message by string id (row 14: 3650) | 14 |
| 21 | 0x10094be0 (server) | exchange: takes items from the player's inventory (listed codes / classes, value 1 = id, amount in value 2), pays currency stat 0x46 (or gold) per item, up to a cap | none in this data |

(Types 6, 7, 9, 10, 4, 5, 21 are implemented but unused by the shipped rows. They matter only if another table or
the quest module (D2Quests.cpp) calls them; port them last or not at all.)

**requirements** (33 rows, 44 bytes) is read through D2Requirements.cpp; another lane owns it. The NPC code needs
only: 0x100a11e0 (row number to yes/no; 0 = yes) and 0x100a10f0 (row to yes/no). Types used by the NPC rows:
1 any-of, 2 none-of, 3 all-of (value lists of row numbers); 4-8 compare a unit stat (==, <, <=, >, >=); 9 and 10
etc. through two pointer tables (0x10191338 / 0x101913d0, 6 entries); 30 (rows 1-7), 35 (player has item, code in
+4: box, 3o[, 0o[, jcat), **36** and 37. Types 36 and 37 are constants: 0x100560e0 is `return false`, 0x10036f50 is
`return true`. Requirement row 32 is type 36 (always false).

**npcstores**: the five quality groups are the item creation qualities 2 (normal), 4 (magic), 5 (set), 6 (rare),
7 (unique), in the 8-byte slots at +0x10, +0x18, +0x20, +0x28, +0x30 (min, max, lower area level, upper area level).
The 4-byte value before each group (+0x0c, +0x14, ...) is a requirement row; it is 0 in all 847 rows. So
`vendors.md` named +16/+17/+19 right (group 0) and +24/+25 (group 1, magic), and the "+40/+41 flags" are the
**rare group's min and max**. They are set on 224 rows of Atma (91), Geglash (121) and Cain act 2 (12); all with
min = max = 1. No row uses the set or unique groups.

### 1.3 Services Median adds beyond D2R

| Service | NPC | Game meaning | Driven by |
|---|---|---|---|
| Shop with Median stock | 21 NPCs | normal stock plus magic items and Median's currencies | npcstores, npcext bit 0; **done in data** (vendors.md) |
| Shops gated "never" | Atma, Geglash, Cain (act 2, 5), act2vendor1 (204) | would sell one copy of each of 224 rare sacred items; the requirement is always false, so the shop never opens in the shipped DLL | npcext +0x0c = 32; rows stay in npcstores |
| Gamble | Gheed, Elzix, Alkor, Jamella, Anya, Nihlathak | gamble page; Median's gamble.txt (already converted) | npcext bit 1 |
| Travel packages | Warriv (act 1), Warriv (act 2), Meshif (act 2, 3), Tyrael (act 4) | menu entries "go east / go west / sail east / sail west / travel to Harrogath"; each grants the waypoint of the target town and sends the player there; shown only when a quest requirement (rows 8-18) is true | slots with actions 4, 5, 23-25 |
| Respec | Akara | menu entry "Reset Stat/Skill Points" (string 11168), visible by requirement 26; runs a D2Client dialog (action type 14) | slot 0 of Akara |
| Imbue / Add sockets / Personalize | Charsi / Larzuk / Anya | menu entries (strings 4017, 22748, 22749) that open client service pages; these replace the vanilla quest rewards | action types 11-13 |
| Cain's replacement materials | Cain (the five Cain rows) | "crafting materials" opens a submenu: Horadric Cube, Catalyst of Disenchantment, Learning, Jewelcraft; each entry is shown only while the player lacks that item (requirement types 35 and 2) and creates it for free (type 17) | slots 0-4 of Cain rows, requirements 19-31 |
| Greetings, per-class lines, idle lines | 34 NPCs | text and sound shown when you talk to an NPC; chosen by player class; idle lines picked at random on a 5-second cadence | npcext +0x18.. |
| Mercenary entries | Kashya, Greiz, Asheara, Qual-Kehk | hire (bit 3) and the bit-2 entry (likely revive); Tyrael of act 4 has bit 3 too | npcext bits 2, 3 |

D2Interact.cpp is a vanilla-style server click handler: click target types 0, 1, 2 (player, monster, object)
forward to the same call (0x10042230, the unit-interact request); type 4 (item) checks the range (distance at
most 75 = 0x4b, else 52 = 0x34 for the others), moves the player next to the item, then picks up (0x1006acc0); type
5 (tile, 0x1006ae80) sends a refusal when too far. It is the engine's click path with Median's range limits and
auto-walk to items. Treat as **n/a**: D2R has its own click handling, and no Median table drives it.

### 1.4 How D2R's menus can host it

What D2R has: npc.txt prices (buy, sell, repair multipliers and quest-flag price steps, plus "max buy"); per-item
vendor columns (`<Npc>Min/Max/MagicMin/MagicMax/MagicLvl`); D2R's own menu entries (Talk, Trade, Gamble, Heal,
Repair, Hire, Resurrect) are built in native code per NPC class (the dialog panels `npcdialogpanel*.json`,
`vendorpanellayout*.json`, `hiremenupanel*.json` are generic list templates with no entry table). Two files the
game ships as templates, `customnpcs.template` (columns: class, ShouldTalk, talkAct, talkFile, talkWelcomeGreeting,
...; one example row, Kashya) and `customgossiptbl.template` (stringId, filter, questFilter, playerClass), look like
a gossip hook; whether D2R 3.3 actually reads them is unknown (open question 3).

| Median feature | Data-only | Needs a hook / service | Needs UI |
|---|---|---|---|
| Stock: normal and magic | yes (done) | | |
| Stock: rare group (Atma, Geglash, Cain) | no (D2R has no rare columns) | a restock hook; moot while the shops are gated off | |
| Gamble flag per NPC | check D2R: gamble NPC set may be fixed | hook on the "can gamble" test if not data | none |
| Greeting by class, idle lines | maybe (customgossip hook), else strings only | hook on the greeting selection (0x100484a0 equivalent) | none |
| Travel packages | partly: D2R already has Warriv/Meshif travel; Median's differ (waypoint grant, level ids) | hook that adds a menu entry and a "travel to level" call | a new entry in D2R's menu |
| Respec (Akara) | | the respec itself is D2Respec.cpp (4 functions, other lane); the menu entry needs a hook | one entry plus a confirm dialog |
| Imbue, sockets, personalize | | D2R already has socketing (Larzuk) and personalize (Anya) as quest/NPC pages; imbue (Charsi) is also a D2R page | new entries that open the existing pages |
| Cain's materials | | server: create item on request, requirement on inventory contents | one submenu |
| Requirements | | the evaluator is shared with quests (another lane) | |

## 2. Hirelings

### 2.1 Data

Median's hireling.txt (60 rows) is already converted into D2R's hireling.txt by `tools/txt_migrate.py` (Resist
splits into four columns; HireDesc becomes a string key through hiredescext; D2R-only columns take D2R's values).
Content: four hire classes, nine types, three difficulties, with level steps 3/36/90 (act 1), 12/43/90 (act 2),
15/49/90 (act 3) and 38/68/90 (act 5), where D2R has 135 rows:

| Class (monster) | Act | Types (hiredescext name) | Six skills per type (Skill1 is the attack) |
|---|---|---|---|
| 271 | 1 | rs1 Ranger, rs2 Priestess | barrage_2, barrage_2, dark_power_2 / vindicate_3, arrowside, trinity_arrow / heartseeker, skills_1852 |
| 338 | 2 | dm1 Exemplar, dm2 Shapeshifter, dm3 Fighter Mage | retaliate_3, werebear_morph_2 / sandstorm_2, blood_thorns_2, with 4 more each (lionheart_3, bloodlust_3, glacial_nova_3, ...) |
| 359 | 3 | es1 Necrolyte, es2 Bloodmage, es3 Abjurer | frigid_nova_2, pyroblast_3, forked_lightning_2, with 4 more each |
| 561 | 5 | barb Barbarian (a hire class vanilla does not have) | wraithsword, thunder_slam_3, ancients_hand_3, screaming_eagle_2, skills_1885 |

Costs per type 100-32,000 gold (sample); Median fills Share (0-6, the vanilla unused column).

**hireclasses** (5 rows of 29 bytes: row 0 is a blank, rows 1-4 = the four classes):

| Offset | Meaning | Values |
|---|---|---|
| +0 u16 | act index | 0, 1, 2, 4 |
| +2 u16 | flag 0/1 | 1 only for the iron-wolf-like class (act 3); read by 0x100fcb80 on the client (likely "uses mana" or a panel variant) |
| +4 i8 | character class the mercenary equips as | 0 amazon, 3 paladin, 1 sorceress, 4 barbarian (this is D2R's `equivalentcharclass`: ama, pal, sor, bar) |
| +5, +0x11 | two lists of 6 u16 state ids | A = 27 / 33, 34 / 30, 2 / 30; B = 112 / 212 / 198 |

Use of the lists: 0x10066fd0 returns true when the player has any of the fixed states (0x...cc0 list plus 15, 12)
or a state of list A with none of list B; 0x10067220 (hire test) and 0x1007e470 (D2Items.cpp, "can this merc
equip this item") call it. The state ids are not fully understood (open question 5). **hiredescext** (10 rows):
only a name string key per hiredesc row (`strhirespecial1..9`), already used by the converter.

### 2.2 What code does

1. **Class index of a monster** (0x10068930): short at +0x2a of the monster record (monstats3-style extension) is the
   hireclasses index (0 = not a hireling). The same record family answers "is this unit a mercenary"
   (0x100bf9e0) and gives the class row (0x100bde30). In D2R: monstats `hcIdx` (to find).
2. **Hire** (0x100672a0): refuses when the player already carries one of three blocking states (0x4c, 0x51, 0x50),
   requires the target to be a hireable unit, calls the game's hire function through a pointer (0x101f1ae8),
   prints one of three messages (0x55 can't, 0x56 fails, 0x57 hired), then 0x10066900. When the blocking state is
   set it dismisses the unit instead (0x10042060). 0x10067220 is the matching "may hire" test. Confirmed.
3. **Class bonuses after hire** (0x10066900) = the one hard-coded Median mercenary rule. On the new mercenary:
   - always: poison length resist (stat 0x6e) 200; item_addexperience (0x55) = 10 x level;
   - rogue (271): damage resist (0x24) = level/4; damage% (0x19) = dexterity/2; hp regen (0x4a) = 0.8 x dexterity (x256);
     velocity (0x43) +100; dodge (0x152) = min(dexterity/40, 60); energy (1) = (level+4)*6; slows attacker (0x16b) =
     level/5+25;
   - desert (338): skill armor% (0xab) 40; damage% = strength/2; hp regen = 0.8 x strength; velocity 80;
     energy 4 x level+16; crushing blow level/10+15; damage resist level/5+25; for the types with id
     8, 11, 14 fire and cold mastery (0x149, 0x14b) = level;
   - iron wolf (359): energy (level+9)*20; skill armor (0xab) (level+60)*5; stat 0x1e5 (level+50)*5; fire, cold and
     lightning mastery (0x149, 0x14b, 0x14a) = level;
   - barbarian (561): energy 2 x level+8; strength (0x167) level/4+20; dexterity (0x168) level/3+20; stat 0xb8 = 20.
   Stat ids are D2Sigma's (row order of Median's itemstatcost); in D2R they have to be matched by name.
   (Exact numbers above are from the decompile; recheck each before coding.)
4. **Death / revive** (0x10068020): on the mercenary, deletes five stats completely and halves another five (the
   list is the 0x10181d40 block). 0x10067e20 re-scales the on-attack / on-kill / gold-find stat layers (0xc3, 0xc4,
   0x124) by a count; the callers are the summon routines (0xad510, 0xadbd0, 0x683d0). Likely a penalty and a split of
   proc chances between minions; low confidence.
5. **Minion scaling** (0x100683d0 with 0x10067b30, 0x10068520, 0x10068120, 0x10068190, 0x10068250, 0x10067db0,
   0x10067bd0, 0x10067c60, 0x10067940): shared with summoned minions. **Already ported** (`plugin/src/pet_scaling.cpp`,
   plugin-spec.md "Minion scaling").
6. **Pet type tests** (0x10067760 = pettype row of a unit, 0x100676a0 / 0x10067820 = two bytes of pettypeext, 0x1005c640
   (D2Damage.cpp) = unit is one of 8 pettypes): belong to the minion lane, not hirelings.
7. 0x10068320 and 0x100681e0: run two stat-list lambdas over a minion, and play the skill's summon sound (skill +0xe4).
   Cosmetic.

### 2.3 Median vs vanilla, in short

- Vanilla 1.13: 3 acts, rogue / desert / iron wolf, fixed skills per type. Median: act 5 barbarian, 9 types with
  six Median skills each (all in the converted hireling.txt).
- Level and stat scaling per row is all in hireling.txt (HP, defense, AR, damage per level): data.
- The class bonuses in 2.2 step 3 are the only scaling that has no table.
- D2R extras that Median lacks: per-element resists (converted), `HiringMaxLevelDifference`, resurrect cost columns,
  `equivalentcharclass` (take from hireclasses +4; check the converter filled it for the barbarian rows).
- D2R's own mercenary equipment rules (head/torso/weapon/shield by class) come from D2R's tables; Median's Head/
  Torso/Weapon/Shield columns exist in hireling.txt and convert as data.

## 3. Vanilla functions hooked or replaced, and D2R equivalents

| Behaviour | D2Sigma function | Vanilla 1.13 equivalent (by behaviour) | D2R address |
|---|---|---|---|
| NPC store init | 0x10093710, 0x10093fb0 | D2Game NPC init from npc.txt and the item vendor columns | to find |
| Restock | 0x10093a30 | D2Game NPC inventory fill (item level, qualities) | to find |
| Gamble selection | 0x100933d0, 0x10093e50 | D2Game gamble list (gamble.txt) | to find |
| Open trade / gamble / menu slot packets | 0x1009a850 (0x38-style NPC packet), 0x1009a9d0 | D2Game packet handlers for NPC trade and hire | to find |
| Click on a unit | 0x1006aef0 and small ones | D2Game interact handler | n/a |
| NPC menu build (client) | 0x10107b40, 0x10107400 (`ui\NpcMenu.cpp`) | D2Client NPC menu (Talk, Trade, Gamble, Hire, ...) | to find |
| Greeting and idle lines | 0x100484a0, 0x10048660 | D2Client/D2Game NPC speech by class | to find |
| Hire, hire test | 0x100672a0, 0x10067220 | D2Game hire merc (cost, level limits) | to find |
| Equip test for mercenary | 0x1007e470 (D2Items.cpp) | D2Common item requirement test for the merc's class | to find |
| Merc class bonuses | 0x10066900 | none in vanilla | to find: stat add (D2R 0x2f34f0 / set 0x2f7d10 are known from pet scaling) |
| Requirement evaluator | 0x100a10f0 / 0x100a11e0 | none in vanilla | other lane |

## 4. Port plan, in the order a player meets it

**(a) Data only**
1. Shop stock with Median items (done). Add: nothing. Size: 0.
2. Mercenary rows, skills, costs, hire strings (done by the converter). Verify D2R's `equivalentcharclass`, level
   limits and resurrect cost columns for the 9 types; check the act 5 barbarian hire row is reachable. Size: S
   (a test session).
3. Gamble list (done), plus strings for menu labels (done with tbl2json).

**(b) D2RLoader service / plugin logic with no UI**
4. Mercenary class bonuses (2.2 step 3): hook merc creation and level-up, add the stats by D2R's stat ids; recompute
   on load (not saved). Size: M. Players see it the first time they hire.
5. Greetings by player class and idle lines: depends on whether D2R's gossip templates work; else a hook on the
   greeting choice. Size: S-M. Low priority.

**(c) Native hook in mxl-core**
6. Travel packages: hook on the NPC menu / travel call so Warriv, Meshif and Tyrael follow Median's rows (grant a
   waypoint, send to a level id), gated by quest flags (needs requirements, another lane). Size: M.
7. Cain's replacement materials: server-side "create item" action plus the per-item "player lacks it" test. Size: S
   once requirements exist; M with a menu hook.
8. Restock for the rare group: only if the gated shops are revived. Size: M. **Defer** (see 11).

**(d) UI rebuild**
9. NPC menu entries Median adds (Respec, Imbue, Sockets, Personalize, travel entries, Cain's submenu). D2R builds its
   menu in code, so this is a hook on the menu builder and the click path, with strings from Median's tables; the
   pages that open (Imbue, Sockets, Personalize) exist in D2R. Size: L. The first thing a player will miss is
   Akara's respec and the Charsi/Larzuk/Anya services; do those first.
10. Hire panel changes (Barbarian entry, hiredescext names): data plus an icon; the act 5 hirers are D2R's own NPCs
    (Qual-Kehk). Size: S.

**(e) Cut / defer**
11. Atma, Geglash, Cain (act 2, 5), act2vendor1 shops: gated off by requirement 36 in the shipped data. Do not port.
12. D2Interact.cpp: n/a. NPC action types 4-7, 9, 10, 21: unused by the data; port when the quest module needs them.
13. Hireling death penalty and proc split (2.2 step 4): defer until minions are tested (it is shared code with
    summons and may already match D2R's).

## 5. Open questions

1. What do npcext flag bits 2 and 3 do exactly, and what entries do they add (the two entries at priorities 50 and
   40; bit 3 also on Tyrael act 4)? Test in Median or read 0x10107f50 and 0x10094ab0's neighbours.
2. Are the NPC gamble sets for D2R the same as for Median (Median adds Nihlathak and Anya)? Is D2R's gamble set
   data, a hardcoded class list, or in npc.txt?
3. Does D2R 3.3 read `customnpcs.template` and `customgossiptbl.template` (a data path for greetings, or for menu
   entries)? A test with a copy named without ".template" would settle it.
4. Requirement types 9-35 and their meaning (quest, difficulty, item): needed to port travel and Cain; owner is the
   requirements/quests lane. Types 36 and 37 are the constants false and true.
5. What are the state ids in hireclasses lists A/B and the six-state list in 0x10066fd0 (taunt, might, prayer,
   energy shield, poison, rabies, two Median states): equip gating, aura-type buffs of the hired unit, or "already
   hired" markers?
6. Does the class bonus function run on every merc spawn (load from the save) or only at hire? Callers found:
   only 0x100672a0. If only at hire, D2Sigma must persist the stats in the save; check the Median save format
   (docs/save-format.md) before choosing where the plugin applies them.
7. Menu action types 18 and 19 (row 13: value 321): meaning of the confirm step.
8. Cain rows 244, 245, 246, 265, 520 all share the same five slots: which Cain appears in which act and whether D2R's
   Cain has the same monster ids (monstats rows 244-246, 265, 520 in the converted data).
9. 0x10067e20 (stat layer scaling by count): which minions it applies to.

## Hireling data check (2026-10-21)

All 60 converted rows follow D2R's 135-row hireling column layout (including the base bank): no shifted columns. Median's shared Resist and Resist/Lvl land in all four D2R elements; shared row fields, six skills per row, and the nine HireDesc mappings match the named table. Every skill exists in the converted skills.txt, and every NameFirst, NameLast and HireDesc key exists in the mod string JSONs. Act I and Act V equivalentcharclass values are ama and bar.

Mismatches (source row numbers, excluding header; column `equivalentcharclass`): rows 13–30, Act II, are blank instead of `pal`; rows 31–48, Act III, are blank instead of `sor`. No other checked row or column differs.

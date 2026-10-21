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

### 1.5 Median's NPC menu actions: the list and the port (lane npc-actions, 2026-10-21)

**The list** (npcext slots, 34 over 14 NPCs; npcactions rows; requirement rows are mxl_requirements.h, quest bit rows read
the game's difficulty unless a difficulty is named; D2R string ids come from tools/npc_actions_data.py). Slots with sub = 10 are
entries of the submenu that the top-level slot before them opens.

| NPC (class id) | Label (Median string id, D2R id, text) | Requirement | npcactions row (type) | What Median does server-side | Port |
|---|---|---|---|---|---|
| Akara (148) | 11168 / 33257 "Reset Stat/Skill Points" | 26 = AND(8: quest 1 bit 0 Den of Evil done; 15: quest 41 bit 1 respec available) | 19 (14) | D2Respec.cpp `0xa2330`: refunds every skill point (stat 5), puts the stat points back to the class start values, re-checks equipment (same sequence as D2R's `0x580f20`) | **done** |
| Cain, five classes (244, 245, 246, 265, 520) | 28640 / 40516 "crafting materials" (opens the submenu) | 31 = OR(27, 28, 29, 30) | 26 (3, text page) | none; the entries below | **done** (one line; see below) |
| ... Cain sub 1 | 28641 / 40517 "horadric cube" | 27 = AND(10: quest 4 bit 0 Search for Cain done; 22: player lacks `box `) | 27 -> 15 (17) | creates `box ` at the player (quality 1, item level 1) | **done** |
| ... Cain sub 2 | 28642 / 40518 "catalyst of disenchantment" | 28 = AND(10; 19: quest 6 bit 0 on Normal; 23: lacks `3o[ `) | 28 -> 16 (17) | creates `3o[ ` | **done** |
| ... Cain sub 3 | 28643 / 40519 "catalyst of learning" | 29 = AND(10; 20: quest 13 bit 0 on Nightmare; 24: lacks `0o[ `) | 29 -> 17 (17) | creates `0o[ ` | **done** |
| ... Cain sub 4 | 28644 / 40520 "catalyst of jewelcraft" | 30 = AND(10; 21: quest 25 bit 0 on Nightmare; 25: lacks `jcat`) | 30 -> 18 (17) | creates `jcat` | **done** |
| Charsi (154) | 4017 / 28265 "imbue" | 16 (quest 3 bit 1) | 20 (11) | client: opens the imbue page, mode 1 | **D2R native** (1.6) |
| Larzuk (511) | 22748 / 22748 "add sockets" | 17 (quest 35 bit 1) | 22 (13) | client: opens the sockets page | **D2R native** (1.6) |
| Drehya / Anya (512) | 22749 / 22749 "personalize" | 18 (quest 38 bit 1) | 21 (12) | client: opens the personalize page | **D2R native** (1.6) |
| Warriv act 1 (155) | 3382 "go east" | 11 (quest 6 bit 0) | 23 (1) = rows 5 (8: travel to level 40) + 1 (16: continue) + 9 (15: waypoint 40) | grants the Lut Gholein waypoint, moves the player there | **D2R native** (1.6) |
| Warriv act 2 (175) | 3383 "go west" | none | 4 (8) | travel to level 1 | **D2R native** (1.6) |
| Meshif act 2 (210) | 3384 "sail east" | 12 (quest 14 bit 0) | 24 (1) = rows 6 (level 75) + 2 + 10 (waypoint 75) | Kurast Docks + waypoint | **D2R native** (1.6) |
| Meshif act 3 (264) | 3385 "sail west" | none | 5 (8) | travel to level 40 | **D2R native** (1.6) |
| Tyrael act 4 (367) | 22738 "travel to harrogath" | 13 (quest 28 bit 0) | 25 (1) = rows 8 (level 109) + 3 + 12 (waypoint 109) | Harrogath + waypoint | **D2R native** (1.6) |

Everything else in npcactions is client-side confirmation: rows 13 (type 18, string 321) and 14 (type 20, string 3650) sit
in front of every Cain item row and only show a client message; types 4-7, 9, 10, 21 are used by no slot. Charsi's
mode 1, Anya's mode 3 and Larzuk's mode 2 in 1.2 are the client pages; in the data Charsi has type 11, Anya 12 and Larzuk 13.
The Median-only NPCs (Atma, Geglash, ...) have no slots.

**Why Cain is one line.** D2R's menu record allows five lines and the builder has no submenu. Median's page shows the entries
whose requirement holds. The port shows one "crafting materials" line; the server gives every material whose row holds
(cube after the rescue, each catalyst once its quest is done and while the player lacks it), so a Cain click is
"give me what I am owed". A real submenu needs a second dialog page and is not worth it (open question 10).
Item quality 1 in the rows is mapped to normal (2); the item level is the row's (1).

**Travel packages, Imbue / sockets / personalize: D2R's own.** Median's slots only open a client page or send the player to a
level; D2R already has each service and binds it to the same quest bits, so no line is drawn, nothing is hooked, and
npc_services.cpp checks the tables at load (section 1.6). The earlier plan to replace the server checks was wrong: Median's
lines are not repeatable (the reward is spent, see 1.6) and the conditions are D2R's own.

**Design (npc_actions.cpp, npc_actions_logic.h, mxl_npc_actions.h).**

- `tools/npc_actions_data.py` turns npcext + npcactions into `mxl_npc_actions.h` (build.py step after sigma_data.py):
  `Actions[31]`, `Slots[34]` with the D2R class id and the D2R string id of the label. Wire format: packet 0x38, kind
  `0x10 + npcRow * 16 + slot` (the game uses kinds 1, 2, 3).
- Client (hook `0x1147a0`, the menu builder): before the original runs, the NPC's record in the heap table is patched:
  string id into the next free id slot, a handler pointer into the next free handler slot (at most five lines), before
  "Identify" (0xfb4) when the record has it, count = lines + 1 on every call. The Identify branch of the builder writes the
  count (2 or 3) back into the record, which would drop lines behind it, hence the re-set on every call and the position.
  Handlers are 16 thunks (`LineHandler<I>`), each sends `0x38 (kind, current NPC id)` through `0xec730`, then calls the
  Cancel handler `0x112c30` to close the menu. Only the lines the server can run are drawn.
- Server (hook `0x4b0470`, the packet 0x38 handler): size 9 and kind >= 0x10 are Median's; the NPC unit must be within 50
  of the player (`0x53b8f0`, the original's own check), the NPC's class must own the slot, then
  `mxl_requirements::Evaluate` (quest bit through `0x325c50` on the player's quest record of the game difficulty, class,
  item through `0x441980`) and the action: create item via `0x517530` (as the Act 1 quests do), respec via `0x580f20` and
  then `0x5d9ae0` (what the vanilla respec packet `0x4b2530` does: the reset, then quest 41 loses bit 1 and the respec
  quest is deactivated, so the respec is used up exactly as in D2R). Every outcome (not found, denied, done, nothing,
  D2R-native) goes to the log with the kind and the NPC class.
- Requirement rows are not drawn on the client (the realm rule: Median logic is server-side). A line can therefore show
  while its requirement fails; the server then logs "requirement not met" and does nothing. Known cost; a server-to-client
  visibility message would fix it.

### 1.6 Imbue, sockets, personalize and the travel packages are D2R's own (lane npc-services, 2026-10-21)

**What Median does.** D2Sigma's three page actions (types 11, 12, 13 at `0x95470`, `0x95490`, `0x954b0`) only call one
D2Client function and store the page mode (1 Charsi imbue, 3 Anya personalize, 2 Larzuk sockets); type 12 is personalize and
type 13 is sockets (the first port had them the other way round). The services themselves run in vanilla D2Game: free, no gold,
the item checks of D2Common, and the reward is spent when used. Sigma's quest module hooks neither quest 3 (Charsi) nor 35
(Larzuk, only a validity stub before the vanilla handler); for quest 38 (Anya) it adds an event-2 handler that re-arms the
quest state when a flag byte is set. So the lines are visible while the reward is pending (requirement rows 16, 17, 18 =
quest 3, 35, 38 bit 1) and a service is used once per reward, not repeatably (the earlier "repeatable" note was wrong).
The travel packages move the player to a town level (types 8: levels 1, 40, 75, 109) and, for the quest-gated ones, set the
waypoint of the arrival town (type 15, `0x94ab0`: level to waypoint number, then the player's waypoint record).

**What D2R does (build 93847).**

- Client menu opener `0x1141d0` walks a fix table at RVA `0x22a70e0` (9 entries of 24 bytes: class, quest, bit, condition,
  function; count dword at `0x22a71b8`) and runs the function when the quest bit has the condition. The entries:
  Charsi (154) quest 3 bit 1 -> `0x109100` adds the line "Imbue" (string `0xfb1`), Larzuk (511) quest 35 bit 1 -> `0x109300`
  ("Socket", `0x58dc`), Anya (512) quest 38 bit 1 -> `0x109400` ("Personalize", `0x58dd`), Warriv (155) quest 6 bit 0 and
  Meshif (210) quest 14 bit 0 -> the travel lines. The three service lines share the click handler `0x10dd30`: close the
  list, mode 1, `0x1e3e20` -> `0x1e5b20` opens `ImbueItemsPanel`, and the panel picks Charsi / Larzuk / Anya from the open
  menu's NPC class (`0x10d150` = `0x2a48764`). The per-class item filter is `0x36aed0` (imbue), `0x36b410` (sockets),
  `0x36b1d0` (personalize); the same three run on the server.
- The server handler for the confirmation is packet `0x2e` (`0x4ae280`, 21 bytes: service u32, NPC id, item id, target
  position) -> `0x4fc230` (PlrMsg.cpp, also run from the delayed task `0x4b6050`). It finds the NPC (`0x53b8f0`, range 50),
  reads the NPC table row (`0x3971a0`: `+0xb` service 1 imbue / 2 sockets / 3 personalize, `+0x10` quest + 1), requires
  quest `+0x10 - 1` bit 1 (`0x325c50`), checks the item with the filter above, performs the change and spends the reward
  through `0x5da1c0` (quest 3), `0x548b60` (quest 35), `0x547c60` (quest 38): bit 0 set, bit 1 cleared. The panel is closed
  with the client message `0x58` (byte `+5` = 6).
- NPC travel is packet `0x37` (`0x4a9880`; the client sends it from the dialog / special line `0x1162f0`): the NPC table row
  gives the destination (`+0xc`) and the gate (`+0x10`, quest + 1; 0 = ungated); gated lines require that quest's bit 0,
  travel with `0x4f2e40(game, player, level, 0)`, and then set the waypoint of the arrival act's town: `0x3d6670(version,
  level, &wp)` (level to waypoint field) and `0x3d6890(record, wp)` (set the flag; record = `*(playerData + 0x58 + difficulty
  * 8)`, playerData from `0x34b240`). Tyrael's way to Harrogath is also the object function `0x5e57e0` (quest 26 bit 0 or 0xd,
  quest 28 set, travel to level 109, waypoint 109).

**Result.** For the quest-gated lines D2R's condition is Median's requirement row, and the waypoint grant is the same as Median's
(Lut Gholein 40 for Warriv, Kurast Docks 75 for Meshif, Harrogath 109 for Tyrael; the "west" trips are ungated and grant none,
as in Median). `npc_services.cpp` reads the NPC table (`0x237ea60`: `+0xb`, `+0xc`, `+0x10`) and the fix table and reports
`N of 8 D2R-native services match Median's rows` in the `npc actions` line; it writes nothing. On build 93847 seven of eight
match. The exception is Tyrael: D2R gates on quest 26 (gate byte 27, "Terror's End" done) and Median's row 13 reads quest 28
(set by the first walk through Tyrael's portal), so D2R lets the first trip happen earlier. It is left as it is (D2R's is the
looser and the first trip is the portal anyway); `Judge` reports it as `quest gate differs`.

Not done, and why: no hook, no menu line, no copy of the panel. A Median line would only duplicate the D2R line under the same
bit. Costs and item filters are D2R's vanilla ones; if Median's item types need a different filter (e.g. more bases accept
sockets), the fix is in the item data the filters read (flags in weapons / armor / misc, `+0xe4`, `+0x13a`, `+0x155` of the
item row), not in code here.

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
| +5, +0x11 | two lists of 6 u16 **item type ids** (the extraction names them StateListA/B; they are not states) | A (allowed) = 27 bow / 33 spea, 34 pole / 30 swor, 2 shie / 30 swor; B (excluded) = 112 scyh / 212 2hsd / 198 scrd |

Use of the lists (corrected 2026-10-22, lane merc-drops): 0x10066fd0(unit, item) returns true when the item is of one of
the fixed types {3 tors, 37 hlm1, 19 belt, 16 glov, 15 boot, 12 amul}, or of a type of list A with none of list B (B is
tested first and wins), and the item is identified and not broken (`0x100bf680` / `0x100bf340` read the item flags
`+0x18` bits 4 and 8). Every `0x100bf950(item, id)` in these functions is D2Common's "item is of type" (ordinal 10744;
D2R `0x373890`), with the ids of work/named/itemtypes.txt (hpot 76, apot 80, wpot 81, bow 27, ...). 0x10067220 and
0x1007e470 (D2Items.cpp, "can this merc equip this item") call it. **hiredescext** (10 rows):
only a name string key per hiredesc row (`strhirespecial1..9`), already used by the converter.

### 2.2 What code does

1. **Class index of a monster** (0x10068930): short at +0x2a of the monster record (monstats3-style extension) is the
   hireclasses index (0 = not a hireling). The same record family answers "is this unit a mercenary"
   (0x100bf9e0) and gives the class row (0x100bde30). In D2R: monstats `hcIdx` (to find).
2. **Item given to the mercenary** (0x100672a0; first read as "hire", corrected 2026-10-22: its first argument is an
   item, tested with `0x100bf680` / `0x100bf340` and for item types 0x4c, 0x51, 0x50 = health, antidote and thawing
   potion). A potion goes down the branch that ends in `0x1007f1c0` / `0x10042060` (not read further); any other item
   goes through a check (`0x100bd4d0`) and the game's equip function through a pointer (0x101f1ae8), prints one of three
   messages (0x55 can't, 0x56 fails, 0x57 equipped) and then runs 0x10066900 again. 0x10067220 is the matching "may the
   mercenary use this item" test (0x1007e470 + 0x10066fd0). D2R has its own potion and equip paths (packet 0x51 ->
   `0x4c0e20`, which asks `0x36bc50` whether the item may be worn: that is where the plugin hooks in).
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
  Torso/Weapon/Shield columns exist in hireling.txt but D2R has no such columns, so they are not converted (2.4, item 9).

### 2.4 Median's hireling changes: the list, the buckets and the port (lane hirelings, 2026-10-21)

Method: Median's hireling.txt (extract) against the built one; D2Sigma's D2Hirelings.cpp (23 functions) and the
D2Items.cpp equip test read again; D2R's hire path read in the dump (packet 0x36 handler `0x4a9520` -> hire `0x53d350`
-> bind `0x544ac0`; save load `0x421240`; level-up and stat setup `0x544f60`; hire menu `0x4b0470` kind 3).

| # | What Median changes | Evidence | Bucket |
|---|---|---|---|
| 1 | Hireling rows: 60 rows, four hire classes (rogue, desert, iron wolf, act 5 barbarian), nine types, levels 3/36/90, 12/43/90, 15/49/90, 38/68/90 | hireling.txt | **data, done** (txt_migrate; D2R's 135 rows are replaced) |
| 2 | Skills, stats per level, resistances, hire cost (`Gold`), `Exp/Lvl`, type names (`HireDesc` -> `strhirespecial1..9`) | hireling.txt, hiredescext | **data, done**; skill, name and desc keys exist in the string JSONs (checked 2026-10-21) |
| 3 | `equivalentcharclass` (which class's items the mercenary may equip): ama / pal / sor / bar. D2R's own table has only ama and bar | hireclasses +4; 0x1007e470 compares the item's class restriction with it | **data, fixed here**: txt_migrate `hireling_tables` sets it for acts 2 and 3 (pal, sor), 72 bank rows. Needs an in-game check (below) |
| 4 | Class bonuses on hire: damage resist, damage %, life regeneration, speed, dodge, energy, masteries, skill armor, crushing blow, slows attacker, extra experience, poison length resist (values below) | 0x10066900, called only by the hire function 0x100672a0 | **plugin, done**: `hirelings.cpp` |
| 5 | Minion scaling (`mnhp`, `mnar`, resist and proc copy) | 0x100683d0 and callees | **already ported** (`pet_scaling.cpp`); summons only, hirelings do not use it |
| 6 | Hire availability: hirer flag (npcext bit 2), quest requirement (Kashya row 9, Qual-Kehk row 14), resurrect entry (bit 3: the four hirers and Tyrael of act 4) | npcext, requirements | **mostly already in D2R's NPC table** (`+7` hire, `+4` resurrect, `+0x10` quest gate: the same five NPCs, the same quests). One difference, fixed: D2R also lets a character of level 8 hire from Kashya without Blood Raven (`+0x11`, read by `0x4b0470` and `0x53d350`); the plugin clears that byte so the quest alone opens her, as in Median |
| 7 | Hire and equip tests: refuse while the player has state 0x4c, 0x51 or 0x50; mercenary equip test (class, strength, dexterity, level, plus a fixed list of eight states and the hireclasses state lists) | 0x100672a0, 0x10067220, 0x10066fd0, 0x1007e470 | **ported as item types** (2026-10-22): the "state" numbers are item type ids, not states. `0x4c`, `0x50`, `0x51` are potion types, the hireclasses lists are item types the class may (A) or may not (B) wield; the plugin refuses weapons and shields in the `0x36bc50` hook (`item_gen.cpp`, `MercRequirement`) by `MercClasses` of `mxl_hirelings.h`. Class-locked items (stat 492) check the merc's `equivalentcharclass` |
| 8 | Revive cost | no D2Sigma code; the cost is D2Common's, and D2R's columns `resurrectcostmultiplier/divisor/max` (15 / 2 / 50000) copy the vanilla formula | **same as D2R**, nothing to do |
| 9 | Median's `Head`, `Torso`, `Weapon`, `Shield` columns (0 to 2, 48 rows) and `Share` (55 rows) | hireling.txt overflow | **cut** (checked in game 2026-10-22): a fresh Rogue has 0 items (`h merc`) and still draws its bow, so D2R's hire look is not made of items; there is no gear to create from these columns. `Share` is converted |
| 10 | Death / revive stat handling (`0x10068020`, `0x10067e20`) | read again: 0x10068020 is the pet attribute copy (energy, strength, ... in full, masteries halved), not a death penalty | **n/a**, part of the minion scaling; the note in 2.2 step 4 is superseded |
| 11 | Hireling skill use | skills are table data; no D2Hirelings code touches skill use | **data** |

**Class bonuses (item 4).** The function *sets* (D2Common 10887) these stats on the new mercenary; level is its level,
strength and dexterity its attributes. Stat ids are matched by name through `mxl_hirelings.h` (generated). Common to
all but the rogue: skill armor % 40, damage % = strength / 2, life regeneration = (strength * 8 / 10) * 256, movement
velocity 80. Rogue: damage resist = level / 4, damage % = dexterity / 2, regeneration from dexterity, velocity 100,
dodge = min(dexterity / 40, 60). Everybody: poison length resist 200, item_addexperience = 10 * level. Then by class:

| Class | Stats set |
|---|---|
| Rogue | energy (level + 4) * 6; slows attacker level / 5 + 25 |
| Desert | energy level * 4 + 16; crushing blow level / 10 + 15; damage resist level / 5 + 25; hireling ids 8, 11, 14 (the Fighter Mage type): fire and cold mastery = level |
| Iron Wolf | energy (level + 9) * 20; skill armor (level + 60) * 5; Median stat 485 (level + 50) * 5; fire, cold, lightning mastery = level |
| Barbarian | energy level * 2 + 8; to strength level / 4 + 20; to dexterity level / 3 + 20; Median stat 184 = 20 |

**Where it runs in D2R.** `0x544f60 (game, player, mercenary or null, level)` is D2R's single "derive the mercenary's
stats from its hireling.txt row" function. The hire (`0x544ac0`), the save load (`0x421240`, twice) and the level-up
(`0x44f52e`) all call it, so a post-hook sets the bonuses on every one of them. D2Sigma ran its function only from the
hire (its only caller; how Median restores the stats from a save is open question 6); here the bonuses are always
present. Because they are stat *sets*, repeating them after a level-up is exact. Strength and dexterity are recomputed
from the hireling.txt row exactly as D2R's own function does (`base + (level - row level) * perLevel / 8`, at least
10), so gear does not feed the regeneration or the damage bonus. The hireling id (the dm3 test) comes from the
player's mercenary record (`0x4ff2d0`, `+8`), the row from `0x3965d0(dataFlag, dataFlag != 1, id, level)`. Entry bytes
of all four functions are checked at load; any mismatch leaves the part off with the reason in the `mxl` line. Switch:
`hirelings` in mxl-core-off.txt.

**Not changed:** the hire NPCs (Qual-Kehk is D2R's), the hire menu panel, the revive price.

**In-game checks** are in docs/testing.md (hirelings row): the `mxl` line, bonuses visible on the hireling screen and
still there after a level-up or reload, Kashya before Blood Raven at level 8 or more, and class items on the desert
mercenary (paladin items) and the iron wolf (sorceress items). If `equivalentcharclass` for acts 2 and 3 is ignored
or harmful, take it out of `hireling_tables` (the only line that writes it).

## 3. Vanilla functions hooked or replaced, and D2R equivalents

| Behaviour | D2Sigma function | Vanilla 1.13 equivalent (by behaviour) | D2R address |
|---|---|---|---|
| NPC store init | 0x10093710, 0x10093fb0 | D2Game NPC init from npc.txt and the item vendor columns | to find |
| Restock | 0x10093a30 | D2Game NPC inventory fill (item level, qualities) | to find |
| Gamble selection | 0x100933d0, 0x10093e50 | D2Game gamble list (gamble.txt) | to find |
| Open trade / gamble / menu slot packets | 0x1009a850 (0x38-style NPC packet), 0x1009a9d0 | D2Game packet handlers for NPC trade and hire | `0x4b0470` (hooked by npc_actions.cpp: kinds >= 0x10) |
| Click on a unit | 0x1006aef0 and small ones | D2Game interact handler | n/a |
| NPC menu build (client) | 0x10107b40, 0x10107400 (`ui\NpcMenu.cpp`) | D2Client NPC menu (Talk, Trade, Gamble, Hire, ...) | `0x1147a0` builder, table `0x2a48830` (hooked by npc_actions.cpp) |
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
6. Travel packages: **done as D2R-native** (1.6): D2R's own packet 0x37 path travels and grants the arrival town's waypoint under the same quest bits; checked at load by npc_services.cpp. Size: 0.
7. Cain's replacement materials: server-side "create item" action plus the per-item "player lacks it" test. Size: S
   once requirements exist; M with a menu hook.
8. Restock for the rare group: only if the gated shops are revived. Size: M. **Defer** (see 11).

**(d) UI rebuild**
9. NPC menu entries Median adds (Respec, Imbue, Sockets, Personalize, travel entries, Cain's submenu). D2R builds its
   menu in code, so this is a hook on the menu builder and the click path, with strings from Median's tables; the
   pages that open (Imbue, Sockets, Personalize) exist in D2R, and so do their menu lines (1.6): only Akara's respec and
   Cain's materials needed code. Size: L, done.
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
5. (answered 2026-10-22) The ids in hireclasses lists A/B and in 0x10066fd0 are item type ids (the migrated item type
   ids equal Median's, row order is kept): equip gating, ported in `item_gen.cpp`.
6. Does the class bonus function run on every merc spawn (load from the save) or only at hire? Callers found:
   only 0x100672a0. If only at hire, D2Sigma must persist the stats in the save; check the Median save format
   (docs/save-format.md) before choosing where the plugin applies them.
7. Menu action types 18 and 19 (row 13: value 321): meaning of the confirm step.
8. Cain rows 244, 245, 246, 265, 520 all share the same five slots: which Cain appears in which act and whether D2R's
   Cain has the same monster ids (monstats rows 244-246, 265, 520 in the converted data).
9. 0x10067e20 (stat layer scaling by count): which minions it applies to.
10. A real Cain submenu (one entry per missing material, as Median draws it) needs a second menu page; D2R has no such widget in the NPC menu. Worth it only if the one-line "give what I am owed" feels wrong.
11. Imbue / sockets / personalize: answered in 1.6. D2R shows its own lines under the same quest bits and spends the reward itself; nothing to replace. Open: whether Median's item types pass the D2R item filters (`0x36aed0`, `0x36b410`, `0x36b1d0`) as Median intends (an in-game test with new base items), and the Tyrael gate (D2R quest 26, Median quest 28).

## Hireling data check (2026-10-21)

All 60 converted rows follow D2R's 135-row hireling column layout (including the base bank): no shifted columns. Median's shared Resist and Resist/Lvl land in all four D2R elements; shared row fields, six skills per row, and the nine HireDesc mappings match the named table. Every skill exists in the converted skills.txt, and every NameFirst, NameLast and HireDesc key exists in the mod string JSONs. Act I and Act V equivalentcharclass values are ama and bar.

Only difference (column `equivalentcharclass`): rows 13–30 (Act II) and 31–48 (Act III) were blank, where Median's hireclasses says `pal` and `sor`. D2R's own hireling.txt leaves Act II and III blank too, so the conversion copied D2R's convention. **Fixed 2026-10-21** (lane hirelings): `txt_migrate.py hireling_tables` writes ama / pal / sor / bar from hireclasses into both banks; what D2R does with a non-blank value for acts 2 and 3 is an in-game check (2.4, testing.md).

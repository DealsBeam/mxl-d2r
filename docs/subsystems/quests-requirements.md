# D2Quests.cpp and D2Requirements.cpp (research lane, 2026-10-20)

Source: `D2Sigma.dll` (1.10, base 0x10000000, Ghidra read-only copy) and the 1.13 `D2Game.dll` it calls into (base 0x6fc20000).
Addresses below are D2Sigma RVAs-as-VAs (0x1009xxxx) unless marked "D2Game +0x...". Data from `extract/mxl`, `work/named`, `work/sigma`.

## Short version

- **Neither module holds the uber quests, uber keys, signets or trophies.** The triage guess was wrong. Keys and the red
  portal are item spells (`srvdofunc` 154, already ported, plugin-spec.md); the Ubers need a new triage (see Open questions).
- **D2Quests.cpp is Median's copy of D2Game's act-quest code** (D2Sigma re-implements the game server). Most of its 60
  functions are the 37 quest *init* functions, a table of quest callbacks, and nine small Median hooks that run in front of
  the vanilla quest handlers. The hooks hand out rewards: **Griswold's Heart**, **crafting catalysts**, **arcane crystals**,
  **rune bundles**, **shrine items**, a **relic orb** and bonus boss drops.
- **D2Requirements.cpp is a small boolean-tree evaluator** for the 33-row `requirements` table. It only gates **NPC
  services** (shop, gamble, hire, and the 16 per-NPC action slots from `npcext`/`npcactions`). In Median's data it uses 6
  of its 37 condition types and decides, for example, which of Cain's gift buttons show.
- Port size: small. About 10 reward rules plus an evaluator, roughly 400-600 lines of plugin code, once the D2R quest
  event dispatcher is found. The NPC menu that shows the gated buttons is the larger job and belongs to the NPC lane.

## 1. D2Requirements.cpp

### What it is

`requirements` (33 rows x 44 bytes, `work/sigma/requirements.txt`, mem +0x74 / count +0x124 in D2Sigma's table block)
is a list of conditions that may refer to each other. Row layout (little-endian i32): +0 **type**, +4 / +8 / +0xc
three arguments (+4 doubles as a 4-char item code for type 35), and for the combiner types +4..+0x2b hold up to 10
child row indexes (0 = unused). Index 0 and "none" always count as "no requirement, allowed" (`0xa1180`, `0xa11e0`
return true for index 0; the NPC tables use -1 or 0 for "none").

Entry points: `0xa11e0(game, player, reqIndex)` server side and `0xa1180(player, reqIndex)` client side. Both fetch the row
(`0x95bc0`, bounds-checked) and call the evaluator `0xa10f0` with a small context: row pointer, game, flag "server" (+8) or
"client" (+9), and the player unit. Types 1-8 and 21-23, 28-37 work on both sides; types 9, 10 and 24-27 have a separate
server and client function (two jump tables at 0x10191338 = server, 0x101913d0 = client; the generic table is 0x101912a0).

### Condition types (index = `type` field)

| Type | Evaluator | Meaning | Used by Median's data |
|---|---|---|---|
| 1 | 0xa0d80 | OR of the child rows | yes (row 31) |
| 2 | 0xa0bc0 | AND of the child rows | yes (rows 26-30) |
| 3 | 0xa0ca0 | NOR (none of the children) | no |
| 4-8 | 0xa1750, 0xa1830, 0xa17c0, 0xa1910, 0xa18a0 | a D2Common value of the player (index a, layer c) ==, >, >=, <, <= value b | no |
| 9 / 10 | server 0xa1060 / 0xa0f90, client 0xa1020 / 0xa0f50 | **quest bit set / not set**: quest id a, bit b, difficulty c (-1 = the game's difficulty). Server reads the player's quest record for that difficulty, client reads its cached copy (`0x9d2f0`, D2QuestFlags.cpp) | yes (rows 8-21) |
| 11-15 | 0xa14d0, 0xa15d0, 0xa1550, 0xa16d0, 0xa1650 | stat a (itemstatcost row) ==, >, >=, <, <= b, variant "1" of the stat getter (Ordinal 10306 flag 1) | no |
| 16-20 | 0xa1250, 0xa1350, 0xa12d0, 0xa1450, 0xa13d0 | same with variant "0" of the getter | no |
| 21 | 0xa0750 | stat a is a valid stat that the unit does not have (meaning of the Ordinal 10708/10630 path not confirmed) | no |
| 22 / 23 | 0xa0550 / 0xa04f0 | player is in / not in act a (a is 1-5, 5 checked against the act count) | no |
| 24 / 25 | server 0xa0650 / 0xa05e0, client 0xa0620 / 0xa05b0 | game difficulty ==, != a | no |
| 26 / 27 | server 0xa0b90 / 0xa0b30, client 0xa0b60 / 0xa0b10 | boolean game/player property (0xa0b60 and 0xa0b10 call `0x1005f500`; likely hardcore or expansion, not confirmed) | no |
| 28 | 0xa0ae0 | unit-in-hierarchy test through `0xbe100` (current area) then `0xb6280` | no |
| 29 | 0xa0e60 | negated boolean (`0xbf8a0`) | no |
| 30 / 31 | 0xa0e90 / 0xa0ef0 | **player class ==, != a** (a below 7) | yes (type 30, rows 1-7) |
| 32 / 33 | 0xa1980 / 0xa19e0 | has / does not have skill-or-item record a (Ordinal 10494) | no |
| 34 | 0xa07b0 | **count of items** of a type set: sums quantity (stat 0x46 for stacked) over the inventory, compares with b | no |
| 35 | 0xa0690 | **player does NOT hold an item with 4-char code a** (inventory search `0xbed10`; the function returns the negation, which is what the rows want) | yes (rows 22-25) |
| 36 / 37 | 0x560e0 / 0x36f50 | constant false / constant true | yes (row 32 = false) |

Only types 1, 2, 9, 30, 35 and 36 occur in the 33 rows, so a port needs just those six (the rest can be added if a
later Median update uses them). Types 4-8, 11-21, 24-29, 32-34 are generic engine features nobody in the data uses.

### The 33 rows (decoded)

- Rows 1-7: class 0..6 (Amazon, Sorceress, Necromancer, Paladin, Barbarian, Druid, Assassin).
- Rows 8-18: quest bit tests in the game's difficulty. a=quest id, b=bit: 8: id 1 b0 (Den of Evil done), 9: id 2 b0
  (Blood Raven done), 10: id 4 b0 (Search for Cain done), 11: id 6 b0 (Andariel done), 12: id 14 b0 (Duriel / Seven Tombs),
  13: id 28 b0, 14: id 36 b0 (Rescue on Mount Arreat), 15: id 41 b1 (Median's own flag, see 2.4), 16: id 3 b1
  (Tools of the Trade, Charsi imbue), 17: id 35 b1 (Siege on Harrogath, Larzuk), 18: id 38 b1.
- Rows 19-21: the same test but in a fixed difficulty: id 6 in Normal, id 13 in Nightmare, id 25 in Nightmare (the three
  bonus-drop quests of 2.3).
- Rows 22-25: "does not hold" Horadric Cube (`box `), catalyst of disenchantment (`3o[ `), catalyst of learning (`0o[ `),
  catalyst of jewelcraft (`jcat`).
- Rows 26-30: AND combinations: 26 = Den done and flag 41/b1 (Akara); 27 = Cain found and no cube (Cain re-issues the cube);
  28 / 29 / 30 = Cain found and the matching boss quest done and the matching catalyst missing.
- Row 31 = OR(27, 28, 29, 30). Row 32 = always false.

### Who asks (all NPC services)

| Caller | Side | Gates |
|---|---|---|
| 0x107b40 (calls 0x94650, 0x943e0, 0x944e0) | client, NPC menu | shop button (npcext flag bit 0, requirement at npcext +0xc), gamble (bit 1, +0x10), hire (bit 2, +0x14) |
| 0x9a850 (calls 0x946c0, 0x94450, 0x94550 and the action check 0x948d0) | server, NPC interaction | the same three services, and the per-action requirement |
| 0x107400, 0x949d0 (0x947d0 / 0x94740) | client, NPC action list | the 16 action slots in `npcext` (+0x50, 16 bytes each: requirement, `npcactions` row, string id, 4th field) |
| 0x93a30 (`0xa11e0`) | server, vendor stock | requirement at +4 of each 0x2c-byte stock entry; the 847 `npcstores` rows carry 0 there, so unused |

Where the data actually uses requirements:

- Shop disabled by requirement 32 (always false): Atma, Geglash, Cain (acts 2 and 5), act2vendor1.
- Hire gated: Kashya (req 9, Blood Raven), Qual-Kehk (req 14, Rescue on Mount Arreat).
- Action slot requirements: Akara (26), Charsi (16), Warriv (11), Meshif (12), Tyrael act 4 (13), Larzuk (17), Drehya (18);
  Cain in acts 2-6 (31, 27, 28, 29, 30). `npcactions` rows 15-18 are the "give item" actions (type 17: `box `, `3o[ `,
  `0o[ `, `jcat`); rows 27-30 chain a text, a second text and the gift. That is the NPC lane's table; the link is just
  this: **Cain is Median's catalyst vendor, and the requirement rows decide which catalyst he offers.**

### Port notes

Tables already exist: `requirements.bin` decodes (`work/sigma/requirements.txt`, schema "structure only"), the name tables
are done. D2R has no `requirements` file, so it stays plugin data (ship as a small JSON or bin next to the plugin).
The evaluator is plain logic; the only engine access is "quest bit of player X in difficulty D", "player class",
"does the player hold item code", "game difficulty".

## 2. D2Quests.cpp

### 2.1 Structure

D2Sigma carries its own quest list: 37 descriptors of 0x18 bytes at **0x101dd0e8** (init function, act 0-4, a field that is 0x64 or 0
and not understood, a flag, slot, quest id; the callback and size tables below are indexed by the descriptor's position), 37 quest-data sizes at **0x101dd050**, and 37 x 15 callback
pointers at **0x101dd460** (0x3c bytes per quest). Zero callback entries mean "use D2Game's original", filled at start-up
exactly like the srvdofunc table in plugin-spec.md. The init functions (0x9d440-0x9e360, 37 of them, about 0x30 bytes
each) all do the same: mark the quest active, set its starting state, copy its 15 callbacks to quest+0xa0 (`0x9fd50`),
allocate its private data block from the size table (`0x9fcb0`), set display tables (`0x9fde0`, `0x9fda0`). They are
vanilla's init functions re-created; the Median differences are the quest ids they register.

Quest ids in the descriptors: 0-6 (Act 1), 8-14 (Act 2), 16-22 (Act 3), 24-27 (Act 4), 35-40 (Act 5), plus the extra ids
29, 30, 31, 32, 33 (one per act, flag 1) and **41** (Median's own, in the Act 1 group). Ids 7, 15, 23, 28, 34 are
not initialised (vanilla's "act done" flags). The quest record per difficulty is the usual 0x60-byte block, so ids up to
47 exist in the save; 41 fits.

Event dispatch: `0x9fb30` sends an event index to every quest of a player (`0x9fc00` is the per-player wrapper; the
dispatcher lambda is `0x9ff80`): handler = pointer at quest +0xa0 + event*4, called only when the quest is active (+10) or the
caller asks for all quests, and the state byte matches. Event slots Median hooks (meaning inferred from the vanilla
handlers it chains to, to be confirmed in D2R): **0** player activates an NPC, **2** a quest-state/area event, **8** a unit
is killed, **11** an NPC dialog message is chosen (the 16-bit value in the event arguments +0x18 is the message id; +0x14 low
byte is the NPC class). Event arguments: game, event, killed/target unit (+8), player (+0xc).

### 2.2 The Median hooks (chain to the vanilla handler afterwards)

Quest-record helpers used by all of them: `Ordinal 10174(record, quest, bit)` = test, `10003` = clear, `10593` = set
(D2Common, named by behaviour from the vanilla callers). Rewards are created with D2Game +0x36470 (create item by 4-char
code in the player's inventory), drops with D2Game +0x12380 (drop a treasure class), unique items with D2Game +0x11490.

| Quest id | Slot | Sigma fn | Vanilla fn (D2Game +) | Trigger and effect |
|---|---|---|---|---|
| 4 (Search for Cain) | 11 | 0x9e730 (reward 0x9e810) | 0x66ce0 | Akara dialog message 0x76 with bit 1 set (Cain rescued): gives **Griswold's Heart** (`GriswoldsHeart`, `(N)`, `(H)` by difficulty; uniqueitems rows 71-73) and shows message 0x61 |
| 5 | 8 | 0x9e8b0 | 0x68b90 | skips the vanilla kill handler when quest 5 already has bit 0, 0xe or 0xf (stops a repeat completion) |
| 6 (Andariel) | 8 | 0x9e9f0 | 0x67e00 | Normal only: Andariel dies -> drops treasure class "Quest Cata Disenchant" (stored on monstats row 155 `warriv1`, field TreasureClass1) once per player in the game, at the killer's level, sets flag byte +0x198 of the quest data (data size 0x199 = vanilla 0x198 + 1) |
| 6 | 0 | 0x9eb40 | 0x60a80 | player activates an NPC while the flag is set: if the player lacks `3o[ ` (catalyst of disenchantment), creates it. Pairs with Cain action row 28 |
| 13 (Summoner, class 250) | 8 | 0x9ed00 | 0xc66b0 | Nightmare only: drops "Quest Cata Learning" (monstats row 177 `drognan` TreasureClass1(N)), flag byte +0xc (data size 0xd) |
| 13 | 0 | 0x9ee40 | 0xc3080 | re-issues `0o[ ` (catalyst of learning) if missing, Cain action row 29 |
| 14 (Duriel quest) | 11 | 0x9ef90 | 0xc8050 | Jerhyn (class 201) dialog 0x1ba: gives one **Arcane Crystal** (`qum `), message 0x5f |
| 19 (Gidbinn / Blade of the Old Religion) | 11 | 0x9f030 (reward 0x9f110) | 0xe9600 | Ormus (class 255) dialog 0x251 with bit 6 set: gives 1 / 3 / 5 random **shrine items** (item type 244 `flsh`: the `*_shrine_10` consumables) by difficulty, message 0x60 |
| 21 | 11 | 0x9f340 | 0xe89a0 | Cain (act 3) dialog 0x272: if bit 0 is not set, gives an **Arcane Crystal**, message 0x5f |
| 25 (Izual, class 256) | 8 | 0x9f3f0 | 0xbabf0 | Nightmare only: drops "Quest Cata Jewelcraft" (monstats row 367 `tyrael2`, TreasureClass1(N)), flag byte +0x1c (size 0x1d) |
| 25 | 0 | 0x9f530 | 0xb9560 | re-issues `jcat` if missing, Cain action row 30 |
| 36 (Rescue on Mount Arreat) | 11 | 0x9f7c0 | 0x5c9e0 | Qual-Kehk (class 515) dialog 0x4e8e, bit 0 clear and bit 1 set: gives **runes** by difficulty (Normal r08 r09 r10, Nightmare r15 r16 r17, Hell r22 r23 r24); 3 runes, 2 if bit 6, minus 1 if bit 7 |
| 35 | 2 | 0x9f7a0 | 0x5a720 | calls a validity stub then the vanilla handler (no new behaviour) |
| 38 | 2 | 0x9f960 | none (replaces) | Drehya/Anya (class 512): if quest data byte +0x86 is set, re-arms the quest (state byte, timer) and clears it; keeps byte +0x88 as "done" |
| 40 (Baal, class 544) | 8 | 0x9fa80 (lambda 0x9e580) | 0x5fe30 | after vanilla: in Hell, for every player in area 132 (Worldstone Chamber) with level above 109 and quest 40 bit 0xb clear: sets bit 0xb and gives a **relic orb** (`qrel`) |
| 0, 8, 16, 24 (the four act intros) | 2 | 0x9e5f0 | none | empty handler: Median switches the vanilla intro/state-1 event off for the act-intro quests |

Message ids 0x5f, 0x60, 0x61 go through `0xb15d0` (sets a pending notice on the player unit and flag 0x400 in unit +0xc4);
they are strings of the client (confirm which).

### 2.3 What these quests mean for the game

The act bosses (Andariel in Normal, the Summoner and Izual in Nightmare) give a once-per-game bonus drop that is a
**crafting catalyst** (the catalysts are `misc` rows `catalyst_of_disenchantment` `3o[ `, `catalyst_of_learning` `0o[ `,
`catalyst_of_jewelcraft` `jcat`). If the player loses it, **Cain re-issues it** (quest flag still set, item missing) via
his action buttons. Other quest rewards: Griswold's Heart (Akara), Arcane Crystals (Jerhyn, Cain), shrine items (Ormus),
rune sets (Qual-Kehk) and a relic orb for killing Baal in Hell. The bonus drop uses Warriv, Drognan and Tyrael's act-4
monstats rows purely as holders of a treasure-class name.

### 2.4 Quest 41 and the extra ids

`0x9e600` (called through the dispatcher thunks `0x9ff10` / `0x9ff40`): when the player's level is above 50, set quest-41
bit 1 and clear bit 0; if bit 0xf is not set deactivate the quest object. Quest 41 bit 1 is requirement row 15 and, with
Den of Evil, opens **Akara's action slot** (requirement row 26): a Median Akara service available from level 51 (the
`npcactions` row it points to is the NPC lane's: `npcext` row `akara` action 0 = `npcactions` 19). Ids 29-33 are one
extra quest object per act with no handlers; nothing in these two modules reads them (open question).

### 2.5 Smaller pieces

- `0x9d380` (D2QuestFlags.cpp): server side, packs the player's 0x60-byte quest record plus the difficulty and sends it
  to the client (packet 99 in D2Sigma's numbering). `0x9d2f0`: client side bit test on the cached record (current
  difficulty at `*0x101f1060`). D2R already syncs quest state to the client.
- `0x9fe10`: find a quest object in the player's list by id. `0x9fe50`: iterate all quests of a player with a callback.
- `0x96820` (D2ObjectFn.cpp, outside the module) uses quest 19 bits 0/7/8 and `0x9fe10` for an object that completes
  that quest (a Median object function; objects lane).

## 3. D2Game / D2R equivalents

| Behaviour | 1.13 D2Game | D2R |
|---|---|---|
| quest event dispatch (event index -> handler pointer at quest +0xa0 + 4*event) | the same lambda-style loop as 0x9ff80; vanilla handlers listed in the table of 2.2 | D2R address: to find |
| quest record bit test / set / clear | D2Common ordinals 10174 / 10593 / 10003 | D2R address: to find (the plugin needs get and set for ids up to 41 in the player's per-difficulty record) |
| per-player quest record by difficulty | `0x10920` ordinal + `[+0x10 + diff*4]` | D2R address: to find |
| create item by 4-char code in player's inventory | D2Game +0x36470 | D2R address: to find (the plugin already creates items for orb and crate spells; reuse that routine) |
| drop a treasure class at a unit | D2Game +0x12380 | D2R address: to find (area damage or goblin code in the plugin may already call a drop routine) |
| create a unique item by index | D2Game +0x11490 (`0x100bb330` wrapper, lookup by name `0x100bb4d0`) | D2R address: to find |
| NPC menu service availability (shop, gamble, hire, actions) | D2Sigma replaces it (0x107b40 client, 0x9a850 server) | D2R address: to find; client UI is a layout question |
| player level / difficulty / area / NPC class from the event | unit fields (+0x6d game difficulty, +0x8c game player count) | D2R offsets: to confirm in the dump |

## 4. Data and where it ends up

| Data | State |
|---|---|
| `requirements` (33 rows), `requirementtypes`, `requirementnames` | decoded in `work/sigma`; plugin data, no D2R counterpart |
| `npcext` (49 rows: service flags + requirement refs + 16 action slots), `npcactions` (31) | decoded, "structure only"; NPC lane |
| Treasure classes "Quest Cata Disenchant / Learning / Jewelcraft", "Quest Countess", "Quest Hellforge ..." | in `treasureclassex` (migrated); rows 155 / 177 / 367 of monstats carry them. The plugin can use the TC names directly |
| Catalysts, arcane crystal `qum `, relic orb `qrel`, runes, `flsh` shrine items, Griswold's Heart uniques | item tables (migrated, renumbered by id-mapping.md) |
| Quest ids 29-33, 41 | no table; only record bits |
| Quest messages 0x5f, 0x60, 0x61 | client strings, to identify |

## 5. Port plan

Order = what a player meets first. Sizes: S < 100 lines, M 100-300, L > 300 of plugin code.

| # | Piece | Bucket | Size | Notes |
|---|---|---|---|---|
| 1 | Quest event hook + rule table (the nine reward rules of 2.2 as data: quest id, event, NPC class or monster class, difficulty, bit conditions, reward) | c | M | the hard part is the D2R dispatcher and the quest-record API; the rules themselves are trivial |
| 2 | Akara's Heart reward (quest 4) | c | S (rule) | first thing in Act 1 |
| 3 | Andariel Normal bonus drop + flag + Cain re-issue of `3o[ ` (and the NM Summoner, NM Izual copies) | c | S each (rules) | needs the per-quest flag byte: store in a plugin-owned bit of the quest record instead of resizing D2R's quest data |
| 4 | Requirement evaluator (6 types) | c | S | pure logic once quest-bit, class, item-held and difficulty accessors exist |
| 5 | NPC menu buttons gated by requirements: shop off for Atma, Geglash, Cain; Cain's 5 gift buttons; Akara, Charsi, Warriv, Meshif, Larzuk, Drehya, Tyrael buttons; hire gating | d (with NPC lane) | L | the NPC action list is the big open item; do it with `npcext`/`npcactions` |
| 6 | Quest 41 level-51 flag | c | S | hook on level-up, or test when Akara is opened |
| 7 | Jerhyn / Cain / Ormus / Qual-Kehk dialog rewards | c | S each (rules) | NPC dialog message ids may differ in D2R, verify |
| 8 | Baal-in-Hell relic orb | c | S | kill event for monster 544 |
| 9 | Notices 0x5f-0x61 | d | S | use D2R's message line |
| 10 | Quest init functions, act-intro stubs, the 15-slot callback table, D2QuestFlags packet | e | - | D2R has its own quest system; test whether intros play; do not port Sigma's list |
| 11 | Quest 5 repeat guard (0x9e8b0), quest 38 re-arm (0x9f960) | e/defer | S | unconfirmed purpose; defer until Act 1 and 5 are tested |

Milestone note (Barbarian, Act 1): items 2, 3 (Andariel part) and 4 are enough for Act 1; Akara's Heart is the first
visible reward. Cain's buttons (item 5) are the first Median NPC UI and are needed to recover a lost catalyst.

## 5b. Implemented: Act 1 (plugin/src/quests_act1.cpp, quests_logic.h; switch `quests`)

Hooks sit on D2R's per-quest handlers (same slot as Median's, run in front of the vanilla handler, then chain):

| Hook | RVA (entry bytes in the source) | Does |
|---|---|---|
| a1q4 event 11 | `0x5e0830` | Akara (class 0x94), message 0x76, quest-4 bit 1: creates **Griswold's Heart** of the game difficulty (uniqueitems rows 69 / 70 / 71 = `GriswoldsHeart`, `(N)`, `(H)`; base armor codes `600 `, `784 `, `@01 `) with `0x517530(game, player, code, 255, 7, 1)` |
| item generator | `0x43cd50` | when a request is armed on the thread, writes unique index + 1 at block +0x50 (read by the quality-7 chooser `0x443910`); the unique-already-exists bit (game+0x3638) is cleared first |
| a1q6 event 8 | `0x5e1f00` | Andariel (class 156) in Normal, flag clear: creates `3o[ ` (catalyst of disenchantment) in the killer's inventory, sets the flag |
| a1q6 event 0 | `0x5e1d30` | NPC activate: flag set, NPC class 0x9b (**Warriv**, from D2Sigma `0xbff40(0)`, not Cain) and no `3o[ ` held (`0x441980`): creates it again |

Findings that change the earlier notes: (1) the Akara reward is not an "Akara's Heart" but the unique Griswold's Heart, picked by
difficulty from three strings at `0x1018fd54`. (2) The re-issue hook fires on Warriv (act 1, monster 155), the same row Median uses as a
treasure-class holder; Cain's buttons (requirement rows 27-31) are a second route and belong to the NPC lane. (3) The flag is not in D2R's
quest object (its data block is full); it is quest id 47, bit 0 of the player's record of that difficulty, so it survives saving
(D2Sigma loses its flag when the game ends). Quest-record bit helpers `0x325c50` (test) and `0x325c00` (set) take `(record, quest, bit)` with
`record = *(playerdata + 0x40 + difficulty * 8)`, no bounds check.

TODO (not done): D2Sigma drops the treasure class once per player in the game on the ground; here only the killer gets one (the TC executor
is still unlocated). Notice 0x61 (`0xb15d0`) after the Heart. Requirement rows 26 (Akara level-51 slot), quest 41 level flag (`0x9e600`), Cain
and Akara/Charsi/Warriv/Meshif/Larzuk/Drehya action buttons (NPC lane). Quest 5 repeat guard `0x9e8b0` (a1q5 event 8 `0x5e10c0`): purpose
unconfirmed, deferred. The requirements evaluator (`requirements.cpp`) gates nothing in Act 1 yet; its only Act 1 consumers are the NPC action rows.

## 6. Open questions

1. **Where are the Ubers?** Not here. Candidates: item spells (red portal, done), D2Dungeon/D2Drlg (level types 36-51),
   D2SpecialEvents/D2Events, objects. The triage should be re-run for them before Phase 4 "requirements + quests/ubers".
2. D2R addresses for the quest event dispatcher, quest-bit get/set, per-player quest record, kill and NPC-message events
   (needs the dump project). Check that D2R keeps the 3.0-era message ids (0x76, 0x1ba, 0x251, 0x272, 0x4e8e) and NPC class ids.
3. Meaning of event slots 0 / 2 / 8 / 11 is inferred from vanilla handlers. Confirm by reading the vanilla table once in
   D2Game and again in D2R.
4. Where does the per-quest flag byte (+0x198 / +0xc / +0x1c) live in D2R, given quest data sizes are D2R's? Plan:
   store it as an extra bit (for example bit 0xc) of the same quest record instead.
5. Who reads quest ids 29-33 and 41 beyond requirement row 15? Needs a search for the constants across D2Sigma
   (objects, NPC code, UI).
6. Stat/skill/item condition types 4-8, 11-21, 26-29, 32-34 are unexplained and unused; confirm only if Median updates
   its data.
7. Message ids 0x5f-0x61 and what `0xb15d0` shows on the client.
8. Quest 4's reward NPC is Akara (class 148) and Cain is class 146: the "Cain found" quest-4 bit means the rescue, not a talk
   with Cain. Check the quest names in the Median strings before naming them in a UI.
9. D2R's NPC dialog menu: is it data-driven (so the gated buttons can be added through tables) or UI code (layout work)?

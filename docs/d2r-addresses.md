# D2R 3.3 build 93847: addresses for the blocked hooks (lane d2r-addresses, 2026-10-21)

All addresses are **RVAs** in the D2R image (`D2RLoader_6ab3782c_dump.exe` memory layout; file offset == RVA, base
`0x140000000`), x64 Microsoft ABI (rcx, rdx, r8, r9, then stack at `[rsp+0x20]...` at the callee's entry).
"Entry bytes" are the first 16 bytes of the function, for the plugin's expected-bytes check.

**Confidence:** *verified* = the code was read and matches the 1.13c function (same structure, constants, assert
strings, callers); *likely* = structure matches but one link rests on inference; *guess* = anchor only.

Everything here was found with `tools/d2r_find.py` (string search, RIP-relative xrefs, `.pdata` bounds, callers,
disassembly; see its docstring) plus Median's 1.13c D2Game decompile (`ghidra-lanes/addr/d2game` copy). Sections 4, 5 and 7-11 (lane
d2r-addresses-2) were read from the analysed Ghidra project (decompiles via `tools/ghidra_scripts/DecompOut.java`, `tools/ghidra_lane.ps1` for worktrees) plus
`tools/d2r_scan.py`; sections 1-3 and 6 are the earlier disassembly-only pass.

Reading aids that apply everywhere:

- `.pdata` splits big functions into pieces. `tools/d2r_find.py func X` can then report a *cold part* (e.g. `0x529d50`
  is a 0x2e-byte entry stub whose body is chained at `0x529dae`). The entry address is still the right hook address.
- Asserts show up as `D2Game\src\<Module>\<File>.cpp` strings; most of the logging strings are GUID-prefixed
  telemetry lines and are not useful anchors.
- Game object offsets seen in this lane: `game+0x104` u8 difficulty (0-2), `game+0x106` u8 expansion/dataFlag (every
  table getter takes it), `game+0x1aaa0` an object with a vtable (telemetry), `game+0x21d8` -> the game-wide quest
  list. Unit helpers: `0x34b240(unit)` = the player data (`+0x40 + difficulty*8` = that difficulty's quest record
  pointer), `0x34b440(unit)` = room, `0x2efc10(room)` = level id, `0x34a330/0x34a360/0x349860(unit, file, line)` =
  unit id / unit-data fetch / class id (the file+line are assert arguments), `0x34b9d0(unit)` = unit type
  (0 player, 1 monster, 4 item), `0x48fe80(game, type, id)` = find unit, `0x484de0(x)` = game from a unit/room.

## Summary

| # | Hook point | RVA | Confidence |
|---|---|---|---|
| 1 | Cube: execute a recipe (post-hook for craft flags + gold) | `0x5269c0` | verified |
| 1 | Cube: transmute packet handler (caller of search + execute) | `0x4f2490` | verified |
| 1 | Cube: recipe search / input match (already hooked, bytes now recorded) | `0x529090` / `0x529d50` | verified |
| 1 | Cube: output-effect dispatch (byte table; only for the "patch like Median" route) | `0x526d3c` (table `0x1d3bfc0`) | verified |
| 2 | Quest event dispatcher (per-player quest list) | `0x518ec0` | verified |
| 2 | Quest kill event wrapper (game, victim, killer) | `0x515640` | verified |
| 2 | Quest NPC-activate event wrapper | `0x515980` | verified |
| 2 | Quest NPC-message event wrapper (game, player, npc id, msg) | `0x516940` | verified |
| 2 | a1q4 (Search for Cain) event 11 handler: Akara messages (Median's Griswold's Heart hook) | `0x5e0830` | verified |
| 2 | a1q6 (Andariel) event 8 handler (kill): Median's catalyst drop | `0x5e1f00` | verified |
| 2 | a1q6 event 0 handler (NPC activate): Median's catalyst re-issue | `0x5e1d30` | verified |
| 2 | Create a quest item by 4-char code in the player's inventory | `0x517530` | likely |
| 2 | Quest bit test / set / clear (record, quest id, bit) | `0x325c50` / `0x325c00` / `0x325990` | verified / verified / likely |
| 2 | Player quest record for a game and player | `*(0x34b240(player) + game[0x104]*8 + 0x40)` | verified |
| 2 | Quest descriptor table (36 entries: init fn, act, quest id) | `0x239c5c0` (stride 0x20) | verified |
| 3 | NPC table (43 x 0x1c bytes: hire/trade/gamble flags, hire quest and level) | `0x237ea60`, getter `0x3971a0` | verified |
| 3 | Server NPC trade / gamble / hire packet handler | `0x4b0470` | verified |
| 3 | NPC click packet (server; calls quest event 0) | block `0x4c26a7` of a PlrMsg handler | likely |
| 3 | NPC dialog-message packet (server; calls quest event 11) | `0x4c33e0` | likely |
| 3 | Client NPC menu module (`D2Client\src\UI\npcmenu.cpp`) | functions `0x10a520`-`0x117634` | likely (module only) |
| 4 | Warp interact packet (server) and the warp action it calls | `0x4adbb0` -> `0x491ff0` | verified (section 4, rewritten) |
| 4 | **Warp / use-level-tile action, the lockout hook** `(game, player, tile unit)` | `0x491ff0` | verified |
| 4 | Warp quest-lock test `(game, player, srcLevel, dstLevel)` -> nonzero refuses (the vanilla twin of Sigma `0x5d810`) | `0x516a50` (one caller, call at `0x49218c`) | verified |
| 4 | Teleport unit into a room at x,y (the level change itself) | `0x491ca0` | verified |
| 4 | Linked destination tile of a warp tile / room level id | `0x2efee0` / `0x2efc10` | verified / verified |
| 5 | **Server game frame** `(Game*)`, frame counter `game+0x170` (Median's `0x2e15a` tick) | `0x406550` | verified |
| 5 | Process all games (timer entry, loops the 1024 game slots) | `0x406400`, slots `0x2a9c020`, `0x4062f0(id)` = game | verified |
| 5 | **Spawn monster at room/x/y** `(game, room, x, y, class, ...)` | `0x4929e0` (core `0x493cc0(spawn struct*)`) | verified |
| 5 | Spawn N monsters around a unit | `0x4934d0` | verified |
| 6 | Requirement gates for NPC services | NPC table bytes +7/+8/+9/+0x10/+0x11 (section 6) | verified |
| 7 | **Treasure-class executor** (1.13c `0x12380`) | `0x4404f0` | verified |
| 7 | TC drop wrapper by TC index (1.13c `0x12d60`) | `0x441300` | verified |
| 7 | TC record getter by index, level-stepped | `0x397290` | verified |
| 7 | Create item from an item-gen struct (1.13c `0x11490`) | `0x43cd50` | verified |
| 7 | Create item of a class (struct builder, quality / ilvl / flags) | `0x43d530` | verified |
| 7 | Unique-item-by-index creator | not found: no equivalent function; see section 7 | open |
| 8 | **Game destroy** `(u64 game id)` ("Deallocate game %p") | `0x4057c0` (called by the idle sweep `0x406e40`) | verified |
| 8 | **Game create** (name, password, ... 14 args) | `0x4049d0` (no direct callers) | likely |
| 9 | Client NPC menu table (48 x 0x48 bytes, heap, global ptr) | pointer `0x2a48830`, count `0x2a48838`, filled by `0x360d0` | verified |
| 9 | Client menu builder / NPC row index / click-send | `0x1147a0` / `0x113990` / `0x117510` | verified |
| 9 | Server handler of the menu packet 0x38 `(kind, npc id)` | `0x4b0470` | verified |
| 10 | Gold stat 14/15 client sync | no explicit packet needed (section 10) | likely |
| 11 | Quest handlers 13, 14, 25, 38, 40 | section 11 | verified |

## 1. Cube output / recipe result

### Flow (all verified by reading the code)

```
0x4f2490  transmute packet handler  (game, player, packet*, flag)   D2Game\src\Player\PlrMsg.cpp
  -> 0x529090 (int* result, game, player, out)    recipe search; result[0] = record index, result[1] = found
  -> if found and flag != 0:
       record = 0x3e3f60(game[0x106], result[0])
       0x5269c0 (game, player, record, ctx)       D2Game\src\Player\PlrTrade.cpp: consume inputs, run the outputs
```

`0x5269c0` also has a second caller, `0x412ad0` (in the function `0x412908`, which uses the returned item pointer).
That path is not the player's Transmute click (it has a different shape); the plugin's post-hook sees both.

| What | Value |
|---|---|
| `0x4f2490` entry bytes | `40 53 55 57 41 54 41 55 41 56 b8 c8 14 00 00 e8` |
| `0x5269c0` entry bytes | `40 55 53 56 57 41 54 41 55 41 56 41 57 48 8d ac` (prologue then `lea rbp,[rsp-0x15f8]`, 0x16f8-byte frame) |
| `0x529090` entry bytes (search) | `40 55 53 56 57 41 54 41 55 41 56 41 57 48 8d 6c` |
| `0x529d50` entry bytes (input match) | `44 89 4c 24 20 55 53 56 41 55 48 8b ec 48 83 ec` (stub of 0x2e bytes; the body is chained at `0x529dae`) |
| `0x3e3f60` entry bytes (record getter) | `48 89 5c 24 08 48 89 74 24 20 57 48 83 ec 30 48` |

Signature of the recipe executor, the recommended hook for craft flags and gold:

`void* ExecRecipe(Game* game /*rcx*/, Unit* player /*rdx*/, const CubeRecord* record /*r8*/, void* ctx /*r9*/)`
returns the **first output item on success** (`[rbp+0x120]` at `0x528128`) and **null on failure** (`0x528181`: no output
could be made, the inputs are put back with `0x471500`). The recipe row is `(record - 0x3e3f60(game[0x106], 0)) / 0x16c`, exactly the
formula `cube_conditions.cpp` already uses. `ctx` is the `out` buffer of the search (`[rsp+0x60]` in the caller).
**Recommendation (port spec option 2, the post-hook):** detour `0x5269c0`, call the original, and when it returns a
non-null item run the recipe's effect list from the generated table:
set player flag, set game flag, pay gold. The search has already validated the conditions, so the flag/gold step is
the only thing left. Option 1 (patch Median's way) is possible but awkward, see below.

### Recipe record and output slots (D2R, 0x16c bytes)

Verified from the cubemain.txt parser (`0x3e5e60`, which handles the output column and writes the "special" byte) and
from the executor loop (`0x5269c0`, `0x526b58..0x526d6f`):

- Output slot `i` (0-2) starts at **`record + 0x5a + 0x5b*i`** (slot size 0x5b).
- In a slot: `+0x00` dword (used by the Red Portal handler), `+0x04` word flags (bit 8 set by an item-code output),
  `+0x06` dword (item base id / class id parsed from the output cell), `+0x0a` byte, `+0x0b` / `+0x0c` bytes (read by
  the Red Portal handler), **`+0x0d` the special-output byte**, `+0x10` byte, `+0x59` word.
- Special byte values the D2R parser writes: **1 Cow Portal, 2 Pandemonium Portal, 3 Pandemonium Finale Portal, 4 Red
  Portal, 0xff `usetype`, 0xfe `useitem`, 0xfd and 0xfc item-by-name/by-code outputs.** Median's bytes 6-10 do not
  exist in D2R; its byte lives at slot `+8` and its parameter at slot `+6` in a 0x54-byte slot (so offsets differ).
- Dispatch (`0x526d3c`):
  ```
  0x526d3c  movzx ecx, byte [r15+0x67]        ; r15 = record + 0x5b*i, so this is slot+0x0d
  0x526d41  lea   eax, [rcx-1]
  0x526d44  cmp   al, 3                        ; bound: bytes 1..4  (imm8 at 0x526d45)
  0x526d46  ja    0x526d6f
  0x526d4a  lea   rcx, [rip-...]               ; rcx = image base
  0x526d51  mov   r9, [rcx+rax*8+0x1d3bfc0]    ; table entry (disp32 at 0x526d55)
  0x526d59  test  r9, r9 / je 0x526d6f
  0x526d5e  lea   r8, [r15+0x5a]               ; r8 = the output slot
  0x526d62  mov   rdx, rsi (player) ; mov rcx, r12 (game)
  0x526d68  call  r9                           ; handler(game, player, slot)
  ```
- Handler table at **`0x1d3bfc0`** (qwords, index = byte, entry 0 null): `[1]` `0x52a9d0` (jmp `0x5da2a0`, Cow),
  `[2]` `0x52a9f0` (jmp `0x5da590`, Pandemonium), `[3]` `0x52a9e0` (jmp `0x5da430`, Finale),
  `[4]` `0x52aa00` (calls `0x5da800` = Red Portal, args game, player, slot dword, slot[0xc], slot[0xb]). The table is
  followed immediately by assert strings, so it cannot be grown in place.
  Route 1 (patch the bound and table like Median) needs: change imm8 at `0x526d45` from 3 to 9, and point the disp32 at
  `0x526d55` to a plugin table. The disp32 is relative to the *image base* (rcx), so the plugin table must be within
  +-2 GB of it (VirtualAlloc near the base). It is workable but fragile; route 2 (post-hook) avoids both.

### Gold payment and flag storage

Gold stats: carried 14, bank 15 (`0x2f5020(unit, stat, layer)` total, `0x2f7d10(unit, stat, value, layer)` set; both
start with a `jmp [rip+..]` thunk, entry bytes `ff 25 f2 51 b3 03 90 90 90 90 ...` and `ff 25 32 25 b3 03 90 90 90 90
56 57 41 54 41 56`, already used by existing plugin code). The cost is checked by the search; the payment is the
post-hook's job: take `param*100` from stat 14 first, the rest from stat 15. Whether the client sees the new gold
without an extra packet: probably yes, no explicit update is needed (evidence in section 10; not tested in game).
Flags: nothing to find in D2R, they live in plugin memory keyed per game pointer and player name (see the port spec).
Clear the flags in the game-destroy hook `0x4057c0` (section 8); clearing when the game pointer changes is also enough.

## 2. Act 1 quest rewards (Barbarian milestone)

### Quest system layout (D2R, verified)

Quests are objects in a **game-wide list**: `game+0x21d8 -> { head* }`, `head -> quest`, next quest at
**`quest+0x310`**. Each player also has a per-player list (`0x34b3c0(player)`: nodes `{quest*, next@+8}`). A quest object:

| Offset | Meaning |
|---|---|
| `+0x08` | game |
| `+0x10` u8 | act the quest belongs to (events only reach quests whose act equals the act of the NPC's level) |
| `+0x11` u8 | state flag read by the kill handlers |
| `+0x12` u8 | active (1) |
| `+0x270 + 8*event` | **event callbacks (15 slots used)**, `(quest*, event*)` (1.13c: `+0xa0 + 4*event`) |
| `+0x310` | next quest in the list |

Event struct built by the wrappers (`rdx` of every callback, 1.13c `esi`): `+0` game, `+8` u32 event number,
`+0x10` primary unit (killed monster / NPC), `+0x18` the **player**, `+0x28` u16 NPC class id, `+0x30` u16 dialog
message id. Events seen: **0** player activates/clicks an NPC (`0x515980`, wrapper args `(x, player, npc, r9)`),
**2** `0x5158c0`, **3** `0x515c30`, **4/5** `0x515400/0x5154e0`, **8** monster killed (`0x515640`),
**9** `0x515a69`, **10** `0x515b4e`, **11** NPC dialog message (`0x516940`), 1/2 object events (`0x5182e0..0x518d00`).
The numbering matches Median's notes for slots 0, 2, 8, 11 (confirmed by the handler bodies below).

The generic sender is `0x518ec0(event*, bool allQuests)` (1.13c `0x9fb30`): runs the event number at `event+8` over the
quest list of the unit at `event+0x10`, calling `quest+0x270+8*n` for active quests (or all with the flag).
All the wrappers are tiny and stable; **hooking the wrappers is simpler than hooking each quest's handler** and covers
every quest at once (the plugin filters on quest id by comparing `quest` with the registered object, or on NPC class /
monster class itself).

Each quest's init function stores its handlers into the object. `tools/d2r_find.py qinit a1q6` prints them:

| Quest (file) | Init fn | event 0 (NPC activate) | event 8 (kill) | event 11 (NPC message) |
|---|---|---|---|---|
| id 4 `a1q4` (Search for Cain) | `0x5db310` | `0x5dfe90` | `0x5e29d0` | **`0x5e0830`** |
| id 6 `a1q6` (Andariel) | `0x5db6c0` | **`0x5e1d30`** | **`0x5e1f00`** | `0x5e2660` |

Quest id to source file and the full list of Median-hooked quests: Appendix A (the quest descriptor table at `0x239c5c0` gives id,
act and init function for all 36 quests; `d2r_find.py qinit <id>` lists the handlers).

Entry bytes: `0x5e0830` `40 53 55 56 57 41 56 48 83 ec 60 44 0f b7 72 28`; `0x5e1f00` `40 53 55 41 56 48 83 ec 40 33 ed 48 89 74 24 68`;
`0x5e1d30` `48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57`; `0x5e2660` `48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48`;
`0x5dfe90` `48 89 5c 24 10 48 89 6c 24 18 48 89 74 24 20 57`; `0x5e29d0` `48 89 5c 24 08 48 89 74 24 18 57 48 83 ec 40 48`;
wrappers: `0x515640` `48 89 5c 24 10 48 89 6c 24 18 57 48 83 ec 70 48`, `0x515980` `48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 60 49`,
`0x516940` `40 53 48 83 ec 60 33 c0 48 89 4c 24 20 c7 44 24`, `0x518ec0` `48 89 74 24 10 57 48 83 ec 20 48 8b f9 0f b6 f2`.

### Kill wrapper `0x515640` (verified)

`void QuestKill(Game* game, Unit* victim, Unit* killer)`; called from `0x44db20` (D2Game\src\Unit\SUnitDmg.cpp, the
unit-death function, at `0x44dd20`) once per death. It resolves a minion killer to its owner player, builds event 8
(`+0x10` victim, `+0x18` player) and calls `0x518ec0`. A pre/post-hook here sees every kill with the killer player:
**Andariel = monster class 156 (`0x9c`)**; the vanilla handler does not test the class itself (the quest's own list
position does), so compare the victim's class id (`0x349860(victim, file, line)`).

### Andariel catalyst (Median `0x9e9f0`, vanilla 1.13c `0x67e00`; D2R `0x5e1f00`)

1.13c's `0x67e00` and D2R's `0x5e1f00` are the same function: tests quest-6 bits 0 and 1 (`0x325c50(record, 6, 0/1)`),
runs the drop sequence (D2R: random state table at `0x23aaa38/0x23aaa58`, drop via `0x43fbf0`), sets state. Median's hook
adds, in Normal, a once-per-player drop of TC "Quest Cata Disenchant" and sets a plugin-owned flag.
**Simplest port:** at `0x515640` (kill) or in a detour of `0x5e1f00`, when the victim class is 156, the game
difficulty `game[0x104] == 0`, and the killer has not yet got the flag, create the catalyst item directly in the
killer's inventory with `0x517530` (code `'3o[ '` = `0x205b6f33`) instead of rolling a treasure class. This avoids needing
D2R's treasure-class executor, now found at `0x4404f0` (section 7; 1.13c `0x12380`). `0x43fbf0` below is not the TC path: it
picks a gold / fallback item (`gld `, `0x443f40`) and creates it at the monster's position. Store the per-player flag as a plugin-owned bit in the player's quest record (e.g. quest 6,
bit 0xc) with the set function.

### Cain re-issue and Akara's reward

- Re-issue (Median `0x9eb40`, vanilla 1.13c `0x60a80`, D2R `0x5e1d30` = a1q6 event 0): when the player activates an NPC
  (event 0; `event+0x10` is the NPC, `event+0x18` the player) and the plugin flag is set, create `'3o[ '` if
  `0x441980(game, player, code)` (find item by code in the inventory, returns the item or null) says the player lacks it.
  Hook `0x515980` (every NPC click) or `0x5e1d30`.
- Akara's / Cain's reward (Median `0x9e730` / `0x9e810`, vanilla 1.13c `0x66ce0`, D2R **`0x5e0830`** = a1q4 event 11): the
  handler is the same function (same constants: NPC class `0x94` Akara, `0x109` = 265 Cain in town (messages `0x7b/0x7d/0x7e` are his), messages
  `0x61, 0x70, 0x76, 0x7d`, `Ordinal 10174` -> `0x325c50`). It handles `msg 0x61` (starts the deciphering), `msg 0x70`
  (hand over `bks `, create `bkd ` through `0x517530`) and `msg 0x76` (quest 4 bit 1 set: clear bit 1, set bit 0, give the
  vanilla `rin ` reward, then follow-up). Median adds Griswold's Heart at `msg 0x76` with bit 1 set: run before the vanilla
  handler (detour `0x5e0830`; `event+0x28` u16 == `0x94`, `event+0x30` u16 == `0x76`, bit test `0x325c50(rec, 4, 1)`),
  create the unique with `0x517530`-style creation (a unique by index needs the item-create call at `0x43d530`, see below).
  D2R's ring is delayed through a quest timer callback at `0x5d9a30` (creates `'rin '` for the player id in the callback
  struct); the Median hook can run independently.
  *Note:* the lane brief calls this "Akara's Heart / Den of Evil reward"; the code path is Akara's message `0x76` after
  the Cain rescue (quest 4), not quest 1.

### Reward and quest-bit helpers

| Function | RVA | Signature | Confidence | How |
|---|---|---|---|---|
| quest bit test | `0x325c50` | `(record, questId, bit)` -> 0/1 (bit `questId*16+bit` of the record's bitmap; tail of `0xa1ba50`) | verified | called 196 times in Quests code, args seen: `(rec, 6, 0)`, `(rec, 4, 1)` |
| quest bit set | `0x325c00` | same; tail `jmp 0xa1ba30` (`or [byte], mask`) | verified | same prologue, the or-form |
| quest bit clear | `0x325990` | same; tail `jmp 0xa1b810` | likely | same prologue, other mask table (`0x1d99750`), not seen executing |
| player quest record | `rec = *(u64*)(0x34b240(player) + game[0x104]*8 + 0x40)` | | verified | repeated in a1q4, a1q6 handlers |
| create quest item by code | `0x517530` | `(game, player, code /*r8d*/, itemLevel /*r9d, 0 = player's level*/, [rsp+0x20] quality/seed arg, [rsp+0x28] = 1)` -> item* or null; creates via `0x43d530`, adds to the inventory (`0x471500`), sends the item packet (`0x535f60`) | likely | matches 1.13c `0x36470` (`QUESTS.CPP`, create + inventory + packet); callers pass `'bkd '`, `'rin '` |
| find item by code in inventory | `0x441980` | `(game, player, code)` -> item or null (`D2Game\src\Items\Items.cpp`; walks the inventory, compares `0x36ef50(item)` with the code, and for quest items also stat 0x164 against the game difficulty) | verified | read; used before the scroll hand-over in `0x5e0830`; entry bytes `48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48` |
| monster drop item creation | `0x43fbf0` | `(game, monster, type /*2 in Andariel*/, out*, ..., -1, 0)`; builds a 0x84..0xc8-byte item-gen struct and calls `0x43cd50` | likely | matches 1.13c `0x11a70`'s role (the drop step of the kill handler) |
| item-gen core | `0x43cd50` | | guess | callee of `0x43fbf0` |

## 3. NPC interaction menu

### Server

- **NPC click**: block `0x4c26a7` of a PlrMsg packet handler (it calls `0x502f60`, sends the 0x27 reply with the NPC unit
  id/type via `0x47e340`, and runs quest event 0 through `0x515980` at `0x4c26f2`). `.pdata` only gives this block, not
  the function entry; the parent is a large `D2Game\src\Player\PlrMsg.cpp` switch. Prefer hooking the quest wrappers
  (section 2) for rule logic; use this block only to read how the click arrives.
- **Dialog message** (the player picks a dialog line): **`0x4c33e0`**, entry bytes
  `40 55 53 57 41 54 41 55 48 8d 6c 24 e0 48 81 ec`; it calls `0x515980` (event 0, `0x4c37b2`) and the message wrapper
  `0x516940` (event 11, at `0x4c35cf`, `0x4c3bbf`, `0x4c3bd4`). Custom actions (Median's 16 action slots) can be run by
  hooking `0x516940` (`(game, player, npc id /*r8d, -1 = none*/, msg /*r9w*/)`; the quest handlers read the message at
  `event+0x30` and the NPC class at `event+0x28`).
- **Trade / gamble / hire packet**: **`0x4b0470`** `(game, player, packet*, size)`, `D2Game\src\Player\PlrMsg.cpp`
  (assert strings "Can't trade / gamble / hire with this NPC. player: %u, dwParam1: %u, dwParam2: %u."). Entry bytes
  `40 55 53 56 57 41 54 41 55 48 8d 6c 24 d1 48 81`. It finds the NPC (`0x53b8f0`), fetches the NPC record
  `0x3971a0(dataFlag, npcClass)` and branches on the request type (`[rbp-8]`: 1 trade, 2 gamble, 3 hire) testing
  record bytes `+8` (trade), `+9` (gamble), `+7` (hire), see section 6.

### Client

The NPC dialog UI is in **`D2Client\src\UI\npcmenu.cpp`** (string `0x1cc78a0`): 22 functions between `0x10a520` and
`0x117634`; global state `0x2a4875c` (NPC unit id), `0x2a48764` (NPC class, word), `0x2a487b0`, `0x2a48760`. Three of
them call the NPC-table getter `0x3971a0` (`0x1147a0`, `0x1162f0`, `0x117510`; `0x117510` is the "click a menu item"
sender: it reads record byte `+0xb` and sends packet 0x2e through `0xec820`). **The menu is built from the same static
NPC table** as the server checks; so editing that table (section 6) changes both the buttons and the server answer.
How the visible list is composed (a heap table of 48 records, 5 lines per record), where extra lines can be added and
what the server does with a chosen line: section 9. Layout (sprites, JSON panels) is separate and not in this lane.

## 4. Level entry / warp (dungeon lockout)

Read from the Ghidra decompile (lane d2r-addresses-2, 2026-10-21). Entry bytes in the table at the end of this section.

### Flow (verified)

```
0x4adbb0  packet handler "use warp" (game, player, packet*, size = 5)   PlrMsg.cpp
   finds the clicked tile unit (type 5, id = packet dword), checks hostility 0x42bfb0 and the distance
   (0x325140(player, tile) < 0x33 then again < 5), then
-> 0x491ff0 (game, player, tile)                                     SUnit.cpp   <-- the warp action
   other callers: object-operate functions of stairs / ladders / trapdoors (0x58e230 -> 0x58e2d8, 0x590160, 0x5906e0,
   0x5a11a0): they look for the type-5 tile in the object's room and call 0x491ff0 with it
```

`0x491ff0(Game* rcx, Unit* rdx = player, Unit* r8 = tile)`; body (decompiled):

1. `room = 0x34b440(tile)`; `cls = 0x349860(tile, "SUnit.cpp", 0x133)` (= `*(u32*)(tile+4)`, the tile's class / link id).
2. `dst = 0x2efee0(game[0x106], room, cls, &out)`: **the linked tile unit in the destination room** (D2Common `Dungeon.cpp`
   walk of the room's unit list for a type-5 unit with the same class), `out` points at a record whose `+0x40/+0x44` are
   the x/y offsets to land at. `dst == 0` -> nothing happens.
3. `dstRoom = 0x34b440(dst)`; `(x, y)` = the tile's position (`unit data +0x10/+0x14`).
4. `landing = 0x362f90(dstRoom, &xy, size, 0x1c09, 1)` (find a free spot, unit size `0x34b580(player)`); null -> return.
5. `srcLevel = 0x2efc10(room)`, `dstLevel = 0x2efc10(landing)` (level id of a room, entry bytes in the table).
6. **Quest lock**: if `dstLevel - 0x49 < 0x3c` and bit `dstLevel-0x49` of `0x880200008000001` is set (levels 73, 100, 118, 128,
   132; 73 = Duriel's Lair, 100 = Durance of Hate 1, 132 = Worldstone Chamber, and 118 / 128 only when the source level is
   120 = the Ancients' gates) then `0x516a50(game, player, srcLevel, dstLevel)`; a nonzero result **refuses** (the code jumps to the end, `0x492295`).
   `0x516a50` is a small switch calling the quest checks (`0x59ec70` Duriel, `0x1405e99e0` level 100, `0x546b20`, `0x546c90`).
   It has exactly one caller (the call at `0x49218c`), and it is the vanilla twin of D2Sigma's replacement `0x5d810`
   `(game, player, srcLevel, dstLevel)` which returns 1 to refuse.
7. Otherwise `0x491ca0(game, player, dstRoom, x, y, 0, 0)` does the move (SetPos / room change; 25 callers: every teleport
   uses it); on success `0x42d2c0(game, player, 0, 2, x, y, 0, 0)` sets the walk mode and `0x47abc0(client, 0x0d, ...)`
   sends the position packet (id 0x0d) to the player.

### Where to refuse and how (answer)

- **Best hook: detour `0x491ff0` itself** (5 callers, one prologue, no mid-function patching). Compute the destination level
  before calling the original:
  ```
  room    = GetRoom(tile)                          // 0x34b440
  cls     = *(uint32_t*)(tile + 4)                 // what 0x349860 returns (the file/line args are only for asserts)
  dstTile = FindTile(game[0x106], room, cls, &out) // 0x2efee0
  dstLvl  = LevelOfRoom(GetRoom(dstTile))          // 0x2efc10(0x34b440(dstTile))
  srcLvl  = LevelOfRoom(room)
  ```
  then run the Median logic (sealed? difficulty / level / class requirements from `mxl_dungeons.h`, unlocking only by the
  lock group). To **refuse, return without calling the original**. That is the vanilla refusal path (`0x492295`): nothing
  is sent and the player stays where he is, the same as an unfinished Duriel quest. The client has no pending state for
  the click (it only sent "use tile"), so there is no hang.
- Alternative (smaller): patch the call at `0x49218c` to a plugin function with the `0x516a50` signature (return 1 to refuse)
  and also bypass the range test in front of it (it only calls `0x516a50` for the five levels above); a detour of
  `0x491ff0` avoids both.
- **Signalling to the player.** The vanilla path is silent. A message needs a server -> client packet. No ready-made
  "send system message" function was located (the telemetry lines in these handlers only log). Raw send works: the game
  has a network service singleton `0x649610()` (`lea rax, [global]; ret`), and a packet is sent with
  `(*(void(**)(void*, uint32_t conn, Span*))(*svc + 0x28))(svc, *client, &span)` where `span = {begin, end}` points at the
  packet bytes (see `0x47abc0`, which sends packet 0x0d this way; `client = *(Client**)(0x34b240(player) + 0xf0)`, `*client` is the
  connection id). For the lockout message use a chat / game-message packet (0x26 or 0x5a) built by hand, or let the
  D2RLoader client plugin show the text (it knows the level id).
- **Portals (red portal / town portal) do not go through `0x491ff0`** (their object functions place the player with their own
  code); Median starts the lockout clock in the portal-object operate functions (item_spells hooks), which are not part of
  this section.

The dungeon `Entry` table (`mxl_dungeons.h`) is plugin-owned: D2R's levels table has no +0x14b..+0x15f extension. Per-game
lockout state: use the game pointer as key and reset in the game-destroy hook (section 8).

| Function | RVA | Entry bytes |
|---|---|---|
| warp action | `0x491ff0` | `48 8b c4 53 55 57 41 56 48 83 ec 68 49 8b d8 4c` |
| quest-lock test | `0x516a50` | `45 8b d1 41 83 ea 49 74 33 41 83 ea 1b 74 28 41` |
| teleport into room | `0x491ca0` | `40 55 53 56 57 41 54 41 55 41 56 41 57 48 8d 6c` |
| linked tile finder | `0x2efee0` | `48 89 5c 24 18 56 48 83 ec 30 48 8b 52 18 48 8d` |
| room -> level id | `0x2efc10` | `48 85 c9 75 03 33 c0 c3 48 8b 49 18 e9 9f 13 07` |
| unit -> room | `0x34b440` | `40 53 48 83 ec 20 48 8b d9 48 85 c9 75 13 88 4c` |
| unit class id (assert args) | `0x349860` | `48 83 ec 28 48 85 c9 75 1d 88 4c 24 30 48 8d 4c` |

Confidence: verified (every step read in the decompile; the five quest-lock levels are decoded from the bit mask). The client's
entry-requirements text (Sigma `0x5d150`) is client UI and does not exist in D2R. The 1.13c function matching `0x491ff0` was not
named by address (D2Game's warp lives next to the quest-lock test that Sigma replaces).

## 5. Monster spawning and the game tick (treasure goblins)

### The server game frame (verified)

`0x406550(Game* game)` is the per-game frame, 25 Hz, called for every live game by `0x406400(tickMs)` ("int SrvProcessAllGames(unsigned
long)", global slot array `0x2a9c020`, 1024 u64 game ids, `0x4062f0(id)` -> `Game*`). It increments the frame counter
**`game+0x170`** (1.13c `+0xa8`), runs the subsystem updates (client list update, `0x516e40` every 20 frames, `0x4088c0` every 12 frames,
the per-player pass every 11 frames) and returns. This is the exact twin of 1.13c `0x2e050`, and Median patches the end of it
(`0x2e15a`). Hook: detour `0x406550`, call the original, then run the plugin's 15 s goblin tick
(`frame % 375 == 0`). Entry bytes `48 89 74 24 20 55 57 41 57 48 8d 6c 24 b9 48 81`. Found with `tools/d2r_scan.py pat2 "e9 a2 8b 2e"
"ab aa aa 2a" "67 66 66 66"` (the x%11, x%12, x%20 magic multipliers all in one function).

Useful game offsets from this function: `game+0x138` first client (linked via `client+0x628`, `client+4 == 4` is "in game",
`0x484c90(client, 0)` = the client's player unit), `game+0x190` vector of per-act level data, `game+0x106` expansion
flag, `game+0x104` difficulty, `game+0x1aaa0` the telemetry object. The frame counter is the clock for the "last kill within 375
frames" test (player's last-kill frame goes into plugin state; there is no D2R field for it).

### Spawn (verified)

All spawns go through **`0x493cc0(SpawnInfo* rcx)`** (returns the monster `Unit*`, null on failure). `SpawnInfo` (stack struct,
filled identically by `0x4929e0`, `0x4934d0` and `0x492e70`):

| Offset | Field |
|---|---|
| `+0x00` | `Game*` |
| `+0x08` | room (`Room*`) |
| `+0x10` | related unit (owner / summoner; 0 for a plain spawn) |
| `+0x18` | **monster class id** (monstats row, `0x1400976e0(game[0x106], class)` is the record getter) |
| `+0x1c` | u32, passed to unit creation (1 in observed calls) |
| `+0x20` | 0 |
| `+0x24` / `+0x28` | **x / y** (sub-room coordinates, absolute) |
| `+0x2c` | i32; -1 normally (negative = default) |
| `+0x30` | u16 flags (0x40 seen with minions; bit 0x80 tested) |

The simplest call is the wrapper **`0x4929e0(Game* game, Room* room, int x, int y, int classId, int a6 = 1, int a7 = -1, int a8 = 0, uint16 flags = 0)`**
(entry bytes `4c 8b dc 49 89 5b 08 57 48 83 ec 70 8b 84 24 a0`): it builds the struct, calls `0x493cc0`, and on success finishes the
monster with `0x4a5800(game, monster, unitId, 1, 1, a8)` (adds to the game / sends it to clients). Returns the monster or null.
`0x4934d0(game, nearUnit, classId, a4 = 1, a5, count, flags)` spawns `count` copies around a unit (used by Diablo's minion calls with
class ids 0x1c5, 0x185, 0x17d). The 1.13c spawn Median uses (`0x10042630(id, x, y)`) maps to `0x4929e0`.
The room for a goblin: `0x34b440(playerUnit)`; position near the player: `0x34a040(player, &xy)` (unit position as two ints) plus an offset,
checked with `0x38dfb0(room, x, y)` ("is this position inside the room", returns the room or null) like `0x43fbf0` does.

| Function | RVA | Entry bytes |
|---|---|---|
| game frame | `0x406550` | `48 89 74 24 20 55 57 41 57 48 8d 6c 24 b9 48 81` |
| process all games | `0x406400` | `48 89 5c 24 18 48 89 6c 24 20 56 57 41 57 48 83` |
| game by id | `0x4062f0` | `4c 8b 05 19 5d 69 02 48 8b d1 4d 85 c0 74 77 4d` |
| spawn core | `0x493cc0` | `48 89 5c 24 10 48 89 74 24 18 48 89 7c 24 20 55` |
| spawn at room x,y | `0x4929e0` | `4c 8b dc 49 89 5b 08 57 48 83 ec 70 8b 84 24 a0` |
| spawn N around unit | `0x4934d0` | `40 53 55 41 54 41 57 48 83 ec 78 45 8b f9 45 8b` |
| monster post-init / add | `0x4a5800` | `48 89 6c 24 18 57 41 56 41 57 48 83 ec 30 4c 8b` |

The "goblin sighted" notice (Median packet 0x5a type 0x15) has no D2R counterpart; use the raw send in section 4 or a client-side message.

## 6. Item / skill requirement checks (the requirements evaluator)

Median's requirement table only gates NPC services (quests-requirements.md). In D2R the same gates exist as **a static
NPC table**, and its checks run on both client and server:

- **Table: RVA `0x237ea60`, 43 entries of 0x1c bytes**, writable data; getter **`0x3971a0(dataFlag /*ecx*/, npcClass /*edx*/)`**
  returns the entry or null (linear scan, entry `+0` is an s16 class id). Entry bytes of the getter:
  `45 33 c0 4c 8d 15 b6 78 fe 01 41 8b c8 4d 8b ca`.
- Fields (verified with Kashya 150, Gheed 147, Qual-Kehk 515 against known game behaviour): `+0` class id (s16), `+7` hire
  allowed, `+8` trade (shop) allowed, `+9` gamble allowed, **`+0x10` hire quest gate: quest id + 1 (0 = none)** (Kashya `3`
  = quest 2 Blood Raven, Qual-Kehk `0x25` = quest 36 Rescue on Mount Arreat: **this is exactly Median's hire gating, so
  requirement rows 9 and 14 already hold in D2R**), `+0x11` minimum level for hiring (Kashya 8), `+0x0a` act index,
  `+0x18` a second id (`-1` none).
- The server checks (`0x4b0470`): hire needs `+7`, then the quest gate (`0x325c50(rec, quest-1, 0)` must be set) and the
  level (`stat 12`, via `0x2f5020`); trade needs `+8`; gamble needs `+9`.
- **Hook plan for `requirements.cpp`:** (1) static cases at plugin load: write the table bytes (Median turns the shop off
  for Atma 176, Geglash, Cain in acts 2 and 5, act2vendor1; the table is in `.data`, so a guarded write like the
  srvdofunc table works). (2) Dynamic cases (anything the table cannot say, such as Cain's per-catalyst buttons): hook the
  server packet handler `0x4b0470`, or call the evaluator from a detour of the getter `0x3971a0`... the getter has no
  player argument, so prefer `0x4b0470` for denial and the client menu functions for display.
- Item level/class requirement checks for equipment and skills (strength, level, class) are not gated by Median's
  evaluator; D2R's own `0x376de0(item, player)` (item level requirement, plugin-known) is the call to reuse.

## 7. Treasure classes and item creation

### Treasure-class executor (verified)

**`0x4404f0`** is 1.13c `0x12380`: same ten parameters, same algorithm (NoDrop float pow, pick count, `0x6ac690c5` LCG,
recursion stack, up to 6 results by default), `D2Game\src\Items\Items.cpp`, log line "Spawn From %d".

`void TCExec(Game* game /*rcx*/, Unit* monster /*rdx, rng owner and drop origin*/, Unit* killer /*r8, player (may be null)*/, TcRecord* tc /*r9*/, int quality /*5th, 0 = roll*/, int a6 /*6th, quality-roll input (1.13c: param_6 of the quality roll)*/, int force /*7th, nonzero skips the NoDrop roll*/, Unit** out /*8th: items array, or null*/, int* outCount /*9th*/, int max /*10th*/)`

(args 1-4 in registers, 5-10 at the caller's `[rsp+0x20]` ... `[rsp+0x48]`; entry `4c 8b dc 55 53 49 8d ab 08 f7 ff ff 48 81 ec e8`; the 5th-10th
meanings are taken from the identical 1.13c body, the D2R body keeps the same order: `out`/`max` are tested as `param_8`/`param_10` and `*outCount` as `param_9`).
With `out == null` up to 6 items (`max <= 0`) are created via the item-gen path around `monster`; with `out` they are returned in the array.
TC record (D2R, `0x397290` hands them out): `+0x10` total probability (classic), `+0x14` (expansion), `+0x18` picks (negative =
"all" mode), `+0x24` flags (0x40 = conditional), `+0x38` entries pointer, `+0x40` entry count; entry stride **0x20**: `+0` prob (classic),
`+4` prob (expansion), `+8` item id or TC id, `+0xc` flags (1 = item code, 2 = TC, 4 = unique/set class, 0x10 = "always", 0x40 = conditional).

**Wrapper by TC index: `0x441300(Game*, Unit* monster, Unit* killer, u16 tcIndex /*r9w*/, int quality, ...)`** (1.13c `0x12d60`): it
adds the monster's level (stat 12, or the area level) to select the right level step of the TC with **`0x397290(dataFlag, tcIndex, level)`**
(returns the TC record; group array `*(base ptr from 0x300a90() + 0xd78)`, count at `+0xd80`, stride `0x150`; if the group has level
steps the one whose `+2` level bound is not exceeded wins), then calls `0x4404f0`. Entry bytes `0x441300` `40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 ec`;
`0x397290` `48 89 5c 24 08 55 56 57 48 83 ec 30 41 8b d8 0f`. Callers of `0x4404f0`: `0x441300` and `0x5932ec` (inside `0x5930b0`, a
monster-side caller, presumably the death drop; not read), exactly the two callers 1.13c `0x12380` has.

For the catalyst: `tcIndex` is the index of the named TC ("Quest Cata Disenchant") in the TreasureClassEx group array; resolve it once by scanning
`0x300a90()+0xd78` (stride 0x150) for the plugin's row (the name string is in the record), or take the index from the monstats row
(`TreasureClass1` field) like Median does. Not all of this has been exercised in game.

### Item creation (verified for the primitives, open for "unique by index")

Item-gen struct `ItemGen` (0xc8 bytes, zeroed by the builders): `+0x00` owner Unit* (the receiver), `+0x10` `Game*`, `+0x18` item level,
**`+0x20` base item class id**, `+0x24` mode (3 = on the ground, 4 = inventory), `+0x28`/`+0x2c` x/y, `+0x30` room, `+0x38` u16 = 1,
`+0x3a` u16 = `game[0x128]`, **`+0x3c` explicit-fields flag** (nonzero: the next fields are applied), `+0x40` quality / drop kind chosen by the caller
(2 in the quest creators, 5 or 6 at `0x541cec`; `0x43fbf0` re-rolls the base item until it can be magic when it is 4; **no reader of `+0x40` was found in
`0x43cd50`**, so how D2R applies it is open), `+0x44` stat 0x46 (defense) or gold amount, `+0x48` / `+0x4c` durability (0x48) / max durability (0x49)
(explicit mode), `+0x54` item flag word, `+0x58` unit seed, `+0x5c` item init seed, `+0x60` ear name.

- **`0x43cd50(Game* rcx, ItemGen* rdx, int applyExplicit /*r8d*/)`** = 1.13c `0x11490`: creates the item (`0x490410`), sets flags, level, stats,
  adds the ear owner for kind 7; returns the item or null. Entry `40 55 53 56 41 54 41 57 48 8d 6c 24 c9 48 81 ec`.
- **`0x43d530(Unit* owner, int classId, Game* game, int mode /*4*/, int quality, int bitA, int bitB, int ilvl, int applyExplicit,
  int seed, int initSeed)`** = a struct builder + `0x43cd50`; the 8 existing callers pass mode 4 and quality 2 (`0x4fa8fd`, `0x4fb843`, `0x4404a2`), a computed
  5 / 6 (`0x541cec`) or a variable. Entry `40 55 53 56 57 48 8d 6c 24 f8 48 81 ec 08 01 00`.
  `0x517530` (create by code in a player's inventory, section 2) is its main wrapper.
- **Unique item by index (open)**: Median's `D2UniqueItems.cpp` `0xbb330(game, owner, uniqueIdx)` (name lookup `0xbb4d0`, table at txt +0xc24, stride 0x14c)
  zeroes a 0x84-byte struct and sets owner, game, ilvl = unique row `+0x34`, class = the row's code (`+0x28`), mode 4, word 1 / game word, **quality 7
  at struct `+0x30`** and **unique row + 1 at struct `+0x40`**, then calls 1.13c `0x11490`. With the same layout shift as the other fields
  (1.13c `+0x2c` explicit flag = D2R `+0x3c`, `+0x30` = `+0x40`), the D2R slots would be quality `+0x40` and unique row + 1 at `+0x50`. **D2R's `0x43cd50` reads neither**
  (`+0x58` / `+0x5c` turned out to be seeds, `FUN_14034edd0` stores the unit seed), so there is no function taking a unique index that I could confirm; the
  quality / unique choice sits deeper in the unit creation (`0x490410` -> `0x1404905a0`), which was not read. Practical routes for Griswold's Heart:
  (1) create the unique's base item with `0x517530` / `0x43d530` with quality 7 in the 5th argument and **test in game** whether the struct's `+0x40`/`+0x50` slots
  select the row (run `0x43d530` and then patch the item data), (2) run a one-entry TC (`0x4404f0` with a TC whose only entry is the unique class).
  Route 2 reuses the vanilla unique roll and needs only TC data (unique drops by TC pick the unique whose base matches; if Griswold's Heart is the only unique
  of its base in the data it is deterministic). Confidence: primitives verified, unique-by-index open.

## 8. Game create and destroy (server)

- **Destroy: `0x4057c0(u64 gameId)`** (log "Deallocate game %p"): looks the game up in the id hash map (`0x2a9c010`), tells the network service,
  clears the id in the slot array (`0x2a9c020`, 1024 entries), frees per-game subsystems (`0x5128d0`, `0x513e20`, `0x5026f0`, `0x486be0`, `0x488bf0`...), then
  the node. The `Game*` is the map node + 0x10: get it with `0x4062f0(id)` **before** calling the original. Hook this to clear any per-game state
  (craft flags, dungeon lockout clocks, goblin timers). Entry bytes `40 53 48 81 ec 80 00 00 00 48 8b 15 40 68 69 02`. The only caller is the idle sweep
  **`0x406e40(tickMs)`** (`48 89 5c 24 18 89 4c 24 08 55 56 57 41 54 41 55`): it destroys a game 300 s after the last client left (`game+0x140 == 0`) or when
  `game+0x1a5d8 != 0`.
- **Create: `0x4049d0`** (14 arguments: name, password, description, ... difficulty, flags; frame of 0x14ec8 bytes, `D2Game\src\Game\Game.cpp`
  strings in the chunk `0x404b9a`): allocates the game, registers it in the slot array and the id map, initializes the subsystems. No direct
  callers (entered through a job / pointer). Entry `40 55 53 56 57 41 55 41 56 41 57 48 8d ac 24 70`. Likely: the function reads the
  game name for the duplicate check and registers the game; the `Game*` is not returned. A first frame hook (`0x406550` seeing a new pointer) is
  an equivalent create hook with no extra risk.
- Per-client join / leave states are handled in `0x408ea0(Game*)` (strings "[JOIN 5] %u clientid, SrvJoinAct: Added to game.", "[JOIN 6]").

## 9. Client NPC menu (npcmenu.cpp)

### Composition (verified)

The menu is **data driven**. `0x360d0` (a CRT static initializer, entry `48 89 5c 24 08 48 89 74 24 10 48 89 7c 24 18 55`) copies a stack image
into a heap array and stores it in the globals **`0x2a48830` (pointer)** and **`0x2a48838` / `0x2a48840` (count 0x30 / capacity)**.
`NpcMenu` records, **stride 0x48**, 48 of them:

| Offset | Field |
|---|---|
| `+0x00` u16 | NPC class id |
| `+0x04` u32 | line count **including the final Cancel** (3 = two lines + Cancel) |
| `+0x08` u16[8] | string table ids per line: **0xd35 Talk, 0xd44 Trade, 0xd46 Gamble, 0xd45 Hire, 0xd06 Repair, 0xfb4 Identify**, 0xd37 / 0xd39 two special lines |
| `+0x18` qword[5] | click handlers per line: **`0x112eb0` Talk, `0x112f30` Trade (sends 0x38 kind 1), `0x112c80` Gamble (0x38 kind 2), `0x112620` Hire, `0x114000` Identify, `0x1162f0` special (quest-gated)**, `0x112c30` Cancel (implicit) |
| `+0x40` u8 | flag (1 for all NPCs that talk) |

Key functions: **`0x1147a0(dataFlag)`** builds the visible list (reads record `0x113990(npcUnit)` = record index by class id; loops `count - 1`
lines, special-casing ids 0x1507, 0xfb4, 0x2ba0 and quest bits 0x29 / 1; line text = `0x143e2b730(stringId)`; adds each line to the list widget
`DAT_142a487b0` with `0x1401e9030`; the last line is always Cancel `0x102e` -> `0x112c30`); **`0x117510(param)`** sends packet 0x2e (the
NPC interact packet, built by `0xec820`); **`0xec730(id, a, b)`** builds the 9-byte client packets (the menu uses `0x38`).
The sprite / frame layout is separate. Records read from the initializer (class: lines): 148 Akara Talk+Trade; 147 Gheed and 199 Talk+Trade+Gamble;
154 Charsi Talk+Repair; 150 Kashya Talk; 198 Greiz and 252 Asheara have Hire (Asheara Talk+Hire+Trade); 244, 245, 246, 265 (0x109) and 520 (0x208), the Cains, are
Talk+Identify; 255 Ormus Talk+Trade; 511 Larzuk Talk+Repair; 512 Anya Talk+Trade+Gamble; 513 Malah Talk+Trade; 175 and 264 Talk plus one special line (0xd37 / 0xd39,
handler `0x1162f0`); 257 (first record) Repair only. All records can be listed again by decompiling `0x360d0` and reading the stack image
(`DecompOut.java 0x360d0`); the field layout is the table above. Maximum **5 lines per record** (the `+0x18` handler array has 5 slots, the id array 8);
there are no spare slots for 16 actions.

### Adding Median's action lines

Two options, both client-side data edits at load time (the table is on the heap, patch it after the process static-init, e.g. at the first
`0x1147a0` call or when `*(void**)(base+0x2a48830) != 0`):

1. **Edit the record** of the NPC (e.g. Cain 0xf4 / 0x109: Talk, Identify, +up to 3 actions): bump `+4`, write a string id in `+8` (use a plugin-added
   string, the string table id space is the same `tbl` lookup `0x143e2b730`), and put a plugin handler (`bool __fastcall (void)`, shaped like `0x112f30`)
   in `+0x18`. The handler sends the action: `0xec730(0x38, kind, npcUnitId)` with `npcUnitId = *(u32*)0x2a4875c` (current NPC) and kind >= 0x10.
2. **One "More..." line** (Median has 16 slots) whose handler opens a plugin list; avoids the 5-line cap.

### Server handler of a chosen custom line

The packet is `0x38 [kind u32][npc unit id u32]` (9 bytes) handled by **`0x4b0470(Game*, Unit* player, packet*, size = 9)`** (section 3). It finds the NPC
(`0x53b8f0`), its row `0x3971a0`, and branches on `kind`: 1 trade, 2 gamble, 3 hire (anything else is logged and dropped). Hook `0x4b0470`: if
`kind >= 0x10`, run the plugin action for `(player, npc, kind - 0x10)` (the requirements evaluator in `mxl_requirements.h`, then the action: cube
craft, catalyst, etc.) and return; otherwise call the original. The "Talk" line goes through the dialog-message packet and the quest wrappers
(`0x4c33e0`, `0x516940`, section 3), not through `0x38`.

### NPC actions lane (2026-10-21; plugin/src/npc_actions.cpp)

| What | Address | Notes |
|---|---|---|
| Menu builder hook | `0x1147a0` | entry `40 55 53 57 41 54 41 56 48 8d ac 24 10 fc ff ff 48 81 ec f0 04 00 00` (23 bytes, no rip-relative). The loop adds `count - 1` lines; the Identify line (id 0xfb4) overwrites the record's `+4` with 3 or 2 on every build, so lines behind it vanish and a skipped Identify is never retried: the plugin keeps its lines in front of Identify and re-sets the count on each call |
| Menu packet hook | `0x4b0470` | entry `40 55 53 56 57 41 54 41 55 48 8d 6c 24 d1 48 81 ec c8 00 00 00` (21 bytes). Packet: `u8 0x38, u32 kind, u32 npc unit id` (9 bytes). The NPC is found with `0x53b8f0(player, unit id, 0x32)` (distance 50), its class is `[unit+4]` |
| Packet sender | `0xec730(u8 id, u32 a, u32 b)` | builds the 9-byte packet; `0xec820` is the 0x2e variant |
| Cancel handler | `0x112c30` | closes the list widget (`0x1e9390`), clears `0x2a486a4`, calls `0x10de80(npc id)` and `0xc7d30(8)`; returns 1 |
| Vanilla respec packet | `0x4b2530` = packet 0x39 (`CCMD_RESPEC`; 0x38 is `CCMD_SUBSCRIBE_TO_VENDOR`) | requires NPC table byte `+5`, and the quest `byte[+0x10] - 1` bit 1; then `0x580f20(game, player, 0)` and, when `+0x10 != 0`, `0x5d9ae0(game, player)` |
| Respec | `0x580f20` | `0x4360f0` (skills: refunds into stat 5, `0x46f220`, `0x474d20`), `0x52ddf0` (stat points back to the charstats start values), `0x470c90` (equipment re-check), `0x2f3760`, `0x474d60`, `0x491960(player, 2, player)` |
| Respec bookkeeping | `0x5d9ae0` | quest 41: sets bit 0, clears bit 1; if bit 0xf is clear the respec quest (a1q1 `+0x12`) is deactivated ("deactivated respec quest") |
| Imbue / sockets / personalize panel | panel 0xd = `ImbueItemsPanel` (`0xc7aa0`) | client opener `0x1e3e20` (mode 1 at `0x2a96960`, then `0x1e5b20`), called by `0x10dd30` (an NPC-menu function) and from the server-to-client message handler `0x1e3c00` (byte `+5`: 0 open, 4, 5, 6/7 close). Class check `0x10d150` (current NPC class): 0x9a Charsi, 0x1ff Larzuk, 0x200 Anya. Server per-NPC checks `0x36aed0`, `0x36b410`, `0x36b1d0` |
| Travel | client special line handler `0x1162f0` sends packet 0x37 with the NPC table `+0xc` destination | D2R's own; Median only adds the waypoint grant |

## 10. Gold stat changes and the client

`0x2f7d10(unit, stat, value, layer)` and `0x2f5020` start with a 10-byte `jmp [rip+x]` (the D2RLoader / protection thunk) followed by the real body
(`0x2f7d1a`, sets the stat in the unit's stat list; asserts `0x2f1dc0`, calls `0x2f9b10`, `0x2f3030` and the stat-change path). The stat engine
itself is not readable (the dump holds the thunk target in `0x3e2a248`). Evidence for "no explicit client update needed": the game's own gold
changes do not send anything: the gold pickup code (`PlrModes.cpp`, function `0x42c110`) does `0x2f5020(unit, 14)`, compares with the cap
(`0x2f5c60` family), then `0x2f7d10(unit, 14, newGold)` and **no packet call follows** (its only network calls are the item pickup packet `0x535f60`);
the same `0x2f7d10(unit, 0xe, ...)` is used by item creation for stackable gold (`0x43cd50`, `0x43ef10`). In 1.13c the stat list of a player
has the "send to client" callback installed, which makes the set function emit the 0x1d / 0x1e / 0x1f attribute packets itself. So the post-hook's
payment (`0x2f7d10(player, 14, gold - cost, 0)`, bank `15`) should show up on the client without extra work; **not tested in game**. If a
refresh is ever needed, the cheapest forcing step is to re-set the stat (set 14 to the old value and then to the new one). The stat-update packet
sender itself was not located (no packet-id constant for 0x1d-0x1f near the network service in 102 sender functions; the builder is behind the
stat callback). Confidence: likely.

## 11. Quest handlers (read)

D2R's quest objects keep the 1.13c private data **inline from `quest+0x20`** (1.13c: a separate block at `quest+0x18`): the Median flag offsets do not
carry over (fields moved: a2q5 uses +0x20/+0x28 (a pointer)/+0x30/+0x31), so keep Median's "once per player" flags in the plugin, keyed by game and player (or
use quest-record bits as in section 2).

| Quest | Event | D2R | Entry bytes | Read / matches | Median's use (1.13c) |
|---|---|---|---|---|---|
| 13 `a2q5` (Summoner) | 8 kill | **`0x5a5490`** | `48 89 5c 24 08 57 48 83 ec 20 80 79 11 00 48 8b` | string "killed summoner for quest"; sets state 2, `quest+0x20 = 1`; same as `0xc66b0` (line-for-line) | Nightmare: drop TC "Quest Cata Learning" (once per player), flag |
| 13 | 0 NPC activate | **`0x5a5360`** | `48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48` | tests quest-13 bits 1 / 0xd, `0x1405184b0` (npc is Drognan?), picks a dialog state | re-issue `0o[ ` |
| 14 `a2q6` (Duriel) | 11 NPC message | **`0x5a66b0`** | `48 89 5c 24 18 48 89 74 24 20 57 48 83 ec 50 48` | NPC `0xc9` = 201 (Jerhyn), message `0x1ba`: `0x325c50(rec, 0xe, 3)`, `0x325c00(rec, 0xe, 4)`, `0x325990(rec, 0xe, 3)`; the same constants as `0xc8050` | Jerhyn gives one Arcane Crystal `qum ` on `0x1ba` |
| 25 `a4q1` (Izual) | 8 kill | **`0x5e67c0`** | `48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48` | string "Finished quest", state 4, sets quest-25 bits 0xd and 1 when 0 and 1 are clear: `0xbabf0` line-for-line | Nightmare: TC "Quest Cata Jewelcraft" once per player |
| 25 | 0 NPC activate | **`0x5e6660`** | `48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48` | NPC class `0x196` (406, Tyrael), bits 0 / 5 / 1 / 0xd of the quest in `quest+0x2f0` | re-issue `jcat` |
| 38 `a5q4` | 2 | **`0x54dd30`** | `48 89 5c 24 08 57 48 83 ec 30 48 8b fa 48 8b d9` | `if NPC class == 0x200 (Anya, 512)`: `if (quest+0xa6) { quest+0x1c = 0; re-arm via 0x517b40; quest+0xa6 = 0 }`, `if (+0xa7 && !+0xa8) 0x550570`. Median's `0x9f960` uses the same fields (data+0x86/+0x88) = quest+0xa6/+0xa8 here (**the +0x20 inline shift holds for this quest**) | re-arm Anya's quest when byte +0x86 is set, keep +0x88 as done |
| 38 | init | `0x548590` | `4c 8b dc 48 81 ec c8 00 00 00 48 8b c1 48 8d 15` | stores event 0 `0x54ddc0`, 2 `0x54dd30`, 3 `0x54dc90`, 8 `0x54df80`, 10 `0x54e070`, 11 `0x54e570`, 13 `0x54e0d0` (the earlier note "init stores by another pattern" was a plain `*(code**)(q+0x270+8n)` store, ordered differently) | |
| 40 `a5q6` (Baal) | 8 kill | **`0x54fa70`** | `48 89 5c 24 20 55 56 57 41 56 41 57 48 81 ec 80` | `0x517db0(game, 0x13)`; reads the killed unit's room; quest-0x28 (40) bit tests; spawns the Baal clone path; same shape as `0x5fe30` | after the vanilla handler: Hell, players in level 132 above level 109 get a relic orb once (bit 0xb of quest 40) |

The quest-bit helpers in these handlers are the section 2 ones (`0x325c50` test, `0x325c00` set, `0x325990` clear). Quest 40 in D2R keeps vanilla
bit 0xb unused as far as read; the plugin can set it itself.

## Appendix A. Quest descriptor table (D2R, verified)

`0x239c5c0`, 36 entries of 0x20 bytes: `+0` init function VA, `+8` u32 act (0-4), `+0xc` u32 (`0x64` or 0), `+0x10`
u32 flag, `+0x14` u32 ordinal in the act, **`+0x18` u32 quest id**. Ids: act 1 `1-6` and `41` (init `0x5db880`,
shared with id 29), act 2 `8-14, 30, 31`, act 3 `16-22, 32`, act 4 `24-27, 33`, act 5 `35-40`. Quest id N to source
file: act 1 `a1qN`, act 2 `a2q(N-8)`, act 3 `a3q(N-16)`, act 4 `a4q(N-24)`, act 5 `a5q(N-34)` (checked from the strings
each init's handlers reference). So Median's hooked quests are: 4 `a1q4`, 5 `a1q5`, 6 `a1q6`, 13 `a2q5`, 14 `a2q6`,
19 `a3q3`, 21 `a3q5`, 25 `a4q1`, 36 `a5q2`, 38 `a5q4`, 40 `a5q6`.

`py -3 tools/d2r_find.py qinit <id|all>` lists the handlers an init stores; results for the Median-hooked quests
(event 0 = NPC activate, 8 = kill, 11 = NPC message; entry bytes in the tool output):

| Quest | Init | event 0 | event 8 | event 11 | Confirmed by |
|---|---|---|---|---|---|
| 4 | `0x5db310` | `0x5dfe90` | `0x5e29d0` | `0x5e0830` | NPC 0x94, msgs 0x61/0x70/0x76/0x7d, same as 1.13c `0x66ce0` |
| 6 | `0x5db6c0` | `0x5e1d30` | `0x5e1f00` | `0x5e2660` | quest-6 bit tests, drop sequence = 1.13c `0x67e00` |
| 13 | `0x59f8f0` | `0x5a5360` | `0x5a5490` | `0x5a5920` | init only |
| 14 | `0x59f9f0` | `0x5a5d40` | (none) | `0x5a66b0` | init only |
| 19 | `0x5ea880` | `0x5ee920` | | `0x5ef040` | constants 0x251, 0xff (Ormus), 0x60 |
| 21 | `0x5eaad0` | `0x5f05d0` | `0x5f06f0` | `0x5f0df0` | constant 0x272 (Cain, act 3) |
| 25 | `0x5e4c20` | `0x5e6660` | `0x5e67c0` | `0x5e6d70` | init only |
| 36 | `0x548220` | `0x54c090` | `0x54c1e0` | `0x54c810` | constant 0x203 (Qual-Kehk) |
| 38 | `0x548590` | | | | init stores by another pattern, not listed |
| 40 | `0x5487f0` | `0x54f800` | `0x54fa70` | `0x550450` | init only |

## Appendix B. Median's D2Game patch table (1.13c call sites that need a D2R analog)

D2Sigma's loader patches D2Game from a table of 520 records at D2Sigma VA `0x101712f0` (five dwords: D2Game RVA, 0,
value, flag, 8). 285 records have flag 0 (a 32-bit constant or table pointer written into code: bounds, table
addresses, `0x90909090` nops, `0xe8`/`0xe9` opcode bytes); 235 have flag 1 (the rel32 of a call/jmp redirected to a
D2Sigma function). These are exactly the places where Median changes 1.13c behaviour. The grouped table is below, by
target; use `tools/ghidra_scripts/SiteFuncs.java` on the D2Game copy to name the 1.13c function around a site. Cases
already mapped: `0x7242a` / `0x72418` / `0x72421` = the cube output dispatch (section 1); `0x348a4..0x35349` and the
`0x101dd0e8/0f8/0fc` constants = the quest descriptor table (Appendix A: D2R has one table, patch nothing, hook the
handlers); `0x2e15a` = the end of the game frame function (the goblin / D2GS tick).

| Sigma target (VA) | D2Game patch sites (1.13c RVAs of the rel32 operand of a call/jmp) |
|---|---|
| `0x1004b560` | 2: 0x79f91 0xdef8b |
| `0x100573a0` | 1: 0xdb7b4 |
| `0x10057930` | 17: 0xdb98 0x760b8 0x7625e 0x76965 0x7cb77 0x7cbe6 0x7d3de 0x7f5d9 0x97891 0xa2f77 ... |
| `0x10057f50` | 4: 0xa10f1 0xdb7a0 0xdb7c2 0xdb846 |
| `0x10057f70` | 1: 0xdbd79 |
| `0x10057f80` | 3: 0x9a300 0xdc7cd 0xddb74 |
| `0x10057f90` | 1: 0xdc180 |
| `0x1005a840` | 1: 0x734f2 |
| `0x1005c780` | 21: 0x3afd9 0x42c20 0x430be 0x43370 0x4f23d 0x4f434 0x4f4b8 0x75664 0x92927 0x95543 ... |
| `0x1005ca30` | 1: 0x4b2ee |
| `0x1005ca50` | 1: 0x4b292 |
| `0x1005e900` | 1: 0xdc076 |
| `0x1005e910` | 1: 0xde8f1 |
| `0x10064dd0` | 1: 0xed30e |
| `0x100664a0` | 1: 0x2e15a |
| `0x10066900` | 1: 0xcedb8 |
| `0x10066c10` | 4: 0x49191 0x55d51 0xdddb0 0xedaef |
| `0x10067450` | 3: 0x97593 0x981f5 0xc0f0e |
| `0x10067910` | 5: 0x4919a 0x55de0 0xc0d9a 0xdddb9 0xedcf0 |
| `0x10067920` | 2: 0xd0d1a 0xd0d1a |
| `0x1006aff0` | 3: 0x79de4 0xd0902 0xd093d |
| `0x1007e3b0` | 1: 0x10c88 |
| `0x1007e7d0` | 2: 0x11e8f 0x4ee05 |
| `0x1008a910` | 1: 0x6f63e |
| `0x1008f880` | 1: 0xaf841 |
| `0x1008f8a0` | 1: 0xaccf5 |
| `0x100931e0` | 2: 0xce48e 0xce4a0 |
| `0x10093200` | 2: 0xcd4ce 0xcd4e0 |
| `0x100936f0` | 3: 0xbd20d 0xbd3ce 0xbf7f2 |
| `0x10093710` | 2: 0x2c4a4 0x2c7ac |
| `0x10093a30` | 3: 0xbf71f 0xbf7bf 0xbf8a0 |
| `0x10093e40` | 23: 0x8f638 0x8f715 0x8f7d4 0x8f82d 0x8f83d 0x8fab3 0x8fb0d 0x9029a 0x906c6 0xb0174 ... |
| `0x10093f90` | 4: 0x488af 0xc02b0 0xc02ca 0xc17f4 |
| `0x10097de0` | 3: 0x3876b 0x6cc74 0xaf531 |
| `0x10097fa0` | 1: 0x6bee9 |
| `0x10098890` | 2: 0x4b5b7 0xe1ac4 |
| `0x100988b0` | 1: 0x6a133 |
| `0x100988c0` | 1: 0xddd74 |
| `0x1009b660` | 1: 0xf0346 |
| `0x1009b720` | 1: 0xc73d7 |
| `0x1009b730` | 1: 0x36f1a |
| `0x1009b750` | 2: 0x2c428 0x2c76c |
| `0x1009b770` | 1: 0x2c567 |
| `0x1009b780` | 1: 0x2c813 |
| `0x1009b7a0` | 1: 0xf0dd5 |
| `0x1009b7b0` | 2: 0x2c48d 0x2c795 |
| `0x1009b7d0` | 1: 0x5fa19 |
| `0x1009b7e0` | 1: 0xdc4ea |
| `0x1009b850` | 1: 0x7242a |
| `0x1009b900` | 4: 0x6bd39 0x6e085 0xc8970 0xd0315 |
| `0x1009b920` | 1: 0xd7b75 |
| `0x1009b990` | 1: 0x2e8d2 |
| `0x1009b9b0` | 1: 0xe12f2 |
| `0x1009b9d0` | 1: 0x2e831 |
| `0x1009ba30` | 1: 0x2cd8b |
| `0x1009bad0` | 1: 0x2e43f |
| `0x1009bb20` | 2: 0x43413 0x6a78e |
| `0x1009bb40` | 2: 0x5618c 0x56389 |
| `0x1009bbc0` | 1: 0x10cca |
| `0x1009bc00` | 2: 0x106b0 0x10ca9 |
| `0x1009bc20` | 9: 0x53471 0x53978 0x539cc 0x53a03 0x53a43 0x53a89 0x53ac3 0x53b06 0xec0b8 |
| `0x1009bc40` | 3: 0x2d568 0x2d7b7 0x2d141 |
| `0x1009bc60` | 6: 0x55195 0x55938 0x79352 0xe1ee9 0xed0e0 0xed638 |
| `0x1009bc80` | 1: 0xe2121 |
| `0x1009bc90` | 3: 0xa09e3 0xa1cd9 0xa1cf1 |
| `0x1009bd10` | 2: 0x8580c 0x8581a |
| `0x1009bd70` | 2: 0xf4d6 0xf66d |
| `0x1009bd90` | 1: 0xf6b5 |
| `0x1009bdc0` | 1: 0xcadc5 |
| `0x1009bde0` | 1: 0x53f94 |
| `0x1009be30` | 2: 0xddeb8 0xddeee |
| `0x1009d420` | 3: 0x35444 0x359a5 0x48916 |
| `0x1009e6a0` | 1: 0xc0423 |
| `0x1009e9a0` | 1: 0x68cdd |
| `0x1009ff10` | 1: 0xc0597 |
| `0x1009ff20` | 3: 0x5c0f1 0x5c1bd 0x5efee |
| `0x1009ff40` | 1: 0x4b4bc |
| `0x100a2070` | 1: 0x9ed3e |
| `0x100a2080` | 3: 0x95d37 0x9fe88 0xa1c8b |
| `0x100a20a0` | 1: 0x79b7c |
| `0x100a20b0` | 1: 0x79b77 |
| `0x100a2370` | 1: 0xc057d |
| `0x100a2a50` | 3: 0x2c210 0x2c21e 0x2c289 |
| `0x100a4af0` | 1: 0x543d1 |
| `0x100aebc0` | 1: 0x459ab |
| `0x100afa10` | 1: 0x29456 |
| `0x100afa20` | 1: 0x3865a |
| `0x100b0f40` | 2: 0xa09d5 0xa1cb7 |
| `0x100b1390` | 2: 0x9fd66 0xa1aed |
| `0x100b3dc0` | 1: 0x7add7 |
| `0x100b3f30` | 3: 0x78a23 0x7addd 0xf3c0b |
| `0x100b6410` | 2: 0x7403f 0x7404d |
| `0x100bca20` | 3: 0x6b273 0x789bb 0xf3b37 |
| `0x100bcd10` | 9: 0x4b17e 0x4b1b6 0x5568b 0x55955 0x9dd29 0xe176f 0xe1858 0xed3fe 0xed655 |


## Unknown / next steps

Solved in lane d2r-addresses-2 (sections 4, 5, 7-11): warp destination and the lockout refusal point, game frame, spawn, TC executor,
game destroy / create, client menu composition, quest handlers 13 / 14 / 25 / 38 / 40, gold sync (by evidence).

1. **Unique item by index** (Griswold's Heart): no D2R function found that takes a unique row; see section 7 for the two workarounds (base item
   via `0x43d530`, or a one-entry treasure class through `0x4404f0`). Needs a look at the unit creation `0x490410` -> `0x1404905a0` or an in-game test.
2. ~~Portal objects~~ lane dungeon-lockout: the portal object operate function is **`0x58f680`** (ctx `{game, portal, player, ?, object class}`; it pairs
   the portal with its twin through `0x490070`, places the player with `0x491ca0`, returns 1 moved / 0 failed with unit message `0x14`). Entry bytes
   `48 89 4c 24 08 55 53 56 57 41 56 41 57 48 8b ec`. Destination level = word from `0x34ad40(portal)`. Hooked by dungeon_lockout.cpp.
3. A ready-made server -> client **system message** sender was not located; use the raw packet send of section 4 or a D2RLoader client-side message.
4. Stat-update packet sender (0x1d-0x1f family) not located; gold sync argued from the game's own gold code (section 10), **untested**.
5. The game create function `0x4049d0` has no direct callers (called through a job pointer); the destroy hook covers the reset need, so this is
   cosmetic. The monster death drop caller `0x5930b0` (the other caller of `0x4404f0`) was not read.
6. Client: the string ids for new menu lines (tbl lookup `0x143e2b730`) and the layout / frame of a longer menu are untested; records hold only 5 lines.
7. Everything above is static analysis: none of the hooks has been run in the game yet.

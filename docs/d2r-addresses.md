# D2R 3.3 build 93847: addresses for the blocked hooks (lane d2r-addresses, 2026-10-21)

All addresses are **RVAs** in the D2R image (`D2RLoader_6ab3782c_dump.exe` memory layout; file offset == RVA, base
`0x140000000`), x64 Microsoft ABI (rcx, rdx, r8, r9, then stack at `[rsp+0x20]...` at the callee's entry).
"Entry bytes" are the first 16 bytes of the function, for the plugin's expected-bytes check.

**Confidence:** *verified* = the code was read and matches the 1.13c function (same structure, constants, assert
strings, callers); *likely* = structure matches but one link rests on inference; *guess* = anchor only.

Everything here was found with `tools/d2r_find.py` (string search, RIP-relative xrefs, `.pdata` bounds, callers,
disassembly; see its docstring) plus Median's 1.13c D2Game decompile (`ghidra-lanes/addr/d2game` copy). The D2R Ghidra
project was still being analysed, so no D2R decompile was used; read the disassembly notes with that in mind.

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
| 3 | NPC interact packet (server) | `0x4c26a7` area in the function starting before it (see section 4) | likely |
| 3 | NPC dialog-message packet (server) | `0x4c33e0` | likely |
| 4 | Warp interact (server, PlrMsg) | `0x4adbb0` | likely |
| 5, 6 | see sections 5 and 6 | | |

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
returns the created item (or null). The recipe row is `(record - 0x3e3f60(game[0x106], 0)) / 0x16c`, exactly the
formula `cube_conditions.cpp` already uses. `ctx` is the `out` buffer of the search (`[rsp+0x60]` in the caller).
**Recommendation (port spec option 2, the post-hook):** detour `0x5269c0`, call the original, and when it returns a
non-null item (or when the inputs are gone) run the recipe's effect list from the generated table:
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
without an extra packet was not tested (vendor purchases in D2R go through the same setter; verify in game).
Flags: nothing to find in D2R, they live in plugin memory keyed per game pointer and player name (see the port spec).
Game create/destroy hooks for clearing the flags were not located; clearing when the game pointer changes (seen in the
search/executor hook) is enough.

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

Quest id to file name: act 1 `a1qN` is id N (checked for 4 and 6 against the handler contents); the other acts
(`a2q*`..`a5q*`) were not mapped to Median's quest ids 13, 14, 19, 21, 25, 36, 38, 40 (use `qinit` and the NPC class
constants listed in `quests-requirements.md`, section 2.2, to match them; D2R's file numbering is not a plain
`8*(act-1)+n`: act 5 starts one later).

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
D2R's treasure-class executor, which was **not located** (1.13c `0x12380`; the item-create step of a monster drop is
`0x43fbf0`, below). Store the per-player flag as a plugin-owned bit in the player's quest record (e.g. quest 6,
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
| find item by code in inventory | `0x441980` | `(game, player, code)` -> item or null | likely | used before the scroll hand-over in `0x5e0830` |
| monster drop item creation | `0x43fbf0` | `(game, monster, type /*2 in Andariel*/, out*, ..., -1, 0)`; builds a 0x84..0xc8-byte item-gen struct and calls `0x43cd50` | likely | matches 1.13c `0x11a70`'s role (the drop step of the kill handler) |
| item-gen core | `0x43cd50` | | guess | callee of `0x43fbf0` |

## 3. NPC interaction menu

Server side (verified flow, entry addresses likely):
- NPC click (packet): the PlrMsg handler containing `0x4c26a7` (it calls `0x502f60`, builds the interact reply, then
  `0x515980` = quest event 0; assert lines in `D2Game\src\Player\PlrMsg.cpp`). The function begins before `0x4c26a7`
  (not located: `.pdata` gives a chained piece at `0x4c26a7`).
- Dialog-message packet: **`0x4c33e0`** (calls `0x515980` at `0x4c37b2` and the event-11 wrapper `0x516940` at
  `0x4c35cf/0x4c3bbf/0x4c3bd4`); entry bytes `40 55 53 57 41 54 41 55 48 8d 6c 24 e0 48 81 ec`. A plugin that wants a
  custom menu entry's action to run server-side hooks `0x516940` (message id = `event+0x30`) or this packet handler.
- The menu **layout** (which buttons the client shows) is client UI code and was not traced here; D2R builds the NPC
  menu in its UI layer from the NPC's class and quest state. Still unknown, see "Unknown".

## 4. Level entry / warp (dungeon lockout)

- Server warp interaction handler: **`0x4adbb0`** (`D2Game\src\Player\PlrMsg.cpp`; logs "Warp object not found",
  "Unable to do warp interact", "Can't warp now due to hostility"), entry bytes
  `40 55 53 57 41 54 41 56 41 57 48 8d 6c 24 d1 48`. likely. Not read to the point of the actual warp (level change)
  call or the place where D2R refuses entry, so the lockout test (Sigma `0x5d810`) has no precise call site yet.
- D2R keeps warp data in D2Common's DRLG (`D2Common\src\Drlg\DrlgWarp.cpp` at `0x1d0dac0`); the client also builds the
  entry-requirements text (Sigma `0x5d150` is client-side text, not the server check).

## 5. Monster spawning (treasure goblins)

Not traced to hook points. Anchor candidates: the unit-death function `0x44db20` (SUnitDmg.cpp) already shows how
D2R drives per-kill logic; spawn would use the monster-create path used by the summon code (`0x4350e0`, plugin-known).
See "Unknown".

## 6. Item / skill requirement checks

Not traced. See "Unknown".

## Unknown / next steps

1. Quests 13, 14, 19, 21, 25, 36, 38, 40: map Median's ids to D2R files with `d2r_find.py qinit` and read their
   handlers (the Act 1 milestone does not need them).
2. D2R's treasure-class executor (1.13c `0x12380`) and the unique-by-index item creator (1.13c `0x11490`), for "drop TC X"
   instead of creating the item directly.
3. NPC menu (client UI): which D2R function decides the button list; the server action path is `0x516940`/`0x4c33e0`.
4. Warp / level entry refusal point for the dungeon lockout (inside `0x4adbb0`'s callees or the level change itself).
5. Goblin spawn call (monster create at a position) and the per-player tick.
6. Item and skill requirement evaluation call sites (strength/level/class checks for equip and skill use).
7. Game create / destroy hook for the cube flag storage (workaround: reset on game pointer change).

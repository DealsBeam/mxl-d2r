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
| 2 | Quest descriptor table (36 entries: init fn, act, quest id) | `0x239c5c0` (stride 0x20) | verified |
| 3 | NPC table (43 x 0x1c bytes: hire/trade/gamble flags, hire quest and level) | `0x237ea60`, getter `0x3971a0` | verified |
| 3 | Server NPC trade / gamble / hire packet handler | `0x4b0470` | verified |
| 3 | NPC click packet (server; calls quest event 0) | block `0x4c26a7` of a PlrMsg handler | likely |
| 3 | NPC dialog-message packet (server; calls quest event 11) | `0x4c33e0` | likely |
| 3 | Client NPC menu module (`D2Client\src\UI\npcmenu.cpp`) | functions `0x10a520`-`0x117634` | likely (module only) |
| 4 | Warp interact packet (server) and the warp action it calls | `0x4adbb0` -> `0x491ff0` | likely |
| 5 | Monster spawn / goblin tick | not found | unknown |
| 6 | Requirement gates for NPC services | NPC table bytes +7/+8/+9/+0x10/+0x11 (section 6) | verified |

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
How the visible list of lines is composed (the loop over trade / gamble / hire / quest lines, and where a 16-slot
action list could be added) is not traced; start at the other npcmenu functions listed by
`d2r_find.py xref 0x1cc78a0`. Layout (sprites, JSON panels) is separate and not in this lane.

## 4. Level entry / warp (dungeon lockout)

- **`0x4adbb0`** `(game, player, packet*, size)`: the server handler of the "use warp" packet
  (`D2Game\src\Player\PlrMsg.cpp`, strings "Warp object not found", "Unable to do warp interact", "Can't warp now due to
  hostility"). Entry bytes `40 55 53 57 41 54 41 56 41 57 48 8d 6c 24 d1 48`. After the checks (distance
  `0x325140(player, target) <= 0x32`, hostility `0x42bfb0`) it calls **`0x491ff0(game, player, warp)`**
  (`D2Game\src\Unit\SUnit.cpp`, 0x90 bytes plus chained parts; entry bytes
  `48 8b c4 53 55 57 41 56 48 83 ec 68 49 8b d8 4c`) which is the warp/level-change action; four more callers exist
  (`0x58e2ce`, `0x590251`, `0x5907f4`, `0x5a11a0`). Confidence: likely (the call chain is read; the call that actually
  changes the player's level inside `0x491ff0` and the level id of the destination were not isolated).
- **Plugin plan:** to implement Median's lockout, hook `0x491ff0` (or `0x4adbb0` before it): read the target warp's
  destination level, check the per-game lockout map and the levels extension (difficulty, level, class), and either
  refuse (return without calling the original) or call it and then start the clock. The destination-level read still
  needs a follow-up (the warp unit's object data holds it; the portal object code in the plugin, `0x432ce0` family, uses
  `0x34e9d0` to write the level byte, so the reader is nearby).
- The client's entry-requirements text (Sigma `0x5d150`) is client UI and does not exist in D2R.
- D2Common keeps the vanilla warp data in `D2Common\src\Drlg\DrlgWarp.cpp` (string `0x1d0dac0`); the vanilla level-record
  extension fields (levels.bin +0x14b..+0x15f) are Median's, so D2R does not read them; the plugin owns that table.

## 5. Monster spawning (treasure goblins)

Not found. What was established:

- Median's tick is a per-game-frame call (Sigma patches 1.13c `D2Game+0x2e15a`, the end of the game frame function
  `0x2e050`, which increments `game+0xa8` and runs the subsystem updates). D2R's game frame function was not located
  (candidates are in `0x403000`-`0x40a000`, where `D2Game\src\Game\Game.cpp` is referenced; none confirmed).
- **Alternative that needs no tick:** roll the goblin chance on each monster death. The kill wrapper `0x515640`
  `(game, victim, killer)` (section 2) sees every kill with the killer player; "last kill within 15 s" is then implicit.
  This changes the spawn rate model (per kill instead of one roll per 15 s per active player); tune the chance down.
- Spawn call: D2R's monster-spawn code lives in `D2Game\src\Monster\MonsterSpawn.cpp` (functions `0x4929e0`, `0x492e70`,
  `0x4934d0`, `0x493870`, `0x4945c0`, `0x495110`); the summon routine `0x4350e0` (plugin-known) creates a monster
  for an owner. Which of them takes `(game, room, x, y, class)` was not decided.

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

1. D2R's treasure-class executor (1.13c `0x12380`) and the unique-by-index item creator (1.13c `0x11490`): needed to
   drop "TC X" or create Griswold's Heart as a unique. Workaround for the Act 1 milestone: create items by code
   (`0x517530`); the unique needs the item-generation core (`0x43d530` / `0x43cd50`, signatures not read).
2. Quest handlers for 13, 14, 25, 40 are known only from the init stores (not read); 38 is not listed.
3. The client NPC menu line composition (where Median's 16 action slots would be added) and the UI layout.
4. The destination-level read inside `0x491ff0` and the exact refusal point for the dungeon lockout.
5. The D2R game frame function (goblin tick) and the monster-spawn call `(game, room, x, y, class)`.
6. A game create / destroy hook for the cube flag storage (workaround: reset when the game pointer changes).
7. Whether the post-hook's gold payment (`0x2f7d10` set stat 14 / 15) needs an explicit client update packet.

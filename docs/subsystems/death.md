# Death, corpse, penalties and respawn (lanes no-corpse 2026-10-22, no-corpse-2 2026-10-23)

Median XL players leave no corpse. This file records what Median's engine does when a player dies, with addresses, what D2R
does instead, and what the plugin part `nocorpse` (plus `respawnwait`) ports. Audit rows: `docs/median-rules-audit.md`
DC1 (no corpse, gear and belt kept), DC2 (no experience loss), DC3 (no gold loss), DC4 (5 second respawn wait), DC9 / HC1
(Hardcore). D2R addresses are in `docs/d2r-addresses.md` section 16.

## 1. Median's death rules (answers)

| Question | Median | Evidence |
|---|---|---|
| Is a corpse created? | **No.** The corpse unit is never built, so there is nothing to walk to and no "<name>'s Corpse" | D2Sigma patch record RVA `0x79790` (below): the call to the corpse creator `0x6fc99210` is NOPped (16 bytes) |
| Do items stay equipped? | **Yes.** The corpse creator is also what moves every equipped item (and the belt contents) onto the corpse; it never runs. The patch note "gear is re-equipped in town, potions stay in the belt" (audit DC1) is this effect: nothing was ever taken off | same NOP; the creator `0x6fc99210` was read: it collects the body-location items and moves them onto the corpse unit |
| Is gold lost or dropped? | **No gold is lost and none is dropped.** The gold pile drop is part of the corpse creator, the gold penalty is part of the penalty call; both are NOPped. Carried gold and the bank stay as they were | NOPs at `0x6fc99790` and `0x6fc99ed6`; penalty body `0x6fc58180` (gold) never runs |
| Is experience lost? | **No.** The experience penalty `0x6fc57730` is in the same NOPped call | NOP at `0x6fc99ed6` (5 bytes, call `0x6fc583a0` = gold penalty + experience penalty) |
| Hardcore? | **Unchanged from D2.** Median patches nothing in the respawn path: a Hardcore character that asks to respawn is kicked from the game (client status byte `+0xa` bit 4 -> `0x6fc4d090(game, 3)`) and the character is dead for good. There is no corpse in Hardcore either (the NOP is not difficulty dependent). The patch notes' Hardcore-to-Softcore conversion on single player (audit DC9) is not in the death code; the plugin does it in D2R's two death handlers (`hardcore_death.cpp`, off word `hcdeath`, check `hcdeath`) | respawn handler `0x6fcf14b0` is outside every patch record; the packet table `0x6fd1a078` and the mode tables `0x6fd1a3fc` are not patched either (no record above RVA `0xf4500` reaches a data table) |
| Mercenary: corpse and items? | **Unchanged from D2.** A killed mercenary is a monster that goes through the ordinary monster death (`0x6fc99e40` is the player's death mode only); it keeps its equipment, stays dead until the hireling NPC revives it for gold (Median's `revive_merc` command string is at D2Sigma `0x16da78`), and is not touched by the player's death patches. Median did not patch the monster death in the player-death function | no record in the monster death functions in the death range; mercenary revive is data (npc-hirelings.md) |
| Respawn penalty or a different respawn place? | **Place: unchanged.** The respawn handler (client packet `0x41`, `0x6fcf14b0`) puts the character into the town of its act with life, mana and stamina set to their maxima (stats 6, 8, 10), clears the dead state `0x36`. **Penalty: none in code**; the 5 second wait of audit DC4 is a patch-note rule, see section 6 | handler disassembly; no patch record in it |

### Median's patch records (D2Sigma table `0x101712f0`, 520 records, D2Game base `0x6fc20000`)

| Record RVA | Address | Value | Meaning |
|---|---|---|---|
| `0x79790` | `0x6fc99790` | NOP, length 16 (`0x90909090`) | in `0x6fc99730` (player Dead mode 0x11 enter): the call to `0x6fc99210` (corpse + equipment move + gold pile) and its argument pushes |
| `0x79ed6` | `0x6fc99ed6` | NOP, length 5 | in `0x6fc99e40` (player Death mode 0 enter): `call 0x6fc583a0` = gold penalty `0x6fc58180` + experience penalty `0x6fc57730` |

Other redirected calls in the same two functions go to the arena / respec / resource helpers of D2Sigma (`D2Arena.cpp`,
`D2Respec.cpp`, `D2Resource.cpp`: the thin wrappers `0x1004b560`, `0x100a20a0`, `0x100a20b0`, `0x1006aff0`, `0x1009bc60`); none
of them touches corpses, gold, experience or the respawn. D2Sigma's `D2Resurrect.cpp` (`0x100a23b0`, `0x100a2570`) is the
client side of a resurrect (stats 6 / 8 / 10 set, state `0x36` toggled); it is the shape of the D2 respawn, not a delay.

## 2. D2R's death path (what the port replaces)

| What | D2R | Notes |
|---|---|---|
| Corpse creator | `0x42c110` (Game*, Unit* player, x, y, room) | builds the corpse unit, moves equipment onto it, drops gold. Two callers, both death-mode transitions: `0x42a5e0` (Dead mode enter, call at `0x42a689`) and the mode-change dispatcher `0x42de40` (mode 0 branch, call at `0x42e24e`). Return value unused; the next calls set mode 0x11 |
| Death penalty | `0x424ac0` (Game*, Unit* victim, Unit* killer) | `0x425cb0` = gold penalty; then, if level > 1, lowers stat 13 (experience) by a share of the level band and records the loss via `0x486260`. One caller: `0x42a441` in the Death mode enter `0x429d10`. Return value unused |
| Respawn request | `0x4b6160` (Game*, Unit* player, packet, int) | client packet `0x41`: unit mode `0x11` required; the Hardcore branch (status `+0x5658` bit `0x800`) logs and ends the game; otherwise sets stats 6, 8, 10 to their maxima, clears state `0x36`, moves the player to the town (`0x42d2c1`). Returns 0 |

## 3. What the plugin does

`plugin/src/no_corpse.cpp`, pure rules in `no_corpse_logic.h`, unit tests `plugin/tests/no_corpse_test.cpp`.

- Part `nocorpse` (off word `nocorpse`): inline hooks on `0x42c110` (entry bytes `40 55 53 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 18 f9 ff ff 48 81 ec e8 07 00 00`, 28 bytes) and `0x424ac0` (`40 53 56 57 48 83 ec 40 49 8b d8 48 8b f2 48 8b f9`, 17 bytes). Both detours return at once (the original is never called), as Median's NOPs remove the calls. Nothing else of the death path is changed: the player still passes mode 0 and 0x11, the death screen and the respawn button work, the death announcement is sent.
- Part `respawnwait` (off word `respawnwait`, needs `nocorpse`): the death hooks stamp the server frame (`game + 0x170`, 25 per second) per player; a hook on the respawn request `0x4b6160` (entry `40 55 41 55 41 56 48 8d 6c 24 b9 48 81 ec b0 00 00 00`) ignores a request that comes less than 125 frames (5 s) after the stamp and passes later ones on. Characters without a stamp are never held.
- Part `corpsepickup` (off word `corpsepickup`, needs `nocorpse`): a corpse that is already stored in a save is picked up for the character when it enters a level (section 8).
- `mxl` prints a `no corpse` line (calls swallowed), a `respawn wait` line (requests held / let through) and a `corpse pickup` line (stored corpses picked up / kept / faults, level entries). `h state` prints `corpses N players=N self=N | no corpse on: skipped corpse calls=.. penalty calls=.. respawns held=.. let=.. xp=..` (player-type units other than the character in mode 0x11) and `bodies other=N stored=N id0=U hidden=N | corpse pickup on: enters=.. picked=.. kept=.. faults=..` (`other` = every player-type unit other than the character that stands somewhere, in any mode; `stored` = entries in the character's corpse list; `hidden` = D2R's ten inert player-type units, ids 2-11 without a position, which are no body). The client verb `h ccorpses` counts the same on the CLIENT's own unit table (`0x2a23910`, type 0).
- The client needs nothing for a death: the corpse unit and its sprite come to the client from the server's unit creation; with no unit created, no client object exists (`h state` and `h ccorpses` show none on both sides).

## 4. Save format: characters with and without a corpse

`docs/save-format.md` ("Items") lists the player items; the corpse comes right after: `JM` + u16 corpse count (0 or 1); for a
corpse a 12 byte header (u32 0, u32 x, u32 y in the area's coordinates) and a second `JM` item list with the items the
corpse holds; then `jf` and the mercenary's `JM` list, `kf` (golem). Real examples from this machine's test saves
(`MxlBarb.d2s` after a death): `4a 4d 01 00 | 00 00 00 00 14 16 00 00 92 15 00 00 | 4a 4d 01 00 ...` = one corpse at (5652, 5522)
holding one item.

- **A character saved without a corpse** (corpse count 0, which is every character that dies with the port after this part) loads cleanly: nothing in the load path is changed, and the part never creates a corpse.
- **A character saved with a pre-existing corpse** (made before the part, by D2R's own death, or by a harness death with the part off): D2R's level-entry function `0x485350` re-creates the corpse unit from the save's corpse list every time the character enters a level, and again at every respawn - which is why a body was visible after the 2026-10-22 lane even though the creator hook worked, and why it "followed the character to town". The first check counted it as pre-existing ("1 before, 1 after") and passed. Since lane no-corpse-2 the corpse is picked up right there (section 8): the saved corpse count is 0 after the first save.

## 5. Check and results

`py -3 tools/game_checks.py all nocorpse --slot N` (check `nocorpse` in tools/game_checks.py, lane no-corpse-2 version): equips a ring, an amulet and a sash (the harness cannot fill a belt; the belt's potions go to the corpse in D2 and are not touched without one), sets gold, bank and a big experience value, then kills the character three times in a row (two in Blood Moor with a 2e9 `hit`, then the attempt in the town). **"No corpse" is absolute**: at the start, while dead, after the early Esc and after every respawn there must be no player-type unit other than the character on the server (any mode), none on the client (`h ccorpses`), none in mode 0x11, no entry in the character's corpse list, the same items on the same body locations, the same gold, bank and experience, no gold pile; the plugin counted the swallowed calls at every death; Esc at once is held back (`respawns held` rises), Esc after 130 frames respawns the character in a town (`let` rises, frames >= 125). After the first death the field is entered again, the death spot is photographed (`nocorpse_death_spot`) and the units there are counted. At the end Save and Exit + Play: the saved .d2s has corpse count 0 (`JM 00 00 jf`) and the reloaded character has no body and the same gear. The counters of the stored-corpse pickup are printed (a stored corpse the character brought into the game must have been picked up: `kept` 0, `faults` 0).

A death in the town is not possible in the engine: a monster's hit does nothing there (`result=0`), and a 2e9 hit or the unit death function `0x44db20` leaves the character at 100 life in mode 5. The third round is therefore an attempt: nothing may change (no body, same gear, gold, bank, experience).

`py -3 tools/game_checks.py all corpsepickup --slot N` (check `corpsepickup`) makes its own stored corpse with D2R's creator and loads it: (1) mxl-core-off.txt `nocorpse` - the control for `nocorpse`: the character dies in the field, D2R makes a corpse that follows it to town, the gear is gone from the character, Save and Exit writes corpse count 1; (2) off word `corpsepickup` - the control for the pickup: the character loads with the body on the server, on the client and in the list; (3) all parts on - the load picks the body up: no body anywhere, the same gear back on the same body locations, `picked` >= 1, `kept` 0, `faults` 0; Save and Exit + Play: the saved .d2s has corpse count 0.

Results: see sections 7 and 9.

## 6. The 5 second respawn wait: what is known

Audit DC4 (Median patch notes P1.0.0): "respawning after death has a 5 second cooldown (replaces the loss penalties; discourages suicide-rushing)". Searched in Median's engine:

- D2Game: the respawn handler `0x6fcf14b0` is unpatched; no record touches the packet table `0x6fd1a078`; the death-mode functions' redirected calls go to arena, respec and resource helpers.
- D2Sigma: no string or function with a respawn timer (strings `respawn`, `cooldown`, `revive`, `corpse` only give `revive_merc`, `D2Resurrect.cpp`).
- D2Client: its reconstructed source has no timer around the respawn send either (not decoded to the end; the client's death screen is the one place left that could hold a delay).

So the 5 seconds are taken from the patch note, not from code. They are 125 frames of the 25 Hz server clock (the same clock as the goblin tick, `goblins_logic.h`). If a later reading of D2Client shows the wait is a client-side button delay, the plugin rule can be turned off with `respawnwait` in `mxl-core-off.txt` without touching the corpse part.

## 7. Verification

Slot 1, plugin 0.53.0 + this lane (the installed DLL equals the built one), test character MxlBarb (Barbarian, level 99). Run folders under
`%USERPROFILE%\mxl-d2r-runs\`.

**With the port** (`checks-20261022-193422-s1`): `nocorpse` **PASS**. Before the death: ring (body 7), amulet (2), belt `629` (8), gold 23456, bank 7890,
experience 1,000,000,000. Killed by a 2e9 hit of a spawned monster (life 18517 -> 0). In mode 0x11: hooks swallowed corpse 0 -> 1 and penalty
0 -> 1, player units other than the character: 1 before and 1 after (see below), items, gold, bank and experience unchanged, no new gold pile. Esc at once
(frame 556, 45 frames after the death): refused (still dead, `respawns held` 0 -> 1). Esc after frame 641+ (respawned at frame 786, 275 frames after the death):
mode 5 (town neutral), area 1, `respawns let` 0 -> 1, same items / gold / bank / experience.

**Control, hooks off** (`checks-20261022-193609-s1`, `mxl-core-off.txt` = `nocorpse respawnwait`, removed afterwards): `nocorpse` **FAIL** as intended: a second corpse unit appears
(1 -> 2), all three equipped items are gone from the character (`items []`), carried gold 23456 -> 0 (gold pile on the ground 1 -> 2: D2R drops all carried gold),
the respawn works at once (no hold). Experience did **not** change in the control (1,000,000,000 both ways): D2R's `DeathExpPenalty` of the built
mod is blank (audit DC2), so the experience half is "no loss" with or without the hook; the swallowed `0x424ac0` call is still counted (0 -> 1 with the port). The
control left that test character with a new corpse that holds the three test items.

**Pre-existing corpse:** the test character already had a corpse in its save from earlier deaths of other lanes (`MxlBarb.d2s`, section 4). It was loaded
as a player-type unit in mode 0x11 (count 1 before the death, 1 after, 1 at respawn): the hook leaves load alone, as expected.

**Not covered:** the belt's potions (the harness cannot put potions in a belt; D2 moves them to the corpse, the port never takes them off), Hardcore (the test
character is Softcore), a mercenary death, a second death and respawn in the same game (the stamp is cleared by a let-through request and
set again by the next death; unit tested), and a client-side view of the missing corpse beyond `h state` (screenshots `nocorpse_dead`, `nocorpse_respawned` in the run folder).

## 8. Stored corpses: pickup at load (lane no-corpse-2)

**Root cause of the visible corpses.** The creator hook works (a new death makes no corpse), but a corpse that was already in a character's save is brought back by D2R itself: the level-entry function `0x485350` (client, player, game, room, x, y) walks the player's corpse list (`inventory + 0x50`, nodes `{flag, id, ..., next at +0x10}`) and creates the body in the room (`0x4245c0`) on every level entry and every respawn. The creator `0x42c110` has two callers only (both death transitions, both hooked), so no other path makes a body. The test characters of slots 1 and 2 carried such a corpse (`MxlBarb.d2s` corpse count 1) since the first, un-hooked deaths, so the body stood next to them in every test window, followed them to town, and the first `nocorpse` check read it as "one body before, one after" = PASS.

**Median's rule** is that a character never has a body. A character that arrives with one must lose nothing and walk nowhere, so the body is picked up at load, the way D2R picks it up when its owner clicks it:

- Hook `0x485350` (entry 21 bytes, checked with the other probe bytes before installing; part `corpsepickup`, needs `nocorpse` - with the creator on, bodies are D2R's again). The original runs first (it places the body), then for every corpse in the list (flag 1; ids copied out first because the pickup edits the list):
  1. the unit is looked up by id (`0x48fe80`, type 0) and D2R's own pickup `0x42b220` (out, game, player, corpse) runs: it puts the body-location items back where they were and places an item whose body location is taken in the inventory, exactly as the click does. No item is built by hand.
  2. only if the pickup said ok, `remaining == 0` and the corpse holds no item any more (`CorpseMayBeRemoved`, unit tested) the interact handler's removal sequence follows: out of the owner's corpse list (`0x389730`), out of the room (`0x2ef4d0`), the clients are told (`0x476fc0`, packet `0x8e`), the unit's collision is cleared (`0x348b00`) and it is freed (`0x48faa0`).
  3. otherwise the corpse stays exactly as it was (`kept` rises) and nothing is deleted. Exceptions inside a game call are caught per corpse (`faults`).
- `mxl`: `corpse pickup on: stored corpses are picked up when a character enters a level (0x485350): picked N, kept N, faults N (level entries N)`; off word `corpsepickup`.
- The next save writes corpse count 0 (docs/save-format.md); the `.d2rl` sidecar is written by D2RLoader with the save as always.

Not covered: a corpse that holds an item the character cannot take (all places full) stays and is retried at the next level entry; a Hardcore corpse (no such character in the test set).

## 9. Verification of lane no-corpse-2

Plugin 0.55.0 + this lane, background mode, locks `no-corpse-2` (slots 1 and 3). Run folders under `%USERPROFILE%\mxl-d2r-runs\`.

| Run | Result |
|---|---|
| `checks-20261023-175403-s1` | `nocorpse` **PASS**. The slot-1 character had a stored corpse (`save-backup-20261023-175403-s1\MxlBarb.d2s`: 1125 bytes, corpse count 1, one item). After the load: no body on server, client or in the list; the next save is 1048 bytes with `JM 00 00 jf` (count 0): a real old save picked up by the plugin. |
| `checks-20261023-182327-s1` | `nocorpse` **PASS** and `corpsepickup` **PASS** (first complete run of both). |
| `checks-20261023-184142-s3` | `mxl` **PASS** (16 feature lines incl. `corpse pickup on`), `deathcrash` **PASS**, `nocorpse` **PASS**, `corpsepickup` **PASS** (slot 3). |
| `checks-20261023-185858-s1` | `corpsepickup` **PASS** with the final check text; screenshots `corpsepickup_off_loaded.png` (the character stands in the Rogue Encampment with "MxlBarb's Corpse" lying next to it: exactly what the owner saw) and `corpsepickup_on_loaded.png` (same place, no body). |
| `checks-20261023-183709-s1` | **Control**, `mxl-core-off.txt` = `nocorpse respawnwait`: `nocorpse` **FAIL** as intended: a body appears on the server (1), in mode 0x11 (1), in the corpse list (1) and on the client (1); the three items are gone (`[]`), gold 23456 -> 0; the respawn works at once; the second death makes a second body (2); the swallowed-call counters stay 0. |

`nocorpse` in detail (`checks-20261023-184142-s3`): before the first death bodies srv / mode17 / stored / client = 0/0/0/0, gear ring, amulet, belt, armour; death 1 (Blood Moor): hooks swallowed corpse 1 -> 2 and penalty 1 -> 2, mode 17, bodies 0/0/0/0 dead and after the respawn (area 2 -> 1), gold 23456, bank 7890, experience 1,000,000,000 unchanged, Esc at once held (frame 953 -> 999), the respawn after 125 frames let through (frame 1222); the death spot (5463, 5738) revisited: 0/0/0/0, screenshot `nocorpse_death_spot.png` (the character, no body on the ground); death 2 the same (corpse calls 2 -> 3); death 3 in the town: the engine leaves the character at 100 life in mode 5, nothing changed; Save and Exit + Play: saved `.d2s` corpse count 0, reloaded gear identical, bodies 0/0/0.

`corpsepickup` in detail (`checks-20261023-185858-s1`): (1) `nocorpse` off: the character dies, bodies 1/1/1/1, gear `[]`, in town the stored corpse follows (srv 0, stored 1), Save and Exit writes corpse count 1; (2) `corpsepickup` off: bodies 1/1/1/1 on load, the corpse is in the screenshot; (3) all on: bodies 0/0/0/0, gear `[('629', 8), ('amu', 2), ('rin', 7)]` = the gear before the death, `picked=1 kept=0 faults=0`; after Save and Exit + Play corpse count 0 and the same gear.

Iterations that failed on the way (check bugs, not the part): `checks-20261023-172326-s3` (D2R keeps ten inert player-type units, ids 2-11 without a position, on both sides: counted as bodies; now reported as `hidden`), `-173332-s3` (a town death: the engine does not allow it; Esc on a not-dead character paused the game), `-180040-s1` (the control's `state` could not read the corpse list with `nocorpse` off; Save and Exit right after a load needs a retry).

Test saves: slot 1's `MxlBarb` (corpse of an earlier harness death) was picked up in `-175403-s1`; slot 3's had none; slot 2's `MxlBarb.d2s` (corpse count 1) was not touched (the lock of slot 2 belonged to other lanes the whole time): it is picked up at its next load with this plugin. Backups: `save-backup-20261023-*-s1` / `-s3` (every game_checks run copies the save folder first) and `nc2\save-backup-s3`.

Not covered: Hardcore, a corpse that cannot be picked up completely (kept path is unit tested, not seen in game), the belt's potions, a death in the town (not possible).

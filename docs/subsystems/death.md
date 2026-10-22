# Death, corpse, penalties and respawn (lane no-corpse, 2026-10-22)

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
| Hardcore? | **Unchanged from D2.** Median patches nothing in the respawn path: a Hardcore character that asks to respawn is kicked from the game (client status byte `+0xa` bit 4 -> `0x6fc4d090(game, 3)`) and the character is dead for good. There is no corpse in Hardcore either (the NOP is not difficulty dependent). The patch notes' Hardcore-to-Softcore conversion on single player (audit DC9) is not in the death code | respawn handler `0x6fcf14b0` is outside every patch record; the packet table `0x6fd1a078` and the mode tables `0x6fd1a3fc` are not patched either (no record above RVA `0xf4500` reaches a data table) |
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
- `mxl` prints a `no corpse` line (calls swallowed) and a `respawn wait` line (requests held / let through). `h state` prints `corpses N players=N self=N | no corpse on: skipped corpse calls=.. penalty calls=.. respawns held=.. let=.. xp=..` (a corpse is a second player-type unit; the scan counts player units other than the character in mode 0x11).
- The client needs nothing: the corpse unit and its sprite come to the client from the server's unit creation; with no unit created, no client object exists (`h state` on the server and the in-game screenshot in the check show none).

## 4. Save format: characters with and without a corpse

`docs/save-format.md` ("Items") lists the player items; the corpse comes right after: `JM` + u16 corpse count (0 or 1); for a
corpse a 12 byte header (u32 0, u32 x, u32 y in the area's coordinates) and a second `JM` item list with the items the
corpse holds; then `jf` and the mercenary's `JM` list, `kf` (golem). Real examples from this machine's test saves
(`MxlBarb.d2s` after a death): `4a 4d 01 00 | 00 00 00 00 14 16 00 00 92 15 00 00 | 4a 4d 01 00 ...` = one corpse at (5652, 5522)
holding one item.

- **A character saved without a corpse** (corpse count 0, which is every character that dies with the port after this part) loads cleanly: nothing in the load path is changed, and the part never creates a corpse.
- **A character saved with a pre-existing corpse** (made before the part, with D2R's own death): the loader recreates the corpse unit from the save through D2R's load path, which is not one of the two callers of `0x42c110` (callers are the death mode transitions only), so the hook does not touch it. The corpse stands where it died and can be picked up as in D2 (the items return to the inventory). This is the sensible handling: nothing is deleted, no item is lost, and the player decides. Median never had this case (its saves have no corpse). Not tested in game (it needs a death save from before the part and a slot; by code reading and the save layout above).

## 5. Check and results

`py -3 tools/game_checks.py all nocorpse --slot N` (check `nocorpse` in tools/game_checks.py): equips a ring, an amulet and a sash (the harness cannot fill a belt; the belt's potions go to the corpse in D2 and are not touched without one), sets gold, bank and a big experience value, kills the character with `hit`, then asserts: no second player unit before or after the respawn, the same items on the same body locations, same gold, bank and experience, no gold pile on the ground, the plugin counted the swallowed calls, Esc at once is held back (still dead, `respawns held` rises), Esc after 130 frames respawns the character in a town (`let` rises, frames between death and respawn >= 125).

Results: see the bottom of this file (section 7).

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

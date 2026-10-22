# Median's own areas: D2Drlg.cpp, D2DrlgLevel.cpp, D2Landscape.cpp and the D2R side (lane areas, 2026-10-21)

Question: what does Median change in level generation, and can its own areas (Trials, Ruins of Ureh, Silver City, Caldeum,
the Dimensional Labyrinth, Heart of Darkness, the uber areas, ...) generate and be entered in D2R?
Sources: `D2Sigma.dll` (Ghidra copy), D2R 3.3 build 93847 (dump + Ghidra), the built tables in `work/mod`, Median's
`extract/mxl` maps and tiles. Tool: `py -3 tools/area_audit.py` (static audit, section 4). Not verified in game (no
level-warp console command exists; the installed mod predates the fixes below).

## Short version

- **Median does not change level generation.** The three D2Sigma files are thin: no maze, preset or outdoor logic of its
  own. Generation is 1.13c's DRLG driven by Median's tables, and D2R has the same engine (Maze.cpp, Preset.cpp,
  Out*.cpp). Everything Median adds is **data** (bucket a).
- The audit found the tables consistent (0 problems before and after; section 4) but found **two real defects in the
  maps** that would have made every Median area wrong or empty in D2R. Both are fixed in the pipeline:
  1. **Objects.** D2R resolves a map object through the per-act `ObjPreset.txt`; Median's maps use its own flat table
     (150 slots per act, negative ids reach other acts). Without Median's table all 27,556 object placements in
     Median's maps (waypoints, portals, doors, chests, shrines) named other objects or none.
     -> `tools/objpreset.py` + `txt_migrate.object_presets()` add the table to ObjPreset.txt; `tools/ds1_remap.py`
     rewrites the maps.
  2. **Monsters.** Map monster ids at or above the act's MonPreset count are raw MonStats rows. Median's rows from 734
     up moved +17 in D2R (docs/id-mapping.md), so 4,033 placements (every boss and Median-only monster) named the
     wrong monster; 62 negative ids read outside D2R's table. -> `tools/ds1_remap.py`.
- Hard limits that could block Median's areas: **level types** (done, `level_types.cpp`, mxl-core 0.7.0/0.33.0) and the
  **maze level-type switch** (a data constraint all Median mazes meet). No new plugin hook is needed.

## 1. What D2Sigma's DRLG files hold (bucket a/e, nothing to port)

| File | Functions | What it does |
|---|---|---|
| D2Drlg.cpp | 4 (`0x5c7e0`, `0x5c8c0`, `0x5c970`, `0x5c9c0`) | `0x5c9c0`: level record byte +3 (levels.bin rows are 0x220 bytes, `D2TxtRecords.h`); `0x5c970`: byte +0x14b (the lockout-group byte, ubers-events.md section 3); `0x5c8c0` / `0x5c7e0`: server enter / leave level wrappers around D2Common (they walk the level's room unit lists and send a small per-unit packet of type `0x0a`). No generation. |
| D2DrlgLevel.cpp | 1 (`0x5cd00`) | level name lookup: D2Lang string of the level record, returned as a wide string. |
| D2Landscape.cpp | 1 (`0x80170`) | client: **not a sound lookup** (corrected 2026-10-22, [sigma-patch-audit.md](../sigma-patch-audit.md)). D2Sigma replaces 50 bytes of the per-frame game-screen update (D2Client rva `0x8b1c8`) that drew hard-coded backdrops for two levels. Levels record +6 (BlankScreen, 1 for levels 74, 161, 169, 171, 190-192, 197, 214, 272) fills the screen with a void, and +0x14c u16 picks a backdrop (summit clouds) for 5 levels. D2R already draws levels 120 and 74 right; the rest is cosmetic and **missing** (listed in [unported.md](../unported.md)). |
| D2DrlgWarp.cpp, D2Dungeon.cpp | 2, 11 | entry requirements and the lockout clock: ported (dungeon_lockout.cpp, ubers-events.md). |

Everything else Median does to levels is in its tables: `levels` (281 rows, up from D2R's 138), `lvlprest` (1,213),
`lvltypes` (52), `lvlmaze` (106), `lvlwarp` (114), `lvlsub`, `monpreset` (70 rows per act), `objpreset` (flat, 750),
`automap` (types 36-44 and 51), and 574 `.ds1` maps / 129 `.dt1` tile sets under `global/tiles/mxl/...`. Median-only
areas are 143 levels: 142 presets (DrlgType 2) and the maze levels, the Dimensional Labyrinth (230-238), the mirror
temples (256-262), the maggot lair (184/185) and the subterranean corridor (223); none uses the outdoor generator.

## 2. D2R's generation, as far as Median depends on it

- `DrlgType` (levels): 1 = maze (`0x3e69e0`), 2 = preset (`0x3dd3c0`), 3 = outdoor (Out*.cpp, chosen **by level id**
  for D2R's own wilderness/desert/jungle/siege levels). Dispatcher `0x3271c0`. Median-only levels must not be DrlgType 3
  (none is).
- **Preset levels**: the level takes the first `lvlprest` row whose `LevelId` is the level (`0x32c410`, 0x1ac-byte rows,
  level at +4); "Level labeled as Preset, but no preset claims the level" asserts otherwise. `txt_migrate.claim_presets`
  already gives every unclaimed level a copy of the "None" row. `Files` is not a file count (0 for the fixed town layouts
  that list four files); the audit looks at `File1..6` directly. `Dt1Mask` is a 32-bit mask over the level type's `File 1..32`.
- **Maze levels**: `lvlmaze` row by level id (`claim_mazes`), pieces are `lvlprest` rows picked by `Def` from fixed
  tables per level type: the post-generation switch at `0x3e69e0` handles **level types 3, 4, 7, 8, 10, 13, 14, 15, 17,
  18, 19, 22, 23, 24, 25, 28, 32, 33, 34, 35**; any other type asserts and exits the process (default arm).
  All 89 maze levels of the mod (the Median-only ones use types 3, 4, 8, 17, 18, 32, 33, 34) are inside the set; the audit checks it.
- **Level count**: `levelDataTbl.size() < MAX_LEVEL_ID` with MAX_LEVEL_ID `0x10001` (`0x330446`): Median's 281 are fine.
  Level ids travel as words (`movzx edx, di` at the id asserts `0x435dd0`, `0x50ed70`; the text says 255 but the code
  tests 0xffff). `lvlprest` rows are 0x1ac bytes in a vector, `lvltypes` in a vector (`0x32db30`), `File 1..32`.
- **Level types**: AutoMap.txt matches 36 names; the one startup assert "ii < NUM_LEVEL_TYPES" is expected and harmless;
  automap tiles for types >= 36 come from `level_types.cpp`. Type 50 (2 levels) has no automap rows: no automap there.
- **Level group**: `levelgroups.txt` is D2R's own (terror zones); Median's levels leave `LevelGroup` blank. The startup log
  shows no assert for it.
- **Map objects** (`PresetFileLoad`, `0x3dfae0`; act = the `.ds1` header act):
  - type 1: `0x396f50(act)` gives the act's MonPreset rows (4 bytes: kind, class). `id < count`: that row
    (kind 0/1/2 = monster id, plain, superunique); `id >= count`: the raw id is the MonStats row. **Negative ids pass the
    signed compare and read before the table** (garbage).
  - type 2: act's ObjPreset array (`+0x1a70` of the data tables, 0x270 bytes per act): `id` outside the array, or a
    negative one, skips the object; else the stored Objects row.
  - D2R special-cases a few resolved monster classes (act 3 ids 0x129 / 0x16e, act 5 0x202 / 0x219-0x21b become objects);
    no Median map uses them.

## 3. The two map defects, in detail

### Objects

Median's `objpreset.bin` is a flat array of 750 Objects rows: slot = 150 x act + id, 150 slots per act (act 1 slots
0-112 equal D2R's act 1 table; Median appended its own entries after them and kept a gap). Maps may use negative ids
(act 5 maps reach back into act 1: `-599 + 600 = 1`); 3,340 of the act-5 placements do. D2R's `ObjPreset.txt` has
5 acts x (113, 135, 116, 66, 154) rows with the same first 108/130/111/61 entries, then diverges. Editing D2R's
table would break D2R's own maps (about 1,600 of the maps the levels use, in vanilla areas Median does not ship), so
Median's table is added: slot `g` -> `Index = 1000 + g` in **all five acts** (586 non-empty slots x 5 = 2,930 rows,
`objpreset.build`). The `.ds1` header act is the one D2R uses (checked on all 60 maps whose header act differs from
the level's act: the header reading gives only valid slots, the level's act gives invalid ones in 32). ObjectClass names
an Objects `Class`, which Median's 16 added objects (rows 587-602 in the mod: dummy_2..12, megaportal, rainbowportal,
one_way_portal, halloween_chest/exit) lacked: `Class = Name` for them. 30 placements name empty slots and are dropped.

### Monsters

Map type-1 ids < 70 (Median's per-act MonPreset size) are kept: the mod's MonPreset is Median's, so the rows match.
Ids >= 70 (all >= 200 in practice) are MonStats rows in Median numbering: `idmap['monstats']` row -> new id
(+17 from 734). 62 negative ids (Median's flat MonPreset, 70 rows per act) name plain monsters in other acts'
rows (act2male, sunless_angel, alkor, mephistospirit...): replaced by the monster's class id.

## 3b. Waypoints (levels 138, 139, 141, 147, 153, 155, 169, 242: numbers 70, 80, 71, 60, 50, 72, 61, 90)

Median numbers its waypoints 0-38 (the 39 of Diablo II) plus `50` (Duncraig, Act 1), `60`/`61` (Tran Athulua, Skartara; Act 2),
`70`/`71`/`72` (Caldeum, Torajan Jungles, Vizjun; Act 3), `80` (Silver City, Act 4) and `90` (t_r_d_lra, Act 5): the tens digit
is the act plus 4, only a naming habit of the data.

**How Median stores them.** Not elsewhere: in the same bit field. Median's `D2Common.dll` (from the install) has the waypoint
test (ordinal 10373) and level-to-number (10436) with a 112-entry bit table (`cmp ax, 0x70`; word `1 + n / 16`, mask
`1 << n % 16`), byte for byte the table D2R has at `0x2389010`. The 24-byte per-difficulty "WS" record of a 1.13c `.d2s`
(2-byte header, 5 bytes for Diablo II's 39 bits, 17 spare) therefore carries Median's bits in the spare bytes. D2Sigma's own
waypoint panel (`D2Waypoints.cpp`, `ui/Waypoint.cpp`) lists every level of the tab's act that has a number, sorted by it, up to
63 rows; it keeps no data of its own.

**What D2R does (build 93847).**

| Part | Size / behaviour |
|---|---|
| Record in memory | 112 bits: table `0x2389010` (112 x `{u16 word, u16 mask}`), count dword `0x23891d0` = 0x70; `0x3d6890` (set) and `0x3d6930` (test) take a number below 0x70 and otherwise hit a debug break path (set does nothing, test returns 0) |
| Save ("WS": `0x52f090` writes, `0x52e910` reads, `0x3d6820` / `0x3d67c0` copy) | 3 x 24 bytes (normal, nightmare, hell), each the first 16 bytes of the record (header `0x0102` + 14 bytes = bits 0-111) and 8 zero bytes; the load sets bit 0. A number up to 111 survives a save and load; the 8 spare bytes are not kept |
| Network | packet `0x63` sends the same 16 bytes (`0x4812c0`); the server's use-waypoint handler `0x4b6f10` converts the destination level to a number (`0x3d6670`, levels row `+0xec`, 255 = none) and tests the bit: any number below 112 works, there is no act check |
| `.d2rl` sidecar | holds the `.d2s` checksum, size and SHA-256 plus its own hash (docs/save-format.md); nothing about waypoints, and nothing needed |
| Waypoint panel (`WaypointsPanel`: `0x1ecbc0` loads the rows, `0x1ea9b0` fills a tab, `0x1e9b40` builds the buttons) | **the limit**: one tab per `actinfo.txt` row (5), each with `waypoint1`..`waypoint9` level names; each row becomes a 0x34-byte entry (9 levels, 9 numbers) and the listed-button table `0x2a969e0` holds 9 entries. A level in no row has no button (a waypoint object there still sets and saves the bit). The 5 tab-unlock quest ids (`0xff, 7, 0xf, 0x17, 0x1a`) are a fixed array of 5 in the panel object (set at `0x1e9adf`), so a sixth tab cannot be added by data alone |

D2R's own five rows name 39 levels and leave Act 4's slots 4-9 empty; 39 + 8 Median levels = 47 > 5 x 9 = 45.

**Panel patch (`plugin/src/waypoints.cpp`, lane waypoint-panel, 2026-10-22).** Every Median waypoint now sits in its own act, up to
27 buttons per tab. The button table is a static array of 9 entries of 8 bytes (`{u32 level, u8 listed}`) at `0x2a969e0`, count dword
`0x2a969d4`, directly in front of the actinfo row vector (`0x2a96a28` data, `0x2a96a30` size, `0x2a96a38` capacity), so it cannot grow
in place. Three functions read it through a 7-byte `lea reg,[rip+disp32]`: the button builder `0x1e9b40` (`0x1ea198`), the click handler
`0x1ebf50` (`0x1ec05a`) and the state refresh `0x1ec430` (`0x1ec582`). The tab fill `0x1ea9b0(panel, tab)` is the only writer, and its
loop is fixed at 9 levels per row.

What the plugin does: it hooks the tab fill (13 entry bytes checked), and for the tab asked for runs the engine's own fill over
synthetic 0x34-byte rows (up to 3 rows of 9 levels, built from the generated `mxl_waypoints.h`, ascending waypoint number) by
swapping the row vector's data pointer and size for the call and restoring them after it. Each run's table entries are appended
to a plugin table allocated within +-2 GB of the image (rip-relative reach), and the count dword is set to the total. The three
`lea` disp32s are patched through `PatchBytes` with the expected bytes verified (the 5 bytes D2RLoader patches are not touched; no
rip-relative copy is made). Because the engine's fill makes the entries, the "not yet visited" and "current level" flags and the
unlock logic stay the engine's. The tab-unlock quests are unchanged (tabs II-V open with quests 7, 15, 23, 26 as before).

Layout: the buttons are not layout widgets but clones of `UnselectableButtonTemplate` made at run time (`Act%d_Button%d`, y =
index * `buttonOffset` + 264, default `buttonOffset` 123). 12 buttons at 123 do not fit the 1162x1507 HD panel, so
`tools/ui_layouts.py` writes `waypointspaneloriginalhd.json` with `"buttonOffset": 100` (art is 129 tall; the buttons overlap
their gap a little, which the art allows). No extra widgets were needed.

Data (`tools/waypoints_data.py`): `plugin/src/mxl_waypoints.h` lists every waypoint level with its tab (levels.txt Act + 1); the
plugin fills each tab from it. `actinfo.txt` is still written for the patch-off case (word `waypoints` in `mxl-core-off.txt`, or an
image the patch does not recognise): D2R's rows plus only the Median levels of their own act that have a free slot (just Silver
City, Act 4). The `mxl` line `waypoints` reports the plugin as `panel list has a button for N`.

Result in game (slot run, HD, 5 tabs, screenshots checked): I 10 (Rogue Encampment to Duncraig), II 11 (to Tran Athulua,
Skartara), III 12 (to Caldeum, Torajan Jungles, Vizjun), IV 4 (Pandemonium Fortress to Silver City), V 10 (to Tur Dulra).
Not verified: a travel click from a real waypoint object succeeding (a click from a harness-opened panel is sent to the server and
refused with "Impossible", the same as D2R's refusal when no waypoint object is in use).

## 4. `tools/area_audit.py`

Checks (read-only, `work/mod/data/global/excel`, Median's and D2R's tiles): level ids contiguous and unique, Act 0-4,
LevelType in lvltypes, DrlgType 1/2/3 (3 only for D2R's ids), a preset or maze row per level, maze LevelType in the switch
above, Vis/Warp targets exist, monster and ObjGrp columns resolve, `Def == row`, `Dt1Mask` 32 bits and slots, every
`File1..6` of lvlprest and `File 1..32` of lvltypes exists in the mod or D2R, every referenced `.ds1` parses, map tile
coverage (floor/wall style and sequence present in the selected `.dt1`, heuristic), the `.ds1` id remap and ObjPreset
rows it needs. Result on the 2026-10-21 build (`work/mod`, both banks identical): **0 problems on the tables**
(281 levels, 143 Median-only; 1,213 presets, 106 mazes, 52 level types, 114 warps; 2,184 distinct maps resolved, 572 of
them Median's); one problem until the data is rebuilt: the built mod has no Median rows in ObjPreset.txt (the audit run
on a rebuilt copy: 0 problems). Warnings and info:

- 8 Median waypoint numbers >= 39 (levels 138, 139, 141, 147, 153, 155, 169, 242: 70, 80, 71, 60, 50, 72, 61, 90): not a
  save problem (the record keeps 112 bits); the panel patch (section 3b) gives each its own act's tab; the audit warns only for an
  act outside 1-5 or more than 27 in one act.
- 2 one-way Vis links (teganze -> torajan_jungles, vizjerei_temple -> ruins_of_ureh), 16 `lvlmaze` rows for levels that
  are presets (vanilla leftovers), 5 level types (45-49) no level uses, type 50 without automap rows, 3 placeholder level
  rows (254, 255, 270; never generated).
- 2 maps with low tile coverage by the heuristic (nexus bridge `bridge1.ds1`, the pacman `marketing.ds1`); Median data as
  shipped, not investigated.
- Median's tile files shadow 426 of D2R's paths (425 differ): Median's copy wins in the mod.
- 30 object placements dropped (empty ObjPreset slots), as above.

## 5. Status

| Item | Status |
|---|---|
| Median's DRLG code (D2Sigma) | nothing to port |
| Table consistency of Median's areas | audited, clean |
| ObjPreset.txt with Median's table (`txt_migrate.object_presets`) | done in the pipeline, run `tools/build.py` |
| `.ds1` monster / object renumbering (`ds1_remap.py`, called by `install_mod.py`) | done; **install + in-game check pending** (docs/testing.md) |
| Plugin hooks | none needed |
| Waypoint numbers >= 39 | save and engine fine (112 bits); panel patch lists every Median waypoint under its own act (up to 27 per tab, HD layout stride 100); tabs checked in game; travel click from a real waypoint object not checked |
| Automap for type 50, low-coverage maps | open |

# Median maps in HD (stand-in scenes)

Goal: every Median map looks as close as possible in the D2R HD client with **vanilla HD data only**. Custom art is
the last step ([PLAN.md](PLAN.md) "Removed or deferred for now"). Tool: `tools/maps_hd.py` (run by
`tools/install_mod.py`, no extra step). Per-map list: [maps-hd-report.md](maps-hd-report.md) (generated; map ->
stand-in, confidence, note). Area/level background: [subsystems/areas-drlg.md](subsystems/areas-drlg.md).

## How D2R HD draws a map

- Each `.ds1` has an HD **scene preset** `data/hd/env/preset/<ds1 path>.json` (2,100 in D2R). A vanilla scene holds a
  baked terrain model, wall/prop entities in world units and a `biomeFilename`
  (`data/hd/env/biome/*.json`: ground texture layers, vis lighting, IBL, tile masks).
- The scene is found by the ds1 path. A map without a scene still loads (the loader logs
  `!bcIsStrNullOrEmpty(biomeFilename)` once, bug #24). **What it draws: a default biome (green gravel/grass painted
  from the floor-tile data) plus the objects/monsters/lights of the ds1, and no walls or buildings.** Median XL's
  town of Caldeum, Silver City, the Torajan jungle all looked like the same dark green field
  (`before-b/138_caldeum.png`, `139_silver_city.png`, `141_torajan_jungles.png`).
- Classic tiles are not upscaled, there is no fallback scene.
- D2R also ships minimal scenes (no entities, no terrain model, only a biome). That shape is valid, so a plain data
  JSON in the mod folder is enough; **no plugin hook is needed**.

## What the tool does

1. Counts Median's 574 `.ds1` against D2R's: 426 share a name with D2R, 148 are Median-only.
2. All 426 shared maps are byte-equal in D2R and 1.13c (D2R changed no map); **425 differ in Median = real Median
   edits** (mostly objects/monsters; `diff` in the report: share of changed wall/floor cells, objects added/removed).
   1 is unchanged. So there are no "D2R updates" to sort out.
3. Edited maps keep **D2R's own scene** (terrain model, walls, props) when the layout is close: size unchanged and at
   most 25% of wall/floor cells changed (`--edited-threshold`). Props of the vanilla scene that Median moved can be
   slightly off. 414 maps. Confidence **high** at <= 3% changed, else **medium**.
4. Edited maps whose size changed or that changed more than the threshold (11: nine `act2/maggot` pieces,
   `act4/mesa/chaos16x08_2`, `expansion/wildtemple/tempenter`) and all 148 Median-only maps get a **minimal scene**
   (`work/mod/data/hd/env/preset/<map>.json`, 159 files) that points at the closest vanilla biome: first by map
   directory (`BIOME_BY_DIR`, about 50 rules), else by the level type of the levels using the map
   (`BIOME_BY_LEVELTYPE`, lvlprest + levels.txt + lvltypes.txt), else `act1_outdoors`. For the edited maps the D2R
   scene's own biome is used.

| Group (Median-added levels, id >= 138) | levels | maps | HD now |
|---|---|---|---|
| random / maze areas on a vanilla level type | 22 | pieces are vanilla-named (edited, D2R scenes; 11 minimal) | D2R scene or minimal |
| fixed layout, vanilla level type | 77 | 76 Median-only | minimal scene, closest biome |
| fixed layout, Median level type (>= 36) | 44 | 69 Median-only (+1 shared with the previous row) | minimal scene, closest biome |
| unused / secret / test maps | - | 2 | minimal scene |

Median-only maps by confidence: 18 high, 117 medium, 13 low (secret/test levels, Whimsyshire, Fauztinville, Realm
of Lies, the riftwalker storm maps, Island of the Sunless Sea). Biomes used (maps): act1_outdoors 27, act4_fort 21,
act2_arcane 15, expansion_baallair 15, expansion_siege 6, act3_jungle 7, act4_lava 6, act3_kurast 4,
act3_jungle_dungeon 4, expansion_ruins 4, and 20 more with 1-3 maps each.

## Measured in game (slot 1, background mode)

Before (`%USERPROFILE%\mxl-d2r-runs\before-b\`) and after (`...\after-a\`), same `goto <level>`, 5 s wait:

| Level | Before (no scene) | After (minimal scene) |
|---|---|---|
| 138 Caldeum | dark green field | sand/dust ground (`act2_town`); the `BC_ASSERT: 0` log line of this level is there before and after |
| 139 Silver City | dark green field | dark cracked stone (`act4_fort`) |
| 141 Torajan Jungles | green field | jungle dirt/stones/grass (`act3_jungle`) |
| 149 Teganze (Torajan Cemetery) | flat green field | jungle dirt, roots and grass (`act3_jungle`) |
| 142 Kurast 3000 BA | green field | jungle grass/dirt (not compared closely) |
| 169 Skartara | green field | desert sand with paving (`act2_outdoors`) |
| 152 Corrupted Abbey | graveyard on green field | same graveyard props; floor shade differs a little (this map is not one of the 159 minimal scenes) |
| 230 Dimensional Labyrinth 1, 2 Blood Moor, 1 Rogue Camp | D2R scene | unchanged (D2R scene reused, Median's edits are object-only) |

Floors now take the right ground type and light. **Walls and buildings are still missing** in the Median-only maps
(Caldeum's houses, Silver City's walls, Diamond Gates). Those are scene entities placed on a baked terrain model.

## What is left

- Stage 2: walls/buildings for the Median-only maps. Needs a terrain model and wall entities per map. Without new art
  this could be generated from the ds1 wall cells with a dt1-tile -> vanilla HD wall model table learned from the
  2,100 vanilla scenes; Median's own `.dt1` sets (129, all unique content) have no HD wall models and no entry in
  `tile_masks.json` (keyed by vanilla dt1 paths).
- Stage 3 (last, [PLAN.md](PLAN.md)): custom HD art (terrain models, textures, props) for the 148 Median-only maps.
- Only the 11 minimal-scene edited maps are not measured in game: they are all in maze levels, and `goto 184`
  (Maggot Lair) kills the game at the level load with or without the scenes ([bugs.md](bugs.md) "maze-level goto").
- `maps_hd.py` is data-only: rerun `install_mod.py` after changing the thresholds or tables.

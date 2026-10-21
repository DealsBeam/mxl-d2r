# HD effects for Median's missiles and overlays (Phase 5)

Goal: show Median's own skill art (missiles, hit explosions, cast/aura overlays) in D2R's HD mode instead of D2R
look-alikes (`tools/missile_hd.py`). Classic mode (G) already draws Median's DCC art.

## 1. How D2R describes an HD effect

Three layers, all data (no code):

| Layer | File | Role |
|---|---|---|
| Name map | `data/hd/missiles/missiles.json` | missiles.txt name (lowercase) -> model name |
| Definition | `data/hd/missiles/<model>.json` (`UnitDefinition`), `data/hd/overlays/<Character>/<overlay name>.json` (`OverlayDefinition`; the folder is overlay.txt's `Character` column) | entities with components |
| Visuals | `data/hd/vfx/particles/**.particles` (PopcornFX baked effects, 2,162 files), `data/hd/vfx/textures/**.texture`, `data/hd/vfx/meshes`, `data/hd/missiles/<model>/*.model` | what is drawn |

Component types used by all 373 missile and 333 overlay definitions: `VfxDefinitionComponent` (613, a `.particles`
file), `TransformDefinitionComponent`, `UnitRootComponent`, `UnitSourceSkillFilterComponent`,
`TrackReleasePointDefinitionComponent`, `ModelDefinitionComponent` (only 15, mostly arrows and thrown weapons),
`AudioEmitterComponent`, `EntityAttachmentDefinitionComponent`, `SpotLightDefinitionComponent`,
`ModelFadeInDefinitionComponent`, `PhysicsBodyDefinitionComponent`, `DefinitionPreloadComponent`.
**There is no sprite or legacy-animation component.** D2R does not convert DCC art; every HD effect is a PopcornFX
particle effect or a 3D model.

The `.particles` files are PopcornFX's baked format (compiled layer graph with a VM program and a string table;
materials `vfx/materials/Diablo_Uber|Light|Smoke|Uber_Ribbon|Uber_Mesh.pkma` live inside the engine, not in CASC).
We cannot author one (no editor, format undocumented), but they are patchable: every texture a layer uses is stored as
`<1-byte length><path>` ("data/hd/vfx/textures/..."), so a same-length path replacement is safe.

### Sprite effects: yes, one real example

`data/hd/vfx/particles/env/ui/vfx_character_create_fire.particles` (also `vfx_character_select_act1_fire`) plays a
flipbook: `T_FX_fireFlipbook_64x64.texture` is 1024 x 1024 BC4, 8 x 8 cells of 128 px = 64 animation frames of a flame,
bound to the Uber material's `Atlas.TextureID` (with `Atlas.SubDiv`, `Atlas.Flipbook`, `Atlas.Blending` parameters
that exist on every Uber layer) and coloured through `GradientRemap.GradientMap` (`T_FX_flame_gradient_07`, 256 x 16
RGBA8). All other atlas textures (`*_atlas_2x2`, `T_FX_debris_basic`, ...) are random-cell sheets, not animations.
Some effects also read game-set attributes `MissileFrames` / `MissileCurrentFrame` (82 effects: novas, flamethrower,
ground fire), D2R's way to sync a particle effect to the legacy missile animation.

## 2. Pipeline (tools/missile_art.py)

DCC -> HD sprite effect, per Median-only animation (one that D2R does not have):

1. `tools/dcc.py` decodes the DCC (direction 0; palette = D2R units palette).
2. 64 frames (the n source frames spread over 64 slots, same timing) in an 8 x 8 BC4 atlas: cell 128 px for animations
   up to 96 px, else 256 px; bilinear x1..3.5 on premultiplied alpha. Mask = brightest colour channel x coverage.
3. A 256 x 16 RGBA8 gradient: per mask level the mean sprite colour at that level.
4. The fire effect's `.particles` is copied with the flipbook and gradient paths replaced by same-length paths to
   the new textures (`tools/d2r_texture.py` writes D2R's `.texture` layout, byte-identical headers verified against
   D2R's own files) and the glow/licks layers' masks pointed at a black texture.
5. `UnitDefinition` / `OverlayDefinition` JSON with the effect scaled so a classic pixel keeps its size; missiles.json
   maps every missile using the CelFile; overlay.txt `Character` = `mxl` for the overlay rows.

First set (`--class bar`, wired into `tools/build.py` after `missile_hd.py`): the Barbarian's skills reach 6 Median-only
missile animations and 1 overlay (greencurseMissle: Snake Bite, LightningExplosion: Thunderstorm,
SomeBlueThing: Deep Freeze / Primordial Strike, FireCracker: Shamanic Trance, ...); every other Barbarian effect
already uses D2R art (Median reuses vanilla art for most skills), which has HD models through missile_hd.py.

## 3. Status and in-game test (2026-10-21)

Verified offline: `tools/dcc.py` decodes Median's DCCs (checked visually on greencurseMissle, LightningExplosion,
SomeBlueThing, FireCracker; ported from the DCC spec), `tools/d2r_texture.py` reproduces D2R's `.texture` header
byte for byte (rewrote `T_FX_fireFlipbook_64x64` with an identical 128-byte header, pixels within 0.001), the atlases
and gradients look right when decoded, the patched particles differ from the template only in the path strings.

In-game (MxlBarb, level 126, 107 spare skill points; files copied into the installed mod and restored afterwards):
**not confirmed.** The game started cleanly with the new `hd/vfx`, `hd/missiles`, `hd/overlays/mxl` files and the
overlay.txt `Character` edit (no new log errors), but no effect of ours was positively identified:
- Town is the only place reachable without walking (the Rogue Encampment exit was not found by blind clicking), and
  no Barbarian skill with a Median-only *missile* can be cast in town. Overlays on town-castable skills were tried:
  `warcry` (Guardian/Defender/Protector Spirit) replaced by D2R's own unpatched campfire effect, `handofathena`
  (Wolf Companion) replaced by our patched effect, then by a synthetic marker sheet (magenta ring, green frame, frame
  number). Neither showed; the white glow at the wolf's spawn point was D2R's own. So either the replaced overlay
  JSONs are not used for the cast overlay of these skills, or the replaced effect needs state the overlay does not
  give it (the campfire template has `ObjectState` events and an `Atlas.Flipbook` layer we cannot inspect).
- Not tested: missile effects in flight, `shamanic_trance` (FireCracker, town-castable; its skill icon was not
  identified in the 18-slot skill list in time).

Next test, cheapest first: (1) learn `shamanic_trance` and cast it in town (real FireCracker overlay through the
`mxl` Character folder); (2) walk out to the Blood Moor and let quill rats shoot (`spike_1..5` remapped to our model
in a test missiles.json) or use Stampede/Overkill; (3) calibrate `CELL_WORLD` (tools/missile_art.py `--scale`).
If the campfire template never plays as a one-shot (ObjectState), the fallback is below.

## 4. Limits and fallback

- The flipbook template is a UI campfire: continuous emission, fixed 8 x 8 grid, one channel + gradient. Colour is
  reduced to a per-brightness gradient; direction and per-frame offsets are lost. Good for glows, fire, lightning,
  novas; poor for multi-colour sprites. Several arts per template layer cannot be combined.
- To get arbitrary sprite effects properly someone needs a PopcornFX editor (or the baked-format spec) to author a
  one-shot billboard with `Atlas.SubDiv` and a frame cursor driven by `LifeRatio` or `MissileCurrentFrame`; the rest
  of this pipeline (DCC decode, atlas, textures, JSON) stays as is.
- Fallback that already works: look-alike D2R models (`tools/missile_hd.py`) and classic mode (G) for exact Median art.

## 5. Plan for all classes

`python tools/missile_art.py --class sor` (etc.), `--all` for everything. Counts of Median-only art reachable from
the player skills: ama 9 missile + 2 overlay, sor 13 + 7, nec 10 + 5, pal 9 + 6, bar 6 + 1, dru 11 + 2, ass 11 + 2
(57 + 19 distinct); all Median-only animations 217 missile CelFiles + 113 overlay files (303 DCCs, 19 MB source).
Output size: 0.7 MB (<= 96 px art) to 2.8 MB (larger) per animation, about 500 MB for all; gate `--all` on in-game
confirmation. Effort: after the first confirmed effect, wiring all classes is a data run (hours); calibrating scale,
height and timing per art (looks, not code) is the real cost, roughly 1-2 days for the 330 animations, less with a
per-class review sheet. Monster-only missile art and the 11 missing CelFiles are out of scope.

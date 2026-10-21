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

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

## 6. Vanilla stand-ins first (tools/effects_hd.py, 2026-10-22)

Custom HD art is the last step (PLAN.md), so every Median missile and overlay row gets D2R's own HD effect now.
`tools/effects_hd.py` runs after `missile_hd.py` in `tools/build.py` and writes `work/mod/effects_hd.md` (row -> HD effect,
class, confidence). Numbers of the last full run are in unported.md "Graphics (HD)".

What it decides, per row (not per CelFile, as `missile_hd.py` does):

| Class | When | Effect |
|---|---|---|
| exact | the row's art (missile CelFile, overlay Filename) is a D2R file that a D2R row with an HD effect also uses | the HD model of that D2R row; when several D2R rows share the art (39 CelFiles have 2-7 models) the row most like the Median row wins: shared element/shape words of name, art and the skills that reach it (skills.txt -> missile -> sub/hit missiles), moving / hit / sub / loop role, then Vel, Range, Light, Trans |
| variant | exact or stand-in effect whose definition is rewritten | `mxl_<model>_s<percent>.json` (missiles) or the overlay file itself: every `TransformDefinitionComponent` scale and position times k. Only stand-ins need it (k from the ratio of Median's classic art to the stand-in's art, damped `ratio^0.7`, 0.6-1.8, 0.1 steps, left alone within 15%); an exact row has no scale of its own in classic data, so none is generated |
| stand-in | Median-only art, or D2R art no D2R row draws in HD | cost over candidates (D2R rows with an HD effect and DCC art): 3.0 x colour distance (mean palette colour of the DCC frames, hue weighted by saturation) + 1.2 x size ratio + 0.35 x frame-count ratio + role mismatches - 1.7 per shared family word (fire, ice, elec, poison, arcane, holy, bone, blood, dark, wind, earth, nature, blade, trap, buff) - 0.7 per shape word (nova, orb, bolt, explode, trail, wall, storm, front/back, small/large, cast) - 0.4 per shared word; hand-picked look-alikes (missile_hd.py LOOKALIKE) win for their art |

Findings that matter beyond this tool:

- **D2R's lookup key is not the plain lowercase name.** In `hd/missiles/missiles.json` and `hd/overlays/<folder>/` the key is
  the name lowercased, spaces as `_`, and an `_` in front of every digit run after a non-digit: `spike1` -> `spike_1`,
  `cyclonearmor1front` -> `cyclonearmor_1front` (all 333 overlay files and 742 of 756 missile keys follow it; the other 14
  are D2R's own misspellings: nihlathak/nehlithak). `missile_hd.py` wrote the plain name only; `effects_hd.py` writes both.
- The overlay's HD file is `hd/overlays/<Character column>/<normalised overlay name>.json`. Median's 316 new overlay rows have an
  empty `Character`, which draws no HD overlay at all; `effects_hd.py` sets `Character` to the folder of the effect it uses (in
  `excel/overlay.txt` and `excel/base/overlay.txt`) and writes the row's own file there.
- 16 missile definitions hold several `VfxDefinitionComponent` entities behind `UnitSourceSkillFilterComponent` (the entity with
  filter "" is the default; the others replace it for one source skill, e.g. Fire Wall, Meteor, Battle Cry). A Median skill with
  another name gets the default entity, which is the intended generic look.
- **What an HD definition can carry of Median's per-row differences:** only the transform scale/position of each entity
  (used), `ModelFadeInDefinitionComponent.alpha` (3 arrow models), and one `SpotLightDefinitionComponent` (colour, power, angle;
  1 definition, not used: the engine may still draw the missile light from missiles.txt Light/Red/Green/Blue, to be checked).
  There is no tint, transparency, speed or animation-timing field: of the 1,345 non-logic exact missile rows, 866 differ in
  light radius, 790 in light colour, 140 in Trans, 782 in Vel, 296 in animation timing from the D2R row they follow; those
  differences are not shown.
- **Tint** would mean editing the particles: 51 of 373 missile and 74 of 331 overlay definitions take their colour from a
  gradient texture (`textures/gradient/T_FX_*gradient*`), the rest from constants baked into the `.particles` program. A
  same-length path replacement (section 1) plus a hue-shifted gradient texture would recolour the gradient ones, but
  patched particles were never confirmed in game (section 3), so colour is matched by choosing the stand-in whose
  art colour is closest instead.
- **In game (slot 1, background mode, 2026-10-22):** casting player skills could not be driven: the harness `pkt` verb
  (client packets 0x3b, 0x3c, 0x05, 0x0c, 0x03) is accepted but the Barbarian neither casts nor moves after `cheat goto 2`
  (its mode stays 5, town neutral). Monsters were used instead: `h spawn 821 2` (Stone Clan goats, skill
  `stone_clan_skill_missile` -> stand-in) in the Blood Moor, 14 screenshots 0.45 s apart, before and after copying the generated
  `hd/missiles/missiles.json`, both `overlay.txt` and the generated hd definitions into the slot's mod. The stand-in effects
  load and draw (new fire bursts and the stand-in missiles appear where the "before" run showed only the old rings), the
  game loads without new error classes (the assert lines seen, `sub.nCelNum`, `ii < NUM_LEVEL_TYPES`, unique item index, were
  also in a run without the files). The look of single stand-ins is not signed off; no Sorceress run.

## Rows without art (tools/art_fix.py)

Median's tables name 12 missile and 12 overlay animations that are neither in D2R nor in Median's files (74 rows in both banks), plus two
names written with ".dcc" and one with "|" for the path separator. art_fix.py (after txt_migrate) repairs the names and points the rest at a
D2R file wherever one fits, so missile_hd.py (by CelFile) and effects_hd.py (per row) find the exact HD model of that D2R row; the one
Median-only target (`blackpentacle`, for vanilla's dead `pentacle`) goes through the closest-effect scoring. work/mod/art_fix.md lists every
row; validate.py fails on any animation name found nowhere.

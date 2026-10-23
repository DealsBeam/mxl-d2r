# D2R HD model formats and a way to add new 3D models (research lane `hd-model-formats`)

Status 2026-10-22, build 93847. Research only: nothing here changes the mod build. Tool: `tools/d2r_model.py` (reader, writer,
BitKnit2 decoder, CLI). No game files are in the repo; samples were extracted with `tools/casc_extract.py`/`native.py` into
`%USERPROFILE%\mxl-d2r-runs\hd-model-samples` (outside the repo, about 735 MB).

**Verdict in one line:** the formats are decoded, a clean Writer produces files the engine loads and draws (proved in game), so a
code-only pipeline from an artist's rig to a D2R unit is realistic; the missing pieces are (a) a Blender -> object-graph exporter
for skinned meshes + skeleton + animation curves, and (b) the art itself.

## 1. What the files are

| Extension | Content | Container |
|---|---|---|
| `.model` | meshes (vertices, triangles, bone bindings), materials, texture refs, optionally a skeleton | RAD Granny 2 (GR2) |
| `.skeleton` | bone hierarchy and bind pose (`Skeletons[0]`, plus a `Models` entry) | GR2 |
| `.animations` | named clips, each with track groups of transform tracks | GR2 |
| `.timelines` | animation events (sounds, hit frames, VFX) | JSON |
| `.texture` | already handled by `tools/d2r_texture.py` | DDS-like wrapper |
| `*.json` | unit, state machine, variant, monsters: see `docs/hd-monsters.md` | JSON |

Nothing in D2R's HD side is a proprietary D2R format except `.texture` and the JSON: `.model`, `.skeleton` and `.animations` are
plain Granny 2 files made by "Granny Standard Exporter, SDK version 2.12.0.5", and the Granny runtime is statically linked into
`D2R.exe` (inferred from the format; not disassembled). That is why generic Granny knowledge applies directly.

### 1.1 GR2 container (verified on every sample)

```
0x00  magic  e5 9b 49 5e 6f 63 1f 14 1e 13 eb a9 90 be ed c4   (64-bit little-endian Granny file)
0x10  header size, header format, 2 reserved
0x20  version = 7 | file size | crc32 | sections offset (0x48, relative to 0x20) | num sections = 8
      root type ref | root node ref | tag 0x80000039 | 4 extra tags | string table crc | 3 reserved
0x68  8 section headers x 44 bytes:
      compression (0 none, 4 BitKnit2) | offset | compressed size | size | alignment | first16bit | first8bit
      | reloc offset | num relocs | marshal offset | num marshal
```

- CRC32 (zlib) covers the file from 0x68 to the end; the reader checks it (`Gr2File.crc_ok`), the writer recomputes it.
- Relocations are 12 bytes (offset in section, target section, target offset). In a compressed section the relocation block is
  `u32 compressed size` + a BitKnit2 stream; pointers are 8 bytes.
- "Mixed marshalling" blocks (16 bytes each: count, offset, type section, type offset) exist in sections that hold data of a
  different byte order; D2R files carry a few in section 0, also compressed (`u32 size` + BitKnit2). They are only needed on
  a big-endian load, so the Writer emits none.
- D2R files use only **section 0** (objects, strings, member names) and **section 6** (the type definitions the file was written
  with). Sections 1-4 and 7 are empty. All sections use BitKnit2 (compression type 4) in shipped files; the engine does not need
  them compressed (see 4.).
- Granny files are self-describing: the root object's type is a list of 44-byte member records (type u32, name ptr, definition
  ptr, array size, 3 extras, ignored u64) ending in a null member. Member types 0-22 (inline struct, reference, reference to
  array, array of references, variant reference, transform (68 bytes), real32, int8/uint8/normal8, binormal, int16/..., int32,
  real16, empty reference, ...). `Reader` is driven entirely by these records, so unknown extra fields do not break it; `types`
  prints a file's schema.

### 1.2 BitKnit2

`tools/d2r_model.py` has a pure Python decoder (`bitknit2_decompress`). Ported from the MIT code in eiz/pybg3 / libbg3, cross
checked against ooz's `bitknit.cpp`: magic 0x75B1, 64 KiB quanta, two interleaved rANS states, adaptive frequency models
(300 / 40 / 21 symbols, 15-bit, adapted every 1024 symbols), a recent-offset cache, delta-coded literals. It decodes every file
sampled and the CRC of every container checks. It is pure Python and slow on multi-MB files (the 192-file check above takes about a minute), which is fine for tooling.
**There is no free BitKnit2 *encoder*** (RAD's `granny2.dll` is the only one), which is why we write sections uncompressed.

### 1.3 `.model`

Root object (Granny `FileInfo`): `Textures`, `Materials`, `Skeletons`, `VertexDatas`, `TriTopologies`, `Meshes`, `Models`,
`TrackGroups`, `Animations`, plus tool info and `ExtendedData` (`dump <file> --depth 1` shows the exact list). A typical part file (`torso_lit_lod0.model`) has
2 meshes, 2 models, 9 textures, 14 materials and a 1-bone skeleton.

- File naming: `<part>_<variant>_lod<0-4>.model`, five LODs per part and variant; a `Model`'s ExtendedData carries `lodLevel`.
  Median monsters reuse the same set (`torso_lit_lod0 .. lod4`) so a new unit must supply all five or the lower LODs are
  missing.
- Mesh: `PrimaryVertexData` (vertex type + `Vertices` variant array), `PrimaryTopology` (`Indices16` triangle lists, `Groups` =
  material ranges; `Indices` 32-bit only in 3 of 566 sampled files), `MaterialBindings`, `BoneBindings` (name + OBB min/max +
  triangle ids), `ExtendedData` (`VertexScale`, others).
- **Vertex layouts** (the vertex type is part of the file, so all of these are self-described):
  - skinned, 28 bytes: `Position` BinormalInt16[4], `BoneWeights` NormalUInt8[4], `BoneIndices` UInt8[4], `Normal`
    BinormalInt8[4], `Tangent` BinormalInt8[4] (w = +-127 handedness), `TextureCoordinates0` Real16[2];
  - static, 20 bytes: the same without weights and indices;
  - 24 bytes: static plus `DiffuseColor0` (vertex colour; occurs on environment/props).
- **Position decoding:** xyz = int16 / 32767 x `VertexScale` (the mesh's ExtendedData float). The fourth component `Position.w`
  holds the same scale in the log domain: `w = round(log2(VertexScale) / 13 * 32767)`. We verified both fit on all sampled
  meshes. In game, rewriting only `Position.w` (x3 scale) visibly enlarges the model, so the engine reads the scale from
  `w` (see 4.2); whether `ExtendedData.VertexScale` alone does anything is not shown.
- `BoneIndices` index the mesh's `BoneBindings` array (not the skeleton directly). Weights are normalised uint8; four influences
  per vertex.
- **Materials:** shader materials whose `Maps` use `Usage` = `AlbedoTexture`, `ORMTexture` (occlusion/roughness/metal),
  `NormalTexture`, `MaskTexture` (the KTINT colour mask the variant tables tint) and `ThicknessTexture` (subsurface). Each map
  is a texture node whose filename is `data/hd/...*.texture`. The huge Maya `ExtendedData` blobs on materials are authoring
  metadata; whether the engine needs any key of them is unknown (a new material can be cloned from a vanilla one).

Sampled statistics (566 of the 1064 `lod0` models, 0 parse errors): median 452 vertices per mesh, p95 11,209, maximum 353,038
(a boss prop); max 174 bone bindings in one mesh; max 141 meshes in one file; a mesh normally has 1 material/group (2,465 of
2,663), 2 in 186, up to 5; biggest file 9.2 MB. Skinned character parts are a few hundred to a few thousand vertices.

### 1.4 `.skeleton`

239 skeletons in the character data, 5 to 263 bones. Standard Granny `Skeleton`: `Bones[]` with `Name`, `ParentIndex`, `LocalTransform`
(flags, position, orientation, scaleShear), `InverseWorldTransform`, `LODError`. Names follow the Maya rigs: `root`, `translate`,
`anim`, `cog`, `pelvis_bind_jnt`, `L_/R_` side prefixes, `R_weapon_attach` and `L_weapon_attach` for attachments, plus IK/aim
helpers. Weapons attach by bone name via `UnitAttachmentComponent`, so a new skeleton must keep the attachment bone names the
state machine and weapon JSONs use.

### 1.5 `.animations`

One file per unit (`combined.animations`; the JSON state machine names clips like `attack1.animation`, which resolve to a clip
of that name inside the file). 245 files, up to 1,443 clips in one, up to 6.5 MB. Each clip (`Animation`: `Name`, `Duration`,
`TimeStep` = 1/60 in the common case, `Oversampling` = 2, `TrackGroups[]`) holds ~25 track groups (one per part plus `root`):
- `root` carries ~126 `TransformTracks` (orientation / position / scale-shear curves per bone) and ~11 `VectorTracks` (IK and
  aim targets);
- curve encodings seen across all clips (counts over all files): `DaIdentity` 36,404 / `D3Constant32f` 22,642 /
  `D4Constant32f` 11,214 / `D4nK16uC15u` 6,305 / `D4nK8uC7u` 4,703 / `DaConstant32f` 1,561 / `D3K8uC8u` 1,169 / `D3I1K8uC8u` 976
  / `D3K16uC16u` 647 / `D3I1K16uC16u` 134 / `DaK8uC8u` 132 / `D9I3K8uC8u` 131 / `DaK32fC32f` 71 / `D9I1K8uC8u` 55 /
  `DaK16uC16u` 47 / `D9I3K16uC16u` 31 / `D9I1K16uC16u` 7. The reader parses all of them as typed objects (knots/controls are
  arrays); it does not yet evaluate or encode them. The simplest writable choice is `DaK32fC32f` (float knots, float controls)
  and `D3Constant32f`/`D4Constant32f` for static tracks; the engine's runtime handles every Granny curve type.
- `ExtendedData` per clip/track group holds `bbAnimMin/Max` (animated bounding boxes).

### 1.6 `.timelines`

JSON event lists per animation; no tool needed. A new unit can copy the donor's event timing.

## 2. Existing tools (searched 2026-10-22)

| Tool | What it does | License | Status / fit |
|---|---|---|---|
| TheRussianX/D2R-Tools | Noesis GR2 import plugin and Blender scripts for D2R models | not stated | read only; the only D2R-specific tool found; needs the same D2R vertex knowledge we documented |
| Norbyte/lslib | Granny to DAE (BG3), issue #67 asks for D2 vertex support | MIT | has a GR2 reader/writer for BG3's layout; the D2R vertex types are not supported; the issue is still open |
| arves100/opengr2 | C GR2 reader | MPL-2.0 | no BitKnit, no writing |
| NoFr1ends/opengr2-rs | Rust GR2 reader | MIT | no BitKnit, no writing |
| eiz/pybg3 (libbg3) | BG3 GR2 / BitKnit2 in Python / C | MIT | decoder source we ported; no D2R schema; 2024 |
| ooz / oozextract | Oodle family, includes a BitKnit decoder | check before reuse | reference only |
| neptuwunium/Knit | C# BitKnit | not checked | archived |
| Noesis, BG3ModStudio | viewers/importers (BG3ModStudio is BG3-specific) | not checked | read only |
| CucFlavius/Zee 010 templates | GR2 binary templates | not checked | useful to cross check |

No Blender GR2 add-on and no D2R GR2 *writer* exists publicly; `tools/d2r_model.py` is, as far as we found, the first one. RAD's
Granny SDK would do export/compression, but it is a commercial SDK we do not have.

## 3. `tools/d2r_model.py`

```
py -I tools/d2r_model.py info  <file>              # header, sections, tree summary
py -I tools/d2r_model.py dump  <file> [--depth N]  # the object tree
py -I tools/d2r_model.py types <file>              # schema of the root type
py -I tools/d2r_model.py check <dir|file>...       # container + tree round trip on every .model/.skeleton/.animations
py -I tools/d2r_model.py repack <src> <dst> [--rewrite]
```

As a module: `Gr2File.read(bytes)`, `parse_tree(g)` (typed `Obj` tree with `.f` fields, lists, `Variant`, `Transform`),
`Writer(template_gr2).build(tree).write()`, `roundtrip(bytes)`.

### Round-trip results

- **Byte-identical compressed files: not possible and not needed.** It would require reproducing RAD's BitKnit2 encoder bit for bit.
  The reader decodes every section, so the *decompressed* content can be compared instead.
- **Container repack** (decoded sections, relocations and marshalling blocks re-written uncompressed, CRC recomputed; re-read
  gives identical sections/relocations/marshalling): passes on every file tested.
- **Object-graph rewrite** (`parse_tree` -> `Writer` -> re-read -> compare trees, including shared-object identity): passes.
  This session: `check` on `enemy/zombie1` (all 5 LODs of every part, 60 models + the skeleton and animation files of that unit)
  and `enemy/skeleton1`: **192 / 192 ok**. Earlier runs: 150 mixed samples ok after fixing marshalling decompression, and the
  parse of all 239 skeletons, 245 animation files and 566 `lod0` models in the stats runs had 0 errors.
- **Section byte identity:** the rewritten sections are not byte-identical to the original in sections 0 and 6 (6 of 8 equal; the
  empty sections match). The original orders objects by size class (32-bit block, 16-bit region at `first16bit`, 8-bit region at
  `first8bit`) and keeps mixed-marshalling blocks; the Writer places strings and objects in dependency order and emits no
  marshalling. The graph is equal; the layout is not. Uncompressed output is about 2x the compressed size (a 160 KB zombie torso
  becomes about 320 KB).
- **Does the engine accept the rewrite? Yes.** Test on slot 1 (ran 2026-10-22, two runs): all 60 zombie `.model` files (head,
  torso, legs, larm x lit/med/hvy x 5 LODs) rewritten by `Writer` (uncompressed, no marshalling) and dropped into
  `mods/MedianXL/MedianXL.mpq/data/hd/character/enemy/zombie1/`. `cheat mspawn zombie1` in the Blood Moor: no crash, no
  loader/core errors, zombies draw. Run 1 scaled by 0.6 (not clearly visible at game zoom, treated as inconclusive); run 2 by
  3.0: legs (only `Position.w` changed) are huge and the torso/head are visibly deformed, so **the files are loaded and geometry
  edits take effect**. `VertexScale` in `ExtendedData` alone could not be told apart in the screenshots; the safe rule is to write
  `Position.w` and `VertexScale` consistently, as the exporter must anyway. Screenshots are in the lane's run folders
  (`checks-20261022-173647-s1`, `checks-20261022-174859-s1`), not committed.

### Mesh swap in game (Wereowl lane, step 2, 2026-10-22)

`tools/d2r_mesh.py` (`replace_with_mesh`) writes a generated triangle mesh into a vanilla `.model` through the Writer: it keeps
one donor mesh as the template, rewrites the vertices (28 B skinned layout, `Position` quantised against `VertexScale`, tangents
from the UVs), `Indices16`, the single group, the bone bindings (box per bone in the bone's own space, via the skeleton's
`InverseWorldTransform`), `bbMin/bbMax/VertexScale`, and drops the donor's other meshes. Without weights the mesh is rigid to
one bone (`pelvis_bind_jnt`). `tools/sprite3d/mesh_prep.py` decimates a Hunyuan3D GLB to that input (`.npz`),
`tools/sprite3d/deploy_unit_model.py` copies the files into a slot and points a built unit at them.

Results on slot 2 (goatman1 torso, Hunyuan3D Wereowl mesh, 5 LODs, ~25k tris at LOD 0, spawned with `cheat mspawn wereowl`):

| Test | Result |
|---|---|
| Swapped model at the donor's own path (`goatman1/torso_lodN.model`) | draws, the Wereowl mesh with goatman's textures on planar UVs |
| Byte-identical copy of the donor's models at a **new directory** (`wc/`, `wereowl3d/`), unit JSON and `ModelDefinitionComponent` pointing there | **invisible** (no error in the logs, only the player is drawn) |
| Writer round trip at a new directory | invisible |
| New file name in the donor's directory (`goatman1/wotorso_lodN.model`) | invisible |
| Swapped model at the path of a vanilla model nothing in Median uses (`wargoatman/torso_lodN.model`; the others are `colossal1-3`, `corpsefire`, `cowking`, `darkelder`, `horse`, `pig`, `seagull`) | draws, only the Wereowl unit shows it; goatman1 keeps its own model |
| Own `.texture` (RGBA8 from `d2r_texture.write_rgba`, a recoloured goatman albedo) written over an existing vanilla path (`pig/textures/pig_alb.texture`), the model's albedo map pointing at that path | draws, but see section 3b: an RGBA8 albedo is read wrongly (colour noise on the wolf material) and a 2048 px one lost the GPU device; BC3 (format 62) at 1024 is the format that works |

So the engine draws a `.model` only when its path is one that D2R's model index lists (section 3c: `data/hd/model_lod_desc.bin`;
the files themselves can be anywhere in the mod folder). Until 3c was found (lane new-asset-paths, 2026-10-23) the way to give one
unit its own model without touching another unit's was to **override the files of a vanilla unit that no Median Id maps to** and
point the unit JSON's model entity at that path; the skeleton and state machine stay the donor's (goatman1). `MXL_OLD_PATHS=1`
still builds the Wereowl that way.

## 3b. The Wereowl in game: Druid form and Corrupted Wereowl (lane wereowl-3d, 2026-10-23)

Build step `tools/wereowl_models.py` (in `tools/build.py` after `hd_monsters.py`; rigging in `tools/sprite3d/rig_fit.py`; in game
test scripts `tools/sprite3d/morph_shots.py` for the Druid and `ingame_shots.py` for the enemy). The assets are in the repo
(Git LFS, see "Assets in the repo" below): `assets/hd/wereowl/` holds `mesh_lods.npz` and `albedo.png` (`MXL_3D_DIR` overrides).

### Assets in the repo (rule from 2026-10-23)
Every HD model, texture and other custom art the port uses is committed, so any PC can build the full mod from a clone:
- one folder per asset, `assets/hd/<name>/`: the build inputs the build step reads, `source/` with the generation sources
  (meshes, `.glb`, references), and a `README.md` (what it is, which build step uses it, how it was made, known flaws);
- everything under `assets/` is stored with **Git LFS** (`.gitattributes`); `.md` / `.json` there stay plain text;
- build steps default to the repo folder; an environment variable may point elsewhere for work in progress;
- on another PC: `git lfs install` once, then `git pull` (or `git lfs pull` in a clone made without LFS), then `tools/build.py`.
  Without LFS the files are small pointer texts and the 3D steps fall back to their stand-ins.

**Off switch:** `MXL_NO_3D=1` in the environment of the build (or `py -3 tools/wereowl_models.py --off`, or no assets in
`MXL_3D_DIR`): the step does nothing and both uses keep the goatman1 stand-ins of `hd_monsters.py`. Nothing else in the build
depends on the step.

### Median's morph mechanics
The Druid's form is a state drawn as a **monster unit**: skill `wereowl_form` (id 1364) sets state `wereowl_form_state` (id 278),
whose `gfxtype` 1 / `gfxclass` is a monstats row (Median row 1066 = Id `wereowl`, row 1083 in the port). The client draws the
player as that monster: monsters.json[Id] -> unit JSON -> model, skeleton, state machine; the player's animation requests
(neutral, walk, run, attack, cast, get hit) are answered by the unit's clips. The port moves Median's monster rows, so
`txt_migrate.py state_graphics()` rewrites the `gfxclass` of Median's own state rows to the migrated ID (before that fix the state
pointed at a different monster). D2R's own wolf and bear forms (430 / 431) are unchanged.

### Skeleton rigged to, and why
**The Druid's Wereowl is rigged to the werebear skeleton** (`enemy/bear`, 93 bones; not `druidbear`, that is the summoned Grizzly;
the unit's textures are `werebear_*`). The unit is a copy of `bear.json` (`mxl_wereowl3d`: state machine, Traversal / Fidget /
LookAt, the `ObjectFX` effects part and all werebear animations are reused), only its torso model entity is pointed at our model.
The enemy uses **goatman1** (the donor `hd_monsters.py` already chose for `corrupted_wereowl`, so its clips, the x1.25 scale and
the corrupted colour stay).

*Why the wolf rig was dropped (first version, same day).* The Wereowl was fitted to the werewolf skeleton (`enemy/wolf`, profile
`wolf`, u 0.55). The werewolf is a hunched, digitigrade beast (its animations stand and run low on bent legs and swing the
front limbs like a wolf), the owl is an upright biped with arms: on the wolf skeleton the owl was drawn as a small crouching
four-legged thing, smaller than the werewolf and nothing like the Median sprite. The werebear is a T-pose, upright biped (hip
4.2, shoulders 7.2, head top 8.5, arm span 13.6 units; its stand is upright, its walk a hunched stride), its animations are those
of a beast with arms, so the owl is on it. The `wolf` profile stays in `rig_fit.py` (nothing uses it; it is the regression
reference of the generalised code).

Rigging (`rig_fit.py`): the mesh is posed to the skeleton's bind proportions with hand-placed joint roles (arms, legs, spine, head),
vertex weights are the 4 nearest capsule segments (smoothed), each role's vertices are un-posed into the bone's bind space, and a
unit scale u goes into the unit JSON's root transform so the in-game size is the mesh's own. The vertex layout is the donor's
(28 byte skinned vertices, bone bindings with boxes). **Profile `bear`** (u 1.0, mesh scale k 1.25: the owl is 8.2 tall, about 9.0
with the head, against the werebear's 8.5; arms span 11.4 against 13.6):

| Owl part (role) | Bear bone | Note |
|---|---|---|
| hip / belly (pelvis, spine_a, spine_b, chest) | `pelvis`, `C_Spine_spine1`, `C_Spine_spine2`, `chest` | joints placed at the owl's belly and chest; the bear's torso is 3 units long, the owl's about 2.4 x 1.25 |
| neck, head (huge ruff and beak) | `C_Neck_neck1`, `head` | the head joint sits in the middle of the head mass (y 5.7), not at its base, or the head would stand 2 units above the bear's |
| shoulder, upper arm, forearm, wrist | `{S}_Clavicle_Clavicle`, `{S}_Arm_UpperArm`, `{S}_Arm_ForeArm`, `{S}_Arm_Wrist` | the owl's arms leave its body at y 3.5 x 1.25, the bear's at 7.2: arms are carried up 2.6-3.2 and join the body behind the head ruff |
| thigh, calf | `{S}_Leg_Thigh`, `{S}_Leg_Calf` | `keep` x: legs stay as wide as the owl's (the bear's stand 0.7 apart) |
| ankle -> bear foot, toe -> bear ball | `{S}_Leg_Foot`, `{S}_Leg_Ball` | the bear has no ankle (`skip` foot); the owl's posed ankle is tilted back like the bear's shin, else the un-posing turned the foot 21 degrees into the ground (feet at y -0.77, now +0.06) |
| fingers, jaw, ears, tail, eyes | not weighted | the owl has no tail, its beak and ears ride on the head bone |

New `rig_fit` fields for this: `k`, `dz` (mesh moved 0.6 forward over the spine), `posed` (joint overrides per profile), `skip`,
`leg_snap`, `keep` (no travel along an axis). The old profiles run through the same code and give identical output.
Template meshes: the werebear's torso models have 4 LODs (`torso_lod0..3`; LOD 4 of ours uses the template of LOD 3); LOD 0
holds eight meshes (the real `bear_bodyShape` and a `bear_body_proxyShape` of the same size, teeth, tongue, eyes, trinkets, fur
shell), LOD 1-3 name the material `enemy_bear_LOD` instead of `body`; `wereowl_models.body_mesh` takes the biggest non-proxy,
non-fur mesh, the other meshes are dropped (the fur shell and the trinkets with them).

### Path problem (new model files for player forms)
Solved by section 3c: the Druid form and the Corrupted Wereowl live at **new paths** (`data/hd/character/mxl/wereowl/`,
`.../corrupted_wereowl/`); no vanilla unit is overridden and the nine unused vanilla units (`colossal1-3`, `corpsefire`, `cowking`,
`darkelder`, `horse`, `pig`, `seagull`, `wargoatman`) are no longer a limit. Before (and with `MXL_OLD_PATHS=1` or
`wereowl_models.py --old-paths`) the files went over the vanilla paths of `enemy/horse` (+ `horse/textures/horse_{alb,nrm,orm}.texture`) and `enemy/wargoatman` (+ `pig/textures/pig_{alb,nrm,orm,sss}`).
Unit JSON files were never a problem: a new `data/hd/character/enemy/<name>.json` is found through monsters.json (hd_monsters.py
writes about 1,200 of them), and player forms are monsters to the renderer.
**Scale:** the form is drawn at the unit JSON's root `TransformDefinitionComponent` scale, like a monster; the character's own
scale does not apply. `set_scale` multiplies it by the rig's u (bear 1.0: the owl is about as tall as the vanilla werebear).

### Findings that cost time (keep)
| Test (slot 2) | Result |
|---|---|
| Plain copy of the wolf models at the horse path, unit = wolf copy | draws the werewolf (control) |
| Writer round trip of all wolf LODs at the horse path | draws |
| Our mesh (rigid or skinned) at the horse path, vanilla textures, plain unit copy | draws |
| Albedo `.texture` RGBA8 (format 31) | drawn as colour noise (maps are read as block compressed) |
| Albedo RGBA8 2048 px (22 MB) | **GPU device lost** ("Present failed" 0x887A0005) at the first draw, every time |
| Albedo BC3 (format 62, `d2r_texture.write_bc3`) 1024 px | correct colours |
| Normal / ORM / mask as BC3 sRGB (format 62) | too dark (decoded as sRGB) |
| Normal / ORM as BC5 (an earlier reading of "format 61") | **wrong**: format 61 is **BC3 read linearly** (`d2r_texture.write_bc3(..., linear=True)`). Decoding the vanilla `werebear_nrm` / `werebear_orm` as BC3 gives DXT5nm (R 255, G = y, B 0, A = x) and ORM (R occlusion about 220, G roughness about 170, B 0, A 255); BC5 data under format 61 decodes as a tilted normal. Both flat maps are now BC3 linear (normal 255,128,0,128; ORM 255,160,0,255) |
| Albedo as baked (mean 41 of 255) | reads as black in game; `tools/sprite3d/albedo_clean.py` fills the near-black texels and sets the median luma to 100 (the old `ALBEDO_GAMMA` 0.5 lift gave about 96) |
| Bear rig with our flat BC5 normal + ORM (the wolf rig took them fine) | the whole body drew **black**. Two causes: the wrong BC5 format (above), and **the werebear material's own mask / thickness maps on our UVs**: with flat BC3-linear maps and the werebear mask the body was still black (also with a uniform grey albedo, so not the albedo); pointing `MaskTexture` / `ThicknessTexture` at the black `pig_sss` texture (what the Corrupted always did) lit it. The previous lane's `shared` werebear maps had hidden this |
| Bear rig, werebear albedo too (UV mismatch test) | lit brown: geometry, normals and bone bindings are fine |

Crash notes: three D2R instances at about 6.5 GB VRAM each exceed 16 GB, and a device lost in one game can take the others down;
run in-game tests while at most one other lane's game is up. The unit keeps its `vfxshell` entity (a copy without it was tried
inside a confounded series of runs and is not recommended).

### In-game result (2026-10-23, build 93847)
Wolf rig (first version, slot 2): morph, neutral, walk, run, attack, cast and get hit drew the textured model with the werewolf
animations, as a small hunched four-legged beast. Corrupted Wereowl (`cheat mspawn 1985`): neutral, walk, attack, get hit and death
draw.

Werebear rig (lane wereowl-bear, slot 1, `morph_shots.py --tag bear`, Blood Moor, Druid casting skill 1364): all phases draw with
the werebear animations; the owl is an upright, shaggy biped about the vanilla werebear's height, and clearly nearer to the Median
sprite than on the wolf. By animation, from the screenshots (local only, run folders `checks-20261023-095739-s1`, `-100032-s1`):
neutral: stands on bent legs, torso leaning a little forward, arms at its sides, head forward (the bear idle's breathing bob);
walk and run: a hunched forward stride (the bear's: the torso leans about 30 degrees, not a straight-backed human walk), big steps,
the legs look long and thin under the heavy body; attack: head and shoulders lunge forward, the arms are hidden in the fur mass so
the swipe reads as a body lunge; cast and get hit: a subtle bob and recoil.
Known flaws (before lane wereowl-quality): the owl is wider and its torso shorter than the bear, so the arms are carried up behind
the head ruff and the legs are stretched between the owl's hip and the bear's (long thin shins); the albedo is a noisy dark chart atlas
(grey-brown with black patches, nearly black in shadow); no fur shell, no jaw / ear animation; automatic skinning (some shoulder
stretch in swings). The wolf-rig screenshots for comparison: `checks-20261023-093955-s1` (`wolfrig_*`).

### Lane wereowl-quality (2026-10-23, slot 3, `morph_shots.py --tag t6 / h2`)
Done: (1) the texture formats and the mask finding above: the Druid form uses our own flat BC3-linear normal / ORM and the black
mask, and the body is lit (no black); (2) the albedo is cleaned (`tools/sprite3d/albedo_clean.py`: near-black and unseen texels
inpainted, light denoise, median luma 100; the raw bake and its coverage map are kept in `assets/hd/wereowl/source/`);
(3) skin weights come from Blender bone heat (`tools/sprite3d/owl_refit.py` + `owl_weights.py` -> `assets/hd/wereowl/druid_lods.npz`:
voxel-remeshed proxy, 8 nearest proxy vertices, 20 smoothing rounds, 4 influences). Offline (linear blend skinning on the bear
skeleton, synthetic idle / walk / swing / arms-up poses) the 99th percentile edge stretch fell from 1.36-1.54 (capsule weights)
to 1.13-1.32; the 99.9th percentile is slightly worse in walk / swing (2.9-3.0 against 2.5-2.6: loose feather shells), the worst
edge better (5.7-8.6 against 8.8-15).
Not done: proportions. rig_fit gained per-segment stretch (`stretch` / `girth` / `rscale` profile fields, off for `bear`); with the
current posed-joint table it changed the pose previews very little, so the owl is still wider and shorter than the bear. A real fix
needs a regenerated or hand-sculpted mesh (shorter ruff, longer torso and legs, arms away from the head), not a refit.
In game the swing deformation is hard to judge from the fixed camera crops; the new weights are not visibly worse than the capsule ones.
The in-game brightness changes with the area light between runs (h2 is darker than t6 with the same files), so the before / after
brightness in screenshots is not exact. The Corrupted Wereowl (`cheat mspawn 1985`) draws (white-blown with the cleaned albedo at
gamma 1; its albedo gamma is 1.7 now (`gamma` in `USES`, `wereowl_models.py`), which looked balanced: dark fur, light chest).

After merging the new paths (3c): the Druid form has its own `_alb` / `_nrm` / `_orm` at `data/hd/character/mxl/wereowl/` and uses the
vanilla `pig_sss` as mask / thickness (an own `_sss` entry at the new path crashed the game right after the cast, so none is written
or indexed). Final in-game run (`morph_shots.py --tag fin`, slot 3): neutral, walk, run, attack, cast and get hit all draw lit
and textured; the pose of the owl on the bear rig is unchanged (proportions as above). A vanilla werebear could not be captured
(cast 1375 refused: usable=2); the baseline is the earlier build (`bear`). The Corrupted Wereowl (`cheat mspawn 1985`) at
`mxl/corrupted_wereowl/` spawns, walks, attacks, is hit and dies. It crashed the game on spawn in one dev session: the cause was stale
files of earlier deploys in the slot (index entries, unit dependencies) that `tools/sprite3d/deploy_wereowl.py` carried along; it
now starts from the game's indexes and a clean unit, and a normal build is not affected.

## 3c. Assets at new paths (lane new-asset-paths, 2026-10-23)

**Result: `.model`, `.skeleton`, `.animations` and `.texture` files all work at new paths, as plain files of the mod folder. A new
`.model` path only needs an entry in the engine's model index `data/hd/model_lod_desc.bin`, a new `.texture` path an entry in
`data/hd/texture_desc_cache.json`. No plugin, hook or archive is needed.** Proved in game (build 93847, slot 1) with the real
Wereowl units at `data/hd/character/mxl/` and with probe units at `enemy/mxl/probe/` (all five drawn: models, skeleton, animations,
textures; screenshots local, run `checks-20261023-121029-s1`). Game check: `py -3 tools/game_checks.py --slot N all assetpaths`.

### Why vanilla ignored new paths
D2R keeps two prebuilt index files in CASC that the loaders consult by name, not the file system:

| Index | Key | What a path that is not listed gets |
|---|---|---|
| `data/hd/model_lod_desc.bin` (5.5 MB, 14,310 entries) | FNV-1a 32 of the lower-cased game path of the unit's model, `data/hd/character/enemy/horse/torso.model` | requested as the plain file `torso.model`, loaded without an error, **never drawn** (no bounds, no LOD mask, no stream info) |
| `data/hd/texture_desc_cache.json` (19 MB, 31,073 entries) | `filename` | the texture is read with the wrong format / size: an RGBA8 or 2048 px file was colour noise or lost the GPU device (section 3b findings) |

A listed model path is opened as `<base>_lodN.model` for every LOD the entry's mask lists. The old results of section 3 (a copy of
a working file at a new directory is invisible, the same bytes at a vanilla path draw) were this index: the vanilla paths have
entries, the new ones had none. The mod folder's own copy of an index file replaces the game's (D2RLoader: active mod first), so
`tools/wereowl_models.py` writes both files with the new paths added (`index_new_paths`; entries copy a vanilla one of the same
kind, new model paths copy the entry of the vanilla unit they used to override). The files are made at build time from the
installed game's CASC, nothing of Blizzard's is committed.

### `model_lod_desc.bin` layout (little endian; tools/model_lod_desc.py reads and extends it)
Header: three `{u32 count, i32 self-relative offset}` pairs (the offset is counted from its own field). Array 1: `count1` entries
of 44 bytes sorted by key: `u32 key | f32 bbMin[3] | f32 bbMax[3] | u8 lodMask (bit k+1 = LOD k) + 3 bytes | u32 lodCount | u32
streamOffset | u32 streamSize`; `streamOffset` is self-relative too (it points into array 2, 12-byte records that point into
array 3, 32-byte records: per-mesh stream data). Adding an entry grows array 1 by 44 bytes and moves arrays 2 and 3, so **every
entry's `streamOffset` has to be rewritten** (the first try did not: the loader asserted `isMeshFlipbook ==
GetModelInfo()->isMeshFlipbook` (model_asset.cpp:397) on the player models and the game died during the first preload). The new
entry copies the donor's stream records (a Wereowl mesh with other vertex counts drew fine with the horse's, as before).

### The order of the investigation
1. **Observe** (D2RLoader dev log `archive_file_opens.log`, config keys `casc_files` / `models`): for a base the index lists, the
   engine opens `X/torso_lodN.model`; for an unknown base it opens only the literal `X/torso.model` and logs no error. A literal
   file that exists loads (`create end` ok) and is not drawn, also when it is a fixed copy (a `Model` needs at least one mesh
   binding: `model_asset.cpp:305 MeshBindingCount >= 1`; `d2r_mesh.replace_with_mesh` now binds the kept mesh in every `Model`).
2. **ResourceService** (tried first, on the coordinator's request): the plugin registered the files at
   `data/.../d2rloader/mxl-core/...` at load (plugin id `mxl-core`; 11 files, 4.5 MiB, every result `Success`) and the engine
   opened them from there. Skeleton, animations and textures (with descriptor entries) worked; models stayed invisible without the
   index entry and drew with it. The same files as plain mod-folder files behave the same, so the ResourceService part (about 140
   lines: walk a folder, `registerResource` each file as `data/<path>`) is **not part of the build**. Its limits: 256 MiB per
   plugin load, 64 MiB per file. The Wereowl needs 3.5 MB (Druid form) and 3.8 MB (Corrupted Wereowl) with five LODs and the
   textures, so about 70 such units would fit; the mod folder has no such limit (the installed mod is 1.2 GB), so the files come
   from the mod folder.
3. **Index** (this section): the cause. Entries copied from a donor, new key, rewritten stream offsets.

### Skeleton, animations, textures at new paths (probes, plain mod-folder files)
- `.skeleton`: a copy of the werebear's at `enemy/mxl/probe/probe_skel/bear.skeleton`, the unit JSON's skeleton entity pointing
  there: draws and animates.
- `.animations`: the engine opens `<directory of the clip>/combined.animations` (a single `.animation` file at a new path is not
  looked up); a copy of the werebear's state machine with every clip directory moved, plus the combined file in the new
  directory: draws and animates.
- `.texture`: BC3 albedo and BC3-linear (format 61) normal / ORM / black mask at new paths with `texture_desc_cache.json` entries
  (size and mip count of the file): draw. Both Wereowl forms have four of their own (alb, nrm, orm, sss); the Druid form used to
  borrow the werebear's normal / ORM (lane wereowl-quality, section 3b, found why flat ones drew it black).
- The unit JSON, `monsters.json` and the state machine JSON were never limited.

### For the next unit
Give the unit a directory under `data/hd/character/mxl/<name>/`, write its files there (`torso_lod0..4.model`, textures, optional
skeleton / animations), and add its model path and textures to the two indexes: `wereowl_models.index_new_paths(data, {new model
path: vanilla model path to copy}, {new texture path: vanilla texture path to copy})` extends a copy already in `work/mod`, so
several build steps can add their own. Choose a template model with the same number of LODs (5) and a similar size.

## 3d. Wereowl mesh: bakes, retopology, fit to the bear (lane wereowl-mesh, 2026-10-23)

Goal: the in-game Druid Wereowl (rigged to the vanilla werebear skeleton at `data/hd/character/mxl/wereowl/`) as close as possible to
the static generated model. Steps in order, each checked in game on slot 3 (`tools/sprite3d/morph_shots.py`, background input; sheets
in the PR description). The build inputs and the exact commands are in `assets/hd/wereowl/README.md`; tools in `tools/sprite3d/`
(`owl_hi`, `owl_retopo`, `owl_bake`, `owl_maps`, `owl_lods`, `owl_pose`, `bl_common`, `render_npz`).

### Vanilla werebear budget and texture conventions (measured)
- Triangles per LOD, every mesh of the unit counted (body, trinkets, teeth, tongue, eyes, fur shell), the duplicate proxy meshes not:
  **38,968 / 17,383 / 10,050 / 5,414** (ratios 1 / 0.446 / 0.258 / 0.139; the bear has four LODs, the build reuses LOD 3 as LOD 4; the
  Wereowl's LOD 4 is 0.075). The Wereowl has 38,124 / 17,002 / 9,836 / 5,298 / 2,858.
- Textures: `werebear_alb` 1024 BC3 (format 62); `werebear_nrm` 1024 format 61 with R = 255, G = y (mean 126), B = 0, A = x (DXT5nm);
  `werebear_orm` 512 format 61 with R = occlusion (mean ~220), G = roughness (mean ~170), B = 0, A = 255. The Wereowl ships all three
  at 1024 (the bake is 2048), with a full mip chain (`log2 + 1` levels) and descriptors of 1024 / 11 levels in `texture_desc_cache.json`.
- Normal map convention. The vanilla bear's vertex tangents equal `d2r_mesh.tangents` (direction dot 0.99, handedness 99.6% equal),
  so the engine's bitangent runs along the stored dP/dv (v down the image) and the map convention is **DirectX-like (green toward the
  bottom of the image)**: `wereowl_models.pack_normal` flips the OpenGL bake's y (`MXL_NRM_GREEN=dx`, the default; `gl` for a test). This
  is derived, not seen: an in-game A/B (dx against gl) was not decisive at the game's camera scale.
- Writing: BC3 linear path (format 61) for normal and ORM, BC3 sRGB (62) for albedo.

### Step 1: normal map and AO baked from the high-poly
`hy_mv_4view.glb` (399k vertices / 799k triangles, 40 loose bodies, not watertight) bakes badly directly: rays hit the inside of
feathers (black AO patches, saturated wrong normals). `owl_hi.py` voxel-remeshes it (0.012 of a 6.6 tall figure) into one closed surface
(3.3M triangles), and `owl_bake.py` (Cycles, GPU/HIP when present, a bake takes seconds) casts from the game mesh pushed 0.04 out along
its vertex normals (the game's own, set as custom split normals, so the map is relative to the shading the engine draws), 0.12 deep:
no artefacts; larger cages (0.12 / 0.35) hit neighbouring feathers. AO is the high-poly's own occlusion (distance 0.3, local only),
pulled 40% toward open (`owl_maps.py --ao-strength 0.6`) because the raw bake averages 170 against the bear's 220.
Result in game: lit, textured, no tearing; the effect is modest at the game's camera distance (the occlusion darkens the glossy
chrome look of the old flat-map build, the silhouette is unchanged).

### Step 2: retopology, new UVs, albedo re-projection
`owl_retopo.py`: collapse the high-poly to a 300k proxy, QuadriFlow to the bear's LOD 0 triangle count (the face count is erratic; the
script retries and keeps the closest at or under budget: 38,124 triangles / 18,981 vertices before UV splits), shrinkwrap back onto the
high-poly. UVs: xatlas gave about 900 charts and 46% fill on this furry surface and was dropped; `project_texture.py`'s camera
charts (197) are used, and the albedo is projected again from the 16 sprite directions onto them (`albedo_clean.py`, median luma 100).
LODs 1-4: `owl_lods.py`, pymeshlab quadric collapse with UVs (seams collapse like edges; vertices are split per position / UV again).
Result in game: the even quad mesh gives a more feathery silhouette; the big visual change comes in step 3.

### Step 3: fit to the bear skeleton, weights
The old fit (`rig_fit.py` / `owl_refit.py`) scaled the whole owl onto the bear and left the arms under the head ruff. `owl_pose.py` keeps
the mesh in its own pose for the Blender bone-heat weights (`owl_weights.py` with the bear skeleton's bone names / hierarchy and the OWL's
joint positions, read off the front and side views: shoulders at x +-1.3, elbows +-2.9, wrists +-4.0, hips y 2.2, knees 1.3), then
carries every bone segment to the bear's: `v' = sum_j w_j (B_j + R_j S_j (v - P_j))` with R_j turning the owl segment into the bear's and
S_j stretching along it by |BC| / |PQ| (clipped 0.5-2.3) and across it by `stretch ** 0.6` (legs fixed 0.85-0.9: bear legs stand 1.3 apart).
The head takes the neck's turn and a 1.15 scale. Weights stay: max 4 influences, normalised, smoothed (`owl_weights.py`).
The owl's joints then sit on the bear's, so the animations bend the owl where it has joints: arms leave the shoulders, the knees and
elbows bend where the clips bend them. Fitted height 10.6 against the bear's ~8.5 (the ruff). Result in game (idle, walk, run, attack, cast,
get hit; eight walk directions with `MXL_MORPH_DIRS=1`): clearly bear-like movement and proportions, no tearing, no black faces,
0 assertions. Remaining difference: the owl's head ruff and chest are larger than the bear's, the thin bear arms look slimmer than the
sculpt's heavy arms, the albedo of the back is the least certain (seen only in the sprite directions).

### Corrupted Wereowl
It reads the same `mesh_lods.npz` / maps (rigged to goatman1 by `rig_fit.py`). `cheat mspawn 1985` spawns, approaches, attacks, is hit and
dies with no crash on the new files; the monster's green aura hides its body in the fixed camera crops both with the old and the new
assets (an unchanged-main run looks the same), so the mesh itself was not judged visually.

### Not done (step 4 and beyond)
Real-ESRGAN 2x on the albedo and a fur normal overlay were not tried. A custom skeleton (owl-like head, shorter legs, wing-ish arms)
is what would remove the remaining proportion differences; the weights and the fit script are the starting point for it.

## 4. Pipeline sketch: artist's Blender rig -> D2R unit

### 4.1 Steps

1. **Artist** delivers per Median look: a rigged, skinned mesh split by part (`hd` head, `tr` torso, `lg` legs, `ra/la` arms, ...
   whichever monstats2 slots the monster uses; Median monsters mostly draw only `tr`), textures (albedo, ORM, normal, KTINT mask,
   optional thickness), and clips named as the state machine expects (see 4.3).
2. **Export from Blender** (a script in the repo, run headless): per part and LOD, write
   `<part>_<variant>_lod<n>.model`. Build an `Obj` tree by cloning a vanilla `.model` of a similar part as the template (so the type
   definitions, material layout and ExtendedData keys are right), then replace `Meshes[].PrimaryVertexData.Vertices`
   (28 B skinned layout: int16 positions normalised by a per-mesh `VertexScale`, matching `Position.w`; weights normalised to
   255; 4 influences; normals/tangents int8 with tangent handedness; half-float UVs), `PrimaryTopology.Indices16` and `Groups`,
   `BoneBindings` (names, OBBs, triangle ids), `MaterialBindings`, and `Textures`/`Materials` pointing at the new `.texture`
   files. LODs 1-4: use Blender decimation (vanilla LODs are decimated copies; use `d2r_model.py dump` on a part's five files to match
   the ratios). Use 16-bit indices (only 3 of 566 sampled files use 32-bit ones).
3. **Skeleton:** same template trick: write `<unit>.skeleton` from the Blender armature (bone names, parents, local transforms,
   inverse world). Keep the attachment bones (`R_weapon_attach`, `L_weapon_attach`) and the helper bone set the unit's IK/aim
   components name, or drop those components.
4. **Animations:** sample each Blender action at 60 Hz and write one `Animation` per clip into `combined.animations`, one track
   per bone with orientation (quaternion, hemisphere-fixed), position and scale-shear curves, `DaK32fC32f` for animated tracks,
   constants for static ones; `ExtendedData bbAnimMin/Max` from the clip's bounds. Compress (optional) with decimation
   rather than a BitKnit encoder: size matters little (the biggest vanilla file is 6.5 MB).
5. **Textures:** `tools/d2r_texture.py` (existing) produces the `.texture` files.
6. **Unit plumbing** (`tools/hd_monsters.py`, existing): `build_unit` deep-copies a donor unit JSON, re-keys parts and remaps
   components. For a real model the donor copy is replaced by a template unit whose `ModelDefinitionComponent`s name the new
   `.model` files (one entity per `(part, variant)`), whose skeleton component (on `entity_root`) names the new skeleton and whose
   `state_machine_filename` is a state machine from `adapt_state_machine` binding the new clip names to the modes
   (0 DT, 1 NU, 2 WL, 3 GH, 4 A1, 5 A2, 6 BL, 7 SC, 8-11 S1-S4, 12 DD, 13 KB, 14 SQ, 15 RN) per weapon class. `variant_table`
   gives tint per `TransLvl`; `monsters.json` maps the monstats id to the new unit. The existing `score`/`choose` donor logic is
   skipped for ids that have real art.
7. **Missiles/objects:** the same `.model` writer; objects use the static layout (20 B) and no skeleton, one `Model` entry. The
   unit/object JSON side is separate and not researched here (`docs/hd-effects.md` covers VFX); missiles are VFX JSON entities
   that point at a `.model` plus particles.

### 4.2 Code vs artist

| Needs code (can be done now or soon) | Needs an artist |
|---|---|
| Blender exporter: mesh -> vertex arrays, bone bindings, quantisation, LOD decimation, `.model`/`.skeleton` via `Writer` | Meshes, UVs, topology, skinning weights, per-LOD review |
| Animation sampler -> Granny curves (`DaK32fC32f`, constants), clip naming, bounds | The animations themselves (walk, attack, death, ...) and their timings |
| Unit JSON + variant + state machine generation (extends `hd_monsters.py`) | Choosing which Median look needs a real model (priorities in `hd-monsters.md` section 4) |
| Texture pipeline (`d2r_texture.py`), mod packaging, in-game check scripts (`game_checks`) | Albedo/ORM/normal/mask textures |
| Validation: `d2r_model.py check` re-reads each exported file; in-game `mspawn` smoke test | Final look-and-feel approval |

A first end-to-end proof that needs no artist: take a vanilla part, rewrite it unchanged by `Writer` (done, 60 files), then
transform vertices (done: scale), then swap in a procedurally built mesh (not yet done) as the next step before an artist is
involved.

### 4.3 Engine limits and unknowns

Known from vanilla data (upper bounds seen, so safe): 263 bones per skeleton; 174 bone bindings in one mesh; 353k vertices in a
mesh; 141 meshes per file; 1,443 clips and 6.5 MB per `.animations`; 9.2 MB per `.model`. Skinned parts stay far below these
(median 452 vertices per mesh). 4 influences per vertex (uint8 weights) is fixed by the vertex layout.

Unknown, to test in game when a first real mesh exists: the asset-cache budget for HD assets (no setting found), whether a model
with *more* bones than the unit's current skeleton works, whether the engine requires specific ExtendedData keys (`modelType`
256 appears on 422 of 566 sampled models, absent on 144; `lodLevel`; `bbAnimMin/Max`), whether materials may reference new
Maya-less shaders (clone a vanilla material), whether uncompressed 6 MB animation files hurt load time (probably not: the
loader decompresses to memory anyway), and the exact tangent/handedness conventions for hand-built meshes (copy a vanilla
mesh's values, compare a normal-mapped result against a vanilla part).

## 5. Next steps (ordered)

1. Procedural mesh swap test: write a new `torso_lit_lodN.model` with generated vertices (a box), same skeleton/bone bindings;
   confirm it deforms with the vanilla animations in game.
2. Extend `Writer` with a `Mesh` builder (vertex quantisation, bone binding OBBs) and curve writer for `.animations`;
   check against vanilla by decode -> re-encode -> compare evaluated curves (tolerance).
3. Blender exporter that calls that builder; exporter test on a simple artist test rig (stick figure, 3 clips) end to end
   through `hd_monsters.py`.
4. Only then commission real art (project rule: custom HD art comes last).
- Size limit: the forge's proxy drops an LFS upload that takes more than about two minutes (~20 MB from the game PC). Store a larger
  file xz-compressed (`<name>.xz`) and decompress it in the build step that reads it; push big batches with
  `git -c lfs.concurrenttransfers=1 push`.

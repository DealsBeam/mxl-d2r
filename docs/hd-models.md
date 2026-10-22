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

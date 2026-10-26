# Wereowl 3D model (generated)

This file used to be `assets/hd/wereowl/README.md`; `assets/` is not in git any more (the asset pack, docs/setup.md step 4),
so the README is tracked here. Paths below are relative to `assets/hd/wereowl/` of the unpacked pack, except `source/cams.json`,
which is tracked in git as `tools/sprite3d/data/wereowl/cams.json` (copy it to `source/cams.json` for step 3).

Used by `tools/wereowl_models.py` (a `tools/build.py` step) for the Druid's Wereowl form and the Corrupted Wereowl enemy
(docs/hd-models.md section 3b). Off switch: `MXL_NO_3D=1` or `tools/wereowl_models.py --off` (goatman1 stand-ins).

| File | Role |
|---|---|
| `mesh_lods.npz` | build input: LOD 0-4 meshes (positions, normals, UVs, faces) in the generated owl's pose: a QuadriFlow retopology of the high-poly, 38,124 / 17,002 / 9,836 / 5,298 / 2,858 triangles (the werebear's 38,968 / 17,383 / 10,050 / 5,414 and one more step); the Corrupted Wereowl is rigged from it by `tools/sprite3d/rig_fit.py` (profile goatman1) |
| `druid_lods.npz` | build input: the same meshes carried segment by segment onto the werebear skeleton's bind pose (`owl_pose.py`), with Blender bone-heat skin weights (`bone_idx`, `weights` (N, 4), `bones` per LOD); the Druid form. Without it the build falls back to rig_fit's capsule weights |
| `albedo.png` | build input: 2048 albedo, `source/albedo_raw.png` cleaned by `albedo_clean.py` (written as a BC3 1024 `.texture`) |
| `normal.png`, `orm.png` | build inputs: 2048 tangent-space normal map (OpenGL layout, +y up the image) and ORM (R = baked occlusion, G = roughness 170, B = 0) baked from the high-poly onto `mesh_lods.npz` LOD 0; `wereowl_models.py` packs them to D2R's BC3 layouts at 1024 (`MXL_NRM_GREEN=dx` default) |
| `source/hy_mv_4view.glb` | source: Hunyuan3D-2mv mesh from Median sprite directions d4-d7 |
| `source/tex.glb` | source: the 25k-triangle textured mesh (camera charts, xatlas), what `mesh_lods.npz` / `albedo_raw.png` come from |
| `source/albedo_raw.png`, `source/albedo_coverage.png` | source: the albedo as `project_texture.py` baked it from the 16 sprite directions onto `mesh_uv.npz`, and its coverage map (255 sampled, 128 mapped but unseen, 0 empty atlas) |
| `source/retopo.npz` | source: LOD 0 as `owl_retopo.py` made it (positions and faces, no UVs) |
| `source/mesh_uv.npz` | source: `retopo.npz` with the camera-chart UVs of `project_texture.py` (vertices split along the charts, normals): LOD 0 of `mesh_lods.npz`, the bake target |
| `source/views_x4.zip` (in the pack), `tools/sprite3d/data/wereowl/cams.json` (in git) | source: the 16 upscaled sprite directions (Median art) and the fitted cameras `project_texture.py` projects |

Made on the game PC with the tools in `tools/sprite3d/` (generation, camera fit, projection, LODs; PRs #95, #103, #107, #122; lane
wereowl-quality; lane wereowl-mesh: the retopology, bakes and fit). 3D venv `D:\mxl-3d\venv`, Blender 4.5 `D:\mxl-3d\blender`
(`B` below); the vanilla werebear `bones.json` comes from `skel_info.py <bear.skeleton> bones.json` (the game's file, not in the repo).
In order:

1. High-poly bake source (voxel remesh of `source/hy_mv_4view.glb` into one closed surface, 6.6 tall, 3.3M triangles, 59 MB so not
   stored): `$B -b -P tools/sprite3d/owl_hi.py -- source/hy_mv_4view.glb hic.npz --voxel 0.012`
2. Game mesh (QuadriFlow to the werebear's LOD 0 triangle count, snapped onto the high-poly):
   `$B -b -P tools/sprite3d/owl_retopo.py -- hic.npz source/retopo.npz --tris 41900` (the quad count is erratic; the script keeps the closest result at or under 38,968; this gave 38,124)
3. UVs and albedo (the 16 sprite views projected onto camera charts, 197 charts):
   `py -3 -I tools/sprite3d/project_texture.py source/retopo.npz source/cams.json views_x4/ pt/ --size 2048` (`views_x4/` = `source/views_x4.zip`
   unzipped) writes `pt/albedo.png`, `pt/coverage.png`, `pt/mesh_uv.npz`; those are `source/albedo_raw.png`, `source/albedo_coverage.png`, `source/mesh_uv.npz`.
4. `albedo.png`: `py -3 -I tools/sprite3d/albedo_clean.py source/albedo_raw.png source/albedo_coverage.png albedo.png --median 100`
   (near-black and unseen texels inpainted, light denoise and bilateral filter, median luma of the mapped texels set to 100).
5. Normal map and occlusion (Cycles bake, the game mesh pushed out 0.04 along its normals, rays 0.12 deep, AO distance 0.3):
   `$B -b -P tools/sprite3d/owl_bake.py -- hic.npz source/mesh_uv.npz pt/b --extrude 0.04 --ray 0.12 --ao-dist 0.3`, then
   `py -3 -I tools/sprite3d/owl_maps.py pt/b . ` (writes `normal.png`, `orm.png`).
6. LODs 1-4 (texture-aware quadric collapse at the werebear's ratios 0.446 / 0.258 / 0.139, and 0.075):
   `py -3 -I tools/sprite3d/owl_lods.py source/mesh_uv.npz mesh_lods.npz`
7. `druid_lods.npz` (the owl carried segment by segment onto the bear skeleton; the OWL joint table is in `owl_pose.py`):
   `py -3 -I tools/sprite3d/owl_pose.py bones bones.json owl_bones.json`;
   `$B -b -P tools/sprite3d/owl_weights.py -- mesh_lods.npz owl_bones.json weighted.npz --voxel 0.12`;
   `py -3 -I tools/sprite3d/owl_pose.py fit weighted.npz owl_bones.json bones.json druid_lods.npz --png fit` (front and side overlay of the result on the bear's bones).

Rebuilding the build inputs from the sources needs the 3D environment in `D:\mxl-3d` (docs/hd-models.md); building the
mod only needs the build inputs above (`mesh_lods.npz`, `druid_lods.npz`, `albedo.png`, `normal.png`, `orm.png`).

Known: no fur relief beyond what the bake carries (no extra fur normal overlay, no Real-ESRGAN pass: step 4 of the lane was not done);
the owl is still a different shape than the werebear (wide ruff head, short legs): the fit stretches its segments onto the bear's
joints, so the arms leave the shoulders but the head is larger and the chest deeper than a bear's; the albedo is projected from small
sprites (soft, no fine fur detail, the back of the owl is only seen in the sprite directions).

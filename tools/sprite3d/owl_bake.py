"""Blender (headless): bake the Wereowl's high-poly detail onto the game mesh (tangent-space normal map, ambient occlusion), and re-project
an albedo from one UV layout to another.

    blender -b -P tools/sprite3d/owl_bake.py -- <hi.npz> <lo.npz> <out prefix> [--size 2048] [--extrude 0.12] [--ray 0.35]
            [--ao-dist 0.4] [--ao-samples 24] [--what normal,ao] [--cpu]
    blender -b -P tools/sprite3d/owl_bake.py -- --albedo <src_lo.npz> <src.png>[,<src2.png>...] <lo.npz> <out prefix> [--size 2048]

<hi.npz>: `pos`, `tris` of the high-poly (tools/sprite3d/owl_hi.py: hy_mv_4view.glb in the mesh's model space). <lo.npz>: `pos`, `nrm`,
`uv`, `tris` of the game mesh (LOD 0, vertices split along UV seams, its vertex normals are the ones the engine lights with: they are
set as custom split normals, so the baked normals are relative to the shading the game draws). Both in the same pose and axes.
Writes `<prefix>_normal.npy` ((S,S,3) float32, image row order top first, OpenGL tangent space: +x along u, +y along v UP the
image, z out; `owl_maps.py` turns it into D2R's layout) and `<prefix>_ao.npy` ((S,S) float32, 1 = open).
`--extrude` / `--ray`: the bake casts from the low mesh pushed out along its normals by `extrude` and looks `ray` deep (model units; the
Wereowl is 6.6 tall and the low mesh is within 0.05 of the high one). The AO is the high-poly's own occlusion (Ambient Occlusion
node, local only, distance `--ao-dist`) baked as emission onto the low mesh's UVs.
`--albedo`: the source mesh (its own `uv`) textured with the given images (one npy-less PNG per output, e.g. the raw albedo and its
coverage map) is baked to `<prefix>_albedo<k>.npy` ((S,S,3) uint8-range float, top first) on the target's UVs (emission bake, same cage).
"""
import os
import sys

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bl_common as bc  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]


def opt(name, default):
    return type(default)(argv[argv.index(name) + 1]) if name in argv else default


SIZE = opt("--size", 2048)
EXTRUDE, RAY = opt("--extrude", 0.12), opt("--ray", 0.35)
bpy.ops.wm.read_factory_settings(use_empty=True)
dev = bc.cycles(samples=1, gpu="--cpu" not in argv)
print("cycles device", dev)


def load_lo(path):
    d = np.load(path)
    return bc.mesh_object("lo", d["pos"], d["tris"], d["uv"], d["nrm"] if "nrm" in d.files else None)


def save(arr, path):
    np.save(path, np.flipud(arr).astype(np.float32))
    print("wrote", path, arr.shape)


if "--albedo" in argv:
    i = argv.index("--albedo")
    src_npz, imgs, lo_npz, prefix = argv[i + 1], argv[i + 2].split(","), argv[i + 3], argv[i + 4]
    s = np.load(src_npz)
    lo = load_lo(lo_npz)
    for k, png in enumerate(imgs):
        src = bc.mesh_object("src%d" % k, s["pos"], s["tris"], s["uv"], s["nrm"] if "nrm" in s.files else None)
        image = bpy.data.images.load(png)
        image.colorspace_settings.name = "Non-Color"
        image.alpha_mode = "NONE"

        def tex(nodes, image=image):
            n = nodes.new("ShaderNodeTexImage")
            n.image, n.interpolation = image, "Linear"
            return n.outputs["Color"]

        src.data.materials.append(bc.emission_material("src%d_mat" % k, tex))
        img = bc.image_target(lo, "alb%d" % k, SIZE)
        bc.bake("EMIT", src, lo, EXTRUDE, RAY)
        save(bc.image_array(img)[..., :3] * 255.0, "%s_albedo%d.npy" % (prefix, k) if k else "%s_albedo.npy" % prefix)
        bpy.data.objects.remove(src)
    sys.exit(0)

hi_npz, lo_npz, prefix = argv[:3]
what = opt("--what", "normal,ao").split(",")
d = np.load(hi_npz)
hi = bc.mesh_object("hi", d["pos"], d["tris"])
lo = load_lo(lo_npz)
print("hi %d tris, lo %d tris" % (len(d["tris"]), len(lo.data.polygons)))

if "normal" in what:
    img = bc.image_target(lo, "normal", SIZE, color=(0.5, 0.5, 1, 1))
    bc.bake("NORMAL", hi, lo, EXTRUDE, RAY)
    save(bc.image_array(img)[..., :3], prefix + "_normal.npy")

if "ao" in what:
    bpy.context.scene.cycles.samples = opt("--ao-samples", 24)

    def ao(nodes):
        n = nodes.new("ShaderNodeAmbientOcclusion")
        n.samples, n.inside, n.only_local = 16, False, True
        n.inputs["Distance"].default_value = opt("--ao-dist", 0.4)
        return n.outputs["Color"]

    hi.data.materials.append(bc.emission_material("ao", ao))
    img = bc.image_target(lo, "ao", SIZE, color=(1, 1, 1, 1))
    bc.bake("EMIT", hi, lo, EXTRUDE, RAY)
    save(bc.image_array(img)[..., 0], prefix + "_ao.npy")

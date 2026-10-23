"""Blender (headless): the Wereowl high-poly as a clean, closed bake source (`hi.npz`: pos, tris) in the game mesh's model space.

    blender -b -P tools/sprite3d/owl_hi.py -- <hy_mv_4view.glb> <out.npz> [--voxel 0.012] [--height 6.6]

The generated mesh (Hunyuan3D, ~800k triangles) has 40 loose bodies, internal shells and holes: bakes from it hit the inside of
feathers (black AO patches, wrong normals). Steps: the glTF axes are kept (y up, +z front); the mesh is placed like
tools/sprite3d/mesh_prep.py does (feet on y = 0, centred in x and z, scaled to `--height`); a voxel remesh (size `--voxel`) unions every
body into one closed surface without the inner shells. Detail finer than about two voxels is lost (0.012 = 0.2% of the height).
"""
import sys

import bpy
import numpy as np

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
import bl_common as bc  # noqa: E402

a = sys.argv[sys.argv.index("--") + 1:]
opt = lambda n, d: type(d)(a[a.index(n) + 1]) if n in a else d
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=a[0])
ob = next(o for o in bpy.context.scene.objects if o.type == "MESH")
me = ob.data
v = np.empty(len(me.vertices) * 3, np.float32)
me.vertices.foreach_get("co", v)
v = v.reshape(-1, 3).astype(np.float64)
# Blender's importer turned glTF (x, y, z) into (x, -z, y): undo it
g = np.stack([v[:, 0], v[:, 2], -v[:, 1]], 1)
tris = np.empty(len(me.polygons) * 3, np.int32)
me.polygons.foreach_get("vertices", tris)
tris = tris.reshape(-1, 3)
g[:, 1] -= g[:, 1].min()
g[:, 0] -= (g[:, 0].min() + g[:, 0].max()) / 2
g[:, 2] -= (g[:, 2].min() + g[:, 2].max()) / 2
g *= opt("--height", 6.6) / g[:, 1].max()
bpy.data.objects.remove(ob)
hi = bc.mesh_object("hi", g, tris)
hi.data.remesh_voxel_size = opt("--voxel", 0.012)
bc.select_only(hi)
bpy.ops.object.voxel_remesh()
pos, t = bc.arrays(hi)
print("voxel remesh: %d vertices, %d triangles" % (len(pos), len(t)))
np.savez(a[1], pos=pos.astype(np.float32), tris=t.astype(np.int32))

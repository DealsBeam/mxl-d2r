"""Blender (headless): QuadriFlow retopology of the Wereowl high-poly into an even game mesh with a triangle budget.

    blender -b -P tools/sprite3d/owl_retopo.py -- <hi.npz> <out.npz> [--tris 38968] [--proxy 300000] [--seed 0]

<hi.npz>: owl_hi.py's clean closed surface. The high-poly is collapse-decimated to `--proxy` triangles (quadriflow wants a modest
input and the proxy keeps the silhouette), QuadriFlow makes a quad mesh of `--tris`/2 faces (even, edge-flow following the
curvature, loops around limbs), every vertex is snapped back onto the high-poly surface (Shrinkwrap, nearest surface point) and
smoothed normals are written. Writes `pos`, `tris` (quads split into triangles). UVs and the weights come in later steps.
"""
import os
import sys

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bl_common as bc  # noqa: E402

a = sys.argv[sys.argv.index("--") + 1:]
opt = lambda n, d: type(d)(a[a.index(n) + 1]) if n in a else d
bpy.ops.wm.read_factory_settings(use_empty=True)
d = np.load(a[0])
hi = bc.mesh_object("hi", d["pos"], d["tris"])
budget = opt("--tris", 38968)
target = budget // 2
best = None
for attempt in range(5):  # QuadriFlow's face count is only roughly the target: retry with a corrected target, keep the closest result under the budget
    ob = bc.mesh_object("proxy", d["pos"], d["tris"])
    bc.select_only(ob)
    m = ob.modifiers.new("dec", "DECIMATE")
    m.ratio = min(1.0, opt("--proxy", 300000) / len(d["tris"]))
    bpy.ops.object.modifier_apply(modifier="dec")
    bpy.ops.object.quadriflow_remesh(use_mesh_symmetry=False, use_preserve_sharp=False, use_preserve_boundary=False,
                                     preserve_attributes=False, smooth_normals=True, mode="FACES", target_faces=target,
                                     seed=opt("--seed", 0))
    got = 2 * len(ob.data.polygons)
    print("attempt %d: target %d quads -> %d quads (%d triangles, budget %d)" % (attempt, target, got // 2, got, budget))
    if got <= budget and (best is None or got > 2 * len(best.data.polygons)):
        if best is not None:
            bpy.data.objects.remove(best)
        best = ob
    else:
        bpy.data.objects.remove(ob)
    if best is not None and 2 * len(best.data.polygons) >= 0.97 * budget:
        break
    target = int(target * budget / got * (0.99 if got > budget else 1.0))
proxy = best
sw = proxy.modifiers.new("snap", "SHRINKWRAP")
sw.target, sw.wrap_method, sw.wrap_mode = hi, "NEAREST_SURFACEPOINT", "ON_SURFACE"
bpy.ops.object.modifier_apply(modifier="snap")
pos, tris = bc.arrays(proxy)
print("result: %d vertices, %d triangles" % (len(pos), len(tris)))
np.savez(a[1], pos=pos.astype(np.float32), tris=tris.astype(np.int32))

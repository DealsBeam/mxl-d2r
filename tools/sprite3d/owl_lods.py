"""LOD 1-4 of the Wereowl game mesh by texture-aware quadric edge collapse (3D venv: numpy, pymeshlab).

    owl_lods.py <lod0.npz> <out mesh_lods.npz> [--ratios 0.446,0.258,0.139,0.075]

<lod0.npz>: `pos`, `uv`, `tris` (vertices split along UV seams; `nrm` is recomputed). The ratios are the vanilla werebear's: its LOD 1-3
hold 17,383 / 10,050 / 5,414 triangles against 38,968 for LOD 0 (every mesh of the unit counted, the duplicate proxy shells not;
docs/hd-models.md 3d); LOD 4 has no vanilla template (the bear has four LODs, the build reuses LOD 3's), 0.075 is half of LOD 3.
The collapse runs on the position-welded mesh with per-corner UVs (pymeshlab keeps the charts: seams are UV discontinuities, so
they can collapse like any edge), and the result is split into vertices per (position, uv) again. Normals are the area-weighted
smooth normals of the welded mesh. Writes `pos`, `nrm`, `uv`, `tris` for LOD 0 and `*_1` .. `*_4` for the others: the layout of
`assets/hd/wereowl/mesh_lods.npz`.
"""
import os
import sys
import tempfile

import numpy as np
import pymeshlab
from PIL import Image

a = sys.argv[1:]
ratios = [float(x) for x in a[a.index("--ratios") + 1].split(",")] if "--ratios" in a else [0.446, 0.258, 0.139, 0.075]
d = np.load(a[0])
pos, uv, tris = d["pos"].astype(np.float64), d["uv"].astype(np.float64), d["tris"].astype(np.int64)


def weld(pos, tris):
    key = np.round(pos * 1e5).astype(np.int64)
    _u, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    return pos[first], inv.ravel()[tris]


def split(wpos, wtris, wuv):
    """Vertices per (welded vertex, uv) corner; normals from the welded mesh."""
    fn = np.cross(wpos[wtris[:, 1]] - wpos[wtris[:, 0]], wpos[wtris[:, 2]] - wpos[wtris[:, 0]])
    vn = np.zeros_like(wpos)
    for k in range(3):
        np.add.at(vn, wtris[:, k], fn)
    vn /= np.maximum(np.linalg.norm(vn, axis=1, keepdims=True), 1e-12)
    corner = np.concatenate([wtris.reshape(-1, 1).astype(np.float64), np.round(wuv.reshape(-1, 2) * 2 ** 14)], 1)
    _u, first, inv = np.unique(corner, axis=0, return_index=True, return_inverse=True)
    vi = wtris.reshape(-1)[first]
    return wpos[vi], vn[vi], wuv.reshape(-1, 2)[first], inv.ravel().reshape(-1, 3)


wpos, wtris = weld(pos, tris)
cuv = uv[tris]  # (F, 3, 2) per corner
out = {}
spos, snrm, suv, stris = split(wpos, wtris, cuv)
out.update(pos=spos.astype(np.float32), nrm=snrm.astype(np.float32), uv=suv.astype(np.float32), tris=stris.astype(np.int32))
print("lod 0: %d vertices, %d triangles (%d welded vertices)" % (len(spos), len(stris), len(wpos)))
tmp = tempfile.mkdtemp()
Image.new("RGB", (4, 4)).save(os.path.join(tmp, "t.png"))
open(os.path.join(tmp, "m.mtl"), "w").write("newmtl m\nKd 1 1 1\nmap_Kd t.png\n")
rows = ["mtllib m.mtl", "usemtl m"]  # pymeshlab only fills a face's texture id through a loaded file
rows += ["v %.6f %.6f %.6f" % tuple(q) for q in wpos]
rows += ["vt %.6f %.6f" % (u, 1 - v) for u, v in cuv.reshape(-1, 2)]
rows += ["f %d/%d %d/%d %d/%d" % (t[0] + 1, 3 * i + 1, t[1] + 1, 3 * i + 2, t[2] + 1, 3 * i + 3) for i, t in enumerate(wtris)]
open(os.path.join(tmp, "in.obj"), "w").write("\n".join(rows) + "\n")
for k, r in enumerate(ratios, 1):
    ms = pymeshlab.MeshSet()
    ms.load_new_mesh(os.path.join(tmp, "in.obj"))
    ms.meshing_decimation_quadric_edge_collapse_with_texture(targetfacenum=int(round(len(tris) * r)), preserveboundary=True, boundaryweight=2.0,
                                                             optimalplacement=True, preservenormal=True, planarquadric=True, extratcoordw=1.0)
    m = ms.current_mesh()
    p, f, w = m.vertex_matrix(), m.face_matrix(), m.wedge_tex_coord_matrix().reshape(-1, 3, 2).copy()
    w[..., 1] = 1 - w[..., 1]
    sp, sn, su, st = split(p, f, w)
    sfx = "_%d" % k
    out["pos" + sfx], out["nrm" + sfx], out["uv" + sfx], out["tris" + sfx] = sp.astype(np.float32), sn.astype(np.float32), su.astype(np.float32), st.astype(np.int32)
    print("lod %d: %d vertices, %d triangles (target %d)" % (k, len(sp), len(st), round(len(tris) * r)))
np.savez(a[1], **out)

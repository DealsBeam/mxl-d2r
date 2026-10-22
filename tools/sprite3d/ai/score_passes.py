"""Score generated meshes against all 16 sprite silhouettes with the known direction ring.

  score_passes.py <frames dir> <mesh.glb[:offset_deg]> ...

Direction angle (degrees, d4 = 0 = front, +90 = d5 'left'): ring order 0,9,5,10,1,11,6,12,2,13,7,14,3,15,4,8, step 22.5.
A mesh generated from a quad whose first view is direction d has its own front at angle ANG[d]; the offset arg is that angle.
"""
import sys
import numpy as np
sys.path.insert(0, "D:/mxl-3d/scripts")
from silfit import load_mesh_points, sprite_mask, project, raster, prep, iou

RING = [0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15, 4, 8]
ANG = {d: ((RING.index(d) - RING.index(4)) % 16) * 22.5 for d in range(16)}
EL = 10.0


def score(path, offset, fdir, pts=None, inputs=()):
    pts = load_mesh_points(path, n=200000) if pts is None else pts
    res = {}
    for d in range(16):
        m = sprite_mask(f"{fdir}/d{d}_f0.png")
        area, c = prep(m)
        res[d] = iou(raster(project(pts, ANG[d] - offset, EL), area, c, m.shape), m)
    return res


if __name__ == "__main__":
    fdir = sys.argv[1]
    for arg in sys.argv[2:]:
        path, off = arg.rsplit(":", 1) if arg.count(":") > 1 else (arg, "0")
        r = score(path, float(off or 0), fdir)
        v = np.array(list(r.values()))
        print(f"{path.split('/')[-1]:28} offset {off or 0:>5}  mean IoU {v.mean():.3f}  min {v.min():.3f}  "
              + " ".join(f"d{d}:{r[d]:.2f}" for d in range(16)), flush=True)

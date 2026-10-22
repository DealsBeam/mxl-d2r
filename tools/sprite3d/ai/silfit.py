"""Fit the sprite camera: for each of the 16 sprite directions find the azimuth (and a shared elevation) at which an
orthographic projection of a generated mesh best matches the sprite silhouette (area/centroid-normalised IoU).

  silfit.py <mesh.glb> <frames dir (d<N>_f0.png)> [--elev E ...]
"""
import sys, glob
import numpy as np, trimesh
from PIL import Image
from scipy import ndimage

R = 128  # mask resolution


def load_mesh_points(path, n=300000, seed=0):
    sc = trimesh.load(path, force="mesh")
    pts, _ = trimesh.sample.sample_surface(sc, n, seed=seed)
    pts = pts - pts.mean(0)
    return pts


def sprite_mask(path):
    a = np.array(Image.open(path).convert("RGBA"))[..., 3] > 40
    return a


def project(pts, az, el):
    a, e = np.radians(az), np.radians(el)
    r = np.array([np.cos(a), 0, -np.sin(a)])
    u = np.array([-np.sin(a) * np.sin(e), np.cos(e), -np.cos(a) * np.sin(e)])
    return np.stack([pts @ r, pts @ u], 1)


def raster(xy, ref_area, ref_c, ref_shape):
    # scale so that filled area matches ref_area: iterate once on a coarse estimate
    h, w = ref_shape
    # initial scale from bbox, then refine by area
    lo, hi = xy.min(0), xy.max(0)
    s = (h * 0.9) / max(hi[1] - lo[1], 1e-6)
    for _ in range(3):
        m = splat(xy, s, ref_c, ref_shape)
        area = m.sum()
        if area == 0:
            break
        s *= (ref_area / area) ** 0.5
    return splat(xy, s, ref_c, ref_shape)


def splat(xy, s, center, shape):
    h, w = shape
    c = xy.mean(0)
    px = np.round((xy[:, 0] - c[0]) * s + center[0]).astype(int)
    py = np.round(-(xy[:, 1] - c[1]) * s + center[1]).astype(int)
    ok = (px >= 0) & (px < w) & (py >= 0) & (py < h)
    m = np.zeros(shape, bool)
    m[py[ok], px[ok]] = True
    m = ndimage.binary_closing(m, iterations=2)
    return ndimage.binary_fill_holes(m)


def iou(a, b):
    return (a & b).sum() / max((a | b).sum(), 1)


def prep(mask):
    ys, xs = np.nonzero(mask)
    c = (xs.mean(), ys.mean())
    return mask.sum(), c


if __name__ == "__main__":
    path, fdir = sys.argv[1], sys.argv[2]
    elevs = [float(x) for x in sys.argv[sys.argv.index("--elev") + 1:]] if "--elev" in sys.argv else [20, 25, 30, 35, 40]
    pts = load_mesh_points(path)
    masks = {d: sprite_mask(f"{fdir}/d{d}_f0.png") for d in range(16)}
    azs = np.arange(0, 360, 5)
    best_el, tot_best = None, -1
    results = {}
    for el in elevs:
        per = {}
        for d, m in masks.items():
            area, c = prep(m)
            sc = [iou(raster(project(pts, az, el), area, c, m.shape), m) for az in azs]
            k = int(np.argmax(sc))
            per[d] = (azs[k], sc[k])
        mean = np.mean([v[1] for v in per.values()])
        print(f"elev {el}: mean best IoU {mean:.3f}")
        results[el] = per
        if mean > tot_best:
            tot_best, best_el = mean, el
    print("best elevation", best_el)
    for d, (az, s) in results[best_el].items():
        print(f"  d{d:2}: az {az:3} IoU {s:.3f}")

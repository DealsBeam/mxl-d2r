"""Bake results (`owl_bake.py` .npy arrays) -> the Wereowl's `normal.png` and `orm.png` build inputs (3D venv: numpy, PIL).

    owl_maps.py <bake prefix> <out dir> [--ao-strength 0.6] [--rough 170] [--size 2048]

normal.png: the tangent-space normal in the usual OpenGL layout (RGB = xyz * 0.5 + 0.5, +y = up the image), renormalised.
orm.png: R = occlusion, G = roughness, B = metal (0), as the engine's ORM map (`tools/wereowl_models.py` packs both into D2R's BC3
layouts: normal = (255, y, 0, x) = DXT5nm, ORM as is). The baked AO is pulled toward 1 by `--ao-strength` (1 = as baked): the
vanilla werebear's occlusion channel averages 220 / 255, the raw bake 170. Roughness is flat `--rough` (the vanilla bear's mean is
170, metal 0).
"""
import sys

import numpy as np
from PIL import Image

a = sys.argv[1:]
prefix, out = a[0], a[1]
opt = lambda n, d: type(d)(a[a.index(n) + 1]) if n in a else d
n = np.load(prefix + "_normal.npy") * 2.0 - 1.0
n /= np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-6)
ao = np.load(prefix + "_ao.npy")
occ = 1.0 - opt("--ao-strength", 0.6) * (1.0 - np.clip(ao, 0, 1))
size = opt("--size", n.shape[0])
rgb = np.clip(np.rint((n * 0.5 + 0.5) * 255), 0, 255).astype(np.uint8)
orm = np.dstack([np.rint(occ * 255), np.full(ao.shape, opt("--rough", 170)), np.zeros(ao.shape)]).astype(np.uint8)
for arr, name in ((rgb, "normal.png"), (orm, "orm.png")):
    im = Image.fromarray(arr)
    if size != im.size[0]:
        im = im.resize((size, size), Image.LANCZOS)
    im.save("%s/%s" % (out, name), optimize=True)
    print("wrote", "%s/%s" % (out, name), im.size, "mean", np.asarray(im).reshape(-1, 3).mean(0).round(1))

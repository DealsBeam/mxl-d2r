"""The Wereowl meshes refitted to a skeleton's bind pose (the input of owl_weights.py).

    py -3 tools/sprite3d/owl_refit.py <skeleton file> <bones.json> <mesh_lods.npz> <out.npz> [--profile bear]
    blender -b -P tools/sprite3d/owl_weights.py -- <out.npz> <bones.json> assets/hd/wereowl/druid_lods.npz

rig_fit.py's carry (per-joint affine from the generated owl pose into the skeleton's bind pose) turns every LOD of `mesh_lods.npz`
into bind space (`pos`, `nrm`, `uv`, `tris`, and the `_1`.. LODs); the capsule weights rig_fit also computes are dropped, owl_weights.py
(Blender bone heat) makes the real ones. `bones.json` is `skel_info.py <skeleton> bones.json`'s output, which owl_weights.py reads.
"""
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rig_fit  # noqa: E402
import skel_info  # noqa: E402


def main():
    a = sys.argv[1:]
    if len(a) < 4:
        print(__doc__)
        return 1
    skel, bonesjson, src, dst = a[:4]
    prof = rig_fit.PROFILES[a[a.index("--profile") + 1] if "--profile" in a else "bear"]
    bones = skel_info.bones(skel)
    json.dump([{"name": b["name"], "parent": b["parent"], "pos": [float(x) for x in b["pos"]]} for b in bones], open(bonesjson, "w"))
    d = np.load(src)
    out = {}
    for k in d.files:
        if k.startswith("pos"):
            sfx = k[3:]
            pos, nrm, *_ = rig_fit.fit(prof, bones, d["pos" + sfx], d["nrm" + sfx], d["tris" + sfx])
            out["pos" + sfx], out["nrm" + sfx] = pos, nrm
            out["uv" + sfx], out["tris" + sfx] = d["uv" + sfx], d["tris" + sfx]
            print("lod%s: %d vertices, box %s .. %s" % (sfx or "_0", len(pos), pos.min(0).round(2), pos.max(0).round(2)))
    np.savez(dst, **out)
    print("wrote", dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())

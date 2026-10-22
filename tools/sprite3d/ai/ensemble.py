"""Fuse several Hunyuan passes: de-rotate each to the d4-front frame, normalise, voxel-vote, re-mesh.

  ensemble.py <out.glb> <mesh.glb:offset_deg> ...   (offset = ANG of the quad's first view, see score_passes.py)
"""
import sys
import numpy as np, trimesh
from scipy import ndimage
from skimage import measure

N = 200


def norm(m, deg):
    m = m.copy()
    # rotate about the up axis (y) so that this pass's view-0 matches the d4-front frame; sign checked against pass 0
    m.apply_transform(trimesh.transformations.rotation_matrix(np.radians(-deg), [0, 1, 0]))
    b = m.bounds
    c = (b[0] + b[1]) / 2
    m.apply_translation(-c)
    m.apply_scale(1.0 / (b[1] - b[0]).max())
    return m


def occupancy(m):
    v = m.voxelized(1.0 / (N - 8)).fill()
    occ = np.zeros((N, N, N), bool)
    idx = v.sparse_indices - v.sparse_indices.min(0)
    c = (np.array(occ.shape) - (idx.max(0) + 1)) // 2
    idx = idx + c
    occ[tuple(idx.T)] = True
    return occ


if __name__ == "__main__":
    out = sys.argv[1]
    sign = 1.0
    occs = []
    for a in sys.argv[2:]:
        p, d = a.rsplit(":", 1)
        m = trimesh.load(p, force="mesh")
        o = occupancy(norm(m, sign * float(d)))
        occs.append(o)
        print(p, "voxels", int(o.sum()), flush=True)
    vote = np.mean(occs, axis=0)
    vote = ndimage.gaussian_filter(vote, 1.2)
    v, f, n, _ = measure.marching_cubes(vote, 0.5)
    mesh = trimesh.Trimesh(v, f)
    mesh.export(out)
    print("wrote", out, len(mesh.vertices), "verts")

"""Fit the Wereowl to the werebear skeleton bone segment by bone segment (3D venv: numpy; Blender for the weights step).

    py -3 tools/sprite3d/owl_pose.py bones <bear bones.json> <owl_bones.json>
    blender -b -P tools/sprite3d/owl_weights.py -- <mesh_lods.npz> <owl_bones.json> <weighted.npz> --voxel 0.12
    py -3 tools/sprite3d/owl_pose.py fit <weighted.npz> <owl_bones.json> <bear bones.json> <druid_lods.npz> [--png prefix]

The old fit (`rig_fit.py`, hand-placed joint roles, capsule weights, whole-body scale k) squashed the owl onto the bear: the owl is wider
and shorter in the torso, its arms leave the body under a huge head ruff. Here the mesh stays in its own pose while the bone heat
weights are made (owl_weights.py with `owl_bones.json`: the bear skeleton's bone names and hierarchy with the OWL's joint positions,
`OWL` below, read off the front and side views of the high-poly), and only then every bone segment is carried to the bear's:

    v' = sum_j w_j (B_j + R_j S_j (v - P_j)),   P_j, Q_j the owl bone's head and tail, B_j, C_j the bear bone's

R_j turns the owl segment's direction into the bear's, S_j stretches along it by |BC| / |PQ| (clipped) and across it by `GIRTH`
(per bone, default: that stretch to the power `GIRTH_EXP`), so the joints of the mesh sit on the bear's joints: shoulders at the
bear's shoulders, elbows, knees and hips where the animations bend. A bone without a segment of its own (pelvis, head) takes its
neighbour's turn and the `HEAD` scale. The skin weights stay the same, so the game skins the warped mesh with them (bone indices into
the 4-influence layout `d2r_mesh.replace_with_mesh` writes). Writes the weighted file with `pos` / `nrm` replaced by the bind-space mesh.
"""
import json
import sys
from pathlib import Path

import numpy as np

# the owl's joints (the mesh's own model space: y up, +z front, 6.6 tall, arms spread; left = +x), by the bear bone each one drives
OWL = {
    "pelvis": (0, 2.3, -0.4), "C_Spine_spine1": (0, 2.3, -0.4), "C_Spine_spine2": (0, 2.85, -0.4), "chest": (0, 3.45, -0.4),
    "C_Neck_neck1": (0, 3.95, -0.3), "head": (0, 4.45, 0.1),
    "{S}_Clavicle_Clavicle": (0.25, 3.7, -0.4), "{S}_Arm_UpperArm": (1.3, 3.5, -0.35), "{S}_Arm_ForeArm": (2.9, 3.15, -0.3),
    "{S}_Arm_Wrist": (4.0, 2.8, -0.3), "{S}_Arm_Wrist_END": (4.6, 2.7, -0.3),
    "{S}_Leg_Thigh": (1.05, 2.2, -0.3), "{S}_Leg_Calf": (1.1, 1.3, -0.1), "{S}_Leg_Foot": (1.1, 0.55, -0.15),
    "{S}_Leg_Ball": (1.2, 0.2, 0.4), "{S}_Leg_Toe_END": (1.25, 0.05, 1.2),
}
# the segment of a bone ends at its tail bone (owl_weights.py CHAIN); None: no segment of its own
TAIL = {"pelvis": None, "C_Spine_spine1": "C_Spine_spine2", "C_Spine_spine2": "chest", "chest": "C_Neck_neck1", "C_Neck_neck1": "head",
        "head": None, "{S}_Clavicle_Clavicle": "{S}_Arm_UpperArm", "{S}_Arm_UpperArm": "{S}_Arm_ForeArm",
        "{S}_Arm_ForeArm": "{S}_Arm_Wrist", "{S}_Arm_Wrist": "{S}_Arm_Wrist_END", "{S}_Leg_Thigh": "{S}_Leg_Calf",
        "{S}_Leg_Calf": "{S}_Leg_Foot", "{S}_Leg_Foot": "{S}_Leg_Ball", "{S}_Leg_Ball": "{S}_Leg_Toe_END"}
SMIN, SMAX = 0.5, 2.3  # clip of the stretch along a segment
GIRTH_EXP = 0.6  # across a segment the mesh grows by stretch ** GIRTH_EXP
GIRTH = {"{S}_Leg_Thigh": 0.85, "{S}_Leg_Calf": 0.85, "{S}_Leg_Foot": 0.9, "{S}_Leg_Ball": 0.9}  # fixed across-scale per bone (slimmer legs: bear legs stand 1.3 apart)
HEAD = 1.15  # uniform scale of the head (the owl's head and ruff are huge; the bear's neck is long, 1.15 keeps the ruff over the shoulders)


def names(table):
    """(bone name, table key, side) for every bone of a table; `{S}` keys exist for L and R."""
    return [(n.replace("{S}", s or ""), n, s) for n in table for s in (("L", "R") if "{S}" in n else (None,))]


def owl_pos(name, side):
    p = np.array(OWL[name], float)
    if side == "R":
        p[0] = -p[0]
    return p


def rot_between(a, b):
    a, b = a / np.linalg.norm(a), b / np.linalg.norm(b)
    v = np.cross(a, b)
    c = float(a @ b)
    if c < -0.9999:
        return -np.eye(3)
    k = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + k + k @ k / (1 + c)


def cmd_bones(a):
    bear = json.load(open(a[0]))
    by = {b["name"]: b for b in bear}
    for full, key, side in names(OWL):
        by[full]["pos"] = [float(x) for x in owl_pos(key, side)]
    json.dump(bear, open(a[1], "w"))
    print("wrote", a[1])


def segment_maps(owl, bear):
    """{bone: (P, B, M)}: the owl bone's head, the bear bone's head and the 3x3 map of its segment."""
    ob = {b["name"]: np.array(b["pos"], float) for b in owl}
    bb = {b["name"]: np.array(b["pos"], float) for b in bear}
    out, rot = {}, {}
    for full, key, side in names(TAIL):
        tail = TAIL[key]
        P, B = ob[full], bb[full]
        if tail is None:
            continue
        tf = tail.replace("{S}", side or "")
        dp, db = ob[tf] - P, bb[tf] - B
        d = dp / np.linalg.norm(dp)
        s = float(np.clip(np.linalg.norm(db) / np.linalg.norm(dp), SMIN, SMAX))
        g = GIRTH.get(key, s ** GIRTH_EXP)
        R = rot_between(dp, db)
        out[full] = (P, B, R @ (g * np.eye(3) + (s - g) * np.outer(d, d)))
        rot[full] = R
    # bones without a segment: the pelvis follows the first spine bone, the head turns like the neck
    for full, src, sc in (("pelvis", "C_Spine_spine1", None), ("head", "C_Neck_neck1", HEAD)):
        P, B = ob[full], bb[full]
        M = out[src][2] if sc is None else rot[src] * sc
        out[full] = (P, B, M)
    return out


def cmd_fit(a):
    d = dict(np.load(a[0], allow_pickle=True))
    owl, bear = json.load(open(a[1])), json.load(open(a[2]))
    maps = segment_maps(owl, bear)
    res = dict(d)
    for sfx in [k[3:] for k in d if k.startswith("pos")]:
        pos, nrm = d["pos" + sfx].astype(np.float64), d["nrm" + sfx].astype(np.float64)
        bidx, wts, bones = d["bone_idx" + sfx], d["weights" + sfx], [str(n) for n in d["bones" + sfx]]
        out, on = np.zeros_like(pos), np.zeros_like(nrm)
        for j, name in enumerate(bones):
            w = np.where(bidx == j, wts, 0.0).sum(1)
            if not w.any():
                continue
            P, B, M = maps[name]
            out += w[:, None] * (B + (pos - P) @ M.T)
            on += w[:, None] * (nrm @ np.linalg.inv(M))  # normals by the inverse transpose
        on /= np.linalg.norm(on, axis=1, keepdims=True) + 1e-9
        res["pos" + sfx], res["nrm" + sfx] = out.astype(np.float32), on.astype(np.float32)
        print("lod%s: %d vertices, bind-space box %s .. %s" % (sfx or "_0", len(out), out.min(0).round(2), out.max(0).round(2)))
    np.savez(a[3], **res)
    if "--png" in a:
        png(a[a.index("--png") + 1], res["pos"], bear)


def png(prefix, P, bear):
    """Front and side silhouettes of the fitted mesh with the bear skeleton drawn over them."""
    from PIL import Image, ImageDraw
    for ax, name, xr in ((0, "front", (-8, 8)), (2, "side", (-5, 7))):
        s = 60
        W, H = int((xr[1] - xr[0]) * s), 11 * s
        im = Image.new("RGB", (W, H), (25, 25, 25))
        dr = ImageDraw.Draw(im)
        X, Y = (lambda x: (x - xr[0]) * s), (lambda y: H - (y - 0.0) * s)
        for p in P[::2]:
            dr.rectangle([X(p[ax]), Y(p[1]), X(p[ax]) + 1, Y(p[1]) + 1], fill=(120, 180, 255))
        keys = {n for _f, n, _s in names(TAIL)} | {"C_Neck_neck1", "head"}
        for b in bear:
            if b["parent"] >= 0 and (b["name"] in {f for f, _k, _s in names(OWL)} or b["name"] in {f for f, _k, _s in names(TAIL)}):
                q = bear[b["parent"]]["pos"]
                dr.line([X(q[ax]), Y(q[1]), X(b["pos"][ax]), Y(b["pos"][1])], fill=(255, 60, 60), width=2)
        im.save("%s_%s.png" % (prefix, name))


if __name__ == "__main__":
    c = sys.argv[1]
    {"bones": cmd_bones, "fit": cmd_fit}[c](sys.argv[2:])

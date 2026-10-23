"""Blender (headless): skin weights for the Wereowl meshes in the werebear skeleton's bind pose (heat weights on a watertight proxy).

    blender -b -P tools/sprite3d/owl_weights.py -- <in.npz> <bones.json> <out.npz> [--voxel 0.15] [--smooth 20] [--knn 8] [--ref 0]

<in.npz>: the fitted meshes in the skeleton's bind space (`pos`, `nrm`, `uv`, `tris` and the `_1`..`_4` LODs; tools/sprite3d/owl_refit.py).
<bones.json>: `skel_info.py bear.skeleton bones.json` (name, parent, pos per bone).
Steps: an armature of the deforming bones (BONES, tails toward the next bone of the chain) is made from the skeleton; LOD `--ref`
(0) is duplicated into a proxy and voxel-remeshed (the generated mesh has holes and loose feathers, Blender's heat solver needs
a closed volume); `ARMATURE_AUTO` (bone heat) weights the proxy; every vertex of every LOD takes the weights of its nearest proxy
vertex; the weights are smoothed over the mesh edges (`--smooth` rounds), cut to the 4 strongest influences and normalised.
Writes the input arrays plus `bone_idx`, `weights` (N, 4) and `bones` (names, indexed by bone_idx) for each LOD suffix, the form
tools/d2r_mesh.py's `replace_with_mesh` takes.
"""
import json
import sys

import bpy
import mathutils
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
src, bonesjson, dst = argv[:3]


def opt(name, default):
    return type(default)(argv[argv.index(name) + 1]) if name in argv else default


VOXEL, SMOOTH, REF, KNN = opt("--voxel", 0.15), opt("--smooth", 20), opt("--ref", 0), opt("--knn", 8)

# deforming bones of the werebear skeleton (the ones the Wereowl mesh is skinned to) and where each one's tail points
CHAIN = {
    "pelvis": None, "C_Spine_spine1": "C_Spine_spine2", "C_Spine_spine2": "chest", "chest": "C_Neck_neck1",
    "C_Neck_neck1": "head", "head": None,
}
for S in ("L", "R"):
    CHAIN.update({
        f"{S}_Clavicle_Clavicle": f"{S}_Arm_UpperArm", f"{S}_Arm_UpperArm": f"{S}_Arm_ForeArm", f"{S}_Arm_ForeArm": f"{S}_Arm_Wrist",
        f"{S}_Arm_Wrist": f"{S}_Arm_Wrist_END", f"{S}_Leg_Thigh": f"{S}_Leg_Calf", f"{S}_Leg_Calf": f"{S}_Leg_Foot",
        f"{S}_Leg_Foot": f"{S}_Leg_Ball", f"{S}_Leg_Ball": f"{S}_Leg_Toe_END"})
EXTRA_TAIL = {"pelvis": (0, -0.5, 0), "head": (0, 1.0, 0.6)}

bones = {b["name"]: b for b in json.load(open(bonesjson))}
d = np.load(src, allow_pickle=True)
lods = [k[3:] for k in d.files if k.startswith("pos")]  # "", "_1", ...

bpy.ops.wm.read_factory_settings(use_empty=True)


def mesh_object(name, pos, tris):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(p) for p in pos], [], [tuple(int(i) for i in t) for t in tris])
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def select_only(*obs, active=None):
    bpy.ops.object.select_all(action="DESELECT")
    for o in obs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active or obs[0]


# --- armature
arm = bpy.data.armatures.new("arm")
arm_ob = bpy.data.objects.new("arm", arm)
bpy.context.scene.collection.objects.link(arm_ob)
select_only(arm_ob)
bpy.ops.object.mode_set(mode="EDIT")
for name, tail in CHAIN.items():
    eb = arm.edit_bones.new(name)
    eb.head = mathutils.Vector(bones[name]["pos"])
    if tail:
        eb.tail = mathutils.Vector(bones[tail]["pos"])
    else:
        eb.tail = eb.head + mathutils.Vector(EXTRA_TAIL[name])
    if (eb.tail - eb.head).length < 0.05:
        eb.tail = eb.head + mathutils.Vector((0, 0.1, 0))
for name in CHAIN:
    par = bones[name]["parent"]
    while par >= 0 and list(bones.values())[par]["name"] not in CHAIN:
        par = list(bones.values())[par]["parent"]
    if par >= 0:
        arm.edit_bones[name].parent = arm.edit_bones[list(bones.values())[par]["name"]]
bpy.ops.object.mode_set(mode="OBJECT")
names = list(CHAIN)

# --- proxy: duplicate of the reference LOD, voxel remeshed, heat weights
ref = d["pos" + ("" if REF == 0 else "_%d" % REF)], d["tris" + ("" if REF == 0 else "_%d" % REF)]
proxy = mesh_object("proxy", *ref)
select_only(proxy)
proxy.data.remesh_voxel_size = VOXEL
bpy.ops.object.voxel_remesh()
print("proxy", len(proxy.data.vertices), "vertices")
select_only(proxy, arm_ob, active=arm_ob)
bpy.ops.object.parent_set(type="ARMATURE_AUTO")
gi = {g.name: g.index for g in proxy.vertex_groups}
PW = np.zeros((len(proxy.data.vertices), len(names)))
for v in proxy.data.vertices:
    for g in v.groups:
        nm = proxy.vertex_groups[g.group].name
        if nm in names:
            PW[v.index, names.index(nm)] = g.weight
nz = (PW.sum(1) > 1e-6).mean()
print("proxy vertices with weights: %.1f%%, bones with weight: %s" % (100 * nz, [n for n, c in zip(names, PW.sum(0)) if c > 0][:30]))
kd = mathutils.kdtree.KDTree(len(proxy.data.vertices))
for v in proxy.data.vertices:
    kd.insert(v.co, v.index)
kd.balance()


def smooth(W, tris, it):
    n = len(W)
    e = np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]])
    e = np.unique(np.concatenate([e, e[:, ::-1]]), axis=0)
    deg = np.bincount(e[:, 0], minlength=n) + 1.0
    for _ in range(it):
        acc = W.copy()
        np.add.at(acc, e[:, 0], W[e[:, 1]])
        W = acc / deg[:, None]
    return W


out = {k: d[k] for k in d.files}
for sfx in lods:
    pos, tris = d["pos" + sfx], d["tris" + sfx].astype(np.int64)
    W = np.zeros((len(pos), len(names)))
    for i, p in enumerate(pos):
        # a few nearest proxy vertices, distance weighted: robust where the proxy is a little off the original surface
        near = kd.find_n(mathutils.Vector(p), KNN)
        ww = np.array([1.0 / (dist + 0.02) for _co, _i, dist in near])
        W[i] = sum(w * PW[j] for w, (_co, j, _d) in zip(ww, near)) / ww.sum()
    # vertices the proxy gave no weight to (rare) take the nearest weighted proxy vertex's
    W = smooth(W, tris, SMOOTH)
    s = W.sum(1, keepdims=True)
    W = np.where(s > 1e-6, W / np.maximum(s, 1e-9), 0)
    top = np.argsort(-W, axis=1)[:, :4]
    wts = np.take_along_axis(W, top, 1)
    wts = wts / np.maximum(wts.sum(1, keepdims=True), 1e-9)
    used = sorted({int(c) for c in np.unique(top[wts > 0])})
    remap = {c: i for i, c in enumerate(used)}
    bidx = np.vectorize(remap.get, otypes=[int])(np.where(wts > 0, top, used[0]))
    out["bone_idx" + sfx], out["weights" + sfx], out["bones" + sfx] = bidx, wts, np.array([names[c] for c in used])
    print("lod%s: %d vertices, %d bones, vertices without weight %d" % (sfx or "_0", len(pos), len(used), int((s[:, 0] < 1e-6).sum())))
np.savez(dst, **out)
print("wrote", dst)

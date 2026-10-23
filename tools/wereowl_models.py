#!/usr/bin/env python
"""Build step: the generated, textured Wereowl 3D model as the Druid's Wereowl form and the Corrupted Wereowl enemy (lane wereowl-3d).

  py -3 tools/wereowl_models.py [--assets DIR] [--off]

Runs after hd_monsters.py. The model is in the repo (Git LFS): DIR (default env MXL_3D_DIR, else assets/hd/wereowl) holds
`mesh_lods.npz` (pos/nrm/uv/tris, LODs as _1.._4) and `albedo.png` from tools/sprite3d. Without them, or with the environment
variable MXL_NO_3D=1 (or --off), the step does nothing and the goatman1 stand-ins of hd_monsters.py stay (docs/hd-models.md).

What it writes under work/mod/data/hd/character:
- the Druid's Wereowl form (skill wereowl_form -> state wereowl_form_state -> gfxclass monstats `wereowl` -> monsters.json -> unit):
  the Wereowl mesh rigged to D2R's werebear skeleton (enemy/bear, `rig_fit.py` profile bear; an upright biped like the owl), at the
  model files of the unused vanilla unit `horse` (the engine only loads models at paths D2R has), as a new unit `mxl_wereowl3d`
  (a copy of the bear unit: skeleton, werebear state machine and animations; its effects part stays; scale u of the rig profile);
- the Corrupted Wereowl enemy: the same mesh rigged to goatman1 (profile goatman1) at the model files of `wargoatman`, patched into the
  unit hd_monsters.py chose for `corrupted_wereowl` (goatman1 donor, scale x1.25, corrupted colour adjustment).
Textures: `.texture` files (BC3 albedo 1024, flat normal / ORM as BC3 read linearly, black mask) written over the vanilla paths of `horse`
(alb / nrm / orm, the Druid form) and `pig` (alb / nrm / orm / sss, the Corrupted Wereowl), the material maps point there.
"""
import copy
import json
import os
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "sprite3d"))
import d2r_mesh as dm  # noqa: E402
import d2r_texture as dt  # noqa: E402
import rig_fit  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
GAME = r"C:\Program Files (x86)\Diablo II Resurrected"
ENEMY = "data/hd/character/enemy/"
DEFAULT_ASSETS = str(ROOT / "assets" / "hd" / "wereowl")  # in the repo (Git LFS, assets/hd/wereowl/README.md)

# per use: rig profile, template model (donor unit dir, number of vanilla LODs), where the files go (a vanilla unit nothing in
# Median uses), texture stem for the albedo/orm/normal maps, the unit it patches, `weights`: bind-space meshes with Blender heat
# weights (assets, tools/sprite3d/owl_weights.py; without the file rig_fit's own capsule weights are used).
USES = {
    "druid": dict(profile="bear", donor="bear", lods=4, slot="horse",
                  tex="horse/textures/horse", unit="mxl_wereowl3d", weights="druid_lods.npz"),
    "corrupted": dict(profile="goatman1", donor="goatman1", lods=5, slot="wargoatman",
                      tex="pig/textures/pig", unit=None),
}
# albedo gamma per texture stem (the build input is already exposed like the vanilla werebear, tools/sprite3d/albedo_clean.py; > 1 darkens)
ALBEDO_GAMMA = {"horse/textures/horse": 1.0, "pig/textures/pig": 1.7}
BLACK = ENEMY + "pig/textures/pig_sss.texture"  # mask and thickness: nothing tinted, no subsurface


def casc_cache(rels):
    """Vanilla files by game path (without `data:`), read once from the installed game into work/hd3d_cache."""
    out = {}
    st = None
    for rel in rels:
        p = ROOT / "work" / "hd3d_cache" / rel
        if not p.exists():
            if st is None:
                import native
                st = native.open_casc(GAME)
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(native.casc_read(st, "data:" + rel))
        out[rel] = p
    return out


def write_textures(data, albedo_png, stems):
    """The maps in the engine's own formats (docs/hd-models.md 3b, d2r_texture.py): albedo BC3 sRGB (format 62) at 1024 (an RGBA8
    or 2048 albedo showed noise or lost the GPU device); normal and ORM maps BC3 read linearly (format 61): flat normal DXT5nm
    (255, y 128, 0, x 128), ORM (occlusion 255, roughness, metal 0, 255); the black thickness / mask map BC3 sRGB."""
    from PIL import Image
    im = Image.open(albedo_png).convert("RGB").resize((1024, 1024), Image.LANCZOS)
    base = np.array(im).astype(np.float32) / 255.0
    nrm = np.full((256, 256, 4), (255, 128, 0, 128), np.uint8)
    orm = np.full((256, 256, 4), (255, 160, 0, 255), np.uint8)
    black = np.full((256, 256, 4), (0, 0, 0, 255), np.uint8)
    for stem, sufs in stems.items():
        for suf in sufs:
            p = data / ENEMY / (stem + "_" + suf + ".texture")
            if suf == "alb":
                rgb = 255.0 * base ** ALBEDO_GAMMA[stem]
                dt.write_bc3(p, np.dstack([np.rint(rgb).astype(np.uint8), np.full((1024, 1024), 255, np.uint8)]), mips=11)
            elif suf == "sss":
                dt.write_bc3(p, black, mips=9)
            else:
                dt.write_bc3(p, {"nrm": nrm, "orm": orm}[suf], mips=9, linear=True)


def body_mesh(tree):
    """The template mesh: the biggest one with a `body` material (names differ per LOD); the bear's LOD 1-3 meshes share a material
    `enemy_bear_LOD` (body, mouth, eyes), there the biggest one that is not fur or trinkets. The bear's LOD 0 also has `*_proxyShape`
    meshes of the same size as the real ones; those are not taken."""
    mat = lambda m: m["MaterialBindings"][0]["Material"]["Name"]
    body = [m for m in tree["Meshes"] if mat(m).startswith("body")] or [m for m in tree["Meshes"] if mat(m) not in ("fur", "trinkets")]
    body = [m for m in body if "proxy" not in m["Name"].lower()] or body
    return max(body, key=lambda m: len(m["PrimaryVertexData"]["Vertices"].obj))["Name"]


def build_models(data, npz, use, cfg, pre=None):
    skel = casc_cache([ENEMY + cfg["donor"] + "/skeleton/" + cfg["donor"] + ".skeleton"])
    bones = rig_fit.skel_info.bones(next(iter(skel.values())))
    prof = rig_fit.PROFILES[cfg["profile"]]
    maps = {"AlbedoTexture": ENEMY + cfg["tex"] + "_alb.texture", "ORMTexture": ENEMY + cfg["tex"] + "_orm.texture",
            "NormalTexture": ENEMY + cfg["tex"] + "_nrm.texture", "MaskTexture": BLACK, "ThicknessTexture": BLACK}
    for lod in range(5):
        sfx = "" if lod == 0 else "_%d" % lod
        tmpl = casc_cache([ENEMY + "%s/torso_lod%d.model" % (cfg["donor"], min(lod, cfg["lods"] - 1))])
        g, tree = dm.load_model(next(iter(tmpl.values())))
        keep = body_mesh(tree)
        if pre:  # bind-space meshes with their own (Blender heat) weights: tools/sprite3d/owl_weights.py
            pos, nrm, uv, tris = pre["pos" + sfx], pre["nrm" + sfx], pre["uv" + sfx], pre["tris" + sfx]
            bidx, wts, names = pre["bone_idx" + sfx], pre["weights" + sfx], [str(n) for n in pre["bones" + sfx]]
        else:
            pos, nrm, bidx, wts, names, *_ = rig_fit.fit(prof, bones, npz["pos" + sfx], npz["nrm" + sfx], npz["tris" + sfx])
            uv, tris = npz["uv" + sfx], npz["tris" + sfx]
        dm.replace_with_mesh(tree, keep, pos, nrm, uv, tris, bone_idx=bidx, weights=wts, bones=names)
        dm.set_maps(tree, keep, maps)
        n = dm.write_model(g, tree, data / ENEMY / cfg["slot"] / ("torso_lod%d.model" % lod))
    return n


def set_scale(unit, factor):
    root = next(e for e in unit["entities"] if e["name"] == "entity_root")
    tc = next((c for c in root["components"] if c["type"] == "TransformDefinitionComponent"), None)
    if tc is None:
        tc = {"type": "TransformDefinitionComponent", "name": "entity_root_TransformDefinition",
              "position": {"x": 0.0, "y": 0.0, "z": 0.0}, "orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
              "scale": {"x": 1.0, "y": 1.0, "z": 1.0}, "inheritOnlyPosition": False}
        root["components"].append(tc)
    for k in "xyz":
        tc["scale"][k] = round(tc["scale"][k] * factor, 4)


def point_unit(unit, cfg, textures):
    """The unit's torso model entity names the model files at the slot; the dependency lists follow."""
    new = ENEMY + cfg["slot"] + "/torso.model"
    if any(m["path"] == new for m in unit["dependencies"]["models"]):
        return False  # patched already (a rerun of this step)
    for e in unit["entities"]:
        for c in e["components"]:
            if c["type"] == "ModelDefinitionComponent" and c["filename"].endswith(cfg["donor"] + "/torso.model"):
                c["filename"] = new
    for m in unit["dependencies"]["models"]:
        if m["path"].endswith(cfg["donor"] + "/torso.model"):
            m["path"] = new
    have = {t["path"] for t in unit["dependencies"]["textures"]}
    unit["dependencies"]["textures"] += [{"path": t} for t in textures if t not in have]
    return True


def main():
    a = sys.argv[1:]
    assets = Path(a[a.index("--assets") + 1] if "--assets" in a else os.environ.get("MXL_3D_DIR", DEFAULT_ASSETS))
    if "--off" in a or os.environ.get("MXL_NO_3D"):
        print("3D Wereowl off (MXL_NO_3D / --off): goatman1 stand-ins stay")
        return 0
    if not (assets / "mesh_lods.npz").exists() or not (assets / "albedo.png").exists():
        print("no Wereowl 3D assets in %s: goatman1 stand-ins stay" % assets)
        return 0
    if (assets / "mesh_lods.npz").read_bytes()[:40].startswith(b"version https://git-lfs"):
        print("Wereowl 3D assets in %s are Git LFS pointers (run `git lfs install` + `git lfs pull`): goatman1 stand-ins stay" % assets)
        return 0
    data = ROOT / "work" / "mod"  # the game paths (ENEMY ...) start with data/
    hdc = data / "data" / "hd" / "character"
    npz = dict(np.load(assets / "mesh_lods.npz", allow_pickle=True))
    mj = json.loads((hdc / "monsters.json").read_text("utf-8"))
    write_textures(data, assets / "albedo.png", {"horse/textures/horse": ("alb", "nrm", "orm"),
                                                  "pig/textures/pig": ("alb", "nrm", "orm", "sss")})
    sizes = {}
    for use, cfg in USES.items():
        wf = assets / cfg["weights"] if cfg.get("weights") else None
        pre = dict(np.load(wf, allow_pickle=True)) if wf is not None and wf.exists() and not wf.read_bytes()[:40].startswith(b"version https://git-lfs") else None
        sizes[use] = build_models(data, npz, use, cfg, pre)
    # the Druid's form: a copy of the donor unit (werebear)
    cfg = USES["druid"]
    donor = json.loads((ROOT / "extract" / "d2r" / "93847" / "data" / "hd" / "character" / "enemy" / (cfg["donor"] + ".json")).read_text("utf-8-sig"))
    unit = copy.deepcopy(donor)
    unit["name"] = cfg["unit"]
    if point_unit(unit, cfg, [ENEMY + cfg["tex"] + s for s in ("_alb.texture", "_nrm.texture", "_orm.texture")] + [BLACK]):
        set_scale(unit, rig_fit.PROFILES[cfg["profile"]]["u"])
    (hdc / "enemy" / (cfg["unit"] + ".json")).write_text(json.dumps(unit, separators=(",", ":")), encoding="utf-8")
    mj["wereowl"] = cfg["unit"]
    # the Corrupted Wereowl: the unit hd_monsters.py built (goatman1 donor, scale, colour)
    cfg = USES["corrupted"]
    up = hdc / "enemy" / (mj["corrupted_wereowl"] + ".json")
    unit = json.loads(up.read_text("utf-8"))
    if point_unit(unit, cfg, [ENEMY + cfg["tex"] + s for s in ("_alb.texture", "_orm.texture", "_nrm.texture", "_sss.texture")]):
        set_scale(unit, rig_fit.PROFILES[cfg["profile"]]["u"])
    up.write_text(json.dumps(unit, separators=(",", ":")), encoding="utf-8")
    (hdc / "monsters.json").write_text(json.dumps(mj, indent=2), encoding="utf-8")
    print("Wereowl 3D: Druid form -> %s (%s skeleton, models at horse/), Corrupted Wereowl -> %s (goatman1 skeleton, models at wargoatman/); "
          "model sizes %s" % (USES["druid"]["unit"], USES["druid"]["donor"], mj["corrupted_wereowl"], sizes))
    return 0


if __name__ == "__main__":
    sys.exit(main())

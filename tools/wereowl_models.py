#!/usr/bin/env python
"""Build step: the generated, textured Wereowl 3D model as the Druid's Wereowl form and the Corrupted Wereowl enemy (lane wereowl-3d).

  py -3 tools/wereowl_models.py [--assets DIR] [--off]

Runs after hd_monsters.py. The model is in the asset pack (tools/asset_pack.py fetch): DIR (default env MXL_3D_DIR, else assets/hd/wereowl) holds
`mesh_lods.npz` (pos/nrm/uv/tris, LODs as _1.._4) and `albedo.png` from tools/sprite3d. Without them, or with the environment
variable MXL_NO_3D=1 (or --off), the step does nothing and the goatman1 stand-ins of hd_monsters.py stay (docs/hd-models.md).

What it writes under work/mod/data/hd/character:
- the Druid's Wereowl form (skill wereowl_form -> state wereowl_form_state -> gfxclass monstats `wereowl` -> monsters.json -> unit):
  the Wereowl mesh rigged to D2R's werebear skeleton (enemy/bear, `rig_fit.py` profile bear; an upright biped like the owl), at the
  model files of the unused vanilla unit `horse` (the engine only draws models at paths D2R's index has: see `place`), as a new unit `mxl_wereowl3d`
  (a copy of the bear unit: skeleton, werebear state machine and animations; its effects part stays; scale u of the rig profile);
- the Corrupted Wereowl enemy: the same mesh rigged to goatman1 (profile goatman1) at the model files of `wargoatman`, patched into the
  unit hd_monsters.py chose for `corrupted_wereowl` (goatman1 donor, scale x1.25, corrupted colour adjustment).
Where the files go (`place`): at NEW paths below data/hd/character/mxl/ (docs/hd-models.md section 3c; the model and texture
indexes get entries, `index_new_paths`); with MXL_OLD_PATHS=1 (or --old-paths) over the vanilla `horse` / `wargoatman` / `pig` paths as before.
Textures: `.texture` files (BC3 albedo 1024, flat normal / ORM as BC3 read linearly, black mask / thickness) of their own for both
forms (old paths: over those of `horse` and `pig`), the material maps point there.
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
DEFAULT_ASSETS = str(ROOT / "assets" / "hd" / "wereowl")  # in the asset pack (docs/hd-wereowl-assets.md)

# per use: rig profile, template model (donor unit dir, number of vanilla LODs), where the files go (a vanilla unit nothing in
# Median uses), texture stem for the albedo/orm/normal maps, the unit it patches, `weights`: bind-space meshes with Blender heat
# weights (assets, tools/sprite3d/owl_weights.py; without the file rig_fit's own capsule weights are used).
USES = {
    "druid": dict(profile="bear", donor="bear", lods=4, slot="horse", new="wereowl", gamma=1.0,
                  tex="horse/textures/horse", unit="mxl_wereowl3d", weights="druid_lods.npz"),
    "corrupted": dict(profile="goatman1", donor="goatman1", lods=5, slot="wargoatman", new="corrupted_wereowl", gamma=1.7,
                      tex="pig/textures/pig", unit=None),
}
# `gamma`: albedo gamma per use (the build input is already exposed like the vanilla werebear, tools/sprite3d/albedo_clean.py; > 1 darkens:
# the Corrupted's boss lighting blew the bright albedo out to white)
MXL = "data/hd/character/mxl/"  # the new directory of the port's models (docs/hd-models.md section 3c)
OLD_PATHS = "--old-paths" in sys.argv or bool(os.environ.get("MXL_OLD_PATHS"))


def place(cfg):
    """(directory of the model files, stem of the texture files), game paths. New paths by default; the vanilla unit's own paths
    (over which the files used to go) with MXL_OLD_PATHS."""
    if OLD_PATHS:
        return ENEMY + cfg["slot"] + "/", ENEMY + cfg["tex"]
    return MXL + cfg["new"] + "/", MXL + cfg["new"] + "/textures/" + cfg["new"]


BLACK = ENEMY + "pig/textures/pig_sss.texture"  # old paths: mask and thickness of both forms (written with the Corrupted's textures)


def black(cfg):
    """The mask / thickness map: nothing tinted, no subsurface. The werebear material's own mask on our UVs drew the body black."""
    return BLACK


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


MAP_SIZE = 1024  # albedo, normal and ORM maps (the vanilla werebear's albedo and normal are 1024; an RGBA8 / 2048 albedo lost the GPU device)
NRM_GREEN = os.environ.get("MXL_NRM_GREEN", "dx")  # the engine's green channel: "dx" = +y toward the image bottom (docs/hd-models.md 3d)


def pack_normal(png, size):
    """`normal.png` (OpenGL tangent space, tools/sprite3d/owl_maps.py) -> D2R's DXT5nm layout (255, y, 0, x) at `size`."""
    from PIL import Image
    im = Image.open(png).convert("RGB")
    if im.size[0] != size:
        im = im.resize((size, size), Image.LANCZOS)
    n = np.asarray(im).astype(np.float32) / 127.5 - 1.0
    n /= np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-6)
    if NRM_GREEN == "dx":
        n[..., 1] = -n[..., 1]
    q = lambda c: np.clip(np.rint((c * 0.5 + 0.5) * 255), 0, 255).astype(np.uint8)
    return np.dstack([np.full((size, size), 255, np.uint8), q(n[..., 1]), np.zeros((size, size), np.uint8), q(n[..., 0])])


def pack_orm(png, size):
    from PIL import Image
    im = Image.open(png).convert("RGB")
    if im.size[0] != size:
        im = im.resize((size, size), Image.LANCZOS)
    return np.dstack([np.asarray(im), np.full((size, size), 255, np.uint8)])


def write_textures(data, assets, stems):  # stems: {texture stem: (suffixes, albedo gamma)}
    """The maps in the engine's own formats (docs/hd-models.md 3b, d2r_texture.py): albedo BC3 sRGB (format 62) at MAP_SIZE; normal and
    ORM maps BC3 read linearly (format 61): normal DXT5nm (255, y, 0, x), ORM (occlusion, roughness, metal 0, 255). `assets/normal.png`
    / `orm.png` (baked from the high-poly, tools/sprite3d/owl_bake.py) are used when present, else flat maps; the black thickness /
    mask map is BC3 sRGB."""
    from PIL import Image
    S = MAP_SIZE
    im = Image.open(assets / "albedo.png").convert("RGB").resize((S, S), Image.LANCZOS)
    base = np.array(im).astype(np.float32) / 255.0
    nrm = pack_normal(assets / "normal.png", S) if (assets / "normal.png").exists() else np.full((256, 256, 4), (255, 128, 0, 128), np.uint8)
    orm = pack_orm(assets / "orm.png", S) if (assets / "orm.png").exists() else np.full((256, 256, 4), (255, 160, 0, 255), np.uint8)
    black = np.full((256, 256, 4), (0, 0, 0, 255), np.uint8)
    for stem, (sufs, gamma) in stems.items():
        for suf in sufs:
            p = data / (stem + "_" + suf + ".texture")
            if suf == "alb":
                rgb = 255.0 * base ** gamma
                dt.write_bc3(p, np.dstack([np.rint(rgb).astype(np.uint8), np.full((S, S), 255, np.uint8)]), mips=11)
            elif suf == "sss":
                dt.write_bc3(p, black, mips=9)
            else:
                m = {"nrm": nrm, "orm": orm}[suf]
                dt.write_bc3(p, m, mips=int(np.log2(m.shape[0])) + 1, linear=True)


# a new texture path needs a descriptor in the engine's data/hd/texture_desc_cache.json (format, size, mips; 31073 entries), a new
# model path an entry in data/hd/model_lod_desc.bin (tools/model_lod_desc.py); the entries copy a vanilla one of the same kind
TEXTURE_TEMPLATES = {"alb": ENEMY + "horse/textures/horse_alb.texture", "nrm": ENEMY + "horse/textures/horse_orm.texture",
                     "orm": ENEMY + "horse/textures/horse_orm.texture"}


def index_new_paths(data, models, textures, assets_dir=Path(DEFAULT_ASSETS)):
    """Entries for the new paths in copies of the two index files, written into the mod folder (a copy already there, from another
    step, is extended; else the game's). `models`: {new model path: vanilla model path whose entry it copies}; `textures`: {new
    texture path: vanilla texture path whose descriptor it copies} (BC3 albedo 1024 with 11 mips, the other maps 256 with 9, as
    write_textures makes them)."""
    import model_lod_desc as ld
    out = data / "data" / "hd"
    out.mkdir(parents=True, exist_ok=True)
    base = lambda name: out / name if (out / name).exists() else casc_cache(["data/hd/" + name])["data/hd/" + name]
    (out / "model_lod_desc.bin").write_bytes(ld.add(base("model_lod_desc.bin").read_bytes(), models))
    cache = json.loads(base("texture_desc_cache.json").read_text("utf-8"))
    by = {e["filename"].lower(): e for e in cache}  # the engine's paths are case-insensitive (`pig_SSS.texture`)
    for path, template in textures.items():
        e = copy.deepcopy(by[template.lower()])
        alb = path.endswith("_alb.texture")
        e["filename"] = path
        flat = not (assets_dir / ("normal.png" if path.endswith("_nrm.texture") else "orm.png")).exists() and not alb
        e["width"] = e["height"] = 256 if flat else MAP_SIZE
        e["levels"] = 9 if flat else 11
        if not alb:
            e["thumbnailMip"] = e["levels"] - 1
        cache = [x for x in cache if x["filename"] != path] + [e]
    (out / "texture_desc_cache.json").write_text(json.dumps(cache, separators=(",", ":")), encoding="utf-8")


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
    mdir, tex = place(cfg)
    maps = {"AlbedoTexture": tex + "_alb.texture", "ORMTexture": tex + "_orm.texture",
            "NormalTexture": tex + "_nrm.texture", "MaskTexture": black(cfg), "ThicknessTexture": black(cfg)}
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
        n = dm.write_model(g, tree, data / mdir / ("torso_lod%d.model" % lod))
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
    new = place(cfg)[0] + "torso.model"
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
        print("Wereowl 3D assets in %s are Git LFS pointers (an old LFS checkout: run `py -3 tools/asset_pack.py fetch --force`): goatman1 stand-ins stay" % assets)
        return 0
    data = ROOT / "work" / "mod"  # the game paths (ENEMY ...) start with data/
    hdc = data / "data" / "hd" / "character"
    npz = dict(np.load(assets / "mesh_lods.npz", allow_pickle=True))
    mj = json.loads((hdc / "monsters.json").read_text("utf-8"))
    dtex, ctex = place(USES["druid"])[1], place(USES["corrupted"])[1]
    write_textures(data, assets, {dtex: (("alb", "nrm", "orm"), USES["druid"]["gamma"]),
                                                 ctex: (("alb", "nrm", "orm") + (("sss",) if OLD_PATHS else ()), USES["corrupted"]["gamma"])})
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
    if point_unit(unit, cfg, [place(cfg)[1] + s for s in ("_alb.texture", "_nrm.texture", "_orm.texture")] + [black(cfg)]):
        set_scale(unit, rig_fit.PROFILES[cfg["profile"]]["u"])
    (hdc / "enemy" / (cfg["unit"] + ".json")).write_text(json.dumps(unit, separators=(",", ":")), encoding="utf-8")
    mj["wereowl"] = cfg["unit"]
    # the Corrupted Wereowl: the unit hd_monsters.py built (goatman1 donor, scale, colour)
    cfg = USES["corrupted"]
    up = hdc / "enemy" / (mj["corrupted_wereowl"] + ".json")
    unit = json.loads(up.read_text("utf-8"))
    if point_unit(unit, cfg, [place(cfg)[1] + s for s in ("_alb.texture", "_orm.texture", "_nrm.texture")] + [black(cfg)]):
        set_scale(unit, rig_fit.PROFILES[cfg["profile"]]["u"])
    up.write_text(json.dumps(unit, separators=(",", ":")), encoding="utf-8")
    (hdc / "monsters.json").write_text(json.dumps(mj, indent=2), encoding="utf-8")
    if not OLD_PATHS:
        index_new_paths(data, {place(c)[0] + "torso.model": ENEMY + c["slot"] + "/torso.model" for c in USES.values()},
                        {place(c)[1] + "_%s.texture" % k: t for c in USES.values() for k, t in TEXTURE_TEMPLATES.items()}, assets)
    print("Wereowl 3D: Druid form -> %s (%s skeleton, models at %s), Corrupted Wereowl -> %s (goatman1 skeleton, models at %s); "
          "model sizes %s" % (USES["druid"]["unit"], USES["druid"]["donor"], place(USES["druid"])[0], mj["corrupted_wereowl"],
                              place(USES["corrupted"])[0], sizes))
    return 0


if __name__ == "__main__":
    sys.exit(main())

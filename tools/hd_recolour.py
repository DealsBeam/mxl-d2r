"""Recolour a D2R HD effect: a hue/saturation/brightness-shifted copy of its gradient textures (docs/hd-effects.md section 6).

D2R draws an effect with a PopcornFX `.particles` file. 51 of 373 missile and 74 of 331 overlay definitions take their
colour from RGBA8 gradient textures (`data/hd/vfx/textures/gradient/T_FX_*.texture`, 256 x 16); the rest has colours
baked into the particle program, which cannot be edited. The texture paths are stored as `<1 byte length><path>` inside
the `.particles` file, so a path replaced by one of the same length is safe (the technique of tools/missile_art.py).

For a recolour (Median's art is vanilla art in another colour, tools/art_provenance.py) this module
  - finds the colourful gradient textures of an effect's particle files (RGBA8 under textures/gradient, mean saturation
    >= MIN_SAT; alpha ramps and grey ramps are left alone),
  - writes a hue/saturation/value shifted copy of each under a same-length path (tag = hash of the shift),
  - writes a patched copy of the `.particles` file (data/hd/vfx/particles/mxl/<name>.particles) and a definition whose
    VfxDefinitionComponents point at it.
An effect with no colourful gradient is reported as not recolourable (effects_hd.py then chooses the effect of the
nearest colour instead). Patched particles are not yet confirmed in game for every effect: see docs/hd-effects.md.
"""
import colorsys
import hashlib
import json
import math
import re
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import d2r_texture  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
D2R = ROOT / "extract" / "d2r" / "93847"
GAME = r"C:\Program Files (x86)\Diablo II Resurrected"
CACHE = ROOT / "work" / "art_provenance" / "vfx"
MIN_SAT = 0.18
TEX_RE = re.compile(rb"data/hd/vfx/textures/")
_casc = None


def casc_read(path: str) -> bytes:
    """A file of D2R's CASC storage (cached under work/art_provenance/vfx)."""
    global _casc
    f = CACHE / path.replace("/", "__")
    if not f.exists():
        import native
        if _casc is None:
            _casc = (native, native.open_casc(GAME))
        d = _casc[0].casc_read(_casc[1], "data:" + path)
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(d or b"")
    return f.read_bytes()


def texture_refs(data: bytes) -> list[str]:
    """Texture paths stored in a .particles file (length-prefixed strings)."""
    out = []
    for m in TEX_RE.finditer(data):
        n = data[m.start() - 1] if m.start() else 0
        p = data[m.start():m.start() + n]
        if n and p.endswith(b".texture") and len(p) == n:
            out.append(p.decode())
    return list(dict.fromkeys(out))


def parse_texture(d: bytes):
    import struct
    _m, v, w, h = struct.unpack_from("<4I", d, 0)
    n = (v >> 24) & 0xFF
    mips = []
    for i in range(n):
        size, off = struct.unpack_from("<II", d, 0x24 + 8 * i)
        pos = 0x28 + 8 * i + off
        mips.append(d[pos:pos + size])
    return v & 0xFF, w, h, mips


_grad: dict[str, np.ndarray | None] = {}


def colour_gradient(path: str) -> np.ndarray | None:
    """(h, w, 4) uint8 pixels of an RGBA8 gradient texture with a real colour (else None)."""
    if path not in _grad:
        res = None
        if "/gradient/" in path:
            try:
                fmt, w, h, mips = parse_texture(casc_read(path))
                if fmt == d2r_texture.FMT_RGBA8 and len(mips[0]) >= w * h * 4:
                    a = np.frombuffer(mips[0][:w * h * 4], np.uint8).reshape(h, w, 4)
                    rgb = a[:, :, :3].astype(np.float32) / 255
                    mx, mn = rgb.max(2), rgb.min(2)
                    sat = np.where(mx > 0.05, (mx - mn) / np.maximum(mx, 1e-6), 0)
                    wgt = mx
                    if (sat * wgt).sum() / max(wgt.sum(), 1e-6) >= MIN_SAT:
                        res = a
            except Exception:
                res = None
        _grad[path] = res
    return _grad[path]


def shift_pixels(a: np.ndarray, hue: float, sat: float, val: float) -> np.ndarray:
    """Hue rotation (turns), saturation offset and brightness factor 2**val of RGBA8 pixels; alpha is kept."""
    rgb = a[..., :3].astype(np.float32) / 255
    mx, mn = rgb.max(-1), rgb.min(-1)
    d = mx - mn
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    dd = np.maximum(d, 1e-9)
    h = np.where(mx == r, ((g - b) / dd) % 6, np.where(mx == g, (b - r) / dd + 2, (r - g) / dd + 4)) / 6.0
    h = np.where(d == 0, 0.0, h)
    s = np.where(mx > 0, d / np.maximum(mx, 1e-9), 0)
    h = (h + hue) % 1.0
    s = np.clip(s + sat * (s > 0.02), 0, 1)
    v = np.clip(mx * (2.0 ** val), 0, 1)
    i = np.floor(h * 6).astype(int) % 6
    f = h * 6 - np.floor(h * 6)
    p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
    out = np.zeros_like(rgb)
    for k, (ra, ga, ba) in enumerate([(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)]):
        m = i == k
        out[..., 0][m], out[..., 1][m], out[..., 2][m] = ra[m], ga[m], ba[m]
    res = a.copy()
    res[..., :3] = np.clip(np.rint(out * 255), 0, 255).astype(np.uint8)
    return res


def spec_of(shift: dict | None, m_art: dict | None, c_art: dict | None) -> dict:
    """The colour change from the vanilla art (c) to Median's (m): provenance shift, else the mean colours."""
    if shift and "hue" in shift:
        hue, sat, val = shift["hue"], shift["sat"], shift["val"]
    else:
        (h1, s1, v1), (h2, s2, v2) = m_art["hsv"], c_art["hsv"]
        hue = (h1 - h2 + 0.5) % 1.0 - 0.5
        sat, val = s1 - s2, math.log2(max(v1, 0.03) / max(v2, 0.03))
    hue, sat, val = round(max(-0.5, min(0.5, hue)), 2), round(max(-0.9, min(0.9, sat)), 2), round(max(-2.0, min(2.0, val)), 1)
    return {"hue": hue, "sat": sat, "val": val, "label": f"hue {hue:+.2f} sat {sat:+.2f} val {val:+.1f}"}


def tag_of(spec: dict) -> str:
    return hashlib.md5(f"{spec['hue']}/{spec['sat']}/{spec['val']}".encode()).hexdigest()[:6]


def new_texture_path(path: str, tag: str) -> str:
    """A path in the same folder and of the same length (the .particles file stores it length-prefixed)."""
    folder, _, name = path.rpartition("/")
    stem = name[:-len(".texture")]
    t = f"_{tag}"
    if len(stem) <= len(t) + 3:
        raise ValueError(f"texture name too short to tag: {path}")
    return f"{folder}/{stem[:len(stem) - len(t)]}{t}.texture"


def recolourable(particles: str) -> list[str]:
    return [t for t in texture_refs(casc_read(particles)) if colour_gradient(t) is not None]


def _defs_particles(d: dict) -> list[str]:
    return [c["filename"] for e in d.get("entities", []) for c in e["components"]
            if c["type"] == "VfxDefinitionComponent" and c.get("filename")]


def can_recolour(d: dict) -> bool:
    return any(recolourable(p) for p in _defs_particles(d))


def recolour_particles(out: Path, particles: str, spec: dict) -> tuple[str, dict[str, str]] | None:
    """Write the patched .particles file and the shifted gradients under `out` (work/mod/data). (new path, textures)."""
    grads = recolourable(particles)
    if not grads:
        return None
    tag = tag_of(spec)
    data = casc_read(particles)
    repl = {}
    for g in grads:
        n = new_texture_path(g, tag)
        px = shift_pixels(colour_gradient(g), spec["hue"], spec["sat"], spec["val"])
        d2r_texture.write_rgba(out / n.split("/", 1)[1], px)
        needle = bytes([len(g)]) + g.encode()
        data = data.replace(needle, bytes([len(n)]) + n.encode())
        repl[g] = n
    name = Path(particles).stem
    new = f"data/hd/vfx/particles/mxl/{name[:40]}_{tag}.particles"
    p = out / new.split("/", 1)[1]
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
    return new, repl


def recolour_def(out: Path, d: dict, spec: dict, name: str) -> dict | None:
    """The definition with every colourful effect recoloured, or None when none of its particles can be."""
    import copy
    d = copy.deepcopy(d)
    d["name"] = name
    swaps, any_ = {}, False
    texs = {}
    for e in d.get("entities", []):
        for c in e["components"]:
            if c["type"] == "VfxDefinitionComponent" and c.get("filename"):
                r = recolour_particles(out, c["filename"], spec)
                if r:
                    swaps[c["filename"]] = r[0]
                    texs.update(r[1])
                    c["filename"] = r[0]
                    any_ = True
    if not any_:
        return None
    deps = d.get("dependencies", {})
    for ent in deps.get("particles", []):
        ent["path"] = swaps.get(ent["path"], ent["path"])
    for ent in deps.get("textures", []):
        ent["path"] = texs.get(ent["path"], ent["path"])
    for old, new in texs.items():
        if not any(t["path"] == new for t in deps.get("textures", [])):
            deps.setdefault("textures", []).append({"path": new})
    return d


# ---- hooks of tools/effects_hd.py -----------------------------------------------------------------
def plan_missile(hdm: Path):
    """recolour(c, shift, m_art, c_art) -> spec when the D2R missile row c's effect can be recoloured, else None."""
    def plan(c, shift, m_art, c_art):
        d = json.loads((hdm / f"{c.model}.json").read_text("utf-8-sig"))
        return spec_of(shift, m_art, c_art) if can_recolour(d) else None
    return plan


def plan_overlay(hdo: Path, norm_key):
    def plan(c, shift, m_art, c_art):
        d = json.loads((hdo / c.folder / f"{norm_key(c.name)}.json").read_text("utf-8-sig"))
        return spec_of(shift, m_art, c_art) if can_recolour(d) else None
    return plan


def recolour_missile(out: Path, model: str, spec: dict) -> str:
    """Write the recoloured copy of D2R's missile definition `model`; its new name."""
    hdm = D2R / "data" / "hd" / "missiles"
    d = json.loads((hdm / f"{model}.json").read_text("utf-8-sig"))
    name = f"mxl_{model}_c{tag_of(spec)}"
    nd = recolour_def(out, d, spec, name)
    (out / "hd" / "missiles").mkdir(parents=True, exist_ok=True)
    (out / "hd" / "missiles" / f"{name}.json").write_text(json.dumps(nd, separators=(",", ":")), "utf-8")
    return name


def recolour_overlay(out: Path, d: dict, spec: dict) -> dict:
    nd = recolour_def(out, d, spec, d.get("name", ""))
    return nd if nd is not None else d

"""HD effects for Median's skill missiles and overlays: exact D2R effect, scaled variant, or closest stand-in (Phase 5).

D2R draws a missile / overlay in HD from a UnitDefinition / OverlayDefinition (data/hd/missiles/<model>.json,
data/hd/overlays/<Character>/<overlay>.json; PopcornFX particles, see docs/hd-effects.md) found by the lowercased
missiles.txt / overlay.txt name. Median adds 4,925 missile rows and 316 overlay rows D2R has no definition for. After
missile_hd.py (which maps by CelFile only), this step decides every Median row one by one:

  exact     the row's art file (missile CelFile / overlay Filename) is a D2R one: it gets the HD effect of the D2R row
            that uses the same art and is most like the Median row (name and skill words, speed/range/loop/hit role,
            light); art nothing in D2R draws in HD falls to stand-in
  variant   exact or stand-in effect scaled (TransformDefinitionComponent of every entity; that and the spot light are
            the only fields an HD definition has for size/colour/light, nothing for tint, transparency or speed): a
            stand-in is scaled by the ratio of Median's classic art to the stand-in's
  provenance  Median-only art that tools/art_provenance.py found to be a vanilla animation (exact, rescaled, recoloured,
            retimed or mashed up from vanilla frames): the HD effect of the D2R row that draws that vanilla art, scaled by the
            measured factor; a recolour keeps the effect when the colour barely moved, takes a hue-shifted copy where the
            effect's colour comes from a gradient texture (tools/hd_recolour.py) and otherwise falls through to
  stand-in  Median-only art: the D2R effect whose classic art and row behave most alike: colour sampled from the DCC
            frames (tools/dcc.py), size, frame count, moving / hit / loop role, element and shape words of the row, its
            art and the skills that reach it; custom HD art replaces these last (docs/unported.md)

D2R's lookup key is the name lowercased with spaces as '_' and an underscore in front of every digit run (spike1 ->
spike_1, cyclonearmor1front -> cyclonearmor_1front); both forms are written.

Writes <out>/hd/missiles/missiles.json (updated) and mxl_*.json variants, <out>/hd/overlays/<Character>/<name>.json,
overlay.txt Character (the folder of the effect used; empty on Median's rows), and <out>/../effects_hd.md (row -> effect,
exact/variant/stand-in, confidence). Run after missile_hd.py.
Usage: python tools/effects_hd.py [--work DIR] [--out DIR] [--only NAME,NAME] [--no-scale]
"""
import argparse
import colorsys
import copy
import hashlib
import json
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import palshift  # noqa: E402
from missile_hd import LOOKALIKE  # noqa: E402
from txt_migrate import read_tsv  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
D2R = ROOT / "extract" / "d2r" / "93847"
MXL = ROOT / "extract" / "mxl" / "data" / "global"
GAME = r"C:\Program Files (x86)\Diablo II Resurrected"
HDM, HDO = D2R / "data" / "hd" / "missiles", D2R / "data" / "hd" / "overlays"
BS = chr(92)
CRLF, LF, TAB = chr(13) + chr(10), chr(10), chr(9)

# ----------------------------------------------------------------------------------------------- words
# family = element/theme (strong hint), shape = what it is (weaker hint). Matched as substrings of the name with
# separators removed, so 'violetlightning', 'blackfiresmall' and 'bluenovab' read right.
FAMILY = {
    "fire": ["fire", "flame", "burn", "inferno", "meteor", "lava", "magma", "molten", "ember", "blaze", "hellfire",
             "pyre", "scorch", "immolat", "brimstone", "firestorm"],
    "ice": ["cold", "ice", "frost", "freez", "frozen", "chill", "blizzard", "glaci", "shiver", "icicle", "arctic"],
    "elec": ["lightning", "ltng", "shock", "elec", "thunder", "static", "charged", "volt", "spark", "tesla", "fury"],
    "poison": ["poison", "venom", "toxic", "plague", "acid", "spit", "goo", "sludge", "pestil", "decay", "spore"],
    "arcane": ["magic", "arcane", "mana", "energy", "psychic", "astral", "mystic", "spell", "force", "teleport"],
    "holy": ["holy", "sacred", "bless", "smite", "divine", "paladin", "vengeance", "conviction", "sanctuary",
             "zeal", "fanat", "redempt", "hammer"],
    "bone": ["bone", "skull", "skeleton", "corpse", "grave", "undead", "revive", "ghost", "spirit", "soul",
             "wraith", "death"],
    "blood": ["blood", "gore", "vamp", "leech", "life", "drain"],
    "dark": ["shadow", "dark", "void", "abyss", "curse", "hex", "chaos", "doom", "demon", "hell", "cursed",
             "decrep", "lower", "amplify", "weaken", "confuse", "terror", "blackhole"],
    "wind": ["wind", "cyclone", "nado", "hurricane", "twister", "whirl", "gale", "storm", "tempest", "vortex"],
    "earth": ["stone", "rock", "boulder", "earth", "quake", "seism", "shatter", "debris", "chunk", "sand", "dust"],
    "nature": ["vine", "root", "plant", "briar", "thorn", "wolf", "bear", "raven", "summon", "druid", "feral",
               "nature", "heart"],
    "blade": ["blade", "sword", "spear", "arrow", "dagger", "javelin", "axe", "fist", "claw", "talon", "spike",
              "needle", "dart", "quill", "sting", "slash", "dragon"],
    "trap": ["trap", "sentry", "turret", "mine", "bomb", "grenade", "tower", "totem", "dervish"],
    "buff": ["aura", "shield", "armor", "armour", "ward", "barrier", "guard", "protect", "cast", "boost", "haste",
             "frenzy", "rage", "trance", "heal", "mend", "spiritual", "bless"],
}
SHAPE = {
    "nova": ["nova", "ring", "wave", "shockwave", "pulse", "circle"],
    "orb": ["orb", "ball", "sphere", "globe", "bubble"],
    "bolt": ["bolt", "missile", "projectile", "dart", "shot", "beam", "ray", "lance"],
    "explode": ["explo", "blast", "burst", "impact", "hit", "detonat", "boom", "splash", "shatter"],
    "trail": ["trail", "tail", "streak", "wake", "smoke", "puff", "mist", "fog", "cloud"],
    "wall": ["wall", "ground", "pool", "field", "zone", "pit", "carpet", "inferno", "nest", "circle"],
    "storm": ["storm", "rain", "fall", "shower", "swarm", "barrage", "cascade"],
    "front": ["front"], "back": ["back"],
    "small": ["small", "mini", "tiny", "little", "sm"], "large": ["large", "big", "huge", "giant", "mega", "great"],
    "cast": ["cast", "start", "charge", "channel", "ready"],
}
FAM_W, SHAPE_W, WORD_W = 1.7, 0.7, 0.4
# PalShift: a shift that moves the art's colour by more than SHIFT_MIN is honoured (the row is scored by the shifted
# colour); a best stand-in further than SHIFT_FAR from it has no vanilla effect of that colour (custom art, low).
SHIFT_MIN, SHIFT_FAR = 0.12, 0.30
# Provenance (work/mod/art_provenance.json): matches below these are not trusted over the scoring
PROV_COVER, PROV_Q, PROV_SHARE = 0.6, 0.8, 0.5
PROV_STATS: Counter = Counter()  # rows by what happened to their provenance match (the report)


def norm_key(name: str) -> str:
    """D2R's HD lookup key for a missiles.txt / overlay.txt name."""
    return re.sub(r"(?<=[^_\d])(\d+)", r"_\1", name.lower().replace(" ", "_"))


def squash(s: str) -> str:
    return re.sub(r"[^a-z]+", "", s.lower())


def words(s: str) -> set[str]:
    s = re.sub(r"([a-z])([A-Z])", r"\1 \2", s)
    return {w for w in re.split(r"[^a-zA-Z]+", s.lower()) if len(w) >= 4}


def tags(*texts: str) -> tuple[set[str], set[str], set[str]]:
    fam, shp, wds = set(), set(), set()
    for t in texts:
        if not t:
            continue
        sq = squash(t)
        wds |= words(t)
        for k, stems in FAMILY.items():
            if any(s in sq for s in stems):
                fam.add(k)
        for k, stems in SHAPE.items():
            if any(s in sq for s in stems):
                shp.add(k)
    return fam, shp, wds


def norm_art(path: str) -> str:
    p = path.lower().replace("/", BS).strip()
    return p[:-4] if p.endswith(".dcc") else p


# ----------------------------------------------------------------------------------------------- D2R HD data
def ensure_hd_json() -> None:
    """D2R's missile and overlay definitions (small json) cached under extract/d2r."""
    if (HDO / "paladin" / "aura_resistfire.json").exists() and (HDM / "fireball.json").exists():
        return
    import native
    st = native.open_casc(GAME)
    pat = re.compile(r"data/hd/(missiles|overlays)/.*\.json$")
    n = 0
    for line in (D2R / "filelist.txt").read_text("utf-8").splitlines():
        l = line.strip()
        if pat.search(l):
            p = D2R / l.split(":", 1)[1]
            if not p.exists():
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(native.casc_read(st, l))
                n += 1
    print(f"extracted {n} HD missile/overlay json files")


def load_json(p: Path):
    return json.loads(p.read_text("utf-8-sig"))


# ----------------------------------------------------------------------------------------------- classic art
class ArtCache:
    def __init__(self, path: Path):
        self.path = path
        self.d = json.loads(path.read_text("utf-8")) if path.exists() else {}
        self.dirty = False
        self.pal = None
        self.files: dict[str, dict[str, Path]] = {}

    def locate(self, side: str, kind: str, rel: str) -> Path | None:
        base = (MXL if side == "mxl" else D2R / "data" / "global") / kind
        key = f"{side}/{kind}"
        if key not in self.files:
            self.files[key] = {p.relative_to(base).with_suffix("").as_posix().lower(): p
                               for p in base.rglob("*.dcc")} if base.exists() else {}
        return self.files[key].get(rel.replace(BS, "/").lower())

    def get(self, side: str, kind: str, rel: str) -> dict | None:
        """Statistics of direction 0 of a DCC: frames n, box w x h (classic pixels), mean colour, hue/sat/val."""
        key = f"{side}/{kind}/{rel.lower()}"
        if key in self.d:
            return self.d[key]
        p = self.locate(side, kind, rel)
        st = None
        if p is not None:
            try:
                st = self.stats(p.read_bytes())
            except Exception as e:  # undecodable art: no statistics
                print(f"   art {key}: {e}")
        self.d[key] = st
        self.dirty = True
        return st

    def get_shifted(self, side: str, kind: str, rel: str, n: int) -> dict | None:
        """The statistics of the art drawn through PalShift n (palshift.py); the D2R copy when Median has none."""
        key = f"{side}/{kind}/{rel.lower()}#p{n}"
        if key not in self.d:
            import palshift
            p = self.locate(side, kind, rel) or self.locate("d2r", kind, rel)
            st = None
            if p is not None:
                try:
                    st = self.stats(p.read_bytes(), palshift.table(n))
                except Exception as e:
                    print(f"   art {key}: {e}")
            self.d[key] = st
            self.dirty = True
        return self.d[key]

    def stats(self, data: bytes, remap: bytes | None = None) -> dict | None:
        import dcc
        import skill_icons
        import struct
        if self.pal is None:
            self.pal = np.array(skill_icons.palette(), np.float64)
        dirs = data[2]
        fpd = struct.unpack_from("<I", data, 3)[0]
        offs = list(struct.unpack_from(f"<{dirs}I", data, 15)) + [len(data)]
        x0, y0, dw, dh, frames = dcc.decode_direction(data[offs[0]:offs[1]], fpd)
        n = len(frames)
        pick = sorted({int(i * n / 12) for i in range(12)}) if n > 12 else range(n)
        sum_rgb, sum_w = np.zeros(3), 0.0
        minx = miny = 10 ** 9
        maxx = maxy = -1
        hue_h = np.zeros(12)
        for i in range(n):
            fx, fy, fw, fh, pix = frames[i]
            a = np.frombuffer(pix, np.uint8).reshape(fh, fw)
            ys, xs = np.nonzero(a)
            if len(xs):
                minx, maxx = min(minx, fx + xs.min()), max(maxx, fx + xs.max())
                miny, maxy = min(miny, fy + ys.min()), max(maxy, fy + ys.max())
            if i in pick and len(xs):
                idx = a[ys, xs]
                rgb = self.pal[np.frombuffer(remap, np.uint8)[idx] if remap is not None else idx]
                w = rgb.max(axis=1) / 255.0
                sum_rgb += (rgb * w[:, None]).sum(axis=0)
                sum_w += w.sum()
                mx, mn = rgb.max(axis=1), rgb.min(axis=1)
                sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1), 0)
                hue = _hues(rgb, mx, mn)
                np.add.at(hue_h, (hue * 12).astype(int) % 12, w * sat)
        if sum_w == 0 or maxx < 0:
            return None
        mean = sum_rgb / sum_w
        h, s, v = colorsys.rgb_to_hsv(*(mean / 255.0))
        return dict(n=n, w=int(maxx - minx + 1), h=int(maxy - miny + 1), rgb=[round(float(c)) for c in mean],
                    hsv=[round(h, 3), round(s, 3), round(v, 3)],
                    hue=[round(float(x), 1) for x in hue_h / max(1e-9, hue_h.sum()) * 100])

    def save(self) -> None:
        if self.dirty:
            self.path.write_text(json.dumps(self.d, separators=(",", ":")), "utf-8")


def _hues(rgb: np.ndarray, mx: np.ndarray, mn: np.ndarray) -> np.ndarray:
    """Hue 0..1 of (n, 3) rgb rows."""
    d = np.maximum(mx - mn, 1e-9)
    r, g, b = rgb[:, 0], rgb[:, 1], rgb[:, 2]
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4))
    return np.where(mx == mn, 0, h / 6) % 1.0


def colour_dist(a: dict, b: dict) -> float:
    """0 (same) .. 1 (opposite): hue distance weighted by the lower saturation, plus saturation and value."""
    (h1, s1, v1), (h2, s2, v2) = a["hsv"], b["hsv"]
    dh = min(abs(h1 - h2), 1 - abs(h1 - h2)) * 2
    return min(1.0, dh * min(1.0, 1.6 * min(s1, s2)) + 0.5 * abs(s1 - s2) + 0.3 * abs(v1 - v2))


def size_of(st: dict) -> float:
    return math.sqrt(max(1, st["w"]) * max(1, st["h"]))


# ----------------------------------------------------------------------------------------------- profiles
class Prof:
    """What a missiles.txt / overlay.txt row looks like: art statistics, behaviour and the words around it."""

    def __init__(self, kind: str, name: str, art_name: str, row: dict, art: dict | None):
        self.kind, self.name, self.art_name, self.row, self.art = kind, name, art_name, row, art
        self.skills: set[str] = set()
        self.hit = self.sub = False
        self.shift = 0  # missiles2 PalShift (Median rows), 0 = none
        self.shift_dist = 0.0  # colour distance the shift moves the art by
        self.via = ""  # 'provenance' / 'provenance+tint' when art_provenance.json decided the effect
        self.model = ""  # D2R: the HD model
        self.folder = ""
        self._tags = None

    def num(self, col: str) -> float:
        try:
            return float(self.row.get(col) or 0)
        except ValueError:
            return 0.0

    @property
    def moving(self) -> bool:
        return max(self.num("Vel"), self.num("MaxVel")) > 0

    def words_tags(self):
        if self._tags is None:
            self._tags = tags(self.name, self.art_name.replace(BS, " "), *self.skills)
        return self._tags


HIT_COLS = ["ExplosionMissile", "HitSubMissile1", "HitSubMissile2", "HitSubMissile3", "HitSubMissile4",
            "CltHitSubMissile1", "CltHitSubMissile2", "CltHitSubMissile3", "CltHitSubMissile4"]
SUB_COLS = ["SubMissile1", "SubMissile2", "SubMissile3", "CltSubMissile1", "CltSubMissile2", "CltSubMissile3"]
SKILL_MISSILE_COLS = ["srvmissile", "srvmissilea", "srvmissileb", "srvmissilec", "cltmissile", "cltmissilea",
                      "cltmissileb", "cltmissilec", "cltmissiled"]
SKILL_OVERLAY_COLS = ["srvoverlay", "sumoverlay", "tgtoverlay", "prgoverlay", "castoverlay", "cltoverlaya",
                      "cltoverlayb"]
STATE_OVERLAY_COLS = ["overlay1", "overlay2", "overlay3", "overlay4", "pgsvoverlay", "castoverlay", "removerlay"]


def table(path: Path) -> tuple[list[str], list[dict]]:
    h, rows = read_tsv(path)
    return h, [dict(zip(h, r)) for r in rows if r and r[0]]


def missile_profiles(excel: Path, side: str, art: ArtCache) -> dict[str, Prof]:
    _, rows = table(excel / "missiles.txt")
    _, skills = table(excel / "skills.txt")
    profs: dict[str, Prof] = {}
    for r in rows:
        cel = norm_art(r["CelFile"])
        st = art.get(side, "missiles", cel) if cel not in ("", "null") else None
        profs[r["Missile"].lower()] = Prof("missile", r["Missile"], cel, r, st)
    for p in profs.values():
        for c in HIT_COLS:
            t = profs.get(p.row.get(c, "").lower())
            if t:
                t.hit = True
        for c in SUB_COLS + ["CltSubMissile1"]:
            t = profs.get(p.row.get(c, "").lower())
            if t:
                t.sub = True
    # skill words flow down the missile chain (skill -> missile -> sub / hit missiles)
    todo = []
    for s in skills:
        for c in SKILL_MISSILE_COLS:
            t = profs.get(s.get(c, "").lower())
            if t and s["skill"] not in t.skills:
                t.skills.add(s["skill"])
                todo.append((t, s["skill"]))
    while todo:
        t, sk = todo.pop()
        for c in HIT_COLS + SUB_COLS + ["ProgOverlay"]:
            u = profs.get(t.row.get(c, "").lower())
            if u and sk not in u.skills and len(u.skills) < 6:
                u.skills.add(sk)
                todo.append((u, sk))
    return profs


def overlay_profiles(excel: Path, side: str, art: ArtCache) -> dict[str, Prof]:
    _, rows = table(excel / "overlay.txt")
    _, skills = table(excel / "skills.txt")
    _, states = table(excel / "states.txt")
    profs: dict[str, Prof] = {}
    for r in rows:
        if not r["overlay"] or r["overlay"] == "Expansion":
            continue
        fn = norm_art(r["Filename"])
        st = art.get(side, "overlays", fn) if fn not in ("", "null") else None
        p = Prof("overlay", r["overlay"], fn, r, st)
        p.folder = r["Character"]
        profs[r["overlay"].lower()] = p
    for s in skills:
        for c in SKILL_OVERLAY_COLS:
            t = profs.get(s.get(c, "").lower())
            if t and len(t.skills) < 6:
                t.skills.add(s["skill"])
    for s in states:
        for c in STATE_OVERLAY_COLS:
            t = profs.get(s.get(c, "").lower())
            if t and len(t.skills) < 6:
                t.skills.add(s["state"])
    return profs


# ----------------------------------------------------------------------------------------------- scoring
def log2r(a: float, b: float) -> float:
    return abs(math.log2(max(a, 1) / max(b, 1)))


def cost(m: Prof, c: Prof, same_art: bool) -> tuple[float, list[str]]:
    """Lower is closer. same_art: both rows use the same art file (colour and size are then equal)."""
    s, why = 0.0, []
    if not same_art:
        if m.art and c.art:
            cd = colour_dist(m.art, c.art)
            s += 3.0 * cd + 1.2 * min(3.0, log2r(size_of(m.art), size_of(c.art))) \
                + 0.35 * min(3.0, log2r(m.art["n"], c.art["n"]))
            why.append(f"colour {cd:.2f}")
        else:
            s += 3.0
    if m.kind == "missile":
        s += 1.5 * (m.moving != c.moving) + 1.0 * (m.hit != c.hit) + 0.5 * (m.sub != c.sub)
        s += 0.6 * ((m.row.get("LoopAnim") == "1") != (c.row.get("LoopAnim") == "1"))
        if same_art:  # same art: the closer rows are the closer movement, light and range
            s += 0.25 * min(4, log2r(max(m.num("Vel"), 1), max(c.num("Vel"), 1)))
            s += 0.2 * min(4, log2r(m.num("Range") + 1, c.num("Range") + 1))
            s += 0.15 * min(4, abs(m.num("Light") - c.num("Light")) / 3)
            s += 0.1 * (m.row.get("Trans") != c.row.get("Trans"))
    else:
        s += 0.8 * ((m.row.get("PreDraw") == "1") != (c.row.get("PreDraw") == "1"))
        s += 0.15 * min(4, abs(m.num("Height1") - c.num("Height1")) / 20)
    mf, ms, mw = m.words_tags()
    cf, cs, cw = c.words_tags()
    if m.shift:  # the name's element (fire, cold...) is the colour before the shift: no family credit
        mf = set()
    s -= FAM_W * min(3, len(mf & cf)) + SHAPE_W * min(3, len(ms & cs)) + WORD_W * min(3, len(mw & cw))
    if mf and cf and not (mf & cf):
        s += 0.8
    for opposed in (("front", "back"),):
        if (opposed[0] in ms and opposed[1] in cs) or (opposed[1] in ms and opposed[0] in cs):
            s += 1.5
    if not c.skills and not same_art:
        s += 0.4  # monster-only effects last
    return s, why


def pick(m: Prof, cands: list[Prof], same_art: bool):
    best = sorted(((cost(m, c, same_art)[0], i, c) for i, c in enumerate(cands)), key=lambda t: (t[0], t[1]))
    sc, _, c = best[0]
    margin = best[1][0] - sc if len(best) > 1 else 9.9
    return c, sc, margin, cost(m, c, same_art)[1]


# ----------------------------------------------------------------------------------------------- definitions
def scaled_definition(d: dict, name: str, k: float) -> dict:
    d = copy.deepcopy(d)
    d["name"] = name
    if k != 1:
        for e in d.get("entities", []):
            for c in e.get("components", []):
                if c["type"] == "TransformDefinitionComponent":
                    for ax in "xyz":
                        c["scale"][ax] = round(c["scale"][ax] * k, 4)
                        c["position"][ax] = round(c["position"][ax] * k, 4)
    return d


def write_json(p: Path, d) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(d, separators=(",", ":")), encoding="utf-8")


def scale_for(m: Prof, c: Prof) -> float:
    """Scale of the stand-in so it fills about the space Median's art does: ratio of the classic arts, damped."""
    if not (m.art and c.art):
        return 1.0
    ratio = size_of(m.art) / size_of(c.art)
    k = min(1.8, max(0.6, ratio ** 0.7))
    k = round(k * 10) / 10
    return 1.0 if 0.85 <= k <= 1.15 else k


def confidence(kind: str, sc: float, margin: float, ncand: int, m: Prof, c: Prof) -> str:
    """exact: sure unless several D2R models share the art and the rows hardly tell them apart. stand-in: high when
    the element/theme words agree and the colours are close, medium when one of them does."""
    if kind == "exact":
        return "high" if ncand == 1 or margin > 0.5 else "medium"
    fam = bool(m.words_tags()[0] & c.words_tags()[0])
    col = colour_dist(m.art, c.art) if m.art and c.art else 1.0
    if fam and col <= 0.25:
        return "high"
    if fam or col <= 0.25:
        return "medium"
    return "low"


# ----------------------------------------------------------------------------------------------- provenance
def load_provenance(path: Path) -> dict:
    """Median art file ('missiles/<name>', '/' separators) -> match record of tools/art_provenance.py (effects group)."""
    if not path.exists():
        return {}
    return json.loads(path.read_text("utf-8")).get("effects", {}).get("files", {})


def prov_pick(prov: dict, m: Prof, by_art: dict[str, list[Prof]], folder: str, recolour):
    """The HD effect of the vanilla animation Median's art was made from. None when the art is not a vanilla animation
    with an HD effect of the same kind (missile / overlay), or when the colour changed and cannot be reproduced (the
    scoring then picks the effect of the nearest colour). Returns a dict: c (D2R row), kind, scale, conf, why, model."""
    r = prov.get(f"{folder}/{m.art_name.replace(BS, '/')}")
    if not r or r.get("category") in (None, "new", "error") or r.get("coverage", 0) < PROV_COVER:
        return None
    PROV_STATS["rows with a vanilla match"] += 1
    for s in r["sources"]:
        if not s["file"].startswith(folder + "/") or s["q"] < PROV_Q or s["share"] < PROV_SHARE:
            continue
        rows = by_art.get(s["file"][len(folder) + 1:].replace("/", BS))
        if not rows:
            PROV_STATS["vanilla source has no HD effect"] += 1
            continue
        c, sc, margin, _ = pick(m, rows, True)
        k = 1.0
        if "rescale" in s["kind"]:
            k = min(2.5, max(0.4, round(s["scale"] * 20) / 20))
            k = 1.0 if 0.85 <= k <= 1.15 else k
        tint = None
        if "recolour" in s["kind"] and m.art and c.art and colour_dist(m.art, c.art) > SHIFT_MIN:
            tint = recolour(c, s.get("shift"), m.art, c.art)
            if tint is None:
                PROV_STATS["recolour not reproducible (no gradient colour), nearest-colour scoring"] += 1
                return None  # colour cannot be reproduced: the nearest-colour scoring decides
        what = {"exact": "exact", "recolour": "recoloured" if tint else "recoloured (colour close)",
                "rescale": "rescaled", "rescale+recolour": "rescaled, recoloured"}[s["kind"]]
        why = (f"provenance: art is {what} vanilla {s['file']} ({s['frames']} of {r['frames']} frames, q {s['q']:.2f}"
               f"{', retimed' if r.get('retime') else ''}{', mashup' if r['category'] == 'mashup' else ''}); "
               f"D2R {c.name} draws it" + (f" ({len({x.model or x.folder for x in rows})} effects)" if len(rows) > 1 else ""))
        PROV_STATS["exact" if not tint and k == 1 else "scaled" if not tint else "tinted"] += 1
        return {"c": c, "kind": "variant" if k != 1 else "exact", "scale": k, "conf": r["confidence"], "why": why,
                "tint": tint, "margin": margin}
    return None


# ----------------------------------------------------------------------------------------------- main
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--work", default=str(ROOT / "work"), help="work dir with mod/ (default work)")
    ap.add_argument("--out", help="output data dir (default <work>/mod/data)")
    ap.add_argument("--only", help="comma separated row names (missiles and overlays)")
    ap.add_argument("--no-scale", action="store_true")
    ap.add_argument("--no-provenance", action="store_true", help="skip tools/art_provenance.py matches")
    args = ap.parse_args()
    work = Path(args.work)
    out = Path(args.out) if args.out else work / "mod" / "data"
    only = {x.lower() for x in args.only.split(",")} if args.only else None
    ensure_hd_json()
    prov = {} if args.no_provenance else load_provenance(out.parent / "art_provenance.json")
    if not args.no_provenance and not prov:
        print("no work/mod/art_provenance.json: run tools/art_provenance.py for exact-art matches")
    import hd_recolour
    art = ArtCache(work / "effects_hd_art.json")
    d2r_excel, mod_excel = D2R / "data" / "global" / "excel", out / "global" / "excel"

    hd_m = load_json(out / "hd" / "missiles" / "missiles.json") if (out / "hd" / "missiles" / "missiles.json").exists() \
        else load_json(HDM / "missiles.json")
    d2r_hd_m = load_json(HDM / "missiles.json")
    deps = hd_m.pop("dependencies", None)
    d2r_hd_m.pop("dependencies", None)
    hdo_files = {p.relative_to(HDO).with_suffix("").as_posix().lower() for p in HDO.rglob("*.json")
                 if "/" in p.relative_to(HDO).as_posix()}

    # ---------------------------------------------------------------- missiles
    dm, mm = missile_profiles(d2r_excel, "d2r", art), missile_profiles(mod_excel, "mxl", art)
    # D2R art that has a Median name: where the art is not Median's own, the D2R copy is the one drawn
    for p in mm.values():
        if p.art is None and p.art_name not in ("", "null"):
            p.art = art.get("d2r", "missiles", p.art_name)
    # PalShift (missiles2): the HD effects cannot be tinted, so the row is scored by the colour of its art after the
    # shift. A shift that moves the colour (> SHIFT_MIN) keeps the row off the exact / hand-picked routes: the art's
    # own D2R effect would be the wrong colour.
    for name, n in palshift.shifted_rows().items():
        m = mm.get(name)
        if m is None or m.art_name in ("", "null"):
            continue
        sh = art.get_shifted("mxl", "missiles", m.art_name, n)
        if sh is not None and m.art is not None:
            m.shift_dist = colour_dist(m.art, sh)
            if m.shift_dist > SHIFT_MIN:
                m.shift, m.art = n, sh
    by_art: dict[str, list[Prof]] = defaultdict(list)
    for p in dm.values():
        if norm_key(p.name) in d2r_hd_m and d2r_hd_m[norm_key(p.name)] != "null":
            p.model = d2r_hd_m[norm_key(p.name)]
            by_art[p.art_name].append(p)
    library = [p for p in dm.values() if p.model and p.art and p.art_name not in ("", "null")]
    d2r_arts = {p.art_name for p in dm.values()}
    changed_vanilla = {norm_key(n) for n, m in mm.items() if n in dm and dm[n].art_name != m.art_name}
    rows_m = []  # (name, art, kind, model, scale, conf, why)
    new_models: dict[str, tuple[str, float]] = {}  # variant model name -> (base model, scale)
    keys: dict[str, str] = {}
    raw: dict[str, str] = {}
    for name, m in mm.items():
        if only and name not in only:
            continue
        v = dm.get(name)
        if v is not None and v.art_name == m.art_name and norm_key(name) in d2r_hd_m and not m.shift:
            continue  # D2R's own missile with D2R's art and HD effect
        c = None
        if m.art_name in ("", "null"):
            model, kind, scale, conf, why = "null", "exact", 1.0, "high", "no art (logic missile)"
        else:
            src = None if m.shift else by_art.get(m.art_name)
            if src:
                c, sc, margin, _ = pick(m, src, True)
                kind, scale, model = "exact", 1.0, c.model
                conf = confidence("exact", sc, margin, len({s.model for s in src}), m, c)
                why = f"D2R {c.name} uses the art" + (f" ({len({s.model for s in src})} models)" if len({s.model for s in src}) > 1 else "")
            elif (pv := None if m.shift else prov_pick(prov, m, by_art, "missiles", hd_recolour.plan_missile(HDM))):
                c, kind, scale, conf, why, model = pv["c"], pv["kind"], pv["scale"], pv["conf"], pv["why"], pv["c"].model
                m.via = "provenance"
                if pv["tint"]:
                    model = hd_recolour.recolour_missile(out, model, pv["tint"])
                    kind, why, m.via = "variant", why + f"; tinted ({pv['tint']['label']})", "provenance+tint"
                if args.no_scale:
                    scale = 1.0
            else:
                hand = [] if m.shift else [p for p in library if p.model == LOOKALIKE.get(m.art_name)]
                c, sc, margin, parts = pick(m, hand or library, False)
                scale = 1.0 if args.no_scale else scale_for(m, c)
                kind, model = "stand-in", c.model
                conf = "high" if hand else confidence("stand-in", sc, margin, len(library), m, c)
                parts = (["hand-picked look-alike (missile_hd.py)"] if hand else []) + parts
                has_d2r_art = m.art_name in d2r_arts
                why = (f"{'vanilla art without HD effect' if has_d2r_art else 'custom art, last'}; like D2R {c.name} "
                       f"({c.art_name}); score {sc:.1f}, margin {margin:.1f}; " + ", ".join(parts))
                if m.shift:
                    cd = colour_dist(m.art, c.art) if c.art else 1.0
                    far = cd > SHIFT_FAR
                    conf = "low" if far else conf
                    why = (f"palette shift {m.shift} (moves the colour by {m.shift_dist:.2f}), chosen by the shifted "
                           f"colour; " + why + ("; NO vanilla effect of that colour: custom art" if far else ""))
            if scale != 1:
                vname = f"mxl_{model}_s{round(scale * 100)}"
                new_models[vname] = (model, scale)
                model = vname
                kind = "variant" if kind == "exact" else kind
        rows_m.append((m.name, m.art_name, kind, model, scale, conf, why, m, c))
        keys[norm_key(name)] = model
        raw[name.lower()] = model
    # write missiles
    for vname, (base, k) in new_models.items():
        src = out / "hd" / "missiles" / f"{base}.json"
        write_json(out / "hd" / "missiles" / f"{vname}.json",
                   scaled_definition(load_json(src if src.exists() else HDM / f"{base}.json"), vname, k))
    final = dict(hd_m)
    kept = 0
    for key, model in {**{k: v for k, v in raw.items() if k not in keys}, **keys}.items():
        if key in d2r_hd_m and d2r_hd_m[key] != model and key not in changed_vanilla:
            kept += 1  # a Median name that normalises onto a D2R key: D2R's effect stays
            continue
        final[key] = model
    if deps is not None:
        final = {"dependencies": deps, **final}
    write_json(out / "hd" / "missiles" / "missiles.json", final)

    stats_m = Counter(PROV_STATS)

    # ---------------------------------------------------------------- overlays
    do, mo = overlay_profiles(d2r_excel, "d2r", art), overlay_profiles(mod_excel, "mxl", art)
    by_file: dict[str, list[Prof]] = defaultdict(list)
    for p in do.values():
        if f"{p.folder}/{norm_key(p.name)}".lower() in hdo_files:
            by_file[p.art_name].append(p)
    olib = [p for p in do.values() if f"{p.folder}/{norm_key(p.name)}".lower() in hdo_files and p.art and p.art_name]
    d2r_oarts = {p.art_name for p in do.values()}
    rows_o = []
    char_edits: dict[str, str] = {}
    for name, m in mo.items():
        if only and name not in only:
            continue
        v = do.get(name)
        if v is not None and v.art_name == m.art_name:
            continue
        c = tint_o = None
        if m.art_name in ("", "null"):
            rows_o.append((m.name, m.art_name, "exact", "", 1.0, "high", "no art", m, None))
            continue
        src = by_file.get(m.art_name)
        if src:
            c, sc, margin, _ = pick(m, src, True)
            kind, scale, conf = "exact", 1.0, confidence("exact", sc, margin, len(src), m, c)
            why = f"D2R {c.name} uses the art"
        elif (pv := prov_pick(prov, m, by_file, "overlays", hd_recolour.plan_overlay(HDO, norm_key))):
            c, kind, scale, conf, why = pv["c"], pv["kind"], pv["scale"], pv["conf"], pv["why"]
            m.via = "provenance"
            if args.no_scale:
                scale = 1.0
            tint_o = pv["tint"]
            if tint_o:
                m.via = "provenance+tint"
                kind, why = "variant", why + f"; tinted ({tint_o['label']})"
        else:
            c, sc, margin, parts = pick(m, olib, False)
            scale = 1.0 if args.no_scale else scale_for(m, c)
            kind, conf = "stand-in", confidence("stand-in", sc, margin, len(olib), m, c)
            has_d2r_art = m.art_name in d2r_oarts
            why = (f"{'vanilla art without HD effect' if has_d2r_art else 'custom art, last'}; like D2R {c.name} "
                   f"({c.art_name}); score {sc:.1f}, margin {margin:.1f}; " + ", ".join(parts))
        if scale != 1 and kind == "exact":
            kind = "variant"
        base = HDO / c.folder / f"{norm_key(c.name)}.json"
        rows_o.append((m.name, m.art_name, kind, f"{c.folder}/{norm_key(c.name)}", scale, conf, why, m, c))
        folder = c.folder
        d = scaled_definition(load_json(base), norm_key(name), scale)
        if tint_o:
            d = hd_recolour.recolour_overlay(out, d, tint_o)
        write_json(out / "hd" / "overlays" / folder / f"{norm_key(name)}.json", d)
        if m.folder != folder:
            char_edits[name] = folder
    # overlay.txt Character: the HD folder, in both banks (excel/ and excel/base/)
    if char_edits:
        for p in (mod_excel / "overlay.txt", mod_excel / "base" / "overlay.txt"):
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            ci, ni = h.index("Character"), h.index("overlay")
            for r in rows:
                if r[ni].lower() in char_edits:
                    r[ci] = char_edits[r[ni].lower()]
            nl = CRLF if CRLF in p.read_text("utf-8-sig", errors="replace")[:4096] else LF
            p.write_text(nl.join(TAB.join(x) for x in [h] + rows) + nl, encoding="utf-8", newline="")

    stats_o = PROV_STATS - stats_m

    # ---------------------------------------------------------------- report
    rep = ["# HD effects of Median's missiles and overlays (tools/effects_hd.py)", ""]
    for label, rows in (("Missiles", rows_m), ("Overlays", rows_o)):
        kinds = Counter(r[2] for r in rows)
        confs = Counter((r[2], r[5]) for r in rows)
        custom = [r for r in rows if r[2] == "stand-in" and "custom art" in r[6]]
        scaled = sum(1 for r in rows if r[2] == "stand-in" and r[4] != 1)
        rep += [f"## {label}: {len(rows)} rows", "", "| Class | Rows | high | medium | low |", "|---|---|---|---|---|"]
        for k in ("exact", "variant", "stand-in"):
            rep.append(f"| {k} | {kinds.get(k, 0)} | {confs.get((k, 'high'), 0)} | {confs.get((k, 'medium'), 0)} | "
                       f"{confs.get((k, 'low'), 0)} |")
        rep += ["", f"Stand-ins drawn scaled to Median's art size: {scaled}. Custom art (last): {len(custom)} rows on "
                f"{len({r[1] for r in custom})} animations; the other stand-ins have D2R art but no D2R HD effect.", ""]
        st = stats_m if label == "Missiles" else stats_o
        via = Counter(r[7].via for r in rows if r[7].via)
        if prov:
            rep += [f"Provenance (tools/art_provenance.py, work/mod/art_provenance.json): {sum(via.values())} rows take the "
                    f"HD effect of the vanilla animation their art was made from instead of a stand-in: "
                    f"{st.get('exact', 0)} exact, {st.get('scaled', 0)} rescaled, {st.get('tinted', 0)} recoloured "
                    f"(gradient tint). Of {st.get('rows with a vanilla match', 0)} rows with a vanilla match, "
                    f"{st.get('vanilla source has no HD effect', 0)} follow a source no D2R row draws in HD and "
                    f"{st.get('recolour not reproducible (no gradient colour), nearest-colour scoring', 0)} are recolours of "
                    f"an effect whose colour is baked into its particles (nearest-colour stand-in).", ""]
        if label == "Missiles":
            shifted = [r for r in rows if r[7].shift]
            far = [r for r in shifted if "NO vanilla effect" in r[6]]
            rep += [f"PalShift (missiles2): {len(palshift.shifted_rows())} Median rows have one. {len(shifted)} of them "
                    f"move the art's colour (> {SHIFT_MIN}) and were scored by the shifted colour; HD effects cannot be "
                    f"tinted, so each takes the vanilla effect of the nearest colour. {len(far)} have none within "
                    f"{SHIFT_FAR} and stay custom art: " + (", ".join(r[0] for r in far) or "none") + ".", ""]
            ex =[r for r in rows if r[2] == "exact" and r[8] is not None]
            diff = Counter()
            for r in ex:
                m, c = r[7], r[8]
                diff["light radius"] += m.num("Light") != c.num("Light")
                diff["light colour"] += ((m.row.get("Red"), m.row.get("Green"), m.row.get("Blue"))
                                         != (c.row.get("Red"), c.row.get("Green"), c.row.get("Blue")))
                diff["transparency (Trans)"] += m.row.get("Trans") != c.row.get("Trans")
                diff["speed (Vel)"] += m.num("Vel") != c.num("Vel")
                diff["range"] += m.num("Range") != c.num("Range")
                diff["animation (animrate / AnimLen / AnimSpeed)"] += (
                    (m.row.get("animrate"), m.row.get("AnimLen"), m.row.get("AnimSpeed"))
                    != (c.row.get("animrate"), c.row.get("AnimLen"), c.row.get("AnimSpeed")))
            rep += [f"Exact rows (art shared with a D2R row) whose missiles.txt values differ from the D2R row they follow "
                    f"({len(ex)} rows): " + "; ".join(f"{k} {v}" for k, v in diff.items()) + ". An HD definition has "
                    f"no field for these (only the transform scale, a spot light and a fade-in alpha exist), so they "
                    f"stay as the engine reads them from missiles.txt.", ""]
    rep += ["## Rows", "", "| Kind | Row | Art | HD effect | Scale | Class | Confidence | Why |", "|---|---|---|---|---|---|---|---|"]
    for kind, rows in (("missile", rows_m), ("overlay", rows_o)):
        for name, a, k, model, scale, conf, why, _, _c in sorted(rows, key=lambda r: (r[2], r[5], r[0])):
            rep.append(f"| {kind} | {name} | {a} | {model} | {scale:g} | {k} | {conf} | {why.replace('|', '/')} |")
    (out.parent / "effects_hd.md").write_text("\n".join(rep) + "\n", encoding="utf-8")
    art.save()
    km, ko = Counter(r[2] for r in rows_m), Counter(r[2] for r in rows_o)
    print(f"missiles {len(rows_m)}: {dict(km)}; overlays {len(rows_o)}: {dict(ko)}; {len(new_models)} scaled "
          f"missile models; report -> {out.parent / 'effects_hd.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

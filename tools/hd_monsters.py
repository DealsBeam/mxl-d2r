"""Better HD looks for Median-only monsters without new 3D models (Phase 5, see docs/hd-monsters.md).

tools/gfx_placeholders.py lets 1,193 Median monsters keep their own Code/monstats2 row (timing, classic art) and only
borrows the HD model of a D2R monster (first D2R monster with the same MonType: often wrong). D2R describes an HD
monster as a UnitDefinition (data/hd/character/enemy|npc/<name>.json: parts, skeleton, scale, state machine, variant
table) that data/hd/character/monsters.json assigns to a monstats Id. Everything but the models is data we can write,
so this step builds one UnitDefinition per Median monster look:

  donor      a D2R unit chosen by family (name keywords + MonType + AI), size (Median's DCC art height against the
             donor's classic art height), animation coverage (the modes Median's monstats2 uses against the donor's
             state machine) and weapon class
  scale      TransformDefinitionComponent scale of the unit, to the size ratio of the two classic arts
  tint       a new VariantTransformTable (colorAdjustment hue/sat/value + colorTint) from the colour difference of
             Median's DCC art (units palette, palshift.dat of the monster's TransLvl) to the donor's classic art
  parts      Median's monstats2 asks for parts and variants (HD/TR/LG.../S1.. flags and the *v lists); the unit gets
             those (part, variant) pairs, cloned from the donor's parts, because a part without its variant is not
             drawn
  state machine  the donor's, with each state bound for Median's weapon class too

  provenance tools/art_provenance.py matches Median's monster parts against D2R's by image: a Median monster whose parts
             are D2R parts (the same sprites, recoloured, rescaled) gets the unit of that D2R monster with the matching
             (part, variant) pairs, the measured scale and the colour difference as tint, ahead of the scoring above,
             which stays the fallback (no match, no D2R unit for the source code, or a unit that lacks Median's modes)

Writes into <out>/hd/character (units mxl_*.json in enemy/, their variant tables and state machines in enemy/mxl/,
monsters.json updated) and <out>/../hd_monsters.md (every Median monster -> donor + scale + tint + confidence).
Run after gfx_placeholders.py. Usage: python tools/hd_monsters.py [--out DIR] [--work DIR] [--only ID,ID]
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
from txt_migrate import read_tsv, is_record  # noqa: E402
from strings_ns import bare  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
D2R = ROOT / "extract" / "d2r" / "93847"
MXL_MON = ROOT / "extract" / "mxl" / "data" / "global" / "monsters"
GAME = r"C:\Program Files (x86)\Diablo II Resurrected"
HDC = D2R / "data" / "hd" / "character"
PARTS = {"HD": "head", "TR": "torso", "LG": "legs", "RA": "rarm", "LA": "larm", "RH": "rhand", "LH": "lhand",
         "SH": "shield", **{f"S{i}": f"special{i}" for i in range(1, 9)}}
PART_CODES = list(PARTS)
MODES = ["DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN"]
MODE_ID = {"DT": 0, "NU": 1, "WL": 2, "GH": 3, "A1": 4, "A2": 5, "BL": 6, "SC": 7, "S1": 8, "S2": 9, "S3": 10,
           "S4": 11, "DD": 12, "KB": 13, "SQ": 14, "RN": 15}


# ------------------------------------------------------------------ D2R HD data (cached under extract/d2r)
def ensure_hd_json() -> None:
    """Unit definitions, variant tables and state machines of D2R's monsters (small json, no models)."""
    marker = HDC / "enemy" / "zombie1" / "zombie1_variant.json"
    if marker.exists():
        return
    import native
    st = native.open_casc(GAME)
    pat = re.compile(r"data/hd/character/(enemy|npc)/.*\.json$")
    n = 0
    for line in (D2R / "filelist.txt").read_text("utf-8").splitlines():
        l = line.strip()
        if pat.search(l):
            p = D2R / l.split(":", 1)[1]
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(native.casc_read(st, l))
            n += 1
    print(f"extracted {n} HD monster json files")


def load_json(p: Path):
    return json.loads(p.read_text("utf-8-sig"))


def unit_path(unit: str) -> Path | None:
    for sub in ("enemy", "npc"):
        p = HDC / sub / f"{unit}.json"
        if p.exists():
            return p
    return None


# ------------------------------------------------------------------ classic art statistics
class Art:
    """Colour histogram (palette indices) + box of a monster's frame 0, direction 0 of mode NU."""

    def __init__(self):
        import dcc
        import skill_icons
        self.dcc = dcc
        self.pal = skill_icons.palette()
        self._casc = None

    def casc(self):
        if self._casc is None:
            import native
            self._casc = (native, native.open_casc(GAME))
        return self._casc

    def read(self, side: str, rel: str) -> bytes | None:
        if side == "mxl":
            p = MXL_MON / rel
            if p.exists():
                return p.read_bytes()
            q = MXL_MON / rel.lower()
            return q.read_bytes() if q.exists() else self._ci(rel)
        if self._d2r is None:
            self._d2r = {l.strip() for l in (D2R / "filelist.txt").read_text("utf-8").splitlines()}
        name = "data:data/global/monsters/" + rel.lower()
        if name not in self._d2r:  # a read of a missing file takes 1.7 s
            return None
        native, st = self.casc()
        return native.casc_read(st, name) or None

    _dirs: dict = {}
    _d2r = None

    def _ci(self, rel: str) -> bytes | None:
        d, _, f = rel.rpartition("/")
        key = d.lower()
        if key not in self._dirs:
            base = MXL_MON / d
            self._dirs[key] = {x.name.lower(): x for x in base.iterdir()} if base.exists() else {}
        p = self._dirs[key].get(f.lower())
        return p.read_bytes() if p else None

    def palshift(self, side: str, code: str) -> list[list[int]] | None:
        b = self.read(side, f"{code}/cof/palshift.dat")
        if not b or len(b) < 2048:
            return None
        return [list(b[i * 256:(i + 1) * 256]) for i in range(8)]

    def stats(self, side: str, code: str, row2: dict, weapon: str) -> dict | None:
        """{hist: 256 counts, box: (x0, y0, x1, y1)} over the monster's parts, first listed variants."""
        hist = [0] * 256
        box = None
        n = 0
        for pc in PART_CODES:
            if row2.get(pc) != "1":
                continue
            variants = [v for v in re.split(r"[,\s]+", row2.get(pc + "v", "").strip('"')) if v and v != "nil"]
            seen = []
            for v in variants:
                if v in seen:
                    continue
                seen.append(v)
            tried = False
            for v in seen[:3]:
                for w in dict.fromkeys([weapon, "hth"]):
                    b = self.read(side, f"{code}/{pc.lower()}/{code}{pc.lower()}{v}nu{w}.dcc")
                    if b:
                        tried = True
                        try:
                            x0, y0, dw, dh, frames = self.dcc.decode(b)[0]
                        except Exception:
                            b = None
                            continue
                        fx, fy, fw, fh, pix = frames[0]
                        for i in pix:
                            if i:
                                hist[i] += 1
                        bx = (fx, fy, fx + fw, fy + fh)
                        box = bx if box is None else (min(box[0], bx[0]), min(box[1], bx[1]), max(box[2], bx[2]),
                                                      max(box[3], bx[3]))
                        n += 1
                        break
                if tried:
                    break
        if not n or not sum(hist):
            return None
        return {"hist": hist, "box": box, "parts": n}


def colour_stats(hist: list[int], pal, shift: list[int] | None = None) -> dict:
    """Mean RGB, mean hue (turns, weighted by saturation x value), hue confidence, saturation, value (0..1)."""
    tot = r = g = b = hx = hy = hw = s_sum = v_sum = 0.0
    for i, c in enumerate(hist):
        if not c:
            continue
        R, G, B = pal[shift[i] if shift else i]
        h, s, v = colorsys.rgb_to_hsv(R / 255, G / 255, B / 255)
        tot += c
        r += R * c; g += G * c; b += B * c
        w = c * s * v
        hx += w * math.cos(h * 2 * math.pi); hy += w * math.sin(h * 2 * math.pi); hw += w
        s_sum += s * c; v_sum += v * c
    if not tot:
        return {}
    return {"rgb": (r / tot / 255, g / tot / 255, b / tot / 255),
            "hue": (math.atan2(hy, hx) / (2 * math.pi)) % 1.0, "hue_conf": math.hypot(hx, hy) / hw if hw else 0.0,
            "sat": s_sum / tot, "val": v_sum / tot}


# ------------------------------------------------------------------ tables
def rows_by(path: Path) -> tuple[list[str], dict[str, dict]]:
    h, rows = read_tsv(path)
    return h, {r[0]: dict(zip(h, r)) for r in rows if is_record(r) and r[0]}


def read_names(root: Path) -> dict[str, str]:
    """Monster / NPC names by key. D2R's text comes from D2R's own monsters.json / npcs.json; the mod ships none, only
    `d2r:<key>` overrides (medianxl/overrides.json, applied to the keys D2R has) and Median-only names in
    medianxl/median.json (NameStr cells say `medianxl:<key>`, strings_ns.py)."""
    names: dict[str, str] = {}
    for f in ("monsters", "npcs"):
        for base in (D2R, root):
            p = base / "data" / "local" / "lng" / "strings" / f"{f}.json"
            if p.exists():
                for e in load_json(p):
                    names[e["Key"]] = e["enUS"]
    folder = root / "data" / "local" / "lng" / "strings" / "medianxl"
    if (folder / "overrides.json").exists():
        for e in load_json(folder / "overrides.json"):
            key = e["Key"].removeprefix("d2r:")
            if key in names:
                names[key] = e["enUS"]
    if (folder / "median.json").exists():
        for e in load_json(folder / "median.json"):
            names.setdefault(e["Key"], e["enUS"])
    return names


def d2r_tables():
    _, ms = rows_by(D2R / "data" / "global" / "excel" / "monstats.txt")
    _, ms2 = rows_by(D2R / "data" / "global" / "excel" / "monstats2.txt")
    return ms, ms2, read_names(D2R)


def median_tables(work: Path):
    named = work / "named"
    _, ms = rows_by(named / "monstats.txt")
    _, ms2 = rows_by(named / "monstats2.txt")
    return ms, ms2, read_names(work / "mod")


def split_variants(v: str) -> list[str]:
    return [x for x in re.split(r"[,\s]+", v.strip('"')) if x]


def slots_of(row2: dict) -> dict[str, list[str]]:
    """Part slot (monstats2 code) -> its variant list for the parts the monster draws."""
    out = {}
    for pc in PART_CODES:
        if row2.get(pc) == "1":
            vs = [v for v in split_variants(row2.get(pc + "v", "")) if v != "nil"]
            out[pc] = list(dict.fromkeys(vs)) or ["lit"]
    return out


def modes_of(row2: dict) -> list[str]:
    return [m for m in MODES if row2.get("m" + m) == "1"]


def unit_info(unit: str) -> dict | None:
    """Parts (part -> variants), scale, state machine and modes per weapon class of a D2R unit definition."""
    p = unit_path(unit)
    if p is None:
        return None
    d = load_json(p)
    parts: dict[str, set] = defaultdict(set)
    scale = None
    sm_path = var_path = None
    for e in d["entities"]:
        for c in e["components"]:
            if c["type"] == "UnitPartComponent":
                parts[c["part"]].add(c["variant"])
            elif c["type"] == "TransformDefinitionComponent" and e["name"] == "entity_root":
                scale = c["scale"]
            elif c["type"] == "UnitRootComponent":
                sm_path = c["state_machine_filename"]
            elif c["type"] == "VariantDefinitionComponent":
                var_path = c["filename"]
    sm = {}
    names = {}
    if sm_path and (D2R / sm_path).exists():
        for s in load_json(D2R / sm_path)["states"]:
            for wc, anims in s["animationBindings"].items():
                if anims:
                    sm.setdefault(wc, set()).add(s["modeId"])
                    names.setdefault(wc, set()).add(s["_name"])
    return {"unit": unit, "parts": {k: sorted(v) for k, v in parts.items()}, "scale": scale, "sm": sm_path,
            "var": var_path, "modes": {k: sorted(v) for k, v in sm.items()},
            "states": {k: sorted(v) for k, v in names.items()}, "path": str(p)}


# ------------------------------------------------------------------ profiles
def d2r_cofs() -> set[str]:
    out = set()
    for line in (D2R / "filelist.txt").read_text("utf-8").splitlines():
        m = re.search(r"data/global/monsters/[^/]+/cof/([^/]+)\.cof$", line)
        if m:
            out.add(m.group(1))
    return out


def missing_cofs(code: str, row2: dict, cofs: set[str]) -> list[str]:
    w = (row2.get("BaseW") or "hth").lower()
    return [f"{code}{m}{w}".lower() for m in MODES if m not in ("KB", "SQ") and row2.get("m" + m) == "1"
            and f"{code}{m}{w}".lower() not in cofs and not (m in ("DT", "DD") and f"{code}{m}hth".lower() in cofs)]


class ArtCache:
    def __init__(self, path: Path):
        self.path = path
        self.data = json.loads(path.read_text("utf-8")) if path.exists() else {}
        self.art = None
        self.dirty = False

    def get(self, side: str, code: str, row2: dict) -> dict | None:
        sig = "|".join([side, code, row2.get("BaseW", ""), *(f"{pc}={row2.get(pc + 'v', '')}" for pc in PART_CODES
                                                            if row2.get(pc) == "1")])
        if sig not in self.data:
            if self.art is None:
                self.art = Art()
            self.data[sig] = self.art.stats(side, code, row2, row2.get("BaseW") or "hth")
            self.dirty = True
        return self.data[sig]

    def save(self):
        if self.dirty:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            self.path.write_text(json.dumps(self.data), encoding="utf-8")
            self.dirty = False


def art_colour(art: Art, side: str, code: str, stats: dict | None, translvl: int) -> dict | None:
    """Colour statistics of cached art stats with the monster's palette shift (TransLvl) applied."""
    if not stats:
        return None
    shifts = art.palshift(side, code)
    shift = shifts[translvl] if shifts and 0 < translvl < 8 else None
    c = colour_stats(stats["hist"], art.pal, shift)
    x0, y0, x1, y1 = stats["box"]
    c["w"], c["h"], c["box"] = x1 - x0, y1 - y0, stats["box"]
    return c


# ------------------------------------------------------------------ unit definitions
WEAPON_PARTS = ("rhand", "lhand", "shield")
TIERS = ["lit", "med", "hvy"]
ZERO4 = {"x": 0.0, "y": 0.0, "z": 0.0, "w": 0.0}


def color_transform(name: str, adjust=(0.0, 0.0, 0.0, 1.0), tint=(0.0, 0.0, 0.0, 0.0)) -> dict:
    return {"type": "ColorTransform", "name": name,
            "colorAdjustment": dict(zip("xyzw", (round(a, 4) for a in adjust))),
            "hueAdjustment0": dict(ZERO4), "hueAdjustment1": dict(ZERO4),
            "satAdjustment0": dict(ZERO4), "satAdjustment1": dict(ZERO4),
            "colorTint": dict(zip("xyzw", (round(a, 4) for a in tint)))}


def variant_table(name: str, donor_var: dict, adjust, tint) -> dict:
    """A VariantTransformTable whose 8 colour shifts (monstats TransLvl) all hold the same colour transform."""
    entries = []
    for i in range(8):
        entries.append({"type": "VariantColorTransform", "name": f"VariantColorTransform_{i:03d}",
                        "transforms": [color_transform(f"ColorTransform_{i:03d}_{j:03d}", adjust, tint)
                                       for j in range(3)]})
    return {"dependencies": donor_var["dependencies"], "type": "VariantTransformTable",
            "name": f"{name}_VariantTransformTable", "entries": entries,
            "customColorShiftEntries": donor_var.get("customColorShiftEntries", [])}


def adapt_state_machine(sm: dict, weapon: str, name: str) -> dict:
    """The donor's state machine with every state also bound for Median's weapon class (animations of the donor's)."""
    sm = copy.deepcopy(sm)
    sm["name"] = name
    for s in sm["states"]:
        ab = s["animationBindings"]
        have = {k: v for k, v in ab.items() if v}
        if have and weapon not in have:
            ab[weapon] = list(have.get("hth") or next(iter(have.values())))
    return sm


def pick_variant(have: list[str], want: str) -> str:
    if want in have:
        return want
    if want in TIERS:
        ts = [v for v in have if v in TIERS]
        if ts:
            return min(ts, key=lambda v: abs(TIERS.index(v) - TIERS.index(want)))
    return have[0]


def remap_parts(dparts: dict[str, list[str]], slots: dict[str, list[str]]) -> tuple[list[tuple], list[str]]:
    """(donor part, donor variant, Median part, Median variant) rows and notes on what could not be placed.

    Median's monstats2 says which part slots a monster draws and with which variants. A donor part without a slot
    of its own goes to a slot the donor does not use (most Median monsters draw TR only: their whole body is one
    sprite), so a donor made of head/torso/legs/arms still shows complete."""
    mslots = {PARTS[c]: vs for c, vs in slots.items()}
    notes = []
    rows = []
    free = [p for p in mslots if p not in dparts and p not in WEAPON_PARTS]
    for dp, dvs in dparts.items():
        if dp in mslots:
            tgt = dp
        elif dp in WEAPON_PARTS:
            notes.append(f"{dp} dropped")
            continue
        elif free:
            tgt = free.pop(0)
        else:
            body = [p for p in mslots if p not in WEAPON_PARTS]
            if not body:
                notes.append(f"{dp} dropped")
                continue
            tgt = "torso" if "torso" in body else body[0]
        for mv in mslots[tgt]:
            rows.append((dp, pick_variant(dvs, mv), tgt, mv))
    return rows, notes


def build_unit(donor: str, name: str, slots: dict, scale: float | None, adjust, tint, weapon: str,
               sm_name: str | None, var_name: str, vmap: dict | None = None) -> tuple[dict, list[str]]:
    """UnitDefinition json (dict) for a Median monster look; (unit, notes)."""
    d = copy.deepcopy(load_json(unit_path(donor)))
    d["name"] = name
    dparts: dict[str, list[str]] = defaultdict(list)
    ents_by_key: dict[tuple, dict] = {}
    base, others = [], []
    for e in d["entities"]:
        pc = next((c for c in e["components"] if c["type"] == "UnitPartComponent"), None)
        if pc is None:
            base.append(e)
            continue
        dparts[pc["part"]].append(pc["variant"])
        ents_by_key[(pc["part"], pc["variant"])] = e
    rows, notes = remap_parts(dict(dparts), slots)
    if vmap:  # provenance: the (part, variant) the art was made from is drawn at that Median slot
        done = {(tp, tv) for tp, tv in vmap}
        rows = [r for r in rows if (r[2], r[3]) not in done] + [(sp, sv, tp, tv) for (tp, tv), (sp, sv) in vmap.items()]
    for dp, dv, tp, tv in rows:
        e = copy.deepcopy(ents_by_key[(dp, dv)])
        nm = f"entity_{tp}_{tv}_{dp}_{dv}"
        e["name"] = nm
        e["id"] = int(hashlib.md5((name + nm).encode()).hexdigest()[:8], 16)
        for c in e["components"]:
            if "part" in c:
                c["part"] = tp
            if "variant" in c:
                c["variant"] = tv
            c["name"] = c["name"].replace(f"_{dp}_{dv}", f"_{tp}_{tv}_{dp}_{dv}") if f"_{dp}_{dv}" in c["name"] else c["name"] + f"_{tp}_{tv}"
        others.append(e)
    d["entities"] = base + others
    for e in base:
        for c in e["components"]:
            if c["type"] == "UnitRootComponent" and sm_name:
                c["state_machine_filename"] = sm_name
            elif c["type"] == "VariantDefinitionComponent":
                c["filename"] = var_name
    if scale:
        root = next(e for e in base if e["name"] == "entity_root")
        tc = next((c for c in root["components"] if c["type"] == "TransformDefinitionComponent"), None)
        if tc is None:
            tc = {"type": "TransformDefinitionComponent", "name": "entity_root_TransformDefinition",
                  "position": {"x": 0.0, "y": 0.0, "z": 0.0}, "orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
                  "scale": {"x": 1.0, "y": 1.0, "z": 1.0}, "inheritOnlyPosition": False}
            root["components"].append(tc)
        for k in "xyz":
            tc["scale"][k] = round(tc["scale"][k] * scale, 4)
    deps = d["dependencies"]
    if sm_name:
        deps["json"] = [j for j in deps["json"] if not j["path"].endswith("_state_machine.json")] + [{"path": sm_name}]
    deps["variantdata"] = [{"path": var_name}]
    return d, notes


# ------------------------------------------------------------------ family tags
# Median's monster ids are descriptive ("undead_lancer_3", "carrion_worm_2"): keywords of the id, the NameStr and the
# MonType give a family and a body plan; D2R's units get theirs from their names and a few overrides.
KEYWORDS = [
    (r"skelet|bone|skull|returned|reanimated|risen|leoric|lich|boneg|bony", {"skel", "undead"}),
    (r"zombie|ghoul|ghast|walker|husk|rotted|rotfiend|rotspawn|corpse|flesh|remade|putrid|afflicted|plague|living|"
     r"pest|contagion|bloated|corpulent|sunken|drowned|foulblooded|hemoplague|rotten|decay|undead|sinner|damned|"
     r"condemned|scorned|enslaved|forsaken", {"zombie", "undead"}),
    (r"ghost|spirit|wraith|phantom|phantasm|spectr|spectral|specter|soul|shade|banshee|ether|wisp|unseen|vision|"
     r"unformed|wailer|veil|apparition|nightmare|haunt", {"ghost"}),
    (r"spider|arach|widow|web|arthropod|broodling|broodmother|astrogha|tarantula", {"spider", "arachnid"}),
    (r"scarab|beetle|mantis|ankheg|scorpion|mosquito|hive|vermin|elytron|scytheclaw|insect|locust|wasp|crawler|"
     r"infestor|stirge|leech|tick", {"insect", "arachnid"}),
    (r"worm|maggot|larva|slimer|eel|grub|mawl|leech", {"worm", "serpent"}),
    (r"snake|serpent|viper|basilisk|couatl|adder|naga|cobra", {"snake", "serpent"}),
    (r"frog|toad|lizard|saurian|gecko|scincid|croc|newt|salamander|leaper|raptor|iguana|drowned_beast", {"lizard"}),
    (r"fish|eel|piranha|shark|leviathan|kraken|squid|jelly", {"fish"}),
    (r"wolf|worg|hound|dog|jackal|warg|direwolf|coyote|fenris|hellhound|cerberus", {"canine", "quad"}),
    (r"lion|panther|cat\b|tiger|leopard|cougar|feline|lynx|cheetah|sabertooth", {"feline", "quad"}),
    (r"bear|yeti|ape\b|baboon|gnoll|boar|rhino|buffalo|porkchop|behemoth|bull|ox\b|minotaur|bovine|beast|centaur|"
     r"horse|steed|mount|pig|hog|stag|deer|elk|goat|sheep|rat\b|rodent|mole|badger|sloth", {"beast", "quad"}),
    (r"bat\b|bird|harpy|wyvern|dragon|drake|gryphon|griffin|crow|raven|hawk|owl|wing|flyer|flit|hexwing|dreadwing|"
     r"fairy|pixie|fly\b|moth|vulture|eagle|condor|phoenix|roc\b", {"flying"}),
    (r"giant|ogre|troll|titan|goliath|gigas|colossus|colossal|hulk|golem|juggernaut|siegebreaker|brute|maul|"
     r"behemoth|ent\b|armored|obsidian|stone|rock", {"giant"}),
    (r"golem|elemental|construct|servant|sentinel|guardian|automaton|statue|sentry|gargoyle", {"golem"}),
    (r"demon|fiend|hellion|imp\b|ravager|fury|succubus|vixen|concubine|balrog|baron|pit|infernal|diabol|abyss|"
     r"hell|nephalem|tiefling|devil|incubus|archon|wicked|malign|horror|terror|fell", {"demon"}),
    (r"goblin|gremlin|hobgoblin|leprechaun|fallen|devilkin|carver|ratfink|kobold|gnome|troll_|imp\b|sprite|"
     r"spriggan|familiar|hoarder|scavenger|thief", {"goblin"}),
    (r"fetish|shaman|tribe|tribal|khazra|savage|witch.?doctor|nomad|gothi|ratman|rat_man", {"tribal"}),
    (r"warrior|knight|soldier|centurion|barbarian|amazon|rogue|hunter|archer|marksman|scout|assassin|ranger|paladin|"
     r"lancer|spear|halbedier|retiarius|defender|fanatic|zealot|templar|guard|bandit|raider|berserker|captain|"
     r"general|gladiator|champion|slayer|mercenary|crusader|inquisitor|warlord|jailor|butcher|executioner|"
     r"black_knight|sapphire_knight|onyx_knight|warleader|swordsman|duelist|bruiser", {"human", "warrior"}),
    (r"necromancer|mage|witch|priest|cleric|cabalist|warlock|seer|sorcer|wizard|enchanter|magineer|scholar|heretic|"
     r"druid|summoner|hexer|illusionist|elder|prophet|oracle|conjurer|pyromancer|bishop|vicar|magistrate|"
     r"cultist|acolyte|emissary|councillor|council|architect|alchemist|diviner|mystic|dame|matriarch|mistress|"
     r"naturist|consort|siren|lazarus|cultist", {"human", "caster"}),
    (r"archer|bow|marksman|dead_?shot|ranger|sniper|slinger|javelin|hunter|crossbow|gunner|thrower", {"archer"}),
    (r"altar|shrine|gate|pillar|chest|spire|tower|trap|crystal|egg\b|nest|fountain|obelisk|portal|door|cage|"
     r"sarcophag|coffin|well\b|void_trap|barricade|totem|brazier|lantern|machine|control_system|orb|beacon|"
     r"monolith|stele|cairn|grave\b|tombstone|lever|rift_storm|storm\b", {"object"}),
    (r"fungus|tree|treewarden|sapling|spore|vine|shroom|flower|poppy|mushroom|bush|plant|dryad|rooted|moss", {"plant"}),
    (r"lord|king|queen|prime|archmage|herald|prince|ancient|primus|azmodan|belial|samael|malthael|lilith|kabraxis|"
     r"mephisto|diablo|baal|tyrael|imperius|zagraal|astrogha|sescheron|tathamet|sovereign|archbishop|warleader|"
     r"avatar|skeleton_king|the_butcher|aranoch|arreat|tal_rasha|nithogur|deimoss|horazon|assur|mefis|"
     r"emissary_of_destruction|goddess|baron_of_hell|dark_star", {"boss"}),
]
KEY_RE = [(re.compile(k), v) for k, v in KEYWORDS]
FAMILY = {"skel", "zombie", "ghost", "spider", "insect", "worm", "snake", "lizard", "fish", "canine", "feline", "beast",
          "flying", "giant", "golem", "demon", "goblin", "tribal", "human", "object", "plant"}
QUAD, ARACH, SERP, FLY, STATIC = "quad", "arachnid", "serpent", "flying", "static"

# D2R unit -> extra tags (what the name does not say)
DONOR_TAGS = {
    "skeleton1": {"skel", "human", "warrior"}, "necroskeleton": {"skel", "human", "warrior"},
    "sk_archer1": {"skel", "human", "archer"}, "necromage": {"skel", "human", "caster"},
    "skmage_cold1": {"skel", "human", "caster"}, "skmage_fire1": {"skel", "human", "caster"},
    "skmage_ltng1": {"skel", "human", "caster"}, "skmage_pois1": {"skel", "human", "caster"},
    "mummy1": {"zombie", "undead"}, "zombie1": {"zombie", "undead"}, "reanimatedhorde1": {"zombie", "undead"},
    "bighead1": {"zombie", "undead"}, "warbighead": {"zombie", "undead"}, "brute2": {"zombie", "giant"},
    "unraveler1": {"zombie", "undead"}, "vampire1": {"human", "caster", "undead"}, "griswold": {"zombie", "undead"},
    "fallen1": {"goblin"}, "fallenshaman1": {"goblin", "caster"}, "minion1": {"demon", "goblin"},
    "suicideminion1": {"demon", "goblin"}, "imp1": {"demon", "goblin", "caster"},
    "fetish1": {"goblin", "tribal"}, "fetish11": {"goblin", "tribal"}, "fetishblow1": {"goblin", "tribal", "archer"},
    "fetishshaman1": {"goblin", "tribal", "caster"}, "bonefetish1": {"goblin", "tribal", "skel"},
    "goatman1": {"beast", "tribal"}, "wargoatman": {"beast", "tribal"},
    "wraith1": {"ghost"}, "willowisp1": {"ghost", "flying"}, "trappedsoul1": {"ghost"}, "trappedsoul2": {"ghost"},
    "spiritofbarbs": {"ghost", "object"}, "arach1": {"spider", "arachnid"}, "scarab1": {"insect", "arachnid"},
    "mosquito1": {"insect", "flying"}, "swarm1": {"insect", "flying"}, "bladecreeper": {"insect", "arachnid"},
    "maggotbaby1": {"worm", "serpent"}, "sandmaggot1": {"worm", "serpent"}, "maggotqueen1": {"worm", "serpent", "boss"},
    "painworm1": {"worm", "serpent"}, "larva": {"worm", "serpent"}, "vilechild1": {"beast", "demon"},
    "vilemother1": {"demon", "beast"}, "clawviper1": {"snake", "serpent", "lizard"},
    "frogdemon1": {"lizard", "demon"}, "sandleaper1": {"lizard"}, "quillrat1": {"beast", "quad"},
    "quillbear1": {"beast", "quad", "giant"}, "baboon1": {"beast", "human"}, "baboon6": {"beast", "human"},
    "snowyeti1": {"beast", "giant"}, "bear": {"beast", "quad"}, "druidbear": {"beast", "quad"},
    "wolf": {"canine", "quad"}, "spiritwolf": {"canine", "quad"}, "hellbovine": {"beast", "giant", "demon"}, "cowking": {"beast", "giant", "demon"}, "druidbear": {"beast", "quad"},
    "batdemon1": {"flying", "demon"}, "foulcrow1": {"flying"}, "vulture1": {"flying"}, "eagle": {"flying"},
    "druidhawk": {"flying"}, "blunderbore1": {"giant"}, "siegebeast1": {"giant", "beast", "quad"},
    "thornhulk1": {"plant", "giant"}, "bloodgolem": {"golem", "giant"}, "claygolem": {"golem"},
    "firegolem": {"golem"}, "irongolem": {"golem", "giant"}, "doomknight1": {"demon", "human", "warrior"},
    "doomknight2": {"demon", "human", "warrior"}, "doomknight3": {"demon", "human", "caster"},
    "bloodlord1": {"demon", "giant"}, "deathmauler1": {"demon", "giant"}, "megademon1": {"demon", "giant", "boss"},
    "succubus1": {"demon", "caster"}, "succubuswitch1": {"demon", "caster"}, "overseer1": {"demon", "caster"},
    "cantor1": {"human", "caster"}, "councilmember1": {"human", "caster"}, "summoner": {"human", "caster", "boss"},
    "fingermage1": {"demon", "caster", "giant"}, "corruptrogue1": {"human", "warrior"},
    "cr_lancer1": {"human", "warrior"}, "cr_archer1": {"human", "archer"}, "bloodraven": {"human", "archer", "boss"},
    "sandraider1": {"human", "warrior"}, "pantherwoman1": {"human", "warrior"}, "slinger1": {"human", "archer"},
    "slinger5": {"human", "archer"}, "act5barb1": {"human", "warrior"}, "act5barb2": {"human", "warrior"},
    "act5barb3": {"human", "warrior"}, "ancientbarb1": {"human", "warrior", "giant", "boss"},
    "ancientbarb2": {"human", "warrior", "giant", "boss"}, "ancientbarb3": {"human", "warrior", "giant", "boss"},
    "zealot1": {"human", "warrior"}, "andariel": {"demon", "boss"}, "diablo": {"demon", "boss"},
    "duriel": {"demon", "boss", "giant"}, "izual": {"demon", "boss"}, "mephisto": {"demon", "boss", "caster"},
    "radament": {"zombie", "boss"}, "venomlord": {"demon", "giant", "boss"}, "hephasto": {"demon", "giant"},
    "sarcophagus": {"object"}, "turret1": {"object"}, "firetower": {"object"}, "lightningspire": {"object"},
    "chargeboltsentry": {"object"}, "deathsentry": {"object"}, "infernosentry": {"object"},
    "lightningsentry": {"object"}, "gargoyletrap": {"object"}, "barricadetower": {"object"},
    "barricadewall1": {"object"}, "barricadewall2": {"object"}, "barricadedoor1": {"object"},
    "barricadedoor2": {"object"}, "prisondoor": {"object"}, "bonewall": {"object"}, "evilhut": {"object"},
    "minionspawner1": {"object"}, "catapult1": {"object"}, "window1": {"object"}, "window2": {"object"},
    "crownest1": {"object"}, "trap-melee": {"object"}, "maggotegg1": {"object"}, "hydra1": {"object", "serpent"},
    "hydra2": {"object", "serpent"}, "hydra3": {"object", "serpent"}, "tentacle1": {"object", "serpent"},
    "tentaclehead1": {"object", "serpent"}, "baaltentacle1": {"object", "serpent"}, "flyingscimitar": {"object"},
    "boneprison1": {"object"}, "boneprison2": {"object"}, "boneprison3": {"object"}, "boneprison4": {"object"},
    "frozenhorror1": {"demon", "giant"}, "regurgitator1": {"zombie", "giant"}, "darkwanderer": {"ghost", "human"},
    "gorgon1": {"beast", "quad"}, "baalminion1": {"demon", "giant"}, "bloodlord": {"demon", "giant"},
    "valkyrie": {"human", "warrior"}, "colbarbfrenzy": {"human", "warrior"}, "colbarbthrow": {"human", "warrior"},
    "colbarbwhirl": {"human", "warrior"},
}
# Specials that must never be donors (invisible helpers, towns folk, cinematic duplicates, tiny dummies)
NOT_DONORS = re.compile(r"^(act2|act3|baal(clone|crab|taunt|throne|high)|trap-|invis|cycle|worldstone|seventombs|"
                        r"catapultspotter|plaguepoppy|vinecreature|hellmeteor|compellingorb|dopplezon|shadow|"
                        r"driver_|bat$|bird|fish|horse|rat$|chicken|bunny|bug|snake$|scorpion$|maggot$|parrot|"
                        r"seagull|cow$|pig$|camel|tyrael|izualghost|mephistospirit|evilhole|colossal|injured|"
                        r"cain|kashya|warriv|akara|gheed|charsi|natalya|drognan|fara|elzix|greiz|jerhyn|"
                        r"lysander|meshif|atma|asheara|alkor|hratli|geglash|halbu|jamella|larzuk|malah|"
                        r"malachai|nihlathak$|qual|smith|navi|rogue[123]|roguehire|ormus|drehya|ancientstatue|"
                        r"fire(tower)$|spiritofbarbs|corpsefire|uberancient|wakeof|window)")


def text_of(*parts: str) -> str:
    out = []
    for p in parts:
        p = re.sub(r"([a-z])([A-Z])", r"\1_\2", p or "")
        out.append(re.sub(r"[^a-z0-9]+", "_", p.lower()))
    return " ".join(out)


def tags_of(text: str) -> set[str]:
    tags: set[str] = set()
    for rx, tg in KEY_RE:
        if rx.search(text):
            tags |= tg
    return tags


def body_of(tags: set[str], w: int, h: int, translate: bool = True) -> str:
    """Body plan from tags, else from the art's aspect."""
    if "object" in tags and not tags & {"human", "golem"}:
        return STATIC
    if "plant" in tags and "giant" not in tags:
        return STATIC
    if tags & {"serpent"}:
        return SERP
    if tags & {"arachnid"}:
        return ARACH
    if "flying" in tags and not tags & {"human", "warrior", "caster", "demon", "giant", "zombie", "skel"}:
        return FLY
    if "quad" in tags and not tags & {"human", "warrior", "caster"}:
        return QUAD
    if tags & FAMILY and tags & {"human", "warrior", "caster", "skel", "zombie", "goblin", "tribal", "demon", "giant",
                                 "golem", "ghost", "lizard"}:
        return "biped"
    if h and w / h > 1.5:
        return QUAD
    return "biped"


# ------------------------------------------------------------------ donor choice
class Donor:
    def __init__(self, unit, ref, ms_row, ms2_row, name, info, col):
        self.unit, self.ref, self.name, self.info, self.col = unit, ref, name, info, col
        self.montype, self.ai, self.basew = ms_row["MonType"], ms_row["AI"], ms2_row["BaseW"]
        self.code = ms_row["Code"]
        txt = text_of(unit, name, ms_row["MonType"])
        self.tags = set(DONOR_TAGS[unit]) if unit in DONOR_TAGS else tags_of(txt)
        self.fam = self.tags & FAMILY
        self.body = body_of(self.tags, col["w"], col["h"])
        self.boss = "boss" in self.tags
        self.modes = set()
        for ms in info["modes"].values():
            self.modes |= set(ms)
        names = set(sum(info["states"].values(), []))
        if "Walk" in names:
            self.modes.add(2)
        if "Run" in names:
            self.modes.add(15)
        self.keys = set(info["modes"])
        self.n_parts = len(info["parts"])


def build_donors(ms, ms2, names, art: Art, cache: ArtCache, mj: dict) -> dict[str, Donor]:
    ref: dict[str, tuple] = {}
    for i, r in ms.items():
        u = mj.get(i)
        if not u or not r["Code"] or r["npc"] == "1" or NOT_DONORS.match(u) or ms2.get(r["MonStatsEx"]) is None:
            continue
        key = (r["enabled"] != "1", int(r["TransLvl"] or 0))
        if u not in ref or key < ref[u][0]:
            ref[u] = (key, i)
    out = {}
    for u, (_, i) in ref.items():
        r = ms[i]
        r2 = ms2[r["MonStatsEx"]]
        info = unit_info(u)
        if not info or not info["modes"]:
            continue
        code = r["Code"].lower()
        col = art_colour(art, "d2r", code, cache.get("d2r", code, r2), int(r["TransLvl"] or 0))
        if not col or col["h"] < 20 or col["w"] < 8:
            continue
        out[u] = Donor(u, i, r, r2, names.get(bare(r["NameStr"]), bare(r["NameStr"])), info, col)
    return out


class Subject:
    """A Median monster that needs an HD look."""

    def __init__(self, mid, ms_row, ms2_row, name, col):
        self.id, self.row, self.row2, self.name, self.col = mid, ms_row, ms2_row, name, col
        self.code = ms_row["Code"]
        self.translvl = int(ms_row["TransLvl"] or 0)
        self.basew = ms2_row["BaseW"] or "hth"
        self.modes = modes_of(ms2_row)
        self.slots = slots_of(ms2_row)
        root = re.sub(r"_\d+$", "", mid)
        self.text = text_of(root, bare(ms_row["NameStr"]), name if name != bare(ms_row["NameStr"]) else "")
        self.tags = tags_of(self.text)
        self.type_tags = tags_of(text_of(ms_row["MonType"]))
        self.ai_tags = tags_of(text_of(ms_row["AI"]))
        self.keyword_family = bool(self.tags & FAMILY)
        if not self.keyword_family:  # nothing in the name: fall back on MonType, then on AI
            self.tags |= self.type_tags or self.ai_tags
        self.fam = self.tags & FAMILY
        self.body = body_of(self.tags, col["w"], col["h"]) if col else "biped"
        if not self.slots:
            self.slots = {"TR": ["lit"]}


def circ(a: float, b: float) -> float:
    d = (a - b + 0.5) % 1.0 - 0.5
    return d


def score(s: Subject, d: Donor) -> tuple[float, float, list[str]]:
    """(score, art size ratio, reasons) of donor d for subject s."""
    why = []
    sc = 0.0
    shared = s.fam & d.fam
    if s.keyword_family:
        sc += 3.0 * len(shared) / len(s.fam) if s.fam else 0
        if shared:
            why.append("family " + "+".join(sorted(shared)))
        elif d.fam:
            sc -= 1.0
    else:
        sc += 1.5 * len(shared) / len(s.fam) if s.fam else 0
    if "archer" in s.tags and "archer" in d.tags:
        sc += 1.5
        why.append("archer")
    if "caster" in s.tags and "caster" in d.tags:
        sc += 1.0
    if s.body == d.body:
        sc += 2.5
        why.append("body " + d.body)
    elif {s.body, d.body} == {QUAD, "biped"} or STATIC in (s.body, d.body):
        sc -= 4.0 if STATIC in (s.body, d.body) else 3.0
    else:
        sc -= 1.5
    if ("boss" in s.tags) != d.boss:
        sc += 1.0 if d.boss else -1.5 if "boss" not in s.tags else 0
    if d.boss and "boss" not in s.tags:
        sc -= 2.5
    if s.row["MonType"] == d.montype and s.row["MonType"]:
        sc += 0.8
    if s.row["AI"] == d.ai:
        sc += 1.2
    # animations: the modes Median's monstats2 asks for, and the weapon class
    need = {MODE_ID[m] for m in s.modes if m not in ("SQ", "KB")}
    miss = [m for m in need if m not in d.modes and not (m in (0, 12) and (0 in d.modes or 12 in d.modes))]
    if miss:
        sc -= 2.0 * len(miss)
        why.append(f"missing modes {sorted(miss)}")
    if s.basew in d.keys:
        sc += 0.8
    elif s.basew in ("bow", "xbw") and "archer" not in d.tags:
        sc -= 1.0
    # size
    ratio = 1.0
    if s.col and d.col:
        hr = s.col["h"] / d.col["h"]
        wr = s.col["w"] / d.col["w"]
        ratio = hr ** 0.7 * wr ** 0.3 if s.body in (QUAD, ARACH, SERP) else hr
        sc -= 1.4 * abs(math.log(ratio))
        # colour
        dc = math.dist(s.col["rgb"], d.col["rgb"])
        sc -= 2.0 * dc
        if d.n_parts > 3 and len(s.slots) <= 2:
            sc -= 0.3  # a donor of many parts folded into one slot
    return sc, ratio, why


def choose(s: Subject, donors: dict[str, Donor]) -> tuple[Donor, float, float, list[str], float]:
    ranked = sorted(((score(s, d), d) for d in donors.values()), key=lambda t: -t[0][0])
    (sc, ratio, why), d = ranked[0]
    margin = sc - ranked[1][0][0]
    return d, sc, ratio, why, margin


# Median Id -> (D2R donor unit, size factor), chosen by hand (owner's pick) instead of the scoring above. Colour, scale
# and the state machine are still worked out the usual way for the pinned donor; the factor multiplies the scale. The OW (Wereowl) rows: goatman1 is the
# closest build; it has no attack2 / cast clip, so those modes reuse attack1 / skill1.
PINNED_DONORS = {"wereowl": ("goatman1", 1.0), "corrupted_wereowl": ("goatman1", 1.25)}


def confidence(s: Subject, d: Donor, sc: float, ratio: float, margin: float) -> str:
    fam = s.keyword_family and bool(s.fam & d.fam)
    size_ok = 0.6 <= ratio <= 1.7
    body_ok = s.body == d.body
    if fam and body_ok and size_ok:
        return "high"
    if (fam or (not s.keyword_family and body_ok and s.fam & d.fam)) and body_ok:
        return "medium"
    return "low"


# ------------------------------------------------------------------ tint
def hue_weight(c: dict) -> float:
    """How well defined a colour's hue is (grey art has none)."""
    return min(1.0, c["hue_conf"]) * min(1.0, c["sat"] / 0.25)


def tint_of(m: dict, d: dict) -> tuple[tuple, tuple, str]:
    """(colorAdjustment, colorTint, label): turns the donor's classic-art colour into Median's.

    colorAdjustment holds (hue shift in turns, saturation, value) offsets of the albedo, colorTint an (r, g, b, amount)
    blend; see docs/hd-monsters.md for what each does in game."""
    dh = circ(m["hue"], d["hue"])
    w = min(hue_weight(m), hue_weight(d))
    hue = dh * w
    sat = max(-0.8, min(0.8, (m["sat"] - d["sat"]) * 1.0))
    val = max(-0.7, min(0.7, math.log2(max(m["val"], 0.03) / max(d["val"], 0.03)) * 0.45))
    tint = (0.0, 0.0, 0.0, 0.0)
    # donor without a hue (bone, grey) against a clearly coloured Median monster: blend towards its colour
    if hue_weight(m) > 0.3 and hue_weight(d) < 0.5 and m["sat"] > 0.3:
        r, g, b = colorsys.hsv_to_rgb(m["hue"], min(1.0, m["sat"] * 1.4), 1.0)
        tint = (r, g, b, round(min(0.6, 0.25 + m["sat"] * 0.4), 2))
    adj = (round(hue % 1.0, 3), round(sat, 3), round(val, 3), 1.0)
    if abs(hue) < 0.02 and abs(sat) < 0.08 and abs(val) < 0.08 and not tint[3]:
        return (0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0, 0.0), "none"
    return adj, tint, f"hue {hue:+.2f} sat {sat:+.2f} val {val:+.2f}" + (f" tint {tint[3]:.2f}" if tint[3] else "")


def q(x: float, step: float) -> float:
    return round(round(x / step) * step, 4)


# ------------------------------------------------------------------ provenance (tools/art_provenance.py)
PROV_COVER = 0.6


def load_provenance(path: Path) -> dict:
    """Median monster code (lowercase) -> part (tr, rh ...) -> variant -> best vanilla source of that part."""
    if not path.exists():
        return {}
    return json.loads(path.read_text("utf-8")).get("monsters", {}).get("parts", {})


def prov_donor(s: Subject, parts: dict, by_code: dict[str, dict[str, str]], donors: dict[str, "Donor"]):
    """(donor, vmap, scale, kind, coverage, text) when the monster's parts are D2R parts, else None.

    vmap: (Median part name, Median variant) -> (D2R part name, D2R variant) of the unit's entities to draw there."""
    mine = parts.get(s.code.lower())
    if not mine:
        return None
    weight: Counter = Counter()
    entries: dict[tuple, dict] = {}
    for pc, vs in s.slots.items():
        for v in vs:
            e = mine.get(pc.lower(), {}).get(v)
            if e and e["coverage"] >= PROV_COVER and e["q"] >= 0.8:
                entries[(pc, v)] = e
                weight[e["source"].split("/")[0]] += e["frames"] * (0.3 if pc in ("RH", "LH", "SH") else 1.0)
    if not entries:
        return None
    vcode, w = weight.most_common(1)[0]
    total_w = sum(w for w in weight.values())
    body = [(k, e) for k, e in entries.items() if k[0] not in ("RH", "LH", "SH") and e["source"].split("/")[0] == vcode]
    if w < 0.5 * total_w or not body:
        return None
    cands = [u for u in by_code.get(vcode, {}) if u in donors]
    if not cands:
        return None
    vmap = {}
    for (pc, v), e in entries.items():
        sc_, sp, sv = e["source"].split("/")
        if sc_ == vcode and sp.upper() in PARTS:
            vmap[(PARTS[pc], v)] = (PARTS[sp.upper()], sv)
    def fit(u: str) -> tuple:
        have = donors[u].info["parts"]
        n = sum(1 for sp, sv in vmap.values() if sv in have.get(sp, []))
        return (n, score(s, donors[u])[0])
    unit = max(cands, key=fit)
    if fit(unit)[0] == 0:
        return None
    have = donors[unit].info["parts"]
    vmap = {k: v for k, v in vmap.items() if v[1] in have.get(v[0], [])}
    scales = [e["scale"] for _k, e in body if e["scale"] != 1.0]
    scale = sorted(scales)[len(scales) // 2] if scales else 1.0
    kinds = Counter(e["kind"] for _k, e in body)
    kind = "rescale+recolour" if scale != 1.0 and "recolour" in "".join(kinds) else         "rescale" if scale != 1.0 else "recolour" if any("recolour" in k for k in kinds) else "exact"
    cover = sum(e["coverage"] for _k, e in body) / len(body)
    text = (f"provenance: {', '.join(sorted({f'{pc} {v}' for (pc, v) in entries if (pc, v) in dict(body)}))} = {kind} "
            f"D2R {vcode}/{unit} ({len(vmap)} of {len(entries)} parts mapped)")
    return donors[unit], vmap, scale, kind, cover, text


# ------------------------------------------------------------------ vanilla art
def vanilla_unit(s: Subject, ms: dict, mj: dict, ms2: dict, by_code: dict[str, dict[str, str]]) -> tuple[str | None, str]:
    """Unit for a Median monster that uses a D2R code (D2R's own art): D2R's unit of the same Id and code, else the
    unit of that code that fits best by AI, MonType and weapon class."""
    d = ms.get(s.id)
    if d and d["Code"].lower() == s.code.lower() and s.id in mj:
        return mj[s.id], "same Id and code in D2R"
    cand = by_code.get(s.code.lower())
    if not cand:
        return None, "code has no D2R unit"
    if len(cand) == 1:
        return next(iter(cand)), "only unit of the code"
    best, bs = None, -9.0
    for u, i in cand.items():
        r = ms[i]
        r2 = ms2.get(r["MonStatsEx"], {})
        sc = (3.0 if r["AI"] == s.row["AI"] else 0) + (2.0 if r["MonType"] == s.row["MonType"] and r["MonType"] else 0)
        sc += 1.0 if r2.get("BaseW", "hth") == s.basew else 0
        sc += 1.0 * len(tags_of(text_of(u)) & s.tags)
        if sc > bs:
            best, bs = u, sc
    return best, f"best of {len(cand)} units of the code"


# ------------------------------------------------------------------ main
def write_json(path: Path, obj) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, separators=(",", ":")), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--work", default=str(ROOT / "work"), help="work dir with named/ and mod/ (default work)")
    ap.add_argument("--out", help="output data dir (default <work>/mod/data)")
    ap.add_argument("--only", help="comma separated Median Ids")
    ap.add_argument("--no-tint", action="store_true")
    ap.add_argument("--no-scale", action="store_true")
    ap.add_argument("--no-provenance", action="store_true", help="skip tools/art_provenance.py matches")
    args = ap.parse_args()
    work = Path(args.work)
    out = Path(args.out) if args.out else work / "mod" / "data"
    only = set(args.only.split(",")) if args.only else None
    ensure_hd_json()
    ms, ms2, dnames = d2r_tables()
    mms, mms2, mnames = median_tables(work)
    mj = load_json(D2R / "data" / "hd" / "character" / "monsters.json")
    cur = work / "mod" / "data" / "hd" / "character" / "monsters.json"
    hd = load_json(cur) if cur.exists() else dict(mj)
    _, mod_ms = rows_by(work / "mod" / "data" / "global" / "excel" / "base" / "monstats.txt")
    cofs = d2r_cofs()
    cache = ArtCache(work / "hd_monsters_art.json")
    art = Art()
    donors = build_donors(ms, ms2, dnames, art, cache, mj)
    by_code: dict[str, dict[str, str]] = defaultdict(dict)
    for i, r in ms.items():
        if r["Code"] and i in mj and not NOT_DONORS.match(mj[i]):
            by_code[r["Code"].lower()].setdefault(mj[i], i)
    print(f"{len(donors)} donor units")

    units: dict[str, dict] = {}   # unit name -> look
    variants: dict[str, tuple] = {}
    rows = []  # report rows
    kept = Counter()
    prov = {} if args.no_provenance else load_provenance(out.parent / "art_provenance.json")
    if not args.no_provenance and not prov:
        print("no work/mod/art_provenance.json: run tools/art_provenance.py for exact-art matches")
    prov_n: Counter = Counter()
    for mid, r in mms.items():
        if only and mid not in only:
            continue
        if not r["Code"] or r["npc"] == "1":
            continue
        r2 = mms2.get(r["MonStatsEx"])
        if r2 is None:
            continue
        if mid in mod_ms and mod_ms[mid]["Code"] != r["Code"]:
            kept["borrows a D2R monster's graphics completely (gfx_placeholders)"] += 1
            rows.append((mid, mnames.get(bare(r["NameStr"]), bare(r["NameStr"])), r["Code"], hd.get(mid, ""), "-", "-", "kept", "graphics borrowed from "
                         + mod_ms[mid]["Code"]))
            continue
        code = r["Code"]
        vanilla = not missing_cofs(code, r2, cofs)
        name = mnames.get(bare(r["NameStr"]), bare(r["NameStr"]))
        side = "d2r" if vanilla else "mxl"
        stats = cache.get(side, code.lower(), r2)
        col = art_colour(art, side, code.lower(), stats, int(r["TransLvl"] or 0))
        s = Subject(mid, r, r2, name, col)
        if vanilla:
            u, how = vanilla_unit(s, ms, mj, ms2, by_code)
            if u:
                hd[mid] = u
            rows.append((mid, name, code, u or hd.get(mid, ""), "-", "-", "vanilla", how))
            kept["vanilla art"] += 1
            continue
        d, sc, ratio, why, margin = choose(s, donors)
        if mid in PINNED_DONORS and PINNED_DONORS[mid][0] in donors:
            d = donors[PINNED_DONORS[mid][0]]
            sc, ratio, why = score(s, d)
            ratio *= PINNED_DONORS[mid][1]
            margin = 0.0
            why = [f"pinned donor {d.unit}"] + [w for w in why if w.startswith("missing")]
        conf = confidence(s, d, sc, ratio, margin) if col else "low"
        vmap, pvtext = None, ""
        pv = prov_donor(s, prov, by_code, donors) if prov else None
        if pv:
            pd, pmap, pscale, pkind, pcover, pvtext = pv
            need = {MODE_ID[m] for m in s.modes if m not in ("SQ", "KB")}
            def missing(x):
                return [m for m in need if m not in x.modes and not (m in (0, 12) and (0 in x.modes or 12 in x.modes))]
            if len(missing(pd)) <= max(1, len(missing(d))):
                d, vmap, ratio = pd, pmap, pscale
                sc, _r, why = score(s, d)
                margin = 0.0
                conf = "high" if pcover >= 0.9 and pkind in ("exact", "recolour") else "medium"
                prov_n[pkind] += 1
                why = [pvtext] + [w for w in why if w.startswith("missing")]
            else:
                prov_n["unit lacks Median's modes"] += 1
        scale = None
        if col and not args.no_scale:
            sq = q(max(0.5, min(2.5, ratio)), 0.05)
            scale = None if abs(sq - 1) < 0.06 else sq
        adj, tint, label = ((0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0, 0.0), "none")
        if col and not args.no_tint:
            adj, tint, label = tint_of(col, d.col)
            adj = (q(adj[0], 0.02) % 1.0, q(adj[1], 0.05), q(adj[2], 0.05), 1.0)
            tint = tuple(q(t, 0.1) for t in tint[:3]) + (q(tint[3], 0.05),)
        vkey = json.dumps([adj, tint])
        vname = "v_" + hashlib.sha1(vkey.encode()).hexdigest()[:8]
        variants[vname] = (adj, tint)
        wk = s.basew if s.basew not in d.keys else None
        key = json.dumps([d.unit, scale, sorted(s.slots.items()), vname, wk, sorted(vmap.items()) if vmap else None])
        uname = f"mxl_{d.unit}_" + hashlib.sha1(key.encode()).hexdigest()[:7]
        units[uname] = dict(donor=d.unit, scale=scale, slots=s.slots, vname=vname, weapon=wk, basew=s.basew, vmap=vmap)
        hd[mid] = uname
        rows.append((mid, name, code, d.unit, f"{scale or 1.0:g}", label, conf,
                     ("; ".join(why) or "default") + f"; score {sc:.1f}, margin {margin:.1f}, size x{ratio:.2f}"
                     + ("" if col else "; no art found")))
    # emit
    hdc = out / "hd" / "character"
    for vname, (adj, tint) in variants.items():
        dv = load_json(D2R / "data/hd/character/enemy/zombie1/zombie1_variant.json")
        write_json(hdc / "enemy" / "mxl" / f"{vname}.json", variant_table(vname, dv, adj, tint))
    sms = set()
    n_notes = Counter()
    for uname, u in units.items():
        info = donors[u["donor"]].info
        sm_name = None
        if u["weapon"]:
            sm_name = f"data/hd/character/enemy/mxl/sm_{u['donor']}_{u['weapon']}.json"
            if sm_name not in sms:
                sms.add(sm_name)
                sm = adapt_state_machine(load_json(D2R / info["sm"]), u["weapon"], Path(sm_name).stem)
                write_json(out / sm_name.replace("data/", "", 1), sm)
        dv = load_json(D2R / info["var"]) if info["var"] else {"dependencies": {}}
        vp = f"data/hd/character/enemy/mxl/{u['vname']}.json"
        d, notes = build_unit(u["donor"], uname, u["slots"], u["scale"], None, None, u["basew"], sm_name, vp, u["vmap"])
        for n in notes:
            n_notes[n] += 1
        write_json(hdc / "enemy" / f"{uname}.json", d)
    write_json(hdc / "monsters.json", hd)
    (hdc / "monsters.json").write_text(json.dumps(hd, indent=2), encoding="utf-8")
    # report
    rep = ["# HD monster looks (tools/hd_monsters.py)", "",
           f"{len(rows)} Median monsters; {len(units)} unit definitions, {len(variants)} colour tables, "
           f"{len(sms)} adapted state machines.", "", "| Stats | |", "|---|---|"]
    conf_n = Counter(r[6] for r in rows)
    rep += [f"| {k} | {v} |" for k, v in sorted(conf_n.items())]
    rep += [f"| {k} | {v} |" for k, v in kept.items()]
    rep += [f"| provenance ({k}) | {v} |" for k, v in prov_n.items()]
    rep += ["", "| Median Id | Name | Code | Donor | Scale | Colour | Confidence | Why |", "|---|---|---|---|---|---|---|---|"]
    for r in sorted(rows, key=lambda r: (r[6], r[0])):
        rep.append("| " + " | ".join(str(x).replace("|", "/") for x in r) + " |")
    (out.parent / "hd_monsters.md").write_text("\n".join(rep) + "\n", encoding="utf-8")
    cache.save()
    print(f"{len(rows)} monsters, {len(units)} units, {len(variants)} colour tables; {dict(conf_n)}; "
          f"provenance {dict(prov_n)}; part notes {dict(n_notes)}; report -> {out.parent / 'hd_monsters.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

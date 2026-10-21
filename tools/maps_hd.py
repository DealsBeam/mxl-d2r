"""HD scenes for Median's maps: closest vanilla D2R stand-ins, no new art (docs/maps-hd.md).

D2R's HD client draws a map from a scene preset, `data/hd/env/preset/<ds1 path without .ds1>.json` (baked terrain model,
wall and prop entities, the biome = terrain textures and lighting, `data/hd/env/biome/*.json`). Without one it logs
`empty biome filename` once, draws the floor tiles on a default biome, and draws no walls or props. D2R has 2,100 scenes,
one per D2R map. Median ships 574 .ds1 files:

  * 426 carry D2R's names. D2R's own .ds1 files are byte-identical to the 1.13c ones, so every difference is a real
    Median edit (tools/maps_hd.py report compares them with the classic MPQs when they are given). Each keeps D2R's scene
    (the file name matches), which fits except where Median changed the layout (the report gives the share of changed tile
    cells per map). Maps whose layout differs too much (size changed, or more than --edited-threshold of the wall/floor
    cells) get a minimal scene of the edited map's own biome instead, which draws the floor from the tile data.
  * 148 are Median's own (146 used by levels, 2 unused): no scene. Each gets a MINIMAL scene, the same shape as D2R's own
    empty scenes (`act3/kurast/chaos08x08_4.json`: no entities, no terrain model, `biomeFilename` only), pointing at the
    closest vanilla biome, chosen by area (BIOME_BY_DIR / BIOME_BY_LEVELTYPE below, with the reason and a confidence).
    The biome gives the area its terrain textures, lighting, weather and ambience; the floor is painted from the tile data.

  py -3 tools/maps_hd.py report [--out DIR]       inventory, decisions, counts; writes report.json / report.md in DIR
  py -3 tools/maps_hd.py build DATA_DIR           write DATA_DIR/hd/env/preset/<map>.json for every map that needs one
tools/install_mod.py calls build() on the data folder it installs. Needs D2R (CASC) and extract/mxl tiles; reads the built
tables in work/mod/data/global/excel for the level -> map relation.
"""
import argparse
import collections
import csv
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parent.parent
MXL_TILES = ROOT / "extract" / "mxl" / "data" / "global" / "tiles"
EXCEL = ROOT / "work" / "mod" / "data" / "global" / "excel"
DEFAULT_GAME = os.environ.get("MXL_D2R_GAME", r"C:\Program Files (x86)\Diablo II Resurrected")
CLASSIC_MPQS = [r"C:\Games\median-xl\patch_d2.mpq", r"C:\Games\median-xl\d2exp.mpq", r"C:\Games\median-xl\d2data.mpq"]
B = chr(92)
BIOME = "data/hd/env/biome/%s.json"
PRESET = "data/hd/env/preset/"
DEFAULT_EDITED_THRESHOLD = 0.25   # share of wall/floor cells that changed above which D2R's baked scene is dropped

# ---- the stand-in table -------------------------------------------------------------------------------------------
# (directory prefix of the map under data/global/tiles, vanilla biome, confidence, why). First match wins; the level
# types below are the fallback. Confidence: high = the map's own tile set is a copy/edit of that biome's tile set;
# medium = same theme and act; low = nearest theme, tile set unrelated.
BIOME_BY_DIR = [
    ("mxl/zone/heaven", "act4_fort", "medium", "Silver City / Library of Fate: fort tile set (Pandemonium Fortress)"),
    ("mxl/generic/fort", "act4_fort", "high", "copy of Act 4 fort tiles"),
    ("mxl/zone/tran", "act3_jungle", "high", "tile set = Act 3 jungle (trees, treegrp, ruinwall) plus docks"),
    ("mxl/zone/tjun", "act3_jungle", "high", "Torajan Jungles: Act 3 jungle tiles"),
    ("mxl/zone/teganze", "act3_jungle", "high", "Teganze: Act 3 jungle tiles"),
    ("mxl/zone/toraja", "act3_jungle", "high", "tile set = Act 3 jungle (trees, pygmy, ruin, terraces)"),
    ("mxl/zone/vizjun", "act3_kurast", "medium", "level type 22 (Kurast)"),
    ("mxl/zone/k3k", "act3_kurast", "medium", "Kurast 3000 BA: Kurast tiles"),
    ("mxl/zone/sunless", "act3_jungle", "low", "Island of the Sunless Sea: jungle/kurast shore, no matching vanilla island"),
    ("mxl/zone/dunc", "act1_outdoors", "medium", "Duncraig: Act 1 monastery/outdoors tiles"),
    ("mxl/zone/truine", "act2_palace", "medium", "Triune: Act 2 palace/harem tile copies"),
    ("mxl/zone/lab", "act2_arcane", "medium", "Dimensional Labyrinth rooms: arcane/fort tiles"),
    ("mxl/zone/scosglen", "expansion_siege", "medium", "Scosglen: snow siege tile copies"),
    ("mxl/zone/whimsy", "act1_outdoors", "low", "Whimsyshire: no vanilla equivalent, bright Act 1 outdoors"),
    ("mxl/zone/fauzt", "act2_tomb", "low", "Fauztinville (level type 17)"),
    ("mxl/zone/halloween", "act1_campfire", "medium", "Rogue Encampment variants"),
    ("mxl/other/caldeum", "act2_town", "medium", "Caldeum: desert city (Lut Gholein)"),
    ("mxl/other/pacman", "act4_lava", "low", "secret level"),
    ("mxl/other/test", "act1_tristram", "low", "test / secret maps"),
    ("mxl/quest/ureh", "expansion_ruins", "high", "tile set = Act 5 town/ruins copies"),
    ("mxl/quest/yshari", "expansion_baallair", "medium", "Yshari sanctum: throne room tiles"),
    ("mxl/quest/void", "expansion_baallair", "medium", "Planes of the Void: Baal lair types"),
    ("mxl/quest/rathma", "act4_mesa", "high", "tile set = Act 4 mesa copies"),
    ("mxl/quest/newlab", "act4_diab", "medium", "Heart of Darkness: Act 4 interior tiles"),
    ("mxl/quest/nexus", "act2_arcane", "medium", "Nexus: arcane tiles"),
    ("mxl/quest/blackroad", "act1_cathedral", "medium", "Church of Dien Ap Sten: Act 1 cathedral/monastery"),
    ("mxl/quest/bremm", "act1_cathedral", "high", "tile set = cathedral copies (basewall, column, frescoes, stained)"),
    ("mxl/quest/cov", "act1_cathedral", "high", "Cathedral of Vanity: level type 9"),
    ("mxl/quest/arreat", "expansion_mountaintop", "high", "Arreat Summit"),
    ("mxl/quest/astro", "act3_spider", "high", "Web of Deceit: spider cavern (level type 23)"),
    ("mxl/quest/azmo", "act4_lava", "medium", "Realm of Sin: lava types"),
    ("mxl/quest/kab", "act4_lava", "medium", "Path to Enlightenment: lava types"),
    ("mxl/quest/samael", "act2_arcane", "medium", "Edge of Nothingness: arcane types"),
    ("mxl/quest/cof", "act2_arcane", "medium", "Dimensional Sanctum: arcane types"),
    ("mxl/quest/quov", "act2_arcane", "medium", "Vizjerei Temple: arcane types"),
    ("mxl/quest/infernal", "act1_caves", "high", "Infernal Cave: cave types"),
    ("mxl/quest/legion", "act4_mesa", "medium", "Court of the Damned: mesa types"),
    ("mxl/quest/mbwiru", "act3_sewer", "high", "Mbwiru Eikura: level type 25 (sewer)"),
    ("mxl/quest/belial", "expansion_icecave", "low", "Realm of Lies: ice cave types"),
    ("mxl/quest/butcher", "act1_tristram", "medium", "Butcher's Lair: level type 11"),
    ("mxl/quest/cow", "act1_outdoors", "medium", "Corral of Corruption: wilderness"),
    ("mxl/quest/danger", "act3_kurast", "medium", "Chamber of Offering: Kurast types"),
    ("mxl/quest/bartuc", "act3_jungle_dungeon", "medium", "Chamber of Blood: jungle dungeon"),
    ("mxl/quest/brolaz", "act3_jungle_dungeon", "medium", "Genesis: jungle dungeon"),
    ("mxl/quest/death", "act3_jungle_dungeon", "medium", "Proving Grounds: jungle dungeon"),
    ("mxl/quest/jday", "act4_fort", "medium", "Pools of Wisdom / Halls of Valor: fort types"),
    ("mxl/quest/riftwalker", "act2_outdoors", "low", "Storm oasis / cave / pit: mixed types, desert first"),
    ("mxl/quest/talrasha", "act2_outdoors", "medium", "Canyon of the Magi: desert"),
    ("mxl/quest/twin", "act2_outdoors", "medium", "Island of Skartara: desert (level type 16)"),
    ("mxl/quest/wretched", "act2_outdoors", "medium", "Wretched Sands: desert (level type 16)"),
]
# level type (levels.txt LevelType) -> biome, used when no directory rule matches (Median edits many vanilla types)
BIOME_BY_LEVELTYPE = {
    1: "act1_outdoors", 2: "act1_outdoors", 3: "act1_caves", 4: "act1_crypt", 5: "act1_outdoors", 6: "act1_court",
    7: "act1_barracks", 8: "act1_barracks", 9: "act1_cathedral", 10: "act1_catacombs", 11: "act1_tristram",
    12: "act2_town", 13: "act2_sewer", 14: "act2_palace", 15: "act2_palace_cells", 16: "act2_outdoors",
    17: "act2_tomb", 18: "act2_maggot", 19: "act2_arcane", 20: "act3_kurast", 21: "act3_jungle", 22: "act3_kurast",
    23: "act3_spider", 24: "act3_jungle_dungeon", 25: "act3_sewer", 26: "act4_fort", 27: "act4_mesa", 28: "act4_lava",
    29: "expansion_town", 30: "expansion_siege", 31: "expansion_mountaintop", 32: "expansion_wildtemple",
    33: "expansion_icecave", 34: "expansion_baallair", 35: "act4_lava", 36: "act2_town", 37: "expansion_ruins",
    38: "act2_arcane", 39: "act4_fort", 40: "act1_outdoors", 41: "act3_kurast", 42: "act1_cathedral",
    43: "act2_arcane", 44: "expansion_siege", 50: "act4_diab", 51: "act4_lava",
}
LEVELTYPE_FALLBACK = "act1_outdoors"


def read_tsv(path: Path):
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def norm(rel: str) -> str:
    return rel.replace(B, "/").lower()


class D2R:
    """What D2R has: scene names, map names, bytes on demand (CASC)."""

    def __init__(self, game=DEFAULT_GAME):
        import native
        self.native = native
        self.s = native.open_casc(game)
        self.scenes, self.ds1, self.biomes = set(), {}, set()
        for n, _size, _ in native.casc_files(self.s, "*"):
            low = n.lower().replace(B, "/")
            if low.startswith("data:data/hd/env/preset/") and low.endswith(".json"):
                self.scenes.add(low[len("data:data/hd/env/preset/"):-5])
            elif low.startswith("data:data/global/tiles/") and low.endswith(".ds1"):
                self.ds1[low[len("data:data/global/tiles/"):-4]] = n
            elif low.startswith("data:data/hd/env/biome/") and low.endswith(".json"):
                self.biomes.add(low[len("data:data/hd/env/biome/"):-5])

    def read_ds1(self, key):
        return self.native.casc_read(self.s, self.ds1[key])

    def scene_biome(self, key):
        """The biome a D2R scene names (its terrain textures and lighting)."""
        j = json.loads(self.native.casc_read(self.s, "data:" + PRESET + key.replace("/", B) + ".json"))
        b = j.get("biomeFilename") or ""
        return b[len("data/hd/env/biome/"):-5] if b.startswith("data/hd/env/biome/") else None


def median_maps(tiles: Path = MXL_TILES) -> dict:
    return {p.relative_to(tiles).as_posix().lower()[:-4]: p for p in sorted(tiles.rglob("*.ds1"))}


def level_usage(excel: Path = EXCEL):
    """map key -> sorted level ids that list it in lvlprest (rows with a LevelId); levels dict id -> row; groups."""
    levels = {int(r["Id"]): r for r in read_tsv(excel / "levels.txt") if (r.get("Id") or "").isdigit()}
    use = collections.defaultdict(set)
    for r in read_tsv(excel / "lvlprest.txt"):
        lid = r.get("LevelId", "")
        piece = not lid.isdigit() or lid == "0"
        for i in range(1, 7):
            f = r.get("File%d" % i, "")
            if f and f != "0":
                key = norm(f)[:-4]
                use[key].add(int(lid) if not piece else -1)
    return levels, use


def level_group(lv: dict) -> str:
    """The three groups of Median-added levels (id >= 138), docs/maps-hd.md."""
    lid = int(lv["Id"])
    if lid < 138 or not lv.get("DrlgType"):
        return "d2r"
    lt = int(lv.get("LevelType") or 0)
    if lv["DrlgType"] == "1":
        return "random (maze) on a vanilla level type"
    return "fixed layout on a vanilla level type" if lt < 36 else "fixed layout on a Median level type"


def choose_biome(key: str, level_ids, levels):
    """-> (biome, confidence, reason) for a Median-only map."""
    for prefix, biome, conf, why in BIOME_BY_DIR:
        if key == prefix or key.startswith(prefix + "/"):
            return biome, conf, why
    lts = collections.Counter(int(levels[l]["LevelType"] or 0) for l in level_ids if l in levels)
    if lts:
        lt = lts.most_common(1)[0][0]
        return BIOME_BY_LEVELTYPE.get(lt, LEVELTYPE_FALLBACK), "medium" if lt < 36 else "low", "level type %d" % lt
    return LEVELTYPE_FALLBACK, "low", "map not used by a level"


def ds1_diff(mbytes: bytes, dbytes: bytes) -> dict:
    """How Median's edit differs from D2R's map: size, layer counts, changed wall/floor cells, objects."""
    import ds1_remap
    m, d = ds1_remap.Ds1(mbytes), ds1_remap.Ds1(dbytes)
    r = {"size_m": [m.width, m.height], "size_d": [d.width, d.height]}
    if (m.width, m.height) != (d.width, d.height):
        r["cls"] = "size changed"
        r["visual_changed"] = 1.0
        return r
    cells = m.width * m.height
    if (m.walls, m.floors, m.tag_type) != (d.walls, d.floors, d.tag_type):
        # layer sets differ: compare the first floor layer and the first wall layer by position
        nm, nd = m.walls * 2, d.walls * 2
        wm, wd = m.cells(0), d.cells(0)
        fm, fd = m.cells(nm), d.cells(nd)
        ch = sum(1 for a, b in zip(wm, wd) if a != b) + sum(1 for a, b in zip(fm, fd) if a != b)
        r["cls"] = "layers differ"
        r["visual_changed"] = min(1.0, ch / (2 * cells))
    else:
        lm, ld = [m.cells(i) for i in range(m.walls * 2 + m.floors)], [d.cells(i) for i in range(d.walls * 2 + d.floors)]
        vis = [i for i in range(m.walls * 2 + m.floors) if i >= m.walls * 2 or i % 2 == 0]   # wall styles, not orientations
        ch = 0
        for i in vis:
            ch += sum(1 for a, b in zip(lm[i], ld[i]) if a != b)
        r["cls"] = "tiles changed" if ch else "tiles same"
        r["visual_changed"] = ch / (len(vis) * cells) if vis else 0.0
    mo = collections.Counter((o[0], o[1], o[2], o[3]) for o in m.objects)
    do = collections.Counter((o[0], o[1], o[2], o[3]) for o in d.objects)
    r["objects_added"], r["objects_removed"] = sum((mo - do).values()), sum((do - mo).values())
    return r


def classic_bytes(key: str, handles, tmp) -> bytes | None:
    import native
    t = os.path.join(tmp, "x.ds1")
    for h in handles:
        if os.path.exists(t):
            os.remove(t)
        if native.extract(h, B.join(["data", "global", "tiles"]) + B + key.replace("/", B) + ".ds1", t) and os.path.exists(t):
            return Path(t).read_bytes()
    return None


def scene_json(biome: str) -> str:
    """The minimal scene: the shape of D2R's own empty presets (act3/kurast/chaos08x08_4.json)."""
    path = BIOME % biome
    scene = {"dependencies": {"particles": [], "models": [], "skeletons": [], "animations": [], "textures": [],
                              "physics": [], "json": [{"path": path}], "variantdata": [], "objecteffects": [],
                              "other": []},
             "type": "Preset", "name": "preset", "entities": [], "terrain": {}, "biomeFilename": path,
             "perTileBiomeOverrides": [], "specialTiles": {}}
    return json.dumps(scene, separators=(",", ":"))


def plan(game=DEFAULT_GAME, tiles=MXL_TILES, excel=EXCEL, threshold=DEFAULT_EDITED_THRESHOLD, classic=True):
    """Decide every map. -> list of dicts (one per Median map) + level summary."""
    d2r = D2R(game)
    maps = median_maps(tiles)
    levels, use = level_usage(excel)
    handles, tmp = [], None
    if classic and all(os.path.exists(p) for p in CLASSIC_MPQS):
        import native
        import tempfile
        handles = [native.open_mpq(p) for p in CLASSIC_MPQS]
        tmp = tempfile.mkdtemp()
    out = []
    for key, p in maps.items():
        lids = sorted(l for l in use.get(key, ()) if l >= 0)
        groups = sorted({level_group(levels[l]) for l in lids if l in levels})
        e = {"map": key + ".ds1", "levels": lids, "level_groups": groups, "piece": -1 in use.get(key, ())}
        mb = p.read_bytes()
        if key in d2r.ds1:
            db = d2r.read_ds1(key)
            e["has_d2r_scene"] = key in d2r.scenes
            cb = classic_bytes(key, handles, tmp) if handles else None
            if cb is not None:
                e["d2r_equals_113c"] = cb == db
            if mb == db:
                e.update(kind="D2R map, unchanged", action="D2R scene", confidence="exact", note="byte-identical to D2R's map")
            else:
                diff = ds1_diff(mb, db)
                e["diff"] = diff
                share = diff["visual_changed"]
                e["kind"] = "D2R map, edited by Median"
                if not e["has_d2r_scene"]:
                    biome, _c, why = choose_biome(key, lids, levels)
                    e.update(action="minimal scene", biome=biome, confidence="low", note="D2R has the map but no scene")
                elif diff["cls"] == "size changed" or share > threshold:
                    biome = d2r.scene_biome(key) or choose_biome(key, lids, levels)[0]
                    e.update(action="minimal scene", biome=biome, confidence="medium",
                             note="layout differs too much for D2R's baked scene (%s, %.0f%% of wall/floor cells)" % (
                                 diff["cls"], 100 * share))
                else:
                    conf = "high" if share <= 0.03 else "medium"
                    e.update(action="D2R scene", confidence=conf,
                             note="Median's layout on D2R's scene: %.1f%% of wall/floor cells differ%s" % (
                                 100 * share, "; %d objects added, %d removed" % (diff["objects_added"], diff["objects_removed"])
                                 if diff["objects_added"] or diff["objects_removed"] else ""))
        else:
            biome, conf, why = choose_biome(key, lids, levels)
            e.update(kind="Median map", action="minimal scene", biome=biome, confidence=conf, note=why)
        if e["action"] == "minimal scene" and e.get("biome") not in d2r.biomes:
            raise SystemExit("biome %s of %s is not in D2R" % (e.get("biome"), key))
        out.append(e)
    return out, levels, d2r


def build(data_dir: Path, game=DEFAULT_GAME, threshold=DEFAULT_EDITED_THRESHOLD, quiet=False, entries=None, excel=None):
    """Write the minimal scenes into data_dir (the mod's data folder). -> entries. The level tables are read from
    data_dir/global/excel when it has them (install_mod), else from work/mod."""
    if entries is None:
        ex = Path(excel) if excel else (Path(data_dir) / "global" / "excel")
        entries, _lv, _d = plan(game, excel=ex if (ex / "levels.txt").exists() else EXCEL, threshold=threshold, classic=False)
    n = 0
    for e in entries:
        if e["action"] != "minimal scene":
            continue
        dest = Path(data_dir) / "hd" / "env" / "preset" / (e["map"][:-4] + ".json")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(scene_json(e["biome"]), encoding="utf-8")
        n += 1
    if not quiet:
        c = collections.Counter(e["action"] for e in entries)
        print("maps_hd: %d maps; %s; wrote %d minimal scenes" % (len(entries), ", ".join("%d %s" % (v, k) for k, v in c.items()), n))
    return entries


def summary(entries, levels):
    lines = []
    c = collections.Counter((e["kind"], e["action"], e["confidence"]) for e in entries)
    for (kind, action, conf), n in sorted(c.items()):
        lines.append("%4d  %-28s %-14s %s" % (n, kind, action, conf))
    g = collections.defaultdict(lambda: collections.Counter())
    for e in entries:
        for grp in e["level_groups"] or ["maze piece / other"]:
            g[grp][e["action"] + " / " + e["kind"]] += 1
    return lines, g


def write_report(entries, levels, out: Path):
    out.mkdir(parents=True, exist_ok=True)
    (out / "report.json").write_text(json.dumps(entries, indent=1), encoding="utf-8")
    lines, groups = summary(entries, levels)
    md = ["# Median maps in HD: stand-in report (tools/maps_hd.py)", "", "Generated; one row per Median .ds1 map.", "",
          "## Counts", "", "| maps | kind | action | confidence |", "|---|---|---|---|"]
    for ln in lines:
        n, rest = ln.split(None, 1)
        parts = [x.strip() for x in rest.split("  ") if x.strip()]
        md.append("| %s | %s |" % (n, " | ".join(parts)))
    md += ["", "## Per map", "", "| map | levels | kind | action | stand-in | confidence | note |", "|---|---|---|---|---|---|---|"]
    for e in entries:
        lv = ", ".join(str(l) for l in e["levels"][:6]) + ("..." if len(e["levels"]) > 6 else "")
        md.append("| %s | %s | %s | %s | %s | %s | %s |" % (
            e["map"], lv or ("piece" if e["piece"] else "-"), e["kind"], e["action"], e.get("biome", "D2R scene"), e["confidence"],
            e["note"].replace("|", "/")))
    (out / "report.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    return out / "report.md"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("cmd", choices=["report", "build"])
    ap.add_argument("path", nargs="?", help="build: the data folder to write into (default: work/maps_hd/data)")
    ap.add_argument("--out", default=str(ROOT / "work" / "maps_hd"), help="report folder")
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--edited-threshold", type=float, default=DEFAULT_EDITED_THRESHOLD)
    ap.add_argument("--no-classic", action="store_true", help="skip the comparison with the 1.13c MPQs")
    a = ap.parse_args()
    entries, levels, _d2r = plan(a.game, threshold=a.edited_threshold, classic=not a.no_classic and a.cmd == "report")
    if a.cmd == "build":
        build(Path(a.path or ROOT / "work" / "maps_hd" / "data"), a.game, a.edited_threshold, entries=entries)
    p = write_report(entries, levels, Path(a.out))
    lines, groups = summary(entries, levels)
    print("\n".join(lines))
    print("report:", p)


if __name__ == "__main__":
    main()

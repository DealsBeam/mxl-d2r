"""HD inventory pictures for Median's items: work/mod/data/hd/items/{items,uniques,sets}.json (bug #18 stopgap).

D2R draws an item in HD from three maps under data/hd/items/: items.json (base item code -> asset), uniques.json and
sets.json (unique / set item name -> asset for its normal, exceptional and elite base). The name key is the
uniqueitems/setitems index lowercased, with spaces and hyphens as "_" and apostrophes dropped ("Buriza-Do Kyanon" ->
"buriza_do_kyanon"). An asset is a path under data/hd/global/ui/items/<weapon|armor|misc>/, the folder being the
base item's table. Median's new codes are not in D2R's map, so their pictures fall back to a missing placeholder
(vendors spam "File not found ...[default_unit]" and crash), and D2R's unique/set entries name items Median does not
have (the Item Spawner crashed on "stormguild"). Until Median's own art is ported (Phase 5) every Median item gets a
picture D2R has:

  base items  D2R's own entry for the code; else the D2R item of the same table with the same invfile (the classic
              picture, which Median mostly reuses); else the most common one of the same item type, or of its
              nearest parent type (itemtypes Equiv1/2); else of the table
  uniques     D2R's entry with the same name when its base is in the same table; else a D2R unique with the same
  and sets    invfile (Median reuses many); else the base item's picture

Phase 5: an item whose picture is Median's own (its invfile, or the unique's / set item's invfile, is a DC6 D2R does
not have; 542 files) gets that picture converted to HD instead (tools/item_art.py: <folder>/mxl/<invfile>.sprite).
Every asset written is checked against D2R's file list or the converted sprites. Report: work/mod/item_hd_report.md.
Usage: python tools/item_hd.py
"""
import csv
import json
from item_art import median_asset
from strings_ns import bare
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
D2R = ROOT / "extract" / "d2r" / "93847"
EXCEL = D2R / "data" / "global" / "excel"
HD = D2R / "data" / "hd" / "items"
OUT_EXCEL = ROOT / "work" / "mod" / "data" / "global" / "excel"
OUT = ROOT / "work" / "mod" / "data" / "hd" / "items"
GAME = r"C:\Program Files (x86)\Diablo II Resurrected"
TABLES = {"weapons": "weapon", "armor": "armor", "misc": "misc"}
# Last resort per table (misc's most common picture is Mephisto's key, which would mislead): a neutral small item
DEFAULTS = {"misc": "charm/charm_small"}


def rows(path: Path) -> list[dict]:
    with path.open(encoding="utf-8", errors="replace", newline="") as f:
        return [r for r in csv.DictReader(f, delimiter="\t")]


def load_map(name: str) -> dict:
    path = HD / f"{name}.json"
    if not path.exists():
        import native
        storage = native.open_casc(GAME)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(native.casc_read(storage, f"data:data/hd/items/{name}.json"))
    return {k: v for e in json.loads(path.read_text("utf-8-sig")) for k, v in e.items()}


def key(index: str) -> str:
    # uniqueitems / setitems index cells are localization cells (medianxl:<key> for Median-only names): the asset name
    # is derived from the plain name
    return re.sub(r"[ \-]", "_", bare(index).lower()).replace("'", "")


def sprites() -> set[str]:
    """'weapon/axe/hand_axe' for every HD inventory sprite D2R has."""
    out = set()
    for line in (D2R / "filelist.txt").read_text("utf-8").splitlines():
        if m := re.fullmatch(r"(?:data:)?data/hd/global/ui/items/(.+)\.sprite", line.strip().lower()):
            if not m[1].endswith(".lowend"):
                out.add(m[1])
    return out


def main() -> int:
    have = sprites()
    d_items, d_uniques, d_sets = load_map("items"), load_map("uniques"), load_map("sets")

    def exists(folder: str, asset: str) -> bool:
        return f"{folder}/{asset}".lower() in have

    # vanilla: code -> (table, invfile, type); invfile/type -> assets per table
    folder_of: dict[str, str] = {}
    by_inv: dict[tuple[str, str], Counter] = defaultdict(Counter)
    by_type: dict[tuple[str, str], Counter] = defaultdict(Counter)
    by_table: dict[str, Counter] = defaultdict(Counter)
    for table, folder in TABLES.items():
        for r in rows(EXCEL / f"{table}.txt"):
            code = r.get("code", "")
            asset = d_items.get(code, {}).get("asset")
            if not code or not asset or not exists(folder, asset):
                continue
            folder_of[code] = folder
            by_inv[(folder, r.get("invfile", "").lower())][asset] += 1
            by_type[(folder, r.get("type", "").lower())][asset] += 1
            by_table[folder][asset] += 1

    # Median's item types: parents through Equiv1/Equiv2, nearest first
    parents = {r["Code"].lower(): [r.get("Equiv1", "").lower(), r.get("Equiv2", "").lower()]
               for r in rows(OUT_EXCEL / "itemtypes.txt") if r.get("Code")}

    def ancestors(t: str) -> list[str]:
        out, todo = [], list(parents.get(t, []))
        while todo:
            p = todo.pop(0)
            if p and p not in out:
                out.append(p)
                todo += parents.get(p, [])
        return out

    # Median base items
    items: dict[str, dict] = {}
    m_folder: dict[str, str] = {}
    m_cells: dict[str, tuple[int, int]] = {}

    def cells(r: dict) -> tuple[int, int]:
        def num(v: str) -> int:
            return int(v) if v and v.strip().lstrip("-").isdigit() else 1
        return num(r.get("invwidth", "")), num(r.get("invheight", ""))

    def own_art(inv: str, folder: str, size: tuple[int, int]) -> str | None:
        a = median_asset(inv, folder, *size)
        if a:
            have.add(f"{folder}/{a}")
        return a
    how = Counter()
    for table, folder in TABLES.items():
        for r in rows(OUT_EXCEL / f"{table}.txt"):
            code = r.get("code", "")
            if not code or code in items:
                continue
            m_folder[code] = folder
            m_cells[code] = cells(r)
            asset = d_items.get(code, {}).get("asset")
            if own := own_art(r.get("invfile", ""), folder, m_cells[code]):
                asset = own
                how["Median's own art"] += 1
            elif asset and folder_of.get(code) == folder and exists(folder, asset):
                how["D2R's own"] += 1
            elif c := by_inv.get((folder, r.get("invfile", "").lower())):
                asset = c.most_common(1)[0][0]
                how["same invfile"] += 1
            elif c := by_type.get((folder, r.get("type", "").lower())):
                asset = c.most_common(1)[0][0]
                how["same item type"] += 1
            elif c := next((by_type[(folder, t)] for t in ancestors(r.get("type", "").lower())
                            if (folder, t) in by_type), None):
                asset = c.most_common(1)[0][0]
                how["parent item type"] += 1
            else:
                asset = DEFAULTS.get(folder) or by_table[folder].most_common(1)[0][0]
                how["table default"] += 1
            items[code] = {"asset": asset}

    # uniques / sets: vanilla invfile -> its unique/set asset, per folder
    def special_by_inv(txt: str, mapping: dict, code_col: str) -> dict[tuple[str, str], str]:
        out = {}
        for r in rows(EXCEL / f"{txt}.txt"):
            entry = mapping.get(key(r.get("index", "")))
            folder = folder_of.get(r.get(code_col, ""))
            inv = r.get("invfile", "").lower()
            if entry and folder and inv and exists(folder, entry["normal"]):
                out.setdefault((folder, inv), entry["normal"])
        return out

    def specials(txt: str, mapping: dict, code_col: str, label: str) -> list[dict]:
        inv_assets = special_by_inv(txt, mapping, code_col)
        vanilla_folder = {key(r.get("index", "")): folder_of.get(r.get(code_col, ""))
                          for r in rows(EXCEL / f"{txt}.txt")}
        out, seen = [], set()
        for r in rows(OUT_EXCEL / f"{txt}.txt"):
            name, code = r.get("index", ""), r.get(code_col, "")
            k, folder = key(name), m_folder.get(code)
            if not name or not folder or k in seen:
                continue
            seen.add(k)
            entry = mapping.get(k)
            if own := own_art(r.get("invfile", ""), folder, m_cells.get(code, (1, 1))):
                entry = {"normal": own, "uber": own, "ultra": own}
                how[f"{label}: Median's own art"] += 1
            elif entry and vanilla_folder.get(k) == folder and all(exists(folder, a) for a in entry.values()):
                how[f"{label}: D2R's own"] += 1
            elif a := inv_assets.get((folder, r.get("invfile", "").lower())):
                entry = {"normal": a, "uber": a, "ultra": a}
                how[f"{label}: same invfile"] += 1
            else:
                a = items[code]["asset"]
                entry = {"normal": a, "uber": a, "ultra": a}
                how[f"{label}: base item"] += 1
            out.append({k: entry})
        return out

    uniques = specials("uniqueitems", d_uniques, "code", "uniques")
    sets = specials("setitems", d_sets, "item", "sets")

    OUT.mkdir(parents=True, exist_ok=True)
    for name, data in (("items", [{c: v} for c, v in items.items()]), ("uniques", uniques), ("sets", sets)):
        (OUT / f"{name}.json").write_text("[\n" + ",\n".join("  " + json.dumps(e) for e in data) + "\n]\n",
                                          encoding="utf-8")
    bad = [c for c, v in items.items() if not exists(m_folder[c], v["asset"])]
    report = ["# HD item pictures (bug #18 stopgap)", "", f"{len(items)} base items, {len(uniques)} uniques, "
              f"{len(sets)} set items; assets missing from D2R: {len(bad)}", ""]
    report += [f"- {k}: {n}" for k, n in sorted(how.items())]
    (ROOT / "work" / "mod" / "item_hd_report.md").write_text("\n".join(report) + "\n", encoding="utf-8")
    print(f"items.json {len(items)}, uniques.json {len(uniques)}, sets.json {len(sets)}; "
          + ", ".join(f"{k} {n}" for k, n in sorted(how.items())) + f"; missing assets {len(bad)}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

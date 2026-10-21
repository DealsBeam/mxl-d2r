"""Median's statscreen table (extra character stat pages) decoded; the stats it lists.

Input:  extract/mxl/data/global/excel/statscreen.bin (tools/mpq_extract.py), extract/mxl/data/local/lng/eng/*.tbl
Output: `python tools/stat_screen.py` prints docs/subsystems/stat-screen.md's row table (markdown, to stdout).
        `advanced_stat_rows()` is what txt_migrate.py's advanced_stats() flags in itemstatcost.advdisplay.

The table (u32 count 242, then 33-byte rows; D2Sigma ..\\ui\\StatScreen.cpp, draw loop FUN_10117dc0 rva 0x117dc0,
one handler per type) is the whole stat sheet: page 0 (the main sheet, drawn by D2R's own panel here), page 1 and 2 (the
two extra pages of stat-screen.dc6, 60+ boxes), page 255 (the prev / next buttons). A row draws one text on its page:
  +0 u8   page (255 = every page)
  +1 u32  type: 1 label (string +9), 2 stat value, 4 resistance (colour by cap), 0x12 stat with the class as layer,
          0x15 stat / 10, 0x17 weapon damage bonus from Strength / Dexterity, 0x19 stat >> ValShift capped at stat 205 + 60;
          0x10 / 0x11 page buttons, 0x0d... 0x0f are page 0's name, class, vitals
  +5 u16 x, +7 u16 y   classic pixels (512 x 704 sheet) of the text's top-left
  +9 u16 string id (table * 10000 + index; 5382 = none); the label text of a type 1 row
  +11 u8 font, +12 u8 colour (1 red, 4 gold, 6 white; the dword at +19 is the alignment: 0 right, 1 left, 2 centre)
  +13 u16 stat (Median's itemstatcost row)   +15 u16 layer (the stat's parameter)
  +17 u8  1 = the base value (GetBaseStat) instead of the total   +18 u8 1 = append "%"
  +19 u32 alignment   +23 u16 tooltip string id of a label row   +29 u32 cap (0 = none; min(value, cap))
"""
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXTRACT = ROOT / "extract" / "mxl"
NAMED = ROOT / "work" / "named"
TABLES = (("string", 0), ("patchstring", 10000), ("expansionstring", 20000))
NO_STRING = 5382
PAGES = (1, 2)       # the extra pages (page 0 is the main sheet)
VALUE_TYPES = {2, 4, 0x12, 0x15, 0x17, 0x19}
# stats whose value D2R's Advanced Stats list cannot show: 83 (class skills) needs the class as layer; 185 (Stat Signets)
# has no description; 299 / 300 are not stats but the Strength / Dexterity damage bonus Median's code computes
NOT_LISTED = {83, 185, 299, 300}
# shown even at 0 (advdisplay 1, listed first): the boxes of the sheet that are never empty
ALWAYS_LISTED = ("tohit", "toblock", "item_fasterattackrate", "item_fastercastrate", "item_fasterblockrate",
                 "item_fastergethitrate", "item_fastermovevelocity", "fireresist", "coldresist", "lightresist", "poisonresist",
                 "magicresist", "damageresist")
TYPE_NAMES ={1: "label", 2: "stat", 4: "resist", 0x12: "stat (class layer)", 0x15: "stat / 10", 0x17: "str / dex dmg bonus",
              0x19: "stat >> shift, capped"}


def load_strings() -> dict[int, str]:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import tbl2json
    tbl2json.TBL = EXTRACT / "data" / "local" / "lng" / "eng"
    by_id, _ = tbl2json.load_median()
    return {i: t for i, (_k, t) in by_id.items()}


def read_rows() -> list[dict]:
    d = (EXTRACT / "data" / "global" / "excel" / "statscreen.bin").read_bytes()
    n = struct.unpack_from("<I", d, 0)[0]
    rows = []
    for i in range(n):
        r = d[4 + i * 33: 4 + (i + 1) * 33]
        rows.append(dict(
            i=i, page=r[0], type=struct.unpack_from("<I", r, 1)[0], x=struct.unpack_from("<H", r, 5)[0],
            y=struct.unpack_from("<H", r, 7)[0], label=struct.unpack_from("<H", r, 9)[0], font=r[11], color=r[12],
            stat=struct.unpack_from("<H", r, 13)[0], layer=struct.unpack_from("<H", r, 15)[0], base=r[17], pct=r[18],
            align=struct.unpack_from("<I", r, 19)[0], tip=struct.unpack_from("<H", r, 23)[0],
            cap=struct.unpack_from("<I", r, 29)[0]))
    return rows


def stat_names() -> list[str]:
    """Median itemstatcost row -> migrated stat name (work/named/itemstatcost.txt)."""
    lines = (NAMED / "itemstatcost.txt").read_text("utf-8-sig", errors="replace").splitlines()
    return [l.split("\t")[0] for l in lines[1:]]


def advanced_stat_rows() -> list[str]:
    """Names (migrated itemstatcost keys) of the stats pages 1 and 2 show, in sheet order, without duplicates."""
    names = stat_names()
    out: list[str] = []
    for r in sorted(read_rows(), key=lambda r: (r["page"], r["y"], r["x"])):
        if r["page"] in PAGES and r["type"] in VALUE_TYPES and r["stat"] not in NOT_LISTED:
            n = names[r["stat"]]
            if n not in out:
                out.append(n)
    return out


def markdown() -> str:
    strings = load_strings()
    names = stat_names()

    def s(i: int) -> str:
        return "" if i == NO_STRING or i not in strings else strings[i].replace("\n", " / ").replace("|", "/")

    rows = read_rows()
    out = []
    for page in PAGES:
        out += ["", f"### Page {page + 1} of the sheet (statscreen page {page})", "",
                "| row | x,y | label (string id) | stat (Median row) | how the value is shown | tooltip |", "|---|---|---|---|---|---|"]
        page_rows = sorted((r for r in rows if r["page"] == page), key=lambda r: (r["y"], r["x"]))
        labels = [r for r in page_rows if r["type"] == 1 and s(r["label"]) not in ("", "-") and not s(r["label"]).startswith("/")]
        for r in page_rows:
            if r["type"] == 1:
                continue
            # the label is the nearest text to the left on (about) the same line, else a section header close by
            near = [l for l in labels if abs(l["y"] - r["y"]) <= 14 and l["x"] < r["x"]]
            label = max(near, key=lambda l: l["x"]) if near else None
            if label is None:
                head = [l for l in labels if l["color"] == 1 and abs(l["y"] - r["y"]) <= 40]
                label = min(head, key=lambda l: abs(l["y"] - r["y"])) if head else None
            how = TYPE_NAMES.get(r["type"], hex(r["type"]))
            if r["base"]:
                how += ", base value"
            if r["pct"]:
                how += ", %"
            if r["cap"]:
                how += f", cap {r['cap']}"
            if r["layer"]:
                how += f", layer {r['layer']}"
            lab = f"{s(label['label'])} ({label['label']})" if label else ""
            tip = s(label["tip"]) if label else ""
            out.append(f"| {r['i']} | {r['x']},{r['y']} | {lab} | {names[r['stat']]} ({r['stat']}) | {how} | {tip} |")
    return "\n".join(out)


def main() -> int:
    print(markdown())
    return 0


if __name__ == "__main__":
    sys.exit(main())

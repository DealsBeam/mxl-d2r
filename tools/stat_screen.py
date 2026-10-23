"""Median's statscreen table (extra character stat pages) decoded; the stats it lists.

Input:  assets/median/data/global/excel/statscreen.bin (tools/mpq_extract.py), assets/median/data/local/lng/eng/*.tbl
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
  +11 u8 colour (statscreen.txt Color: text colour code), +12 u8 font (Font: D2 font id); the dword at +19 is the
          alignment: 0 right, 1 left, 2 centre
  +13 u16 stat (Median's itemstatcost row)   +15 u16 layer (the stat's parameter)
  +17 u8  1 = the base value (GetBaseStat) instead of the total   +18 u8 1 = append "%"
  +19 u32 alignment   +23 u16 tooltip string id of a label row   +25 u32 min (always 0)   +29 u32 max (0 = none; min(value, max))
"""
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXTRACT = ROOT / "assets" / "median"
NAMED = ROOT / "work" / "named"
TABLES = (("string", 0), ("patchstring", 10000), ("expansionstring", 20000))
NO_STRING = 5382
PAGES = (1, 2)       # the extra pages (page 0 is the main sheet)
VALUE_TYPES = {2, 4, 0x12, 0x15, 0x17, 0x19}
# stats whose value D2R's Advanced Stats list cannot show: 83 (class skills) needs the class as layer; 185 (Stat Signets)
# has no description. 299 / 300 are not stats but the Strength / Dexterity damage bonus Median's code computes: they are
# listed (the plugin's stat_columns.cpp answers their value) and so are the "% bonus" stats of the attribute / life / mana
# rows, which are not rows of their own in the list but the middle column of the attribute rows (column_groups)
NOT_LISTED = {83, 185}
# shown even at 0 (advdisplay 1, listed first): the boxes of the sheet that are never empty
ALWAYS_LISTED = ("item_tohit_undead_bytime", "item_crushingblow_bytime", "tohit", "toblock", "item_fasterattackrate", "item_fastercastrate", "item_fasterblockrate",
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
            y=struct.unpack_from("<H", r, 7)[0], label=struct.unpack_from("<H", r, 9)[0], color=r[11], font=r[12],
            stat=struct.unpack_from("<H", r, 13)[0], layer=struct.unpack_from("<H", r, 15)[0], base=r[17], pct=r[18],
            align=struct.unpack_from("<I", r, 19)[0], tip=struct.unpack_from("<H", r, 23)[0],
            cap=struct.unpack_from("<I", r, 29)[0]))
    return rows


def stat_names() -> list[str]:
    """Median itemstatcost row -> migrated stat name (work/named/itemstatcost.txt)."""
    lines = (NAMED / "itemstatcost.txt").read_text("utf-8-sig", errors="replace").splitlines()
    return [l.split("\t")[0] for l in lines[1:]]


def stat_ids() -> list[int]:
    """Median itemstatcost row -> D2R stat id (the ID column of work/named/itemstatcost.txt: rows from the 1.13c count up are
    moved behind D2R's own rows)."""
    lines = (NAMED / "itemstatcost.txt").read_text("utf-8-sig", errors="replace").splitlines()
    return [int(l.split("	")[1]) for l in lines[1:]]


def plugin_header() -> str:
    """plugin/src/mxl_statcols.h: the stats (D2R ids) of the columns and of the damage bonus rows, for stat_columns.cpp."""
    ids = stat_ids()
    names = stat_names()
    groups = column_groups()
    strength, dexterity = damage_bonus_stats()
    entries = [f"    {{{ids[t]}, {ids[b] if b is not None else -1}}},  // {names[t]}" + (f" / {names[b]}" if b is not None else "")
               for t, b in groups]
    return "\n".join(["// Generated by tools/txt_migrate.py (advanced_stats, from statscreen.bin via tools/stat_screen.py); do not edit.",
                      "#pragma once", "", "#include <cstdint>", "", "namespace mxl_statcols {", "",
                      "// Median's character sheet page 2 shows these as total / % bonus / base columns: the stat (total and base) and",
                      "// the stat of the percent column (-1: none), D2R stat ids.", "struct Group {", "	int32_t stat;", "	int32_t bonusStat;", "};",
                      "inline constexpr Group Groups[] = {", *entries, "};", "",
                      "// The two computed rows (statscreen type 0x17): Strength / Dexterity damage bonus, listed as stats of these ids.",
                      f"inline constexpr int32_t StrengthDamageStat = {ids[strength]};",
                      f"inline constexpr int32_t DexterityDamageStat = {ids[dexterity]};", "",
                      "}  // namespace mxl_statcols", ""])


def column_groups() -> list[tuple[int, int | None]]:
    """The attribute / life / mana / stamina rows of page 1: (Median stat row of the total and base columns, Median stat
    row of the "% bonus" column or None). A row of the sheet is a total (GetStat), a percent stat and a base (GetBaseStat,
    the `base` flag); the three share a line (y) and read left to right."""
    rows = [r for r in read_rows() if r["page"] == 1 and r["type"] == 2]
    out: list[tuple[int, int | None]] = []
    for b in sorted((r for r in rows if r["base"]), key=lambda r: (r["y"], r["x"])):
        line = [r for r in rows if abs(r["y"] - b["y"]) <= 3 and r["x"] < b["x"] and not r["base"]]
        total = max((r for r in line if r["stat"] == b["stat"]), key=lambda r: r["x"], default=None)
        bonus = max((r for r in line if r["pct"] and r["stat"] != b["stat"] and (total is None or r["x"] > total["x"])),
                    key=lambda r: r["x"], default=None)
        if total is not None:
            out.append((b["stat"], bonus["stat"] if bonus else None))
    return out


def damage_bonus_stats() -> tuple[int, int]:
    """The two stats of the type 0x17 rows (Strength first: D2Sigma's handler 0x115e90 compares the row's stat with 0x12b)."""
    stats = sorted({r["stat"] for r in read_rows() if r["type"] == 0x17})
    assert stats == [299, 300], stats
    return stats[0], stats[1]


def advanced_stat_rows() -> list[str]:
    """Names (migrated itemstatcost keys) of the stats pages 1 and 2 show, in sheet order, without duplicates. The
    "% bonus" stats of column_groups() are the middle column of an attribute row, not a row of their own."""
    names = stat_names()
    folded = {names[b] for _t, b in column_groups() if b is not None}
    out: list[str] = []
    for r in sorted(read_rows(), key=lambda r: (r["page"], r["y"], r["x"])):
        if r["page"] in PAGES and r["type"] in VALUE_TYPES and r["stat"] not in NOT_LISTED:
            n = names[r["stat"]]
            if n not in out and n not in folded:
                out.append(n)
    return out


# The sheet's section headers (label rows of font 1, "Stacked Resists" / "Damage Absorb" font 4: gold) are drawn beside the
# boxes they head, not above them; a section owns the rows from its first y (classic pixels of the 512 x 704 sheet) down to
# the next section's. (page, header text as in Median's string table, first y.)
SECTIONS = ((1, "General", 0), (1, "Weapon Damage", 301), (1, "Spell Damage", 501), (1, "Elemental Pierce", 596),
            (2, "Speed", 0), (2, "Summon", 111), (2, "Defenses", 161), (2, "Stacked Resists", 255), (2, "Damage Absorb", 296),
            (2, "Sustain", 391), (2, "Miscellaneous", 531))


def section_order() -> list[tuple[str | None, int | None]]:
    """The Advanced Stats list in Median's order: [(header, None)] for a section header, [(None, Median stat row)] for a stat,
    both pages one after the other, rows by (y, x) inside a section, a stat once (its first box). Same stats as
    advanced_stat_rows()."""
    strings = load_strings()
    rows = read_rows()
    names = stat_names()
    folded = {names[b] for _t, b in column_groups() if b is not None}
    for page, text, _y in SECTIONS:  # every header must be a label of that page
        assert any(r["page"] == page and r["type"] == 1 and r["font"] in (1, 4) and strings.get(r["label"]) == text for r in rows), text
    out: list[tuple[str | None, int | None]] = []
    seen: set[str] = set()
    for page in PAGES:
        starts = [(y, text) for p, text, y in SECTIONS if p == page]
        sections: dict[str, list[dict]] = {text: [] for _y, text in starts}
        for r in sorted((r for r in rows if r["page"] == page and r["type"] in VALUE_TYPES and r["stat"] not in NOT_LISTED),
                        key=lambda r: (r["y"], r["x"])):
            sections[[text for y, text in starts if y <= r["y"]][-1]].append(r)
        for _y, text in starts:
            body = []
            for r in sections[text]:
                n = names[r["stat"]]
                if n not in seen and n not in folded:
                    seen.add(n)
                    body.append(r["stat"])
            if body:
                out.append((text, None))
                out += [(None, s) for s in body]
    assert sorted(names[s] for _h, s in out if s is not None) == sorted(advanced_stat_rows()), "section order lost a stat"
    return out


def order_header() -> str:
    """plugin/src/mxl_statorder.h: the Advanced Stats list in Median's order (D2R stat ids and section headers), for
    stat_columns.cpp."""
    ids = stat_ids()
    names = stat_names()
    entries = []
    for header, stat in section_order():
        if header is not None:
            entries.append(f'    {{-1, "{header}", false}},')
        else:
            always = "true" if names[stat] in ALWAYS_LISTED else "false"
            entries.append(f"    {{{ids[stat]}, nullptr, {always}}},  // {names[stat]}")
    lines = ["// Generated by tools/txt_migrate.py (advanced_stats, from statscreen.bin via tools/stat_screen.py); do not edit.",
             "#pragma once", "", "#include <cstdint>", "", "namespace mxl_statorder {", "",
             "// Median's character sheet pages 2 and 3 as one list: the sections in sheet order (stat -1: a section header with",
             "// its text, Median's string table), each followed by its stats (D2R stat ids) by line then column. `always`: listed",
             "// at 0 too (advdisplay 1); the others while their value is not 0 (advdisplay 2).",
             "struct Entry {", "	int32_t stat;", "	const char* header;", "	bool always;", "};",
             f"inline constexpr int32_t Count = {len(entries)};", "inline constexpr Entry Order[Count] = {", *entries, "};", "",
             "}  // namespace mxl_statorder", ""]
    return "\n".join(lines)


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
                head = [l for l in labels if l["font"] == 1 and abs(l["y"] - r["y"]) <= 40]
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

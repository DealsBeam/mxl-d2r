"""Migrate Median XL's named tables (1.13c columns) to D2R's table layout and merge them with D2R's rows.

Input:  work/named/<table>.txt + idmap.json (tools/mxl_names.py, then tools/formula.py convert)
        extract/d2r/<build>/data/global/excel/{,base/}<table>.txt (tools/casc_extract.py)
Output: work/mod/data/global/excel/base/<table>.txt   Classic/LoD bank (D2R base header)
        work/mod/data/global/excel/<table>.txt        RotW bank (D2R RotW header)
        work/mod/migrate_report.md, work/mod/overflow/<table>.txt (values D2R has no column for)

Both banks are written because which one a Median character loads is not confirmed yet (docs/id-mapping.md).

Row merge, by table kind:
  indexed   tables the engine reads by row number (every idmap entity marked index-addressed): output row i is
            the Median row whose new ID is i, else D2R's row i. D2R's own additions keep their IDs; Median's new
            rows follow them. Rows missing from the smaller bank are taken from the other bank.
  keyed     (default) Median's rows, then D2R rows whose key Median does not have.
  replace   Median's own sets (uniques, set items, affixes, runewords, cube recipes, ...): Median rows only.
  d2r_wins  D2R rows, then Median rows whose key D2R does not have (sounds: D2R's audio system differs;
            calc code lists: D2R's codes and their order define its formula compiler).

Columns: same name (case-insensitive) or a curated rename (RENAMES) copies Median's value. D2R-only columns take
D2R's value from the matching vanilla row (same ID / key), else stay empty. "*" comment columns that hold
IDs get the row's ID. Median values with no D2R column (runewords' slots 8-13, objects' Parm5-7, ...) are
written to overflow/<table>.txt; columns D2R dropped on purpose (OBSOLETE) are only counted in the report.

Usage: python tools/txt_migrate.py [--table skills ...]
"""
import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAMED = ROOT / "work" / "named"
D2R = ROOT / "extract" / "d2r" / "93847" / "data" / "global" / "excel"
OUT = ROOT / "work" / "mod"

# Median (1.13c) column -> D2R column, per table. Case-only differences are matched automatically.
RENAMES = {
    "*": {**{f"Hralti{s}": f"Hratli{s}" for s in ("Min", "Max", "MagicMin", "MagicMax", "MagicLvl")},
          **{f"Drehya{s}": f"Anya{s}" for s in ("Min", "Max", "MagicMin", "MagicMax", "MagicLvl")}},
    "monstats": {f"TreasureClass{n}{d}": f"TreasureClass{s}{d}"
                 for n, s in (("1", ""), ("2", "Champ"), ("3", "Unique"), ("4", "Quest"))
                 for d in ("", "(N)", "(H)")},
    "levels": {"MonLvl1": "MonLvl", "MonLvl2": "MonLvl(N)", "MonLvl3": "MonLvl(H)",
               "MonLvl1Ex": "MonLvlEx", "MonLvl2Ex": "MonLvlEx(N)", "MonLvl3Ex": "MonLvlEx(H)"},
    "difficultylevels": {"HireableBossDamagePercent": "MercenaryDamagePercentVSBoss"},
    "skills": {"delay": "localdelay"},  # D2R split the cooldown into localdelay / globaldelay
    "uniqueitems": {"enabled": "spawnable"},
    "itemtypes": {"MaxSock1": "MaxSockets1", "MaxSock25": "MaxSockets2", "MaxSock40": "MaxSockets3"},
    "soundenviron": {f"EAX {s}": f"SFX EAX {s}" for s in ("Environ", "Room Vol", "Room HF", "Decay Time", "Decay HF",
                                                        "Reflect", "Reflect Delay", "Reverb", "Rev Delay")},
}
# 1.13c NoShldBlock=1 "can block without a shield" == D2R ShieldBlockOverride=1 (2 = never blocks).
RENAMES["monstats"]["NoShldBlock"] = "ShieldBlockOverride"
# One Median column feeding several D2R columns.
SPLITS = {
    "hireling": {"Resist": ["ResistFire", "ResistCold", "ResistLightning", "ResistPoison"],
                 "Resist/Lvl": ["ResistFire/Lvl", "ResistCold/Lvl", "ResistLightning/Lvl", "ResistPoison/Lvl"]},
}
# Constant D2R columns implied by a Median layout (itemtypes' fixed 25/40 socket thresholds).
CONSTANTS = {"itemtypes": {"MaxSocketsLevelThreshold1": "25", "MaxSocketsLevelThreshold2": "40"}}

# Median replaced the vanilla item bases (its weapons use codes like "4@A", "115"; "hax", "axe" are gone).
# Merging D2R's item rows back would make removed items drop again through the auto weapN/armoN classes,
# so item and treasure tables are Median-only. Dangling references are for the validator to report.
REPLACE = {"uniqueitems", "setitems", "sets", "magicprefix", "magicsuffix", "automagic", "rareprefix", "raresuffix",
           "cubemain", "runes", "qualityitems", "lowqualityitems", "uniqueprefix", "uniquesuffix",
           "uniqueappellation", "gamble", "monpreset", "weapons", "armor", "misc", "treasureclassex"}
D2R_WINS = {"sounds", "soundenviron", "skillcalc", "misscalc", "playerclass", "plrtype", "storepage"}
# (Median key column, D2R key column) where they differ: the .bin keeps the short code in the first column.
KEY_PAIRS = {"playerclass": ("Player Class", "Code"), "storepage": ("Store Page", "Code")}
# Median columns D2R dropped on purpose; not copied to overflow/.
OBSOLETE = {"*": {"Beta", "Expansion", "eol", "EOL", "OpenBetaGfx", "Source Art", "Game Art"},
            "itemstatcost": {"ID"}, "states": {"id"}, "skills": {"Id"}, "missiles": {"Id"},
            "monstats": {"hcIdx"}, "objects": {"Id"}, "pettype": {"idx"}, "overlay": {"Frames"},
            "sounds": {"Index", "Volume", "Reverb", "Stereo", "Cache", "Async Only"},
            "soundenviron": {"EAX Env Size", "EAX Env Diff", "EAX Air Absorb"}, "gems": {"nummods"},
            "qualityitems": {"nummods"}, "runes": {"Rune Name"},
            "automap": {"Type1", "Type2", "Type3", "Type4"}}  # D2R keeps these only as comments
BINS = ROOT / "extract" / "mxl" / "data" / "global" / "excel"


def automap_level_names(mh: list[str], mrows: list[list[str]], d2r_rows: list[list[str]], dh: list[str]):
    """automap.bin stores LevelName as a level-type number (first int of each 44-byte record), which the first
    decoder lost. D2R's automap names are its lvltypes names reformatted: "Act 1 - Town" -> "1 Town" (33 of
    35 match; "5 Ice" is "Act 5 - Ice Caves"). Median's new level types have no such name yet.
    Returns (names per Median row, unmapped level-type numbers)."""
    import struct
    b = (BINS / "automap.bin").read_bytes()
    n = struct.unpack_from("<I", b)[0]
    size = (len(b) - 4) // n
    types = [struct.unpack_from("<i", b, 4 + r * size)[0] for r in range(n)]
    known = {r[dh.index("LevelName")] for r in d2r_rows}
    lh, lrows = read_tsv(D2R / "base" / "lvltypes.txt")
    mapping = {}
    for r in lrows:
        m = re.match(r"Act (\d) - (.*)", r[lh.index("Name")])
        if not m:
            continue
        name = f"{m.group(1)} {m.group(2)}"
        name = "5 Ice" if name == "5 Ice Caves" else name
        if name in known:
            mapping[int(r[lh.index("Id")])] = name
    unmapped = sorted({t for t in types if t not in mapping})
    return [mapping.get(t, f"mxl_lvltype_{t}") for t in types], unmapped
KEYS = {"weapons": "code", "armor": "code", "misc": "code", "itemtypes": "Code", "treasureclassex": "Treasure Class",
        "objects": "Name"}
ID_COMMENT_COLUMNS = {"*id", "*hcidx", "*index"}


def read_tsv(path: Path) -> tuple[list[str], list[list[str]]]:
    lines = path.read_text("utf-8-sig", errors="replace").split("\r\n" if "\r\n" in path.read_text("utf-8-sig", errors="replace")[:4096] else "\n")
    if lines and lines[-1] == "":
        lines.pop()
    rows = [l.split("\t") for l in lines]
    header = rows[0]
    body = []
    for r in rows[1:]:
        r += [""] * (len(header) - len(r))
        body.append(r[:len(header)])
    return header, body


def write_tsv(path: Path, header: list[str], rows: list[list[str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        f.writelines("\t".join(r) + "\r\n" for r in [header, *rows])


def is_record(r: list[str]) -> bool:
    return bool(r) and r[0] != "Expansion"


class Migrator:
    def __init__(self):
        self.idmap = json.loads((NAMED / "idmap.json").read_text("utf-8"))
        import mxl_names
        self.entities = mxl_names.ENTITIES
        self.report: list[str] = []
        self.dropped: dict[str, Counter] = defaultdict(Counter)
        self.overflow: dict[str, tuple[list[str], int]] = {}

    def kind(self, table: str) -> str:
        if table in REPLACE:
            return "replace"
        if table in D2R_WINS:
            return "d2r_wins"
        ent = self.entities.get(table)
        if ent and ent[4] and table in self.idmap:
            return "indexed"
        return "keyed"

    def column_map(self, table: str, mh: list[str], dh: list[str]) -> dict[str, str]:
        """D2R column -> Median column."""
        renames = {**RENAMES["*"], **RENAMES.get(table, {})}
        lower = {c.lower(): c for c in mh}
        out = {}
        for dc in dh:
            if dc in mh:
                out[dc] = dc
            elif dc.lower() in lower and not dc.startswith("*"):
                out[dc] = lower[dc.lower()]
        for mc, dc in renames.items():
            if mc in mh and dc in dh:
                out[dc] = mc
        for mc, dcs in SPLITS.get(table, {}).items():
            for dc in dcs:
                if mc in mh and dc in dh:
                    out[dc] = mc
        return out

    def migrate(self, table: str) -> None:
        mh, mrows = read_tsv(NAMED / f"{table}.txt")
        mrows = [[c.rstrip() for c in r] for r in mrows]  # .bin codes are space-padded ("gps ")
        if table == "automap":
            dh0, dr0 = read_tsv(D2R / "base" / "automap.txt")
            names, unmapped = automap_level_names(mh, mrows, [r for r in dr0 if is_record(r)], dh0)
            for r, nm in zip(mrows, names):
                r[mh.index("LevelName")] = nm
            self.report.append(f"\nautomap: level-type numbers {unmapped} have no D2R automap name (Median's new "
                               f"level types); their rows use mxl_lvltype_<n> and need engine support.\n")
        if table == "missiles" and "LevRange" in mh and "Range" in mh:
            # D2R merged LevRange into Range (now an expression): range + levrange * level
            ri, li = mh.index("Range"), mh.index("LevRange")
            for r in mrows:
                if r[li] not in ("", "0"):
                    r[ri] = f"{r[ri] or 0}+{r[li]}*lvl"
            mh = [("*LevRange" if c == "LevRange" else c) for c in mh]
        banks = {"base": D2R / "base" / f"{table}.txt", "rotw": D2R / f"{table}.txt"}
        banks = {k: v for k, v in banks.items() if v.exists()}
        if not banks:
            self.report.append(f"| {table} | no D2R table | {len(mrows)} | | | skipped (needs a custom table or plugin) |")
            return
        d2r = {k: read_tsv(v) for k, v in banks.items()}
        kind = self.kind(table)
        for bank, (dh, drows_all) in d2r.items():
            drows = [r for r in drows_all if is_record(r)]
            cmap = self.column_map(table, mh, dh)
            mi = {c: i for i, c in enumerate(mh)}
            di = {c: i for i, c in enumerate(dh)}
            consts = CONSTANTS.get(table, {})

            def convert(mrow: list[str], match: list[str] | None, row_id: int | None) -> list[str]:
                out = []
                for dc in dh:
                    if dc in cmap:
                        out.append(mrow[mi[cmap[dc]]])
                    elif dc in consts:
                        out.append(consts[dc])
                    elif dc.lower() in ID_COMMENT_COLUMNS and row_id is not None:
                        out.append(str(row_id))
                    elif match is not None:
                        out.append(match[di[dc]])
                    else:
                        out.append("")
                return out

            if kind == "indexed":
                # D2R rows by ID; the smaller bank borrows missing IDs from the other bank (same IDs in both).
                other = [r for b, (h, rr) in d2r.items() if b != bank for r in [None]]
                d2r_by_id = list(drows)
                for b, (oh, orows) in d2r.items():
                    if b == bank:
                        continue
                    orows = [r for r in orows if is_record(r)]
                    for r in orows[len(d2r_by_id):]:
                        d2r_by_id.append([r[oh.index(c)] if c in oh else "" for c in dh])
                entries = self.idmap[table]
                by_id = {}
                key = self.entities[table][2]
                grouped = table in ("monseq",)
                if grouped:  # several Median rows per entry: keep them together, in Median order
                    out_rows = []
                    for r in mrows:
                        out_rows.append(convert(r, None, None))
                    rows = out_rows
                else:
                    for e, r in zip(entries, mrows):
                        by_id[e["id"]] = (e, r)
                    rows = []
                    for i in range(max(len(d2r_by_id), max(by_id) + 1 if by_id else 0)):
                        if i in by_id:
                            e, r = by_id[i]
                            match = d2r_by_id[i] if e["source"].startswith("vanilla") and i < len(d2r_by_id) else None
                            rows.append(convert(r, match, i))
                        elif i < len(d2r_by_id):
                            rows.append(d2r_by_id[i])
                        else:
                            rows.append(convert([""] * len(mh), None, i))  # gap (should not happen)
                            self.report.append(f"| {table} | {bank} | gap at ID {i} | | | |")
            else:
                kcol = KEYS.get(table) or (self.entities[table][2] if table in self.entities else mh[0])
                dkey = kcol if kcol in di else dh[0]
                mkey = kcol if kcol in mi else mh[0]
                if table in KEY_PAIRS and KEY_PAIRS[table][1] in di:
                    mkey, dkey = KEY_PAIRS[table]
                d_by_key = {}
                for r in drows:
                    d_by_key.setdefault(r[di[dkey]].lower(), r)
                m_keys = {r[mi[mkey]].lower() for r in mrows}
                if kind == "d2r_wins":
                    rows = list(drows) + [convert(r, None, None) for r in mrows if r[mi[mkey]].lower() not in d_by_key]
                else:
                    rows = [convert(r, d_by_key.get(r[mi[mkey]].lower()), None) for r in mrows]
                    if kind == "keyed":
                        rows += [r for r in drows if r[di[dkey]].lower() not in m_keys]

            write_tsv(OUT / "data" / "global" / "excel" / ("base" if bank == "base" else "") / f"{table}.txt", dh, rows)
            if bank == next(iter(d2r)):
                used = set(cmap.values())
                obsolete = OBSOLETE["*"] | OBSOLETE.get(table, set())
                keep = []
                for c in mh:
                    if c in used or c.startswith("*") or not c:
                        continue
                    n = sum(1 for r in mrows if r[mi[c]] not in ("", "0"))
                    if n:
                        self.dropped[table][c] = n
                        if c not in obsolete:
                            keep.append(c)
                if keep:  # values D2R has no column for: kept for plugins / later decisions
                    rows_o = [[r[0]] + [r[mi[c]] for c in keep] for r in mrows
                              if any(r[mi[c]] not in ("", "0") for c in keep)]
                    write_tsv(OUT / "overflow" / f"{table}.txt", [mh[0]] + keep, rows_o)
                    self.overflow[table] = (keep, len(rows_o))
                defaulted = [c for c in dh if c not in cmap and not c.startswith("*")]
                self.report.append(f"| {table} | {kind} | {len(mrows)} | {len(drows)} | {len(rows)} | "
                                   f"{len(cmap)} mapped, {len(defaulted)} D2R-only |")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--table", action="append")
    args = ap.parse_args()
    m = Migrator()
    tables = args.table or sorted(p.stem for p in NAMED.glob("*.txt") if p.stem != "report")
    m.report += ["# txt-migrate report", "", "| Table | Kind | Median rows | D2R rows | Output rows | Columns |",
                 "|---|---|---|---|---|---|"]
    for t in tables:
        m.migrate(t)
    m.report += ["", "## Overflow: Median values with no D2R column (kept in work/mod/overflow/<table>.txt)", ""]
    for t, (cols, n) in sorted(m.overflow.items()):
        m.report.append(f"- **{t}** ({n} rows): " + ", ".join(f"{c} ({m.dropped[t][c]})" for c in cols))
    m.report += ["", "## Dropped as obsolete in D2R (non-empty counts)", ""]
    for t, cols in sorted(m.dropped.items()):
        obs = [c for c in cols if c not in m.overflow.get(t, ([], 0))[0]]
        if obs:
            m.report.append(f"- {t}: " + ", ".join(f"{c} ({cols[c]})" for c in obs))
    (OUT / "migrate_report.md").write_text("\n".join(m.report) + "\n", encoding="utf-8")
    print("\n".join(m.report))
    return 0


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    sys.exit(main())

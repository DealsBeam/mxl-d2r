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
import struct
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
           "uniqueappellation", "gamble", "monpreset", "weapons", "armor", "misc", "treasureclassex", "hireling"}
D2R_WINS = {"sounds", "soundenviron", "skillcalc", "misscalc", "playerclass", "plrtype", "storepage"}
# D2R's own rows only, for now: D2R's inventory has 2 rows per page (one per screen resolution) and the engine
# asserts on anything else. Median's inventory sizes need converting to that layout (later).
D2R_ONLY = {"inventory"}
MAX_ITEMS_PER_SET = 6  # D2R and D2RLoader
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


def decode_automap(d2r_rows: list[list[str]], dh: list[str]):
    """Median's automap straight from automap.bin. The first decoder's text does not line up with the records
    (5,118 lines incl. 32 blank ones for 5,086 records), so it is not used. Record (44 bytes): i32 level type
    (+12 pad), char TileName[8] @16, i8 Style @24, i8 StartSequence @25, i8 EndSequence @26, pad, i32 Cel1-4 @28.
    The layout reproduces D2R's own first row ("1 Town", fl, 0, 1, 46, cels 0-3).
    LevelName: D2R's automap names are its lvltypes names reformatted: "Act 1 - Town" -> "1 Town" ("5 Ice" is
    "Act 5 - Ice Caves"). D2R's schema says level types are fixed and cannot be added, so Median's new ones
    keep mxl_lvltype_<n> and are reported.
    Returns (header, rows, unmapped level-type numbers)."""
    import struct
    b = (BINS / "automap.bin").read_bytes()
    n = struct.unpack_from("<I", b)[0]
    size = (len(b) - 4) // n
    types, rows = [], []
    for r in range(n):
        o = 4 + r * size
        t = struct.unpack_from("<i", b, o)[0]
        tile = b[o + 16:o + 24].split(b"\0", 1)[0].decode("latin-1")
        style, start, end = struct.unpack_from("<bbb", b, o + 24)
        cels = struct.unpack_from("<4i", b, o + 28)
        types.append(t)
        rows.append([str(t), tile, str(style), str(start), str(end), *map(str, cels)])
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
    for t, row in zip(types, rows):
        row[0] = mapping.get(t, f"mxl_lvltype_{t}")
    header = ["LevelName", "TileName", "Style", "StartSequence", "EndSequence", "Cel1", "Cel2", "Cel3", "Cel4"]
    return header, rows, unmapped


def hiredesc_keys() -> dict[str, str]:
    """hiredesc code (rs1, dm1, ...) -> string key from the Sigma table hiredescext (one string per hiredesc row).
    D2R's hireling HireDesc column is a string key (e.g. skillname103), not a hiredesc code."""
    codes = [l.split("\t", 1)[0] for l in (NAMED / "hiredesc.txt").read_text("utf-8").splitlines()[1:]]
    ext = ROOT / "work" / "sigma" / "hiredescext.txt"
    if not ext.exists():
        return {}
    lines = ext.read_text("utf-8").splitlines()
    col = lines[0].split("\t").index("NameStr")
    keys = [(l.split("\t") + [""] * (col + 1))[col] for l in lines[1:]]
    return {c.strip(): k for c, k in zip(codes, keys) if c.strip() and k}
KEYS = {"weapons": "code", "armor": "code", "misc": "code", "itemtypes": "Code", "treasureclassex": "Treasure Class",
        "objects": "Name"}
ID_COMMENT_COLUMNS = {"*id", "*hcidx", "*index"}


def read_codes_bin(name: str) -> list[str]:
    """Calc code names in a Median *calc.bin (4-character records)."""
    b = (BINS / f"{name}.bin").read_bytes()
    n = struct.unpack_from("<I", b)[0]
    size = (len(b) - 4) // n
    return [b[4 + i * size: 4 + i * size + 4].rstrip(b"\0").decode("latin1").strip() for i in range(n)]


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
        self.dangling: dict[str, int] = defaultdict(int)

    def kind(self, table: str) -> str:
        if table in REPLACE:
            return "replace"
        if table in D2R_WINS:
            return "d2r_wins"
        if table in D2R_ONLY:
            return "d2r_only"
        ent = self.entities.get(table)
        if ent and ent[4] and table in self.idmap:
            return "indexed"
        return "keyed"

    _schema = None

    def clean_treasure_classes(self) -> None:
        """Treasure class entries must name an item code, item type, treasure class, unique or set item (D2R's
        format). Case-only mismatches are fixed; anything else (Median's sacred auto classes "sarm93"/"swea93",
        which D2Sigma generates, or names that exist nowhere) is cleared with its Prob and reported, because D2R
        asserts on it at load."""
        for bank in ("base", ""):
            d = OUT / "data" / "global" / "excel" / bank
            p = d / "treasureclassex.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            known: dict[str, str] = {}
            for t, c in (("weapons", "code"), ("armor", "code"), ("misc", "code"), ("itemtypes", "Code"),
                         ("treasureclassex", "Treasure Class"), ("uniqueitems", "index"), ("setitems", "index")):
                if (d / f"{t}.txt").exists():
                    th, trows = read_tsv(d / f"{t}.txt")
                    if c in th:
                        known.update({r[th.index(c)].lower(): r[th.index(c)] for r in trows if r[th.index(c)]})
            auto = re.compile(r"^(weap|armo|mele|bow|mag[a-z]*|rar[a-z]*)\d+$")
            cleared = []
            for ci, c in enumerate(h):
                m = re.fullmatch(r"Item(\d+)", c)
                if not m:
                    continue
                pi = h.index(f"Prob{m.group(1)}") if f"Prob{m.group(1)}" in h else None
                for r in rows:
                    v = r[ci]
                    quoted = len(v) > 1 and v[0] == v[-1] == '"'
                    body = v[1:-1] if quoted else v
                    name, sep, rest = body.partition(",")
                    if not name or auto.match(name) or name in known.values():
                        continue
                    if name.lower() in known:  # case-only difference
                        body = known[name.lower()] + sep + rest
                        r[ci] = f'"{body}"' if quoted else body
                        continue
                    cleared.append(f"{r[0]}: {name}")
                    r[ci] = ""
                    if pi is not None:
                        r[pi] = ""
            # D2R resolves a treasure class's items against the classes above it only, so a class that names one
            # defined further down asserts ("Couldn't parse treasure class item"). Move each such class up to just
            # before its first user (D2R references classes by name, so the row shift is harmless).
            items = [i for i, c in enumerate(h) if re.fullmatch(r"Item\d+", c)]
            moved = []
            for _ in range(len(rows)):
                pos = {r[0]: i for i, r in enumerate(rows) if r[0] and r[0] != "Expansion"}
                fwd = next(((i, pos[r[c]]) for i, r in enumerate(rows) for c in items
                            if r[c] in pos and pos[r[c]] > i), None)
                if not fwd:
                    break
                user, target = fwd
                moved.append(f"{rows[target][0]} (before {rows[user][0]})")
                rows.insert(user, rows.pop(target))
            write_tsv(p, h, rows)
            if bank == "base" and moved:
                self.report.append(f"\ntreasureclassex: moved {len(moved)} classes above their first user "
                                   f"(D2R only resolves backward references): {'; '.join(moved)}")
            if bank == "base" and cleared:
                self.report.append(f"\ntreasureclassex: cleared {len(cleared)} entries D2R cannot resolve "
                                   f"(D2Sigma-generated classes or missing names): {'; '.join(cleared)}")

    def claim_presets(self) -> None:
        """A level with DrlgType 2 (preset) needs a lvlprest row whose LevelId names it. 1.10 fell back to preset 0
        ("None") when none did, and Median relies on that for levels it no longer uses; D2R asserts and crashes
        building the act. Each such level gets a copy of the "None" row that claims it."""
        for bank in ("base", ""):
            d = OUT / "data" / "global" / "excel" / bank
            if not (d / "levels.txt").exists() or not (d / "lvlprest.txt").exists():
                continue
            lh, lrows = read_tsv(d / "levels.txt")
            ph, prows = read_tsv(d / "lvlprest.txt")
            li, di = ph.index("LevelId"), ph.index("Def")
            claimed = {r[li] for r in prows if is_record(r)}
            idc, dt = lh.index("Id"), lh.index("DrlgType")
            unclaimed = [r[idc] for r in lrows if is_record(r) and r[dt] == "2" and r[idc] not in claimed]
            none = next(r for r in prows if is_record(r))
            n = sum(1 for r in prows if is_record(r))
            for k, lid in enumerate(unclaimed):
                row = list(none)
                row[0], row[di], row[li] = f"mxl_unclaimed_{lid}", str(n + k), lid
                prows.append(row)
            write_tsv(d / "lvlprest.txt", ph, prows)
            if bank == "base" and unclaimed:
                self.report.append(f"\nlvlprest: {len(unclaimed)} preset levels had no preset (1.10 used preset 0); "
                                   f"added 'None' copies claiming levels {', '.join(unclaimed)}")

    def claim_mazes(self) -> None:
        """A level with DrlgType 1 (maze) needs a lvlmaze row. Median leaves a few without one (D2Sigma or unused
        levels); D2R sizes every level of an act when the act loads and asserts, then crashes, on them. Each gets
        a copy of the lvlmaze row of a level with the same LevelType (else the first row)."""
        for bank in ("base", ""):
            d = OUT / "data" / "global" / "excel" / bank
            if not (d / "levels.txt").exists() or not (d / "lvlmaze.txt").exists():
                continue
            lh, lrows = read_tsv(d / "levels.txt")
            mh, mrows = read_tsv(d / "lvlmaze.txt")
            mi = mh.index("Level")
            have = {r[mi] for r in mrows if is_record(r)}
            idc, dt, ti = lh.index("Id"), lh.index("DrlgType"), lh.index("LevelType")
            ltype = {r[idc]: r[ti] for r in lrows if is_record(r)}
            missing = [r[idc] for r in lrows if is_record(r) and r[dt] == "1" and r[idc] not in have]
            records = [r for r in mrows if is_record(r)]
            for lid in missing:
                src = next((r for r in records if ltype.get(r[mi]) == ltype[lid]), records[0])
                row = list(src)
                row[0], row[mi] = f"mxl_unclaimed_{lid}", lid
                mrows.append(row)
            write_tsv(d / "lvlmaze.txt", mh, mrows)
            if bank == "base" and missing:
                self.report.append(f"\nlvlmaze: {len(missing)} maze levels had no lvlmaze row; added copies of a "
                                   f"same-LevelType row for levels {', '.join(missing)}")

    def extra_calc_codes(self) -> None:
        """D2Sigma's skill calc codes 73-110 (syn1-syn6, aura, area, bl12, ...) are not D2R codes, and D2R's own
        rows 73+ differ, so formulas naming them would not compile. Each bank's skillcalc.txt is rebuilt as D2R's
        rows for that bank, placeholder rows up to D2R's full count (127; the base bank has fewer rows), then
        Median's codes that D2R lacks, in Median's order: every Median code gets the same row in both banks. D2R's
        code switch (0x33b3b0) answers unknown rows with 0; the mxl-core plugin answers the ones it implements.
        The rows and the migrated ids of the stats those codes read go to plugin/src/mxl_ids.h."""
        mxl = read_codes_bin("skillcalc")
        full = len(read_tsv(D2R / "skillcalc.txt")[1])
        added: list[str] = []
        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "skillcalc.txt"
            if not p.exists():
                continue
            h, d2r_rows = read_tsv(D2R / bank / "skillcalc.txt")
            rows = [r for r in d2r_rows if r and r[0]]
            have = {r[0].strip() for r in rows}
            pad = 0
            while len(rows) < full:
                pad += 1
                row = [""] * len(h)
                row[0] = f"mxpad{pad}"
                rows.append(row)
            added = []
            for code in mxl:
                if code and code not in have and code not in added:
                    added.append(code)
            for code in added:
                row = [""] * len(h)
                row[0] = code
                row[1] = "Median XL (D2Sigma) calc code; value from the mxl-core plugin, else 0"
                rows.append(row)
            write_tsv(p, h, rows)
        self.write_plugin_ids(full, added)
        self.report.append(f"\nskillcalc: {len(added)} Median calc codes as rows {full}..{full + len(added) - 1} in "
                           f"both banks ({', '.join(added)})")

    def write_plugin_ids(self, first: int, codes: list[str]) -> None:
        """plugin/src/mxl_ids.h: calc code rows and migrated stat ids the plugin needs (regenerated each build)."""
        h, rows = read_tsv(OUT / "data" / "global" / "excel" / "base" / "itemstatcost.txt")
        idc = h.index("*ID")
        named = [r[0] for r in read_tsv(ROOT / "work" / "named" / "itemstatcost.txt")[1]]
        stat_id = {r[0]: int(r[idc]) for r in rows if r and r[idc].lstrip("-").isdigit()}

        def stat(median_id: int) -> int:
            return stat_id[named[median_id]]

        lines = ["// Generated by tools/txt_migrate.py (extra_calc_codes); do not edit.",
                 "#pragma once", "", "#include <cstdint>", "", "namespace mxl_ids {", "",
                 "// skillcalc.txt rows of D2Sigma's calc codes (D2R's own codes end before FirstCalcCode).",
                 f"inline constexpr int32_t FirstCalcCode = {first};"]
        for i, code in enumerate(codes):
            lines.append(f"inline constexpr int32_t Calc_{code} = {first + i};")
        lines += ["", "// Stats those codes read (Median itemstatcost row -> migrated id).",
                  f"inline constexpr int32_t StatSyn1 = {stat(383)};  // Median 383",
                  f"inline constexpr int32_t StatSyn2 = {stat(393)};  // Median 393",
                  f"inline constexpr int32_t StatSyn3 = {stat(396)};  // Median 396",
                  f"inline constexpr int32_t StatSyn4 = {stat(397)};  // Median 397",
                  f"inline constexpr int32_t StatSyn5 = {stat(399)};  // Median 399",
                  f"inline constexpr int32_t StatSyn6 = {stat(400)};  // Median 400",
                  f"inline constexpr int32_t StatSkillCapBonus = {stat(449)};  // Median 449, skcp",
                  f"inline constexpr int32_t StatCooldownReduction = {stat(309)};  // Median 309, skcd",
                  "", "}  // namespace mxl_ids", ""]
        (ROOT / "plugin" / "src" / "mxl_ids.h").write_text("\n".join(lines), encoding="utf-8")

    def item_spells(self) -> None:
        """misc.txt pSpell (what using an item does). D2R maps pSpell 0-19 to its handlers (0x236fe30) and ignores
        20+; D2Sigma added its own. Median's healing/mana/rejuvenation potions use D2Sigma's 22, a timed potion
        (state for len frames adding stat1-stat3), which is exactly D2R's 3. Median's other values (12-21: quest
        items, cube items, mystic orbs) still need the plugin (docs/bugs.md)."""
        to_d2r = {"22": "3"}
        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "misc.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            ps = h.index("pSpell")
            n = 0
            for r in rows:
                if is_record(r) and r[ps] in to_d2r:
                    r[ps] = to_d2r[r[ps]]
                    n += 1
            write_tsv(p, h, rows)
            if bank == "base":
                self.report.append(f"\nmisc pSpell: {n} items with D2Sigma's timed potion (22) -> D2R's 3")

    def vendor_stock(self) -> None:
        """Shops. Median leaves every item's vendor columns empty; D2Sigma stocks vendors from npcstores.bin, 52-byte
        rows (fields read from the data, docs/vendors.md):
          +0 u16 NPC (monstats row)   +4 char[4] item code (space padded)   +8 u8 always in stock (consumables)
          +16 u8 Min  +17 u8 Max  +19 u8 MagicLvl  +24 u8 MagicMin  +25 u8 MagicMax   (equipment)
          +40/+41 u8 flags used only by Atma, Geglash and Cain on sacred items (not a shop; not converted)
        These become D2R's per-item columns <Vendor>Min/Max/MagicMin/MagicMax/MagicLvl; always-in-stock rows
        get Min = Max = 1 like D2R's potions (PermStoreItem stays as Median set it)."""
        vendor = {"charsi": "Charsi", "gheed": "Gheed", "akara": "Akara", "fara": "Fara", "lysander": "Lysander",
                  "drognan": "Drognan", "hratli": "Hratli", "alkor": "Alkor", "ormus": "Ormus", "elzix": "Elzix",
                  "asheara": "Asheara", "cain2": "Cain", "halbu": "Halbu", "jamella": "Jamella", "larzuk": "Larzuk",
                  "drehya": "Anya", "malah": "Malah"}
        data = (ROOT / "extract" / "mxl" / "data" / "global" / "excel" / "npcstores.bin").read_bytes()
        count = struct.unpack_from("<I", data)[0]
        mh, mrows = read_tsv(NAMED / "monstats.txt")
        stock: dict[str, dict[str, list[str]]] = defaultdict(dict)  # item code -> column -> value
        skipped, per_npc = Counter(), Counter()
        for i in range(count):
            row = data[4 + 52 * i:4 + 52 * (i + 1)]
            npc = mrows[struct.unpack_from("<H", row)[0]][0]
            code = row[4:8].decode("latin-1").strip()
            prefix = vendor.get(npc)
            if row[8]:
                values = (1, 1, 0, 0, 0)
            else:
                values = (row[16], row[17], row[24], row[25], row[19])
            if prefix is None or not any(values):
                skipped[npc] += 1
                continue
            for col, v in zip(("Min", "Max", "MagicMin", "MagicMax", "MagicLvl"), values):
                stock[code][prefix + col] = str(v)
            per_npc[prefix] += 1
        for bank in ("base", ""):
            for t in ("weapons", "armor", "misc"):
                p = OUT / "data" / "global" / "excel" / bank / f"{t}.txt"
                if not p.exists():
                    continue
                h, rows = read_tsv(p)
                ci = h.index("code")
                for r in rows:
                    for col, v in stock.get(r[ci], {}).items():
                        if col in h:
                            r[h.index(col)] = v
                write_tsv(p, h, rows)
        self.report.append(f"\nvendors: {sum(per_npc.values())} npcstores rows -> item vendor columns "
                           f"({', '.join(f'{k} {v}' for k, v in sorted(per_npc.items()))}); not shops, skipped: "
                           f"{', '.join(f'{k} {v}' for k, v in sorted(skipped.items()))}")

    def skill_rules(self) -> None:
        """Skill rules D2Sigma enforced in code, rebuilt in data where D2R's formulas can express them:
        - skpoints "(blvl+1>mlvl+skcp+skcn)?999:1" uses three D2Sigma calc codes (docs/plugin-spec.md, skill caps).
          mlvl (a per-skill number) and skcn (a per-skill formula: Specialization bonuses, skills that lock each
          other) come from skills2 (work/formulas/skill_caps.txt, tools/formula.py) and are written in; skcp (items'
          "+N to the maximum level of this skill") stays as a skillcalc row the mxl-core plugin answers.
        - rightskill: D2Sigma's own skill picker ignored it, D2R's honours it; active class skills get 1.
        - srvdofunc 153-171 are D2Sigma's added skill functions, but D2R's own table (0x238ea00, 191 slots, bound
          0xbe) already uses 153-171 for its newer skills. They move to the free slots 172-190 (+19), where the
          mxl-core plugin installs D2Sigma's functions (docs/plugin-spec.md); until it does, those skills do
          nothing instead of running an unrelated D2R function. D2R's own rows (Warlock, uber skills at IDs 357-428,
          same skill and srvdofunc as D2R's table) keep their numbers.
        - town_portal (D2Sigma 155, the town portal without a scroll or book) -> D2R's own itemless town portal,
          srvdofunc 109 ("Townportal O Skill", 0x5822b0: creates object 0x3b and links it to town), with
          rightskill 1 (it has no charclass, so the rule above misses it).
        - D2Sigma 160 (thundergod, armageddon_2, frigid_domain, overheat) is its Armageddon: a timed aurastate with
          the skill's aura stats and a periodic event, the same as D2R's 124 (0x575de0); the rows use vanilla
          Armageddon's columns."""
        caps = {"mlvl", "skcp", "skcn"}
        named = [row[0] for row in read_tsv(ROOT / "work" / "named" / "skills.txt")[1]]
        skill_caps = {}
        for line in (ROOT / "work" / "formulas" / "skill_caps.txt").read_text("utf-8").splitlines()[1:]:
            row, mlvl, skcn = (line.split("\t") + ["", ""])[:3]
            if row.isdigit() and int(row) < len(named):
                skill_caps[named[int(row)]] = (mlvl, skcn)
        sigma_first, sigma_last, shift = 153, 171, 19
        to_d2r = {155: 109, 160: 124}
        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "skills.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            dh, drows = read_tsv(D2R / bank / "skills.txt")
            d2r_own = {(r[0], r[dh.index("srvdofunc")]) for r in drows}
            sp, ml, cc = h.index("skpoints"), h.index("maxlvl"), h.index("charclass")
            c9, c10 = h.index("calc9"), h.index("calc10")
            rs, pa, do = h.index("rightskill"), h.index("passive"), h.index("srvdofunc")
            n_cap = n_right = n_moved = 0
            for r in rows:
                if not is_record(r):
                    continue
                if caps & set(re.findall(r"[a-z]+", r[sp])):
                    mlvl, skcn = skill_caps.get(r[0], ("0", ""))
                    text = re.sub(r"\bmlvl\b", mlvl, r[sp])
                    # skcp stays: a skillcalc row (extra_calc_codes) the mxl-core plugin answers, else 0
                    text = re.sub(r"\bskcn\b", f"({skcn})" if skcn else "0", text)
                    r[sp] = text
                    # calc9/calc10 (D2R-only columns 1.13c lacks) carry mlvl and skcn for the plugin's
                    # bmax/maxe codes
                    if not r[c9] and not r[c10]:
                        r[c9], r[c10] = mlvl, skcn or "0"
                    n_cap += 1
                if r[cc] and r[pa] != "1" and r[rs] in ("", "0"):
                    r[rs] = "1"
                    n_right += 1
                if r[do].isdigit() and int(r[do]) in to_d2r:
                    r[do] = str(to_d2r[int(r[do])])
                    if r[pa] != "1":
                        r[rs] = "1"
                elif r[do].isdigit() and sigma_first <= int(r[do]) <= sigma_last and (r[0], r[do]) not in d2r_own:
                    r[do] = str(int(r[do]) + shift)
                    n_moved += 1
            write_tsv(p, h, rows)
            if bank == "base":
                self.report.append(f"\nskills (D2Sigma rules): {n_cap} skpoints formulas with Median's skill caps "
                                   f"written in (mlvl, skcn; skcp from the plugin);{n_right} active class skills -> rightskill 1; "
                                   f"{n_moved} skills with D2Sigma srvdofunc {sigma_first}-{sigma_last} -> +{shift}")

    def fill_skilldesc(self) -> None:
        """D2R asserts when a class skill has no skilldesc (Median's hidden class skills, granted at character
        creation, have none). They point at the "attack" description, which has no skill tree page."""
        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "skills.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            cc, sd = h.index("charclass"), h.index("skilldesc")
            filled = [r[0] for r in rows if is_record(r) and r[cc] and not r[sd]]
            for r in rows:
                if is_record(r) and r[cc] and not r[sd]:
                    r[sd] = "attack"
            write_tsv(p, h, rows)
            if bank == "base" and filled:
                self.report.append(f"\nskills: {len(filled)} class skills had no skilldesc; set to 'attack'")

    def split_large_sets(self) -> None:
        """D2R allows 6 items per set; Median's "Orphan's Call" has 9. Items 7+ go to a copy of the set named
        "<set> (2)" so the tables load; the full set needs a plugin (reported)."""
        for bank in ("base", ""):
            d = OUT / "data" / "global" / "excel" / bank
            if not (d / "setitems.txt").exists():
                continue
            ih, irows = read_tsv(d / "setitems.txt")
            sh, srows = read_tsv(d / "sets.txt")
            si, key = ih.index("set"), sh.index("index")
            members: dict[str, list[list[str]]] = defaultdict(list)
            for r in irows:
                members[r[si]].append(r)
            for name, items in members.items():
                if len(items) <= MAX_ITEMS_PER_SET:
                    continue
                for n, start in enumerate(range(MAX_ITEMS_PER_SET, len(items), MAX_ITEMS_PER_SET), 2):
                    new = f"{name} ({n})"
                    for r in items[start:start + MAX_ITEMS_PER_SET]:
                        r[si] = new
                    base_row = next(r for r in srows if r[key] == name)
                    clone = list(base_row)
                    clone[key] = new
                    srows.append(clone)
                if bank == "base":
                    self.report.append(f"\nsets: '{name}' has {len(items)} items; D2R allows {MAX_ITEMS_PER_SET}. "
                                       f"Items 7+ moved to '{name} (2)' (needs a plugin for the real set bonus).")
            write_tsv(d / "setitems.txt", ih, irows)
            write_tsv(d / "sets.txt", sh, srows)

    def refs_into_replaced(self, table: str, dh: list[str]) -> dict[str, tuple[str, str]]:
        """D2R columns that are references (per D2R's data guide) into a table Median replaces wholesale:
        column -> (target table, target column)."""
        if Migrator._schema is None:
            import validate
            Migrator._schema = validate.Schema()
        out = {}
        for c in dh:
            f = Migrator._schema.field(table, c)
            typ = (f or {}).get("type", {})
            if isinstance(typ, dict) and typ.get("type") == "reference" and str(typ.get("file", "")).lower() in REPLACE:
                out[c] = (typ["file"].lower(), typ.get("field", ""))
        return out

    _median_values: dict[tuple[str, str], set[str]] = {}

    def drop_dangling(self, row: list[str], dh: list[str], dead_refs: dict, table: str, row_id: int) -> list[str]:
        """A row kept from D2R (e.g. RotW's Colossal monsters) may point into a table Median replaced; such
        references are emptied when Median's version has no such entry."""
        row = list(row)
        for c, (tfile, tcol) in dead_refs.items():
            v = row[dh.index(c)]
            if not v:
                continue
            key = (tfile, tcol)
            if key not in self._median_values:
                h, rows = read_tsv(NAMED / f"{tfile}.txt") if (NAMED / f"{tfile}.txt").exists() else ([], [])
                i = next((k for k, x in enumerate(h) if x.lower() == tcol.lower()), None)
                self._median_values[key] = {r[i].rstrip() for r in rows} if i is not None else set()
            if v not in self._median_values[key]:
                row[dh.index(c)] = ""
                self.dangling[table] += 1
        return row

    def typed_columns(self, table: str, dh: list[str]) -> tuple[set[str], set[str]]:
        """(string-key columns, boolean columns) per D2R's data guide."""
        self.refs_into_replaced(table, [])  # loads the schema
        strings, bools = set(), set()
        for c in dh:
            f = Migrator._schema.field(table, c)
            typ = (f or {}).get("type", {})
            kind = typ.get("type") if isinstance(typ, dict) else typ
            if kind == "string":
                strings.add(c)
            elif kind == "boolean":
                bools.add(c)
            elif kind == "int":
                self._int_cols.setdefault(table, set()).add(c)
        return strings, bools

    _int_cols: dict[str, set[str]] = {}

    @property
    def idkeys(self) -> dict[str, str]:
        if not hasattr(self, "_idkeys"):
            p = OUT / "strings_idkeys.json"
            self._idkeys = {k: v for k, v in json.loads(p.read_text("utf-8")).items() if v.startswith("mxl_")} if p.exists() else {}
        return self._idkeys

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
            mh, mrows, unmapped = decode_automap([r for r in dr0 if is_record(r)], dh0)
            self.report.append(f"\nautomap: decoded from automap.bin ({len(mrows)} records). Level-type numbers "
                               f"{unmapped} are Median's new level types; D2R's level types are fixed, so their "
                               f"rows use mxl_lvltype_<n> and need engine support.\n")
        if table == "hireling" and "HireDesc" in mh:
            hk = hiredesc_keys()
            hi = mh.index("HireDesc")
            for r in mrows:
                r[hi] = hk.get(r[hi], r[hi])
            # hireling.bin has no type names; take D2R's for the same act (the engine groups hirelings by them)
            dh0, dr0 = read_tsv(D2R / "base" / "hireling.txt")
            names = {r[dh0.index("Act")]: r[0] for r in dr0 if is_record(r)}
            ni, ai = mh.index("Hireling"), mh.index("Act")
            for r in mrows:
                r[ni] = r[ni] or names.get(r[ai], "")
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

            dead_refs = self.refs_into_replaced(table, dh)
            string_cols, bool_cols = self.typed_columns(table, dh)

            def median_value(dc: str, v: str) -> str:
                # string keys Median only had as numbers are mxl_<id> after tools/tbl2json.py
                if dc in string_cols and v.isdigit() and v in self.idkeys:
                    return self.idkeys[v]
                # the first decoder wrote a few small numbers as raw control characters ("")
                if v and dc in self._int_cols.get(table, ()) and len(v) == 1 and ord(v) < 32:
                    return str(ord(v))
                # D2R booleans are 0/1; 1.13c sometimes stored other non-zero flags
                if dc in bool_cols and re.fullmatch(r"-?\d+", v) and v not in ("0", "1"):
                    return "1"
                return v

            def convert(mrow: list[str], match: list[str] | None, row_id: int | None) -> list[str]:
                out = []
                for dc in dh:
                    if dc in cmap:
                        out.append(median_value(dc, mrow[mi[cmap[dc]]]))
                    elif dc in consts:
                        out.append(consts[dc])
                    elif dc.lower() in ID_COMMENT_COLUMNS and row_id is not None:
                        out.append(str(row_id))
                    elif match is not None:
                        # D2R-only columns pointing into a table Median replaces (e.g. monstats'
                        # TreasureClassDesecrated* -> D2R treasure classes) would dangle: leave them empty.
                        out.append("" if dc in dead_refs else match[di[dc]])
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
                if grouped:  # several Median rows per entry: Median's groups in order, then D2R's other groups
                    rows = [convert(r, None, None) for r in mrows]
                    median_groups = {r[mi[key]] for r in mrows}
                    rows += [r for r in drows if r[di[key]] not in median_groups]
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
                            rows.append(self.drop_dangling(d2r_by_id[i], dh, dead_refs, table, i))
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
                if kind == "d2r_only":
                    rows = list(drows_all)  # exactly D2R's file, "Expansion" marker rows included
                elif kind == "d2r_wins":
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
    m.split_large_sets()
    m.clean_treasure_classes()
    m.claim_presets()
    m.claim_mazes()
    m.fill_skilldesc()
    m.skill_rules()
    m.vendor_stock()
    m.item_spells()
    m.extra_calc_codes()
    if m.dangling:
        m.report += ["", "## References emptied in rows kept from D2R (target only existed in D2R's replaced tables)", ""]
        m.report += [f"- {t}: {n} cells" for t, n in sorted(m.dangling.items())]
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

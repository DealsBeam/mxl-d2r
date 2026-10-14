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
# D2Sigma's item spells (misc pSpell) that D2R lacks, all moved to PLUGIN_SPELL (item_spells); D2R's item spell
# table (0x236fe30) has slots 0-19 and 16-19 are empty, so 16 is usable but does nothing without mxl-core.
SIGMA_ITEM_SPELLS = set(range(12, 22))
SIGMA_TARGETED_SPELLS = {14, 20}  # dyes, mystic orbs: applied to a clicked item
SIGMA_HELP_SPELLS = {13, 21}  # right-click shows the item's help text (help_tooltips)
SIGMA_KEPT_SPELLS = {13, 18, 21}  # not used up (also keys with calc3 set)
PLUGIN_SPELL = 16
PLUGIN_SPELL_KEEP = 17  # same, but the item is not used up (mxl-core sets the slot's "keep" flag)
# Tables with item property slots (code/param/min/max columns), for remap_property_params.
PROPERTY_TABLES = ("uniqueitems", "setitems", "sets", "runes", "magicprefix", "magicsuffix", "automagic",
                   "rareprefix", "raresuffix", "qualityitems", "lowqualityitems", "gems", "cubemain", "monprop")
# Stat tooltip functions (descfunc) whose parameter is a skill: 15 on-event, 16 aura, 24 charges, 27/28 +skill,
# and Median's 33 (cooldown reduction), 37 (maximum skill level), 38 (activation frequency).
SKILL_PARAM_DESCFUNCS = {"15", "16", "24", "27", "28", "33", "37", "38"}
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
# Columns 1.13c has but left empty, which D2R fills with new meaning: a Median row that matches a D2R row keeps D2R's
# value. overlay Character is the HD overlay folder (data/hd/overlays/<Character>/<overlay>.json); without it D2R
# draws no HD overlay (Whirlwind's spin, auras, cast effects: 293 rows).
D2R_WHEN_EMPTY = {"overlay": {"Character"}}
# Engine tokens D2R renamed: a matching row always takes D2R's value. plrmode/monmode Code: 1.13c spelled Sequence and
# Knockback "GH" (like Get Hit); D2R's are SQ and KB, and skills.txt's SQ resolves through them (with GH, Whirlwind's
# sequence found no mode and the animation lookup crashed, 2026-10-14).
D2R_ALWAYS = {"plrmode": {"Code"}, "monmode": {"code"}}
# Signed bytes in the .bin records that the decoder read unsigned: monstats coldeffect -20 came out as 236 (chilled
# monsters at +236% speed instead of -20%), spawnx/spawny -2 as 254, misc spellicon -1 (none) as 255. Found
# 2026-10-14 by comparing shared rows with D2R.
SIGNED_BYTE = {"monstats": {"coldeffect", "coldeffect(N)", "coldeffect(H)", "spawnx", "spawny"},
               "misc": {"spellicon"}}
# 32-bit fields the decoder printed unsigned: -1 as 4294967295 (automagic mod1min, cubemain value, skills ELevLen1,
# EMinLev5, ...). Written as the signed value they hold, so D2R's text parser cannot clamp them to 2147483647.
# experience.txt holds real unsigned 32-bit values (D2R's own Warlock column).
UNSIGNED_32_TABLES = {"experience"}
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

    # Median's grid sizes (inventory.bin, the same for both screen rows): player inventory and Horadric Cube 15 x 10.
    # D2R keeps its own rows (2 per page); only gridX/gridY change, the HD panels come from tools/ui_layouts.py.
    # Classic mode (G) still draws D2R's classic panel art and grid positions. Stash (14 x 14) waits for a check of
    # the shared stash file with a bigger page.
    INVENTORY_SIZES = {"Amazon": (15, 10), "Sorceress": (15, 10), "Necromancer": (15, 10), "Paladin": (15, 10),
                       "Barbarian": (15, 10), "Druid": (15, 10), "Assassin": (15, 10), "Warlock": (15, 10),
                       "Transmogrify Box Page 1": (15, 10), "Transmogrify Box": (15, 10)}

    def inventory_sizes(self) -> None:
        n = 0
        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "inventory.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            gx, gy = h.index("gridX"), h.index("gridY")
            for r in rows:
                size = self.INVENTORY_SIZES.get(r[0]) or self.INVENTORY_SIZES.get(r[0][:-1] if r[0].endswith("2") else "")
                if size:
                    r[gx], r[gy] = str(size[0]), str(size[1])
                    n += 1
            write_tsv(p, h, rows)
        self.report.append(f"\ninventory: {n} rows set to Median's grid sizes (player 15x10, cube 15x10)")

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

    def formula_proxies(self) -> None:
        """D2Sigma's formula functions (hasstate, statlayer, itemstat, tabpoints): tools/formula.py renders each
        distinct call as skill('mxf_N'.mxfn) and lists the calls in work/formulas/proxies.txt. Each becomes a hidden
        skills row named mxf_N (appended in both banks) with Param1 = function (1 hasstate, 2 statlayer, 3 itemstat,
        4 tabpoints) and Param2..4 = its arguments as migrated ids; skillcalc gets the code mxfn, which the mxl-core
        plugin answers from those params (else 0)."""
        src = ROOT / "work" / "formulas" / "proxies.txt"
        if not src.exists():
            return
        calls = [line.split("\t") for line in src.read_text("utf-8").splitlines()[1:] if line.strip()]
        sh, srows = read_tsv(OUT / "data" / "global" / "excel" / "base" / "states.txt")
        state_id = {r[0]: i for i, r in enumerate(r for r in srows if r and r[0])}
        ih, irows = read_tsv(OUT / "data" / "global" / "excel" / "base" / "itemstatcost.txt")
        stat_id = {r[0]: r[ih.index("*ID")] for r in irows if r and r[0]}

        def arg(kind: str, pos: int, value: str) -> str:
            if (kind == "1" and pos == 0) or (kind == "2" and pos == 0) or (kind == "3" and pos == 1):
                table = state_id if kind == "1" else stat_id
                return str(table.get(value, -1))
            return value

        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "skills.txt"
            h, rows = read_tsv(p)
            rows = [r for r in rows if not (r and r[0].startswith("mxf_"))]
            ci, p1 = h.index("*Id"), h.index("Param1")
            first = len(rows)
            for k, (name, _fn, kind, *args) in enumerate(calls):
                row = [""] * len(h)
                row[0], row[ci], row[p1] = name, str(first + k), kind
                for j, v in enumerate(args[:3]):
                    if v != "":
                        row[p1 + 1 + j] = arg(kind, j, v)
                rows.append(row)
            write_tsv(p, h, rows)
            sc = OUT / "data" / "global" / "excel" / bank / "skillcalc.txt"
            ch, crows = read_tsv(sc)
            if not any(r and r[0] == "mxfn" for r in crows):
                row = [""] * len(ch)
                row[0], row[1] = "mxfn", "Median XL formula function proxy (mxl-core plugin)"
                crows.append(row)
                write_tsv(sc, ch, crows)
        self.report.append(f"\nformula functions: {len(calls)} proxy skills (mxf_*) for D2Sigma's hasstate/statlayer/"
                           f"itemstat/tabpoints, calc code mxfn")

    LAYERED_FUNCS = {"165": None, "18": 174, "124": 180}  # D2Sigma srvdofunc -> plugin slot (165 moves with 153-171)

    def layered_skills(self) -> set[str]:
        """Median skills on a D2Sigma function that gives aurastats a layer from skills2 (first six dwords): aura 165,
        and its versions of the self buff 18 and the timed storm 124."""
        h, rows = read_tsv(NAMED / "skills.txt")
        s2h, s2rows = read_tsv(ROOT / "work" / "sigma" / "skills2.txt")
        return {r[0] for i, r in enumerate(rows) if r[h.index("srvdofunc")] in self.LAYERED_FUNCS and i < len(s2rows)
                and any(int(s2rows[i][2 + k] or 0) for k in range(12))}

    def aura_tables(self) -> None:
        """plugin/src/mxl_auras.h: D2Sigma's aura function (165, 0x100ab190) gives each aurastat a layer from its
        skills2 table (first six dwords; callback 0x100bbbe0). Layers are Median numbers: for the stat `state` a state,
        for skill-on-event stats (Encode 2/3) skill << 6 | level, for +skill and synergy stats a skill; translated to
        the migrated ids. Listed for the skills with srvdofunc 165 that use a layer, and for D2Sigma's versions of the
        self buff 18 and the storm 124, which do the same (0x100ab710, 0x100a9510; plugin slots 174/180)."""
        h, rows = read_tsv(NAMED / "skills.txt")
        s2h, s2rows = read_tsv(ROOT / "work" / "sigma" / "skills2.txt")
        ih, irows = read_tsv(OUT / "data" / "global" / "excel" / "base" / "itemstatcost.txt")
        out_stats = {r[0]: r for r in irows if r}
        skill_ids, state_ids = self.median_ids("skills"), self.median_ids("states")
        skill_param_stats = {"itemstatcost_383", "itemstatcost_393", "itemstatcost_396", "itemstatcost_397",
                             "itemstatcost_399", "itemstatcost_400"}  # Median's synergy stats (calc codes syn1-6)
        entries = []
        for i, r in enumerate(rows):
            if r[h.index("srvdofunc")] not in self.LAYERED_FUNCS or i >= len(s2rows):
                continue
            layers = [(int(s2rows[i][2 + 2 * k] or 0) | (int(s2rows[i][3 + 2 * k] or 0) << 16)) for k in range(6)]
            slots = []
            for k in range(6):
                name = r[h.index(f"aurastat{k + 1}")]
                srow = out_stats.get(name)
                if not name or srow is None:
                    slots.append("{-1, 0}")
                    continue
                layer = layers[k]
                encode, descfunc = srow[ih.index("Encode")], srow[ih.index("descfunc")]
                if layer and name == "state":
                    layer = state_ids.get(layer, layer)
                elif layer and encode in ("2", "3"):
                    layer = (skill_ids.get(layer >> 6, layer >> 6) << 6) | (layer & 0x3f)
                elif layer and (descfunc in SKILL_PARAM_DESCFUNCS or name in skill_param_stats):
                    layer = skill_ids.get(layer, layer)
                slots.append(f"{{{srow[ih.index('*ID')]}, {layer}}}")
            if any(layers):
                entries.append(f"    {{{skill_ids.get(i, i)}, {{{', '.join(slots)}}}}},  // {r[0]}")
        lines = ["// Generated by tools/txt_migrate.py (aura_tables); do not edit.", "#pragma once", "",
                 "#include <cstdint>", "", "namespace mxl_auras {", "",
                 "// Median skills (D2Sigma srvdofunc 165, 18, 124) whose aurastats carry a layer: skill id, then per aurastat",
                 "// (itemstatcost id or -1, layer as migrated id).",
                 "struct Slot {", "\tint32_t stat;", "\tint32_t layer;", "};",
                 "struct LayeredAura {", "\tint32_t skill;", "\tSlot slots[6];", "};",
                 "inline constexpr LayeredAura Auras[] = {", *entries, "};", "", "}  // namespace mxl_auras", ""]
        (ROOT / "plugin" / "src" / "mxl_auras.h").write_text("\n".join(lines), encoding="utf-8")
        self.report.append(f"\nauras: {len(entries)} layered Median auras -> plugin/src/mxl_auras.h")

    def damage_tables(self) -> None:
        """plugin/src/mxl_damage.h: D2Sigma's area damage (171, 0x100acee0) adds the attacker's weapon damage scaled by
        the skill's SrcDam (1.13c record +0x1a5, through D2Game 0x6fcfd450) to every unit it hits. The plugin reads
        SrcDam from here rather than from D2R's record."""
        h, rows = read_tsv(NAMED / "skills.txt")
        skill_ids = self.median_ids("skills")
        entries = [f"    {{{skill_ids.get(i, i)}, {int(r[h.index('SrcDam')] or 0)}}},  // {r[0]}"
                   for i, r in enumerate(rows) if r[h.index("srvdofunc")] == "171"]
        lines = ["// Generated by tools/txt_migrate.py (damage_tables); do not edit.", "#pragma once", "",
                 "#include <cstdint>", "", "namespace mxl_damage {", "",
                 "// Skills on D2Sigma's area damage (srvdofunc 171): migrated skill id, SrcDam (weapon damage, 128 = 100%).",
                 "struct AreaDamage {", "\tint32_t skill;", "\tint32_t srcDam;", "};",
                 "inline constexpr AreaDamage Skills[] = {", *entries, "};", "", "}  // namespace mxl_damage", ""]
        (ROOT / "plugin" / "src" / "mxl_damage.h").write_text("\n".join(lines), encoding="utf-8")
        self.report.append(f"\ndamage: {len(entries)} area-damage skills -> plugin/src/mxl_damage.h")

    def automap_tables(self) -> None:
        """plugin/src/mxl_automap.h: AutoMap.txt rows of Median's own level types (36-51, LevelName mxl_lvltype_<n>).
        D2R's loader (0x32cae0) knows 36 level-type names and files these rows under type 0, but keeps every row in file
        order (vector 0x2a9a8c8, 0x20 bytes each); the plugin's automap lookup searches these row ranges for types 36+
        (level_types.cpp)."""
        h, rows = read_tsv(OUT / "data" / "global" / "excel" / "base" / "automap.txt")
        runs: list[tuple[int, int, int]] = []
        for i, r in enumerate(rows):
            m = re.fullmatch(r"mxl_lvltype_(\d+)", r[0]) if r else None
            if not m:
                continue
            t = int(m[1])
            if runs and runs[-1][0] == t and runs[-1][2] == i:
                runs[-1] = (t, runs[-1][1], i + 1)
            else:
                runs.append((t, i, i + 1))
        entries = [f"    {{{t}, {a}, {b}}}," for t, a, b in runs]
        lines = ["// Generated by tools/txt_migrate.py (automap_tables); do not edit.", "#pragma once", "",
                 "#include <cstdint>", "", "namespace mxl_automap {", "",
                 "// Median level types D2R's AutoMap loader does not know: level type, first row, end row (AutoMap.txt).",
                 "struct TypeRows {", "\tint32_t levelType;", "\tint32_t first;", "\tint32_t end;", "};",
                 f"inline constexpr int32_t RowCount = {len(rows)};",
                 "inline constexpr TypeRows Rows[] = {", *entries, "};", "", "}  // namespace mxl_automap", ""]
        (ROOT / "plugin" / "src" / "mxl_automap.h").write_text("\n".join(lines), encoding="utf-8")
        self.report.append(f"\nautomap: {sum(b - a for _t, a, b in runs)} rows of {len({t for t, _a, _b in runs})} "
                           f"Median level types -> plugin/src/mxl_automap.h")

    CLASS_IDS = {"ama": 0, "sor": 1, "nec": 2, "pal": 3, "bar": 4, "dru": 5, "ass": 6}

    def tab_tables(self) -> None:
        """plugin/src/mxl_tabs.h: D2Sigma's formula function tabpoints(tab) (0x100a6a70 -> D2Skills.cpp 0x100aefe0) sums
        the base levels of the unit's class skills (D2Common 10044, the class skill list) whose skilldesc SkillPage is
        tab. Median's own pages, per class: (class, page, migrated skill id)."""
        h, rows = read_tsv(NAMED / "skills.txt")
        dh, drows = read_tsv(NAMED / "skilldesc.txt")
        pages = {r[0]: r[dh.index("SkillPage")] for r in drows}
        skill_ids = self.median_ids("skills")
        entries = []
        for i, r in enumerate(rows):
            cls = self.CLASS_IDS.get(r[h.index("charclass")])
            page = pages.get(r[h.index("skilldesc")], "")
            if cls is not None and page.isdigit() and int(page) > 0:
                entries.append(f"    {{{cls}, {page}, {skill_ids.get(i, i)}}},  // {r[0]}")
        lines = ["// Generated by tools/txt_migrate.py (tab_tables); do not edit.", "#pragma once", "",
                 "#include <cstdint>", "", "namespace mxl_tabs {", "",
                 "// Class skills by Median skill page (D2Sigma tabpoints): player class, page, migrated skill id.",
                 "struct TabSkill {", "\tint32_t playerClass;", "\tint32_t page;", "\tint32_t skill;", "};",
                 "inline constexpr TabSkill Skills[] = {", *entries, "};", "", "}  // namespace mxl_tabs", ""]
        (ROOT / "plugin" / "src" / "mxl_tabs.h").write_text("\n".join(lines), encoding="utf-8")
        self.report.append(f"\ntabs: {len(entries)} class skills by page -> plugin/src/mxl_tabs.h")

    def pet_tables(self) -> None:
        """plugin/src/mxl_pets.h: what D2Sigma's minion scaling (0x100683d0, after each pet of srvdofunc 153) needs:
        its per-pettype table pettypeext (14 bytes, work/sigma/pettypeext.txt; byte 0: copy the owner's attributes,
        byte 2: copy its skill-on-event stats (1: also on attack), byte 13: their chance divisor) indexed by the
        migrated pettype id, and the Median stats it reads, as migrated ids."""
        h, rows = read_tsv(OUT / "data" / "global" / "excel" / "base" / "itemstatcost.txt")
        stat_id = {r[0]: int(r[h.index("*ID")]) for r in rows if r and r[h.index("*ID")].lstrip("-").isdigit()}
        named_stats = [r[0] for r in read_tsv(NAMED / "itemstatcost.txt")[1]]

        def stat(median_id: int) -> int:
            return stat_id[named_stats[median_id]]

        ph, prows = read_tsv(ROOT / "work" / "sigma" / "pettypeext.txt")
        pet_ids = self.median_ids("pettype")
        size = max(pet_ids.values()) + 1
        entries = ["{0, 0, 1}"] * size
        for i, r in enumerate(prows):
            raw = bytearray(14)
            for j, c in enumerate(ph):
                if c.startswith("u16_0x") and r[j].isdigit():
                    struct.pack_into("<H", raw, int(c[6:], 16), int(r[j]))
            if i in pet_ids:
                entries[pet_ids[i]] = f"{{{raw[0]}, {raw[2]}, {raw[13] or 1}}}"
        lines = ["// Generated by tools/txt_migrate.py (pet_tables); do not edit.", "#pragma once", "",
                 "#include <cstdint>", "", "namespace mxl_pets {", "",
                 "// D2Sigma's pettypeext, indexed by the migrated pettype id: copy the owner's attributes, copy its",
                 "// skill-on-event stats (1: attack and kill, else kill only), their chance divisor.",
                 "struct PetType {", "\tuint8_t copyAttributes;", "\tuint8_t copyProcs;", "\tuint8_t procDivisor;", "};",
                 "inline constexpr PetType PetTypes[] = {" + ", ".join(entries) + "};", "",
                 f"inline constexpr int32_t StatSummonAttackRating = {stat(500)};   // Median 500",
                 f"inline constexpr int32_t StatSummonElementalResist = {stat(487)};  // Median 487",
                 f"inline constexpr int32_t StatSummonDamageResist = {stat(229)};   // Median 229",
                 "// copied from the owner when not 0 (D2Sigma 0x10067db0): pierce",
                 "inline constexpr int32_t PierceStats[] = {" + ", ".join(str(stat(s)) for s in (333, 335, 334, 336)) + "};",
                 "// with pettypeext byte 0 (0x10068020): in full, and halved",
                 "inline constexpr int32_t AttributeStats[] = {" + ", ".join(str(stat(s)) for s in (1, 0, 485, 278, 86)) + "};",
                 "inline constexpr int32_t HalfStats[] = {" + ", ".join(str(stat(s)) for s in (329, 331, 330, 332, 357)) + "};",
                 "// with pettypeext byte 2 (0x10067e20): every layer of these owner stats, value / procDivisor (at least 1);",
                 "// skill on kill and Median 292 always, skill on attack only when byte 2 is 1",
                 f"inline constexpr int32_t StatSkillOnKill = {stat(196)};    // Median 196",
                 f"inline constexpr int32_t StatSkillOnAttack = {stat(195)};  // Median 195",
                 f"inline constexpr int32_t StatProc292 = {stat(292)};        // Median 292",
                 "", "}  // namespace mxl_pets", ""]
        (ROOT / "plugin" / "src" / "mxl_pets.h").write_text("\n".join(lines), encoding="utf-8")
        self.report.append(f"\npets: {len(prows)} pettypeext rows -> plugin/src/mxl_pets.h")

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
                row[0] = f"x{pad:03d}"  # codes are at most 4 characters
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
        lines.append(f"inline constexpr int32_t Calc_mxfn = {first + len(codes)};  // formula function proxy")
        miss = [r[0].strip() for r in read_tsv(OUT / "data" / "global" / "excel" / "base" / "misscalc.txt")[1] if r]
        lines += ["", "// misscalc.txt rows of D2Sigma's missile calc codes (the same in both banks)."]
        lines += [f"inline constexpr int32_t MissCalc_{c} = {miss.index(c)};" for c in ("msyn", "blvl")]
        lines += ["", "// Stats those codes read (Median itemstatcost row -> migrated id).",
                  f"inline constexpr int32_t StatSyn1 = {stat(383)};  // Median 383",
                  f"inline constexpr int32_t StatSyn2 = {stat(393)};  // Median 393",
                  f"inline constexpr int32_t StatSyn3 = {stat(396)};  // Median 396",
                  f"inline constexpr int32_t StatSyn4 = {stat(397)};  // Median 397",
                  f"inline constexpr int32_t StatSyn5 = {stat(399)};  // Median 399",
                  f"inline constexpr int32_t StatSyn6 = {stat(400)};  // Median 400",
                  f"inline constexpr int32_t StatSkillCapBonus = {stat(449)};  // Median 449, skcp",
                  f"inline constexpr int32_t StatCooldownReduction = {stat(309)};  // Median 309, skcd",
                  f"inline constexpr int32_t StatMissileSyn = {stat(382)};  // Median 382, missile msyn",
                  "", "}  // namespace mxl_ids", ""]
        (ROOT / "plugin" / "src" / "mxl_ids.h").write_text("\n".join(lines), encoding="utf-8")

    def item_spells(self) -> None:
        """misc.txt pSpell (what using an item does). D2R maps pSpell 0-19 to its handlers (0x236fe30) and ignores
        20+; D2Sigma replaced 12-22 with its own (D2Sigma 0x101d95b0). Median's healing/mana/rejuvenation potions
        use D2Sigma's 22, a timed potion (state for len frames adding stat1-stat3), which is exactly D2R's 3.
        D2Sigma's 12-21 (quest/cube items, dyes, mystic orbs, keys) would run D2R's unrelated 12-15 (12 Scroll of
        Resistance, 13 Token of Absolution: a respec) or nothing, so they all become PLUGIN_SPELL, a slot D2R leaves
        empty but usable; the mxl-core plugin recognises the items by code (plugin/src/mxl_items.h) and refuses the
        ones it does not implement yet. Targeted spells (dyes 14, orbs 20: used on another item) get D2R's
        Identify cursor (spellicon 0): D2R passes the clicked item to the item spell when spellicon is not -1."""
        to_d2r = {"22": "3"}
        self.sigma_items: list[tuple[str, int, str]] = []  # (code, D2Sigma spell, calc1)
        self.help_keys: set[str] = set()
        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "misc.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            ps, ic, cc, ci = h.index("pSpell"), h.index("spellicon"), h.index("code"), h.index("calc1")
            sd, sdm, c3 = h.index("spelldescstr"), h.index("spelldesc"), h.index("calc3")
            n = 0
            for r in rows:
                if not is_record(r):
                    continue
                if r[ps] in to_d2r:
                    r[ps] = to_d2r[r[ps]]
                    n += 1
                elif r[ps].isdigit() and int(r[ps]) in SIGMA_ITEM_SPELLS:
                    if bank == "base":
                        self.sigma_items.append((r[cc], int(r[ps]), r[ci]))
                    if int(r[ps]) in SIGMA_TARGETED_SPELLS:
                        r[ic] = "0"
                    if int(r[ps]) in SIGMA_HELP_SPELLS and r[sd] and not r[sdm]:
                        r[sdm] = "1"  # show the help text in the tooltip (help_tooltips)
                        self.help_keys.add(r[sd])
                    # D2Sigma keeps these items (its handler reports "not used"): help items, containers, keys
                    # with calc3 set. D2R decides on the client from the slot's flag, which mxl-core sets for 17.
                    keep = int(r[ps]) in SIGMA_KEPT_SPELLS or (int(r[ps]) == 16 and r[c3] not in ("", "0"))
                    r[ps] = str(PLUGIN_SPELL_KEEP if keep else PLUGIN_SPELL)
            write_tsv(p, h, rows)
            if bank == "base":
                by = Counter(s for _c, s, _k in self.sigma_items)
                self.report.append(f"\nmisc pSpell: {n} items with D2Sigma's timed potion (22) -> D2R's 3; "
                                   f"{len(self.sigma_items)} items with D2Sigma's item spells -> {PLUGIN_SPELL} "
                                   f"(mxl-core): " + ", ".join(f"{s}: {k}" for s, k in sorted(by.items())))

    def help_tooltips(self) -> None:
        """D2Sigma's item spells 13 and 21 (arcane shards, catalysts, oils, ...) only show a help panel on right-click
        (packet 0x27 with the item's spelldescstr; D2R has no such packet). item_spells sets spelldesc 1 so D2R shows
        the text in the tooltip instead; here the panel's leading layout number line ("90\\n") is dropped from those
        strings (both string folders, every language)."""
        lead = re.compile(r"^\s*\d+\s*\n")
        n = 0
        for folder in ("strings", "strings-legacy"):
            for p in sorted((OUT / "data" / "local" / "lng" / folder).glob("*.json")):
                recs = json.loads(p.read_text("utf-8-sig"))
                changed = False
                for e in recs:
                    if e.get("Key") in self.help_keys:
                        for lang, text in e.items():
                            if lang not in ("id", "Key") and isinstance(text, str) and lead.match(text):
                                e[lang] = lead.sub("", text, count=1)
                                changed = True
                        n += folder == "strings"
                if changed:
                    text = json.dumps(recs, ensure_ascii=False, indent=2).replace("\n", "\r\n")
                    p.write_text("﻿" + text, encoding="utf-8", newline="")
        self.report.append(f"\nhelp tooltips: {len(self.help_keys)} item help texts (D2Sigma spells 13/21) shown in "
                           f"tooltips ({n} strings)")

    def row_ids(self, table: str, key: str | None = None) -> dict[str, int]:
        """Output row number of each key (column 0, or `key`) in the base bank."""
        h, rows = read_tsv(OUT / "data" / "global" / "excel" / "base" / f"{table}.txt")
        k = h.index(key) if key else 0
        return {r[k]: i for i, r in enumerate(rows) if r and r[k]}

    def median_ids(self, entity: str) -> dict[int, int]:
        """Median row -> migrated ID for an index-addressed entity (idmap.json)."""
        return {e["row"]: e["id"] for e in self.idmap[entity]}

    def param_kinds(self) -> dict[str, str]:
        """Properties whose parameter is a row number of a renumbered table, by the property's func1 and its
        stat1's tooltip function (descfunc) / encoding: "skill" (procs, oskills, charges, auras, Median's per-skill
        stats), "skill-range" (func 12, random skill: min/max are skills) and "monster" (descfunc 23, reanimate).
        Median's own descfuncs 31/34 take string ids and 35 element numbers; those stay."""
        ph, prows = read_tsv(OUT / "data" / "global" / "excel" / "base" / "properties.txt")
        ih, irows = read_tsv(OUT / "data" / "global" / "excel" / "base" / "itemstatcost.txt")
        stats = {r[0]: r for r in irows if r}
        kinds = {}
        for r in prows:
            if not r or not r[0]:
                continue
            func, s = r[ph.index("func1")], stats.get(r[ph.index("stat1")])
            descfunc = s[ih.index("descfunc")] if s else ""
            encode = s[ih.index("Encode")] if s else ""
            if func == "12":
                kinds[r[0]] = "skill-range"
            elif func in ("11", "19", "22") or descfunc in SKILL_PARAM_DESCFUNCS or encode in ("2", "3"):
                kinds[r[0]] = "skill"
            elif descfunc == "23":
                kinds[r[0]] = "monster"
        return kinds

    @staticmethod
    def property_columns(header: list[str]) -> list[tuple[int, int | None, int | None, int | None]]:
        """(code, param, min, max) column indexes of each property slot in an item/affix/recipe table."""
        lower = {c.lower(): i for i, c in enumerate(header)}
        groups = []
        for i, c in enumerate(header):
            if m := re.fullmatch(r"(a?)prop(\d+[ab]?)((?: \([nh]\))?)", c, re.I):
                keys = [f"{m[1]}{x}{m[2]}{m[3]}" for x in ("par", "min", "max")]
            elif m := re.fullmatch(r"([pf]|t1)code(\d+[ab]?)", c, re.I):
                keys = [f"{m[1]}{x}{m[2]}" for x in ("param", "min", "max")]
            elif m := re.fullmatch(r"(\w*mod\d+)code", c, re.I):
                keys = [f"{m[1]}{x}" for x in ("param", "min", "max")]
            elif m := re.fullmatch(r"((?:[bc] )?mod \d+)", c, re.I):
                keys = [f"{m[1]} {x}" for x in ("param", "min", "max")]
            else:
                continue
            groups.append((i, *(lower.get(k.lower()) for k in keys)))
        return groups

    # D2Sigma's missile hit functions (server table 0x101dc7c8) that are its own: added 61-77, and 34/41/42 whose
    # numbers D2R 3.x gave to its own missiles. D2R's server hit table (0x2391030) is bounded at 71 with 61-70 empty;
    # they move there, the plugin fills the ones it has ported (docs/plugin-spec.md, Missile functions).
    # 69 (0x1008eeb0) is vanilla hit 1 (explosion: missile damage | HitFlags/ResultFlags to every unit within
    # sHitPar1, D2R 0x45bc40) with weapon damage recomputed at the hit and an optional to-hit roll; it runs D2R's 1.
    SRV_HIT_MOVED = {"30": "69", "34": "67", "41": "68", "42": "70", "61": "61", "62": "62", "69": "1", "71": "63",
                     "72": "64",
                     "73": "65", "77": "66"}
    # D2Sigma's client hit 5-7: D2R's 6/7 are empty, its 5 is Echoing Strike's; Median's 5 moves to the empty 20.
    CLT_HIT_MOVED = {"5": "20"}
    # D2Sigma's missile do 4 (server 0x10089cc0) / client do 69 (0x10089660), phalanx: D2R's 4 and 69 are its own,
    # so they move to the empty server slot 46 and the client slot 14 no missile uses (plugin missile_hits.cpp).
    SRV_DO_MOVED = {"4": "46"}
    CLT_DO_MOVED = {"69": "14"}

    def missile_functions(self) -> None:
        """Median's missiles on D2Sigma's own hit functions move to the slots above; for hit 72 (the owner casts the
        skill sHitPar1, D2Sigma 0x1008d400) sHitPar1 is a Median skill row, translated to the migrated id."""
        skill_ids = self.median_ids("skills")
        state_ids, stat_ids = self.median_ids("states"), self.median_ids("itemstatcost")
        ih, irows = read_tsv(OUT / "data" / "global" / "excel" / "base" / "itemstatcost.txt")
        stat_rows = {r[ih.index("*ID")]: r for r in irows if r}

        def stat_param(stat: str, param: str) -> str:
            """A stat's parameter as D2R numbers: a skill (procs/oskills/auras...), or skill << 6 | level (encode 2/3)."""
            s = stat_rows.get(stat)
            if s is None or not param.isdigit():
                return param
            if s[0] == "state":  # stat 98: the parameter is a state
                return str(state_ids.get(int(param), int(param)))
            if s[0] == "item_aura":  # hidden, but its parameter is the aura skill
                return str(skill_ids.get(int(param), int(param)))
            if s[ih.index("Encode")] in ("2", "3"):
                v = int(param)
                return str(skill_ids.get(v >> 6, v >> 6) << 6 | v & 63)
            if s[ih.index("descfunc")] in SKILL_PARAM_DESCFUNCS:
                return str(skill_ids.get(int(param), int(param)))
            return param

        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "missiles.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            d_h, d_rows = read_tsv(D2R / bank / "missiles.txt") if (D2R / bank / "missiles.txt").exists() else ([], [])
            d2r_own = {r[0] for r in d_rows if r} if d_h else set()
            sh, ch, p1 = h.index("pSrvHitFunc"), h.index("pCltHitFunc"), h.index("sHitPar1")
            sd, cd = h.index("pSrvDoFunc"), h.index("pCltDoFunc")
            moved = skills = 0
            for r in rows:
                if not is_record(r) or r[0] in d2r_own:
                    continue
                if r[sh] == "72" and r[p1].isdigit():
                    r[p1] = str(skill_ids.get(int(r[p1]), int(r[p1])))
                    skills += 1
                if r[sh] == "71":
                    # D2Sigma 0x1008e6f0: state Param4 on the owner, stat sHitPar1 (param Param5) and sHitPar2
                    # (param CltParam5); all Median row numbers
                    ps, s1, s2 = h.index("Param4"), p1, h.index("sHitPar2")
                    if r[ps].isdigit():
                        r[ps] = str(state_ids.get(int(r[ps]), int(r[ps])))
                    for sc, pc in ((s1, h.index("Param5")), (s2, h.index("CltParam5"))):
                        if r[sc].isdigit():
                            r[sc] = str(stat_ids.get(int(r[sc]), int(r[sc])))
                            r[pc] = stat_param(r[sc], r[pc])
                    skills += 1
                if r[sh] in self.SRV_HIT_MOVED:
                    r[sh] = self.SRV_HIT_MOVED[r[sh]]
                    moved += 1
                if r[ch] in self.CLT_HIT_MOVED:
                    r[ch] = self.CLT_HIT_MOVED[r[ch]]
                    moved += 1
                if r[sd] in self.SRV_DO_MOVED:
                    r[sd] = self.SRV_DO_MOVED[r[sd]]
                    moved += 1
                if r[cd] in self.CLT_DO_MOVED:
                    r[cd] = self.CLT_DO_MOVED[r[cd]]
                    moved += 1
            write_tsv(p, h, rows)
            if bank == "":
                self.report.append(f"\nmissiles: {moved} hit/do functions moved to D2R's free slots; "
                                   f"{skills} hit-71/72 rows translated")
        # Hit 41 (D2Sigma 0x1008f210) spawns the monstats row its skill's calc1 gives; those formulas name Median
        # rows (e.g. "ulvl+2387"). Numbers whose monstats row moved are rewritten; others ("ulvl==140") stay.
        mh, mrows = read_tsv(NAMED / "missiles.txt")
        summoners = {r[mh.index("Skill")] for r in mrows if r and r[mh.index("pSrvHitFunc")] == "41"} - {""}
        monster_ids = self.median_ids("monstats")
        fixed = 0
        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "skills.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            c1 = h.index("calc1")
            for r in rows:
                if is_record(r) and r[0] in summoners and r[c1]:
                    new = re.sub(r"(?<![\w.'])\d+(?![\w'])",
                                 lambda m: str(monster_ids.get(int(m[0]), int(m[0]))), r[c1])
                    fixed += new != r[c1] and bank == "base"
                    r[c1] = new
            write_tsv(p, h, rows)
        self.report.append(f"missiles: {fixed} hit-41 summon formulas moved to D2R monstats rows")

    def remap_property_params(self) -> None:
        """Median's item properties give skill (and monster) parameters as Median row numbers, but D2R keeps its
        own rows at their IDs and Median's from that point move (skills 357+ by 72: idmap.json). Rewrite numeric
        parameters of Median's rows in every table with property slots; names and other parameters stay."""
        kinds = self.param_kinds()
        ids = {"skill": self.median_ids("skills"), "monster": self.median_ids("monstats")}
        counts: Counter = Counter()
        for table in PROPERTY_TABLES:
            named = NAMED / f"{table}.txt"
            if not named.exists():
                continue
            median_keys = None if table in REPLACE else {r[0] for r in read_tsv(named)[1] if r}
            for bank in ("base", ""):
                p = OUT / "data" / "global" / "excel" / bank / f"{table}.txt"
                if not p.exists():
                    continue
                h, rows = read_tsv(p)
                groups = self.property_columns(h)
                changed = 0
                for r in rows:
                    if not is_record(r) or (median_keys is not None and r[0] not in median_keys):
                        continue
                    for code, par, lo, hi in groups:
                        kind = kinds.get(r[code])
                        cols = [lo, hi] if kind == "skill-range" else [par]
                        table_ids = ids["monster" if kind == "monster" else "skill"]
                        for c in cols if kind else []:
                            if c is not None and r[c].isdigit() and int(r[c]) in table_ids \
                                    and table_ids[int(r[c])] != int(r[c]):
                                r[c] = str(table_ids[int(r[c])])
                                changed += 1
                write_tsv(p, h, rows)
                if bank == "base":
                    counts[table] = changed
        self.report.append("\nproperty parameters: Median skill/monster row numbers -> migrated IDs: " +
                           ", ".join(f"{t} {n}" for t, n in counts.items() if n))

    def item_spell_tables(self) -> None:
        """plugin/src/mxl_items.h: the items behind PLUGIN_SPELL (item_spells) and D2Sigma's mysticorbs table
        (work/sigma/mysticorbs.txt, 143-byte records; docs/plugin-spec.md, Item spells), with every id migrated."""
        props = self.row_ids("properties")
        types = self.row_ids("itemtypes", "Code")
        mh, mrows = read_tsv(NAMED / "properties.txt")
        th, trows = read_tsv(NAMED / "itemtypes.txt")
        kinds = self.param_kinds()
        skill_ids, monster_ids = self.median_ids("skills"), self.median_ids("monstats")
        h, rows = read_tsv(OUT / "data" / "global" / "excel" / "base" / "itemstatcost.txt")
        stat_id = {r[0]: int(r[h.index("*ID")]) for r in rows if r and r[h.index("*ID")].lstrip("-").isdigit()}
        named_stats = [r[0] for r in read_tsv(NAMED / "itemstatcost.txt")[1]]

        def stat(median_id: int) -> int:
            return stat_id[named_stats[median_id]]

        def itemtype(median_row: int) -> int:
            if median_row <= 0 or median_row >= len(trows):
                return -1
            return types.get(trows[median_row][th.index("Code")], -1)

        oh, orows = read_tsv(ROOT / "work" / "sigma" / "mysticorbs.txt")
        unmapped = 0
        orbs = []
        for orow in orows:
            raw = bytearray(143)
            for j, c in enumerate(oh):
                if c.startswith("field_0x") and orow[j].lstrip("-").isdigit():
                    raw[int(c[8:], 16)] = int(orow[j]) & 0xff
            req, = struct.unpack_from("<b", raw, 0x20)
            inc = struct.unpack_from("<2h", raw, 0x23)
            exc = struct.unpack_from("<2h", raw, 0x27)
            maxn, = struct.unpack_from("<h", raw, 0x2b)
            slots = []
            for k in range(6):
                prop, par, lo, hi = struct.unpack_from("<4i", raw, 0x2d + 16 * k)
                if prop < 0:
                    slots.append((-1, 0, 0, 0))
                    continue
                code = mrows[prop][0]
                kind = kinds.get(code)
                if kind == "skill":
                    par = skill_ids.get(par, par)
                elif kind == "monster":
                    par = monster_ids.get(par, par)
                elif kind == "skill-range":
                    lo, hi = skill_ids.get(lo, lo), skill_ids.get(hi, hi)
                pid = props.get(code, -1)
                unmapped += pid < 0
                slots.append((pid, par, lo, hi))
            orbs.append((orow[1], req, maxn, [itemtype(t) for t in inc], [itemtype(t) for t in exc],
                         raw[0x8d], raw[0x8e], slots))

        def code32(code: str) -> str:
            b = (code + "    ")[:4].encode("latin1")
            return f"0x{struct.unpack('<I', b)[0]:08x}"

        def esc(s: str) -> str:  # quoted, so a trailing backslash cannot continue the // comment
            return '"' + s.replace("*/", "* /") + '"'

        # D2Sigma's per-item table misc2 (rows = Median's misc rows): +0x26 = difficulty + 1 the item works in (0 any)
        nh, nrows = read_tsv(NAMED / "misc.txt")
        misc_row = {r[nh.index("code")].strip(): i for i, r in enumerate(nrows)}
        m2h, m2rows = read_tsv(ROOT / "work" / "sigma" / "misc2.txt")
        items = []
        for code, spell, calc1 in self.sigma_items:
            arg = int(calc1) if calc1.lstrip("-").isdigit() else -1
            if spell == 20:
                arg -= 1  # D2Sigma: mysticorbs row = calc1 - 1
            row = misc_row.get(code.strip(), -1)
            diff = m2rows[row][m2h.index("field_0x26")] if 0 <= row < len(m2rows) else ""
            items.append(f"    {{{code32(code)}, {spell}, {arg}, {int(diff or 0)}}},  // {esc(code)}")
        # Containers (D2Sigma spell 18, 0x1006f240): the item code they hold is misc.bin +0x8c (ultracode; not in
        # the named table), each absorbed item adds calc1 to stat1 up to len.
        bin_ = (BINS / "misc.bin").read_bytes()
        rec = (len(bin_) - 4) // struct.unpack_from("<I", bin_)[0]
        containers = []
        for code, spell, calc1 in self.sigma_items:
            row = misc_row.get(code.strip(), -1)
            if spell != 18 or row < 0:
                continue
            held = bin_[4 + row * rec + 0x8c: 4 + row * rec + 0x90].rstrip(b"\0").decode("latin1")
            r = nrows[row]
            stat_name, cap = r[nh.index("stat1")], r[nh.index("len")]
            if held.strip() and stat_name in stat_id and calc1.isdigit() and cap.isdigit():
                containers.append(f"    {{{code32(code)}, {code32(held.strip())}, {stat_id[stat_name]}, {calc1}, {cap}}},"
                                  f"  // {esc(code)} holds {esc(held.strip())}")
        level_ids = self.median_ids("levels")
        levels = [str(level_ids.get(i, -1)) for i in range(max(level_ids) + 1)]
        lines = ["// Generated by tools/txt_migrate.py (item_spell_tables); do not edit.", "#pragma once", "",
                 "#include <cstdint>", "", "namespace mxl_items {", "",
                 f"inline constexpr int32_t PluginSpell = {PLUGIN_SPELL};  // misc pSpell of D2Sigma's item spells",
                 f"inline constexpr int32_t PluginSpellKeep = {PLUGIN_SPELL_KEEP};  // the same, item not used up",
                 "", "// Items with D2Sigma item spells: code (4 chars, little-endian), D2Sigma's pSpell, argument",
                 "// (mystic orbs: mysticorbs row; dyes: calc1; others: calc1 when constant, else -1).",
                 "struct SigmaItem {", "\tuint32_t code;", "\tint32_t spell;", "\tint32_t arg;",
                 "\tint32_t difficulty;  // D2Sigma misc2 +0x26: difficulty + 1 the item works in, 0 any", "};",
                 "inline constexpr SigmaItem Items[] = {", *items, "};", "",
                 "// D2Sigma's mysticorbs rows (index = row, also the layer of StatOrbCount). Properties: properties.txt",
                 "// row (-1 none), param, min, max. Item types: itemtypes rows (-1 none); qualities: D2Sigma's 1-based",
                 "// item quality + 1 (0 any).",
                 "struct OrbProp {", "\tint32_t prop, param, min, max;", "};",
                 "struct Orb {", "\tint32_t reqLevel;", "\tint32_t maxCount;", "\tint32_t include[2];",
                 "\tint32_t exclude[2];", "\tint32_t includeQuality;", "\tint32_t excludeQuality;",
                 "\tOrbProp props[6];", "};", "inline constexpr Orb Orbs[] = {"]
        for name, req, maxn, inc, exc, qi, qe, slots in orbs:
            ps = ", ".join(f"{{{a}, {b}, {c}, {d}}}" for a, b, c, d in slots)
            lines.append(f"    {{{req}, {maxn}, {{{inc[0]}, {inc[1]}}}, {{{exc[0]}, {exc[1]}}}, {qi}, {qe}, "
                         f"{{{ps}}}}},  // {esc(name)}")
        lines += ["};", "",
                  f"inline constexpr int32_t StatOrbCount = {stat(290)};       // Median 290, layer = orb row",
                  f"inline constexpr int32_t StatOrbMultiplier = {stat(289)};  // Median 289: orb effects x N",
                  f"inline constexpr int32_t StatItemLevelReq = {stat(92)};    // item_levelreq",
                  f"inline constexpr int32_t StatDyeColour = {stat(183)};      // Median 183",
                  "", "// Median level number -> migrated level id (item formulas give Median's numbers, e.g. keys' calc1).",
                  "inline constexpr int16_t LevelIds[] = {" + ", ".join(levels) + "};",
                  f"inline constexpr int32_t ObjectOneWayPortal = {self.median_ids('objects')[586]};  // Median 586",
                  "", "// Containers (D2Sigma spell 18): container code, code of the items it absorbs, stat counting them,",
                  "// amount per item, maximum.",
                  "struct Container {", "\tuint32_t code;", "\tuint32_t held;", "\tint32_t stat;", "\tint32_t amount;",
                  "\tint32_t max;", "};", "inline constexpr Container Containers[] = {", *containers, "};",
                  f"inline constexpr int32_t ItemTypeWeapon = {types['weap']};",
                  f"inline constexpr int32_t ItemTypeArmor = {types['armo']};",
                  "", "}  // namespace mxl_items", ""]
        (ROOT / "plugin" / "src" / "mxl_items.h").write_text("\n".join(lines), encoding="utf-8")
        self.report.append(f"\nitem spells: {len(items)} items, {len(orbs)} mystic orb rows -> plugin/src/mxl_items.h"
                           + (f" ({unmapped} orb properties without a D2R row)" if unmapped else ""))

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
          Armageddon's columns.
        - D2Sigma 161 (hyena_strike, exorcism: srvstfunc 57 like Rabies) is D2R's Rabies 121 (0x576bb0): melee roll,
          on a hit auratargetstate for ELen, a missile (flags 0x8000), the skill's elemental damage and the melee
          hit. D2Sigma picks srvmissilea/b/c by calc2; D2R by its own rule (srvmissilea here), so hyena_strike's
          upgraded missile (srvmissilec, Median stat 191) is not used.
        - D2Sigma 167 (resurrect_2, srvstfunc 21 and pettype revive like Revive) is a Revive: D2R's 58. D2Sigma sets
          the pet's life to calc1 outright and adds the minion pierce/procs; D2R reads calc1 as its life bonus.
        - cltdofunc: D2Sigma's client table adds 96-99, which D2R 3.x uses for its own skills (97 Volcano, 98 Sigil
          Death Explosion, 99 Ring of Fire). Median's 97/98/99 (the client halves of D2Sigma 158/159/166) move to
          D2R's free client slots 110/111/112; mxl-core fills 111 (strike burst), 110/112 stay empty (no client effect).
        - Teleport (27): D2Sigma's version (0x100ad9d0) limits the distance to aurarangecalc (-1: no teleport) for
          players. Rows with aurarangecalc move to plugin slot 179 (D2R's teleport with that limit).
        - D2Sigma's self buff (18) and storm (124) give aurastats a layer like its aura 165: rows that use one move
          to plugin slots 174/180 (D2R's function, then the layers). Slots 172-190 hold D2Sigma 153-171 (+19); the
          ones those leave free (174: 155 -> 109, 179: 160 -> 124, 180: 161 -> 121) carry these.
        - Stop functions (srvstopfunc/cltstopfunc, columns 1.13c also has): D2R gives its Whirlwind (srvdofunc 76,
          cltdofunc 45; Talic's 168) stop function 1, which ends the spin and its aurastate when the skill is let go
          or interrupted; 1.13c's rows had none. Median rows on a do function that D2R pairs with a stop function
          (whirlwind_2, windform) get the same one.
        - anim / seqtrans "GH" -> "SQ": 1.13c's plrmode.txt gives the Sequence mode (18) the code GH, the same as Get
          Hit, so the decoded tables wrote GH for every sequence skill (Whirlwind, Zeal, Fend, Strafe, ... 122 rows).
          D2R's plrmode.txt names it SQ (and Knockback KB; plrmode/monmode Code are kept from D2R, D2R_ALWAYS) and
          reads GH as get hit: those skills played the knockback frames and never ran their sequence (no skill
          function, no damage). No D2R skill uses GH."""
        caps = {"mlvl", "skcp", "skcn"}
        named = [row[0] for row in read_tsv(ROOT / "work" / "named" / "skills.txt")[1]]
        skill_caps = {}
        for line in (ROOT / "work" / "formulas" / "skill_caps.txt").read_text("utf-8").splitlines()[1:]:
            row, mlvl, skcn = (line.split("\t") + ["", ""])[:3]
            if row.isdigit() and int(row) < len(named):
                skill_caps[named[int(row)]] = (mlvl, skcn)
        sigma_first, sigma_last, shift = 153, 171, 19
        to_d2r = {155: 109, 160: 124, 161: 121, 167: 58}  # 161: melee + elemental + target state + missile = Rabies
        ranged = {27: 179}  # D2Sigma's replaced originals that the plugin reimplements, for rows with aurarangecalc
        layered = self.layered_skills()  # D2Sigma 18/124 rows whose aurastats carry a layer -> plugin 174/175
        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "skills.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            dh, drows = read_tsv(D2R / bank / "skills.txt")
            d2r_own = {(r[0], r[dh.index("srvdofunc")]) for r in drows}
            d2r_clt = {(r[0], r[dh.index("cltdofunc")]) for r in drows}
            sp, ml, cc = h.index("skpoints"), h.index("maxlvl"), h.index("charclass")
            c9, c10 = h.index("calc9"), h.index("calc10")
            rs, pa, do = h.index("rightskill"), h.index("passive"), h.index("srvdofunc")
            arc, cdo = h.index("aurarangecalc"), h.index("cltdofunc")
            # D2Sigma's client functions -> D2R's: 97-99 to the plugin's free slots; replaced vanilla ones to the D2R
            # function that does what D2Sigma's does (15: Charged Strike fan = D2R 19; 11: spray = D2R 15; 95: like 98)
            # or to the plugin (22: wall -> 113). 31 (Raise Skeleton) and 94 (doomknight2) keep D2R's.
            # do function -> stop function, from D2R's rows (Whirlwind)
            ss, cs = h.index("srvstopfunc"), h.index("cltstopfunc")
            srv_stop = {r[dh.index("srvdofunc")]: r[dh.index("srvstopfunc")] for r in drows
                        if r[dh.index("srvstopfunc")] not in ("", "0")}
            clt_stop = {r[dh.index("cltdofunc")]: r[dh.index("cltstopfunc")] for r in drows
                        if r[dh.index("cltstopfunc")] not in ("", "0")}
            n_stop = n_seq = 0
            an, st = h.index("anim"), h.index("seqtrans")
            clt_moved = {"97": "110", "98": "111", "99": "112", "15": "19", "11": "15", "95": "111", "22": "113"}
            n_cap = n_right = n_moved = n_ranged = 0
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
                if r[an] == "GH" or r[st] == "GH":
                    r[an] = "SQ" if r[an] == "GH" else r[an]
                    r[st] = "SQ" if r[st] == "GH" else r[st]
                    n_seq += 1
                if r[do] in srv_stop and r[ss] in ("", "0"):
                    r[ss] = srv_stop[r[do]]
                    n_stop += 1
                if r[cdo] in clt_stop and r[cs] in ("", "0"):
                    r[cs] = clt_stop[r[cdo]]
                if r[cc] and r[pa] != "1" and r[rs] in ("", "0"):
                    r[rs] = "1"
                    n_right += 1
                if r[cdo] in clt_moved and (r[0], r[cdo]) not in d2r_clt:
                    r[cdo] = clt_moved[r[cdo]]
                if r[0] in layered and self.LAYERED_FUNCS.get(r[do]):
                    r[do] = str(self.LAYERED_FUNCS[r[do]])
                    n_ranged += 1
                elif r[do].isdigit() and int(r[do]) in ranged and r[arc]:
                    r[do] = str(ranged[int(r[do])])
                    n_ranged += 1
                elif r[do].isdigit() and int(r[do]) in to_d2r:
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
                                   f"{n_moved} skills with D2Sigma srvdofunc {sigma_first}-{sigma_last} -> +{shift}; "
                                   f"{n_ranged} teleports with a range / layered buffs -> 179/174/180; "
                                   f"{n_stop} skills given D2R's stop function for their do function (Whirlwind); "
                                   f"{n_seq} sequence skills anim/seqtrans GH -> SQ")

    def sound_rows(self) -> None:
        """Median's own sound rows (appended after D2R's: sounds is d2r_wins) come from 1.13c's format and miss D2R's
        audio columns: no Channel, no HD Redirect, no Volume Min/Max (1.13c had one Volume, dropped as obsolete) and
        a .wav name where D2R's files are .flac. Rows that reuse a vanilla file (Whirlpool's "distilledwater" plays
        object\\sewerwater) copy those columns from the D2R row playing the same file, so they get its HD sound;
        Median's Volume becomes Volume Min/Max. Rows with Median's own audio (taunts etc.) get the channel D2R uses
        for that folder and a .flac name: they stay silent until the audio itself is ported."""
        mh_src, mrows_src = read_tsv(NAMED / "sounds.txt")
        volume = {r[0]: r[mh_src.index("Volume")] for r in mrows_src if r and r[0]}
        copied = custom = 0
        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "sounds.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            dh, drows = read_tsv(D2R / bank / "sounds.txt")
            vanilla = {r[dh.index("Sound")] for r in drows if r}
            stem = lambda f: f.lower().replace("/", "\\").rsplit(".", 1)[0]
            by_file: dict[str, list[str]] = {}
            channel_of: dict[str, Counter] = defaultdict(Counter)
            for r in drows:
                if not r or not r[dh.index("FileName")]:
                    continue
                f = stem(r[dh.index("FileName")])
                by_file.setdefault(f, r)
                if r[dh.index("Channel")].endswith("_sd"):
                    channel_of[f.split("\\")[0]][r[dh.index("Channel")]] += 1
            audio = ["Redirect", "Channel", "FileName", "IsLocal", "IsMusic", "IsAmbientScene", "IsAmbientEvent",
                     "IsUI", "Pitch Min", "Pitch Max", "LFEMix", "3dSpread", "Is2D", "HDOptOut", "Delay"]
            for r in rows:
                name = r[h.index("Sound")]
                if not name or name in vanilla:
                    continue
                f = stem(r[h.index("FileName")])
                twin = by_file.get(f)
                if twin is not None:
                    for c in audio:
                        r[h.index(c)] = twin[dh.index(c)]
                    copied += bank == "base"
                else:
                    top = f.split("\\")[0]
                    if channel_of[top]:
                        r[h.index("Channel")] = channel_of[top].most_common(1)[0][0]
                    if r[h.index("FileName")]:
                        r[h.index("FileName")] = r[h.index("FileName")].rsplit(".", 1)[0] + ".flac"
                    for c, v in (("Pitch Min", "100"), ("Pitch Max", "100"), ("3dSpread", "60")):
                        if not r[h.index(c)]:
                            r[h.index(c)] = v
                    custom += bank == "base"
                v = volume.get(name, "")
                if v:
                    r[h.index("Volume Min")] = r[h.index("Volume Max")] = v
            write_tsv(p, h, rows)
        self.report.append(f"\nsounds: {copied} Median rows took a vanilla file's D2R audio columns, "
                           f"{custom} play Median's own audio (silent until it is ported)")

    def skill_costs(self) -> None:
        """Median's mana costs live in skills2 formulas (tools/formula.py export_skill_costs), not in skills.txt's
        mana/lvlmana, so the port's skills cost nothing beyond minmana. D2R's mana/lvlmana are 16-bit numbers and its
        cost is D2Sigma's ((level - 1) * lvlmana + mana) << manashift, so constant formulas ("300", "(0-9)") are
        written in. The 17 others (16 summons' petmax-based lvlmana, titan_s_fortitude's) go to calc7 (lvlmana) and
        calc8 (mana), which no Median skill uses; mxl-core evaluates them for the caster and applies stat 228
        (Median's mana cost %) to lvlmana (plugin mana_costs.cpp)."""
        named = [row[0] for row in read_tsv(NAMED / "skills.txt")[1]]
        costs: dict[str, tuple[str, str]] = {}
        for line in (ROOT / "work" / "formulas" / "skill_costs.txt").read_text("utf-8").splitlines()[1:]:
            parts = (line.split("\t") + ["", ""])[:3]
            if parts[0].isdigit() and int(parts[0]) < len(named):
                costs[named[int(parts[0])]] = (parts[1], parts[2])

        def number(text: str) -> int | None:
            m = re.fullmatch(r"\(?(-?\d+)\)?|\(0-(\d+)\)", text.strip())
            if not m:
                return None
            v = int(m[1]) if m[1] is not None else -int(m[2])
            return v if -32768 <= v <= 32767 else None

        written = formulas = 0
        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "skills.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            mc, lc = h.index("mana"), h.index("lvlmana")
            calc_for = {mc: h.index("calc8"), lc: h.index("calc7")}
            for r in rows:
                if not is_record(r) or r[0] not in costs or r[mc] or r[lc]:
                    continue
                mana, lvl = costs[r[0]]
                for col, text in ((mc, mana), (lc, lvl)):
                    v = number(text) if text else None
                    if v:
                        r[col] = str(v)
                        written += bank == "base"
                    elif text and v is None:
                        assert not r[calc_for[col]], (r[0], h[calc_for[col]])
                        r[calc_for[col]] = text
                        formulas += bank == "base"
            write_tsv(p, h, rows)
        self.report.append(f"\nskill costs: {written} mana/lvlmana values from D2Sigma's skills2 formulas written in; "
                           f"{formulas} formulas in calc7/calc8 for the plugin")

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

    # Which strings D2R's skill description line types read (D2Client UI/SkillDesc.cpp, 0x287a50: a switch on the
    # line type that asserts "stringIndex1 != strdummy [&& stringIndex2 != strdummy]" when one is missing).
    DESCLINE_NEEDS = {2: "AB", 3: "AB", 4: "A", 5: "A", 6: "A", 7: "A", 12: "A", 13: "A", 15: "AB", 17: "A",
                      18: "A", 19: "A", 20: "AB", 21: "AB", 23: "A", 24: "A", 25: "A", 30: "A", 31: "A", 32: "AB",
                      33: "AB", 34: "A", 35: "A", 37: "A", 38: "AB", 40: "AB", 41: "A", 42: "A", 46: "AB", 47: "A",
                      49: "A", 50: "A", 51: "A", 53: "A", 54: "A", 55: "A", 56: "A", 57: "A", 58: "AB", 59: "A",
                      62: "AB", 63: "A", 64: "A", 65: "AB", 69: "A", 70: "AB", 71: "AB", 72: "A", 73: "A", 74: "A",
                      76: "AB", 77: "AB", 78: "A", 79: "AB"}

    def skilldesc_lines(self) -> None:
        """Median's skill description lines that D2R's renderer can't draw (bug #9, the strdummy assert):
        - type 40 "%s Receives Bonuses From:" (Sksyn, 401 lines) without textb: D2Sigma fills %s with the skill's
          own name; written in as textb = the row's str name.
        - D2Sigma's own types that collide with D2R's 74-79: 77 with no strings (a line D2Sigma builds in code, the
          first line of 516 skills) and 76 (challenge unlock hints, calcb = unlocked). D2R's 76/77 are synergy lines
          that need two strings; dropped.
        - any other line missing a string its D2R type needs (a handful of Median data slips); dropped."""
        groups = (("descline", "desc", 6), ("dsc2line", "dsc2", 5), ("dsc3line", "dsc3", 7))
        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "skilldesc.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            name = h.index("str name")
            n_syn = n_drop = 0
            for r in rows:
                if not is_record(r):
                    continue
                for line, prefix, count in groups:
                    for i in range(1, count + 1):
                        if f"{line}{i}" not in h:
                            continue
                        cols = [h.index(c) for c in (f"{line}{i}", f"{prefix}texta{i}", f"{prefix}textb{i}",
                                                     f"{prefix}calca{i}", f"{prefix}calcb{i}")]
                        kind, a, b = r[cols[0]], r[cols[1]], r[cols[2]]
                        if not kind.isdigit():
                            continue
                        if kind == "40" and a and not b and r[name]:
                            r[cols[2]] = r[name]
                            n_syn += 1
                            continue
                        needs = self.DESCLINE_NEEDS.get(int(kind), "")
                        if ("A" in needs and not a) or ("B" in needs and not b):
                            for c in cols:
                                r[c] = ""
                            n_drop += 1
            write_tsv(p, h, rows)
            if bank == "":
                self.report.append(f"\nskilldesc (D2R line types): {n_syn} synergy headers given the skill's name; "
                                   f"{n_drop} lines D2R can't draw dropped (mostly D2Sigma's code-built type 77)")

    def clean_cube_outputs(self) -> None:
        """The decoded cubemain (mxl_extracted/2_txt) has `""` for empty output b/c cells and `""ow Portal` for the
        special output "Cow Portal" (5 recipes, which then opened nothing). Written as empty / Cow Portal; other quoted
        cells ("item,mod,...") are D2R's own convention and stay."""
        for bank in ("base", ""):
            p = OUT / "data" / "global" / "excel" / bank / "cubemain.txt"
            if not p.exists():
                continue
            h, rows = read_tsv(p)
            cols = [i for i, c in enumerate(h) if c.lower().startswith("output")]
            fixed = 0
            for r in rows:
                for i in cols:
                    if i < len(r) and r[i] in ('""', '""ow Portal'):
                        r[i] = "Cow Portal" if r[i] == '""ow Portal' else ""
                        fixed += 1
            write_tsv(p, h, rows)
            if bank == "":
                self.report.append(f"\ncubemain: {fixed} output cells with stray quotes fixed (Cow Portal, empty)")

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
                if dc in SIGNED_BYTE.get(table, ()) and v.isdigit() and 127 < int(v) < 256:
                    return str(int(v) - 256)
                if table not in UNSIGNED_32_TABLES and v.isdigit() and 2**31 <= int(v) < 2**32:
                    return str(int(v) - 2**32)
                # D2R booleans are 0/1; 1.13c sometimes stored other non-zero flags
                if dc in bool_cols and re.fullmatch(r"-?\d+", v) and v not in ("0", "1"):
                    return "1"
                return v

            def convert(mrow: list[str], match: list[str] | None, row_id: int | None) -> list[str]:
                out = []
                for dc in dh:
                    if dc in cmap:
                        v = median_value(dc, mrow[mi[cmap[dc]]])
                        if match is not None and dc in D2R_ALWAYS.get(table, ()):
                            v = match[di[dc]]
                        elif v == "" and match is not None and dc in D2R_WHEN_EMPTY.get(table, ()):
                            v = match[di[dc]]
                        out.append(v)
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
    m.inventory_sizes()
    m.claim_presets()
    m.claim_mazes()
    m.sound_rows()
    m.skill_costs()
    m.fill_skilldesc()
    m.skilldesc_lines()
    m.clean_cube_outputs()
    m.skill_rules()
    m.vendor_stock()
    m.item_spells()
    m.help_tooltips()
    m.remap_property_params()
    m.missile_functions()
    m.item_spell_tables()
    m.pet_tables()
    m.aura_tables()
    m.damage_tables()
    m.tab_tables()
    m.automap_tables()
    m.extra_calc_codes()
    m.formula_proxies()
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

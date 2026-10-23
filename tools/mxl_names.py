"""Resolve Median XL placeholder names and renumber Median's rows around D2R's.

Input:  assets/median/2_txt (tables decoded from Median's .bin files; key names were lost and written
        as placeholders such as pr116, is467, st194, ms12, ml98, sd5, mi0, cm0, ol3, lv9, se0, su0, ev2).
Output: work/named/<table>.txt   same tables (1.13c columns), placeholders replaced by names, IDs renumbered
        work/named/idmap.json    per entity: Median row -> {id, name, source}
        work/named/report.txt    summary

Rules (see docs/id-mapping.md):
  * A Median row below the 1.13c row count is the vanilla row: it keeps its ID and takes D2R's name.
    Exception: when D2R put a new row into a slot that 1.13c left unused ("unused183", "Not used") and Median
    also uses that slot, the Median row moves to the new-row range and D2R keeps the slot.
  * skilldesc is referenced by name only and D2R re-sorted it, so vanilla skilldesc rows keep the 1.13c name.
  * New Median rows get IDs after D2R's last row (D2R's own additions stay intact). Their names come from
    in-game text, else from whatever uses them, else "<entity>_<row>". Names are lowercase slugs without a
    prefix (formula text is limited to 255 characters; idmap.json records where every name came from).

Usage: python tools/mxl_names.py [--mxl ...] [--vanilla ...] [--d2r ...] [--out work/named]
"""
import argparse
import csv
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

import vanilla113

ROOT = Path(__file__).resolve().parent.parent
SUGMA = ROOT.parent
DEFAULT_MXL = ROOT / "assets" / "median"
DEFAULT_D2R = ROOT / "extract" / "d2r" / "93847" / "data" / "global" / "excel"

# entity -> (placeholder prefix, table file, key column, Median ID column or None, index-addressed?)
# Index-addressed tables are the ones the engine reads by row number; their new rows go after D2R's.
ENTITIES = {
    "properties":   ("pr", "properties",   "code",        None,    True),
    "itemstatcost": ("is", "itemstatcost", "Stat",        "ID",    True),
    "states":       ("st", "states",       "state",       "id",    True),
    "skills":       ("sk", "skills",       "skill",       "Id",    True),
    "skilldesc":    ("sd", "skilldesc",    "skilldesc",   None,    False),
    "missiles":     ("ml", "missiles",     "Missile",     "Id",    True),
    "monstats":     ("ms", "monstats",     "Id",          "hcIdx", True),
    "monstats2":    ("ms", "monstats2",    "Id",          None,    True),
    "overlay":      ("ol", "overlay",      "overlay",     None,    True),
    "levels":       ("lv", "levels",       "Name",        "Id",    True),
    "superuniques": ("su", "superuniques", "Superunique", "hcIdx", True),
    "events":       ("ev", "events",       "event",       None,    True),
    "sets":         ("se", "sets",         "index",       None,    False),
    "misc":         ("mi", "misc",         "name",        None,    False),
    "cubemain":     ("cm", "cubemain",     "description", None,    False),
    "monplace":     ("me", "monplace",     "code",        None,    False),
    "objects":      ("",   "objects",      "Name",        "Id",    True),
    "monai":        ("ma", "monai",        "AI",          None,    True),
    "pettype":      ("pt", "pettype",      "pet type",    "idx",   True),
    "monprop":      ("mp", "monprop",      "Id",          None,    True),
    "monsounds":    ("mo", "monsounds",    "Id",          None,    True),
    "montype":      ("mt", "montype",      "type",        None,    True),
    "monumod":      ("mu", "monumod",      "uniquemod",   "id",    True),
    "monseq":       ("mq", "monseq",       "sequence",    None,    True),
    "monlvl":       ("mv", "monlvl",       "Level",       None,    True),
    "lvlprest":     ("lp", "lvlprest",     "Name",        "Def",   True),
    "lvlmaze":      ("lm", "lvlmaze",      "Name",        None,    True),
    "lvlsub":       ("ls", "lvlsub",       "Name",        None,    True),
    "lvltypes":     ("lt", "lvltypes",     "Name",        "Id",    True),
    "lvlwarp":      ("lw", "lvlwarp",      "Name",        "Id",    True),
    "objgroup":     ("og", "objgroup",     "GroupName",   None,    True),
    "shrines":      ("sr", "shrines",      "Shrine Type", None,    True),
    "belts":        ("bl", "belts",        "name",        None,    True),
    "difficultylevels": ("dl", "difficultylevels", "Name", None,   True),
    "inventory":    ("iv", "inventory",    "class",       None,    True),
    "itemratio":    ("ir", "itemratio",    "Function",    None,    True),
    "arena":        ("ae", "arena",        "Arena",       None,    False),
    "chartemplate": ("ct", "chartemplate", "Template",    None,    False),
}
# Names that formulas reference by text (skill('x'), stat('x'), miss('x'), hasstate('x')).
FORMULA_NAMES = {"skills", "itemstatcost", "missiles", "states"}
# D2R key column when it differs from Median's (objects' first D2R column is "Class").
D2R_KEY = {"objects": "Name", "shrines": "Name"}
# Tables whose key repeats over several rows (one sequence = several frames); the placeholder number counts
# distinct keys, not rows.
GROUPED = {"monseq"}
# Tables whose key is the row number itself (monlvl: one row per monster level).
NUMERIC_KEY = {"monlvl"}
# (table, columns) D2R reads as row numbers of an entity whose IDs can shift (see docs/id-mapping.md)
NUMERIC_REFS = {
    ("levels", ("Depend", *(f"Vis{i}" for i in range(8)))): "levels",
    ("lvlprest", ("LevelId",)): "levels",
    ("lvlmaze", ("Level",)): "levels",
    ("shrines", ("LevelMin",)): "levels",
    ("weapons", ("missiletype",)): "missiles",
    ("misc", ("missiletype",)): "missiles",
    ("armor", ("missiletype",)): "missiles",
}
PREFIX_ENTITY = {p: e for e, (p, *_r) in ENTITIES.items() if e != "monstats2" and p}
# Columns that mix plain values with placeholders of one entity (under the 90% rule): monstats' Sk1mode..Sk8mode hold
# monmode codes (A1, S1, ...) or a monseq sequence (mqN = the Nth sequence); unresolved, D2R found no mode for ~1,100
# monster skill slots (2026-10-14).
MIXED_COLUMNS = {("monstats", f"Sk{i}mode"): "monseq" for i in range(1, 9)}
# Columns whose "ms" placeholders point at monstats2 rows instead of monstats rows.
MONSTATS2_COLUMNS = {("monstats", "MonStatsEx"), ("monstats2", "Id")}
UNUSED_SLOT = re.compile(r"^(unused\d*|not used)$", re.I)
PLACEHOLDER = re.compile(r"^([a-z]{2})(\d+)$")
COLOR = re.compile(r"ÿc.|\[[A-Za-z]+\]")
CONTROL = re.compile(r"[\x00-\x08\x0a-\x1f]")


def read_tsv(path: Path, encoding: str = "utf-8") -> tuple[list[str], list[list[str]]]:
    # Some .bin string fields hold leftover bytes (levels.txt EntryFile) that include line breaks, which split
    # their row in two and shift every row after it. Rejoin a short line with the next while that gives the
    # header's column count, and drop control characters from cells.
    text = path.read_text(encoding, errors="replace")
    lines = text.split("\r\n") if "\r\n" in text else text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    tabs, joined = lines[0].count("\t"), [lines[0]]
    i = 1
    while i < len(lines):
        line = lines[i]
        while line.count("\t") < tabs and i + 1 < len(lines) and line.count("\t") + lines[i + 1].count("\t") <= tabs:
            i += 1
            line += lines[i]
        joined.append(line)
        i += 1
    rows = [[CONTROL.sub("", c) for c in line.split("\t")] for line in joined]
    header = rows[0]
    if "EntryFile" in header:  # the leftover bytes can also hold a tab: fold the extra cells back into EntryFile
        e = header.index("EntryFile")
        for r in rows[1:]:
            extra = len(r) - len(header)
            if extra > 0:
                r[e:e + extra + 1] = ["".join(r[e:e + extra + 1])]
    return header, rows[1:]


def write_tsv(path: Path, header: list[str], rows: list[list[str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        f.writelines("\t".join(r) + "\r\n" for r in [header, *rows])


def data_rows(rows: list[list[str]]) -> list[list[str]]:
    """Records of a table: every row except "Expansion" separator rows. Rows with an empty key ARE records
    (monsounds, montype, the affix tables start with one); D2R's own ID columns confirm this rule."""
    return [r for r in rows if r and any(c for c in r) and r[0] != "Expansion"]


def slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")[:48]


class Strings:
    """Median string tables: key<TAB>text lines (patchstring and expansionstring follow string)."""

    def __init__(self, folder: Path):
        self.by_key: dict[str, str] = {}
        for name in ("string", "patchstring", "expansionstring"):
            for line in (folder / f"{name}.txt").read_text("utf-8").splitlines():
                key, _, text = line.partition("\t")
                self.by_key[key] = text

    def display(self, key: str) -> str:
        """In-game label: last line of the string, colour codes removed (same rule as D2Sigma's lookup)."""
        text = self.by_key.get(key, "")
        return COLOR.sub("", text.split("\\n")[-1]).strip() if text else ""


class Table:
    def __init__(self, name: str, header: list[str], rows: list[list[str]]):
        self.name, self.header, self.rows = name, header, rows
        for r in rows:
            r.extend([""] * (len(header) - len(r)))

    def col(self, name: str) -> int:
        return self.header.index(name)

    def column(self, name: str) -> list[str]:
        return [r[self.col(name)] for r in self.rows] if name in self.header else []


class Resolver:
    def __init__(self, args):
        txt = args.mxl / "2_txt"
        self.strings = Strings(txt / "strings")
        self.tables = {p.stem: Table(p.stem, *read_tsv(p)) for p in sorted(txt.glob("*.txt"))
                       if not p.stem.endswith("code")}
        self.vanilla_dir = args.vanilla  # optional folder of classic 1.13c tables (A/B check); default: D2R-derived view
        self.d2r = args.d2r
        # Reverse maps for columns the first decoder already turned into display names.
        self.skill_display = {}  # "Attack (sk0)" / "Kick" -> skill row
        self.skill_label = {}  # skill row -> in-game name without the "(skN)" tag
        for line in (args.mxl / "skill_names.txt").read_text("utf-8").splitlines()[1:]:
            ph, _sd, _key, used = line.split("\t")
            row = int(ph[2:])
            self.skill_display[used] = row
            if used != ph and not used.startswith("(no name"):
                self.skill_label[row] = re.sub(r"\s*\(sk\d+\)$", "", used)
        self.item_display = {}  # "Amulet (amu)" -> item code
        for line in (args.mxl / "item_names.txt").read_text("utf-8").splitlines()[1:]:
            code, used = line.split("\t")
            self.item_display[used] = code
        self.idmap: dict[str, list[dict]] = {}
        self.taken: dict[str, set[str]] = {}
        self.columns: dict[tuple[str, str], tuple[str, str]] = {}  # (table, column) -> (entity, kind)

    # ---- column classification ---------------------------------------------------------------------------
    def classify_columns(self) -> None:
        """A column references entity E if >= 90% of its non-empty cells are E placeholders (rest may be
        skill display names in skill columns). Key columns are handled separately."""
        keycols = {(tname, key) for (_p, tname, key, _i, _x) in ENTITIES.values()}
        for t in self.tables.values():
            for c in t.header:
                if (t.name, c) in keycols and (t.name, c) not in MONSTATS2_COLUMNS:
                    continue
                vals = [v.strip() for v in t.column(c) if v.strip()]
                if not vals:
                    continue
                prefixes = Counter(m.group(1) for v in vals if (m := PLACEHOLDER.match(v)) and m.group(1) in PREFIX_ENTITY)
                skill_names = sum(1 for v in vals if v in self.skill_display)
                if prefixes:
                    extra = skill_names if "sk" in prefixes else 0
                    if sum(prefixes.values()) + extra >= 0.9 * len(vals):
                        if (t.name, c) in MONSTATS2_COLUMNS:
                            ent = "monstats2"
                        elif len(prefixes) == 1:
                            ent = PREFIX_ENTITY[next(iter(prefixes))]
                        else:
                            ent = "*"  # mixed: each cell resolves by its own prefix
                        self.columns[(t.name, c)] = (ent, "placeholder")
                        continue
                if skill_names and skill_names == len(vals):
                    self.columns[(t.name, c)] = ("skills", "display")
        for (tname, c), ent in MIXED_COLUMNS.items():
            if tname in self.tables and c in self.tables[tname].header:
                self.columns[(tname, c)] = (ent, "placeholder")

    def cell_entity(self, ent: str, value: str) -> str:
        if ent != "*":
            return ent
        m = PLACEHOLDER.match(value.strip())
        return PREFIX_ENTITY.get(m.group(1), "*") if m else "*"

    def cell_row(self, ent: str, value: str) -> int | None:
        if ent == "*":
            return None
        v = value.strip()
        if ent == "skills" and v in self.skill_display:
            return self.skill_display[v]
        m = PLACEHOLDER.match(v)
        if m and m.group(1) == ENTITIES[ent][0]:
            return int(m.group(2))
        return None

    def references(self) -> dict[str, Counter]:
        refs: dict[str, Counter] = defaultdict(Counter)
        for (tname, c), (col_ent, _kind) in self.columns.items():
            for v in self.tables[tname].column(c):
                ent = self.cell_entity(col_ent, v)
                if ent == "*" or tname == ENTITIES[ent][1]:
                    continue  # self-references don't count as "Median uses this row"
                if (row := self.cell_row(ent, v)) is not None:
                    refs[ent][row] += 1
        # Formula tables (*code.txt) reference stats/skills/states numerically through placeholders too.
        for p in (DEFAULT_MXL / "2_txt").glob("*code.txt"):
            for prefix, row in re.findall(r"\b([a-z]{2})(\d+)\b", p.read_text("utf-8")):
                if prefix in PREFIX_ENTITY:
                    refs[PREFIX_ENTITY[prefix]][int(row)] += 1
        return refs

    def vanilla(self, tname: str) -> tuple[list[str], list[list[str]]] | None:
        """The table as 1.13c has it, or None when the build has no vanilla reference for it (arena, belts, ...)."""
        if self.vanilla_dir:
            p = self.vanilla_dir / f"{tname}.txt"
            return read_tsv(p, "latin-1") if p.exists() else None
        if tname in vanilla113.ROW_COUNTS:
            return vanilla113.load(tname, lambda p: read_tsv(p, "utf-8-sig"), self.d2r / "base")
        return None

    # ---- ID and name assignment --------------------------------------------------------------------------
    def assign_ids(self, refs: dict[str, Counter]) -> list[str]:
        notes = []
        for ent, (_p, tname, keycol, _idcol, indexed) in ENTITIES.items():
            if tname not in self.tables:
                continue
            # (monseq's first group has an empty name and counts as group 0, like any empty-key record)
            rows_of = data_rows
            vrows: list[str] = []
            if (van := self.vanilla(tname)):
                vh, vr = van
                vk = vh.index(keycol) if keycol in vh else 0
                vrows = [r[vk] for r in rows_of(vr)]
            banks = []
            for bank in (self.d2r / f"{tname}.txt", self.d2r / "base" / f"{tname}.txt"):
                if bank.exists():
                    dh, dr = read_tsv(bank, "utf-8-sig")
                    dk = dh.index(D2R_KEY[ent]) if ent in D2R_KEY and D2R_KEY[ent] in dh else 0
                    banks.append([r[dk] for r in rows_of(dr)])
            if ent in GROUPED:  # one entry per distinct key, in order of first appearance
                vrows = list(dict.fromkeys(vrows))
                banks = [list(dict.fromkeys(b)) for b in banks]
            d2r_rows = max(banks, key=len) if banks else []  # RotW is the larger bank; IDs stay valid in both
            if not vrows and not van:
                vrows = d2r_rows  # no 1.13c reference file: D2R's rows stand in for the vanilla ones
            self.taken[ent] = {n.lower() for b in banks for n in b} | {n.lower() for n in vrows}

            count = len(self.tables[tname].rows)
            if ent in GROUPED:
                key_vals = self.tables[tname].column(keycol)
                nums = [int(m.group(2)) for v in key_vals if (m := PLACEHOLDER.match(v.strip()))]
                count = max(nums) + 1 if nums else len(dict.fromkeys(key_vals))
            entries, new_rows, moved = [], [], []
            for i in range(count):
                e = {"row": i, "id": i, "name": None, "source": None}
                if i < len(vrows):
                    vname = vrows[i]
                    dname = d2r_rows[i] if indexed and i < len(d2r_rows) else None
                    if dname and dname != vname and UNUSED_SLOT.match(vname) and refs[ent][i]:
                        moved.append(i)
                        new_rows.append(e)  # D2R owns the slot; Median's use of it moves
                    elif ent == "skilldesc" or not dname:
                        e["name"], e["source"] = vname, "vanilla 1.13c"
                    else:
                        e["name"], e["source"] = dname, "vanilla (D2R name)"
                else:
                    new_rows.append(e)
                entries.append(e)
            if indexed:
                for k, e in enumerate(new_rows):
                    e["id"] = len(d2r_rows) + k
            self.idmap[ent] = entries
            if moved:
                notes.append(f"{ent}: Median rows {moved} moved out of slots D2R now uses")
        return notes

    def name(self, ent: str, row: int) -> str | None:
        return self.idmap[ent][row]["name"] if 0 <= row < len(self.idmap[ent]) else None

    def give(self, ent: str, row: int, label: str, source: str) -> bool:
        if not (0 <= row < len(self.idmap[ent])):
            return False
        e = self.idmap[ent][row]
        if e["name"] is not None or not label:
            return False
        base = slug(label)
        if not base:
            return False
        limit = self.key_limit(ent)
        base = base[: limit - 4].rstrip("_")  # room for a "_NN" uniqueness suffix
        name, n = base, 2
        while name.lower() in self.taken[ent]:
            name, n = f"{base}_{n}", n + 1
        self.taken[ent].add(name.lower())
        e["name"], e["source"] = name, source
        return True

    _limits: dict[str, int] = {}

    def key_limit(self, ent: str) -> int:
        """Maximum length of the entity's key column in D2R (data guide schema; 31 for missiles/objects)."""
        if ent not in self._limits:
            limit = 47
            try:
                import validate
                schema = getattr(Resolver, "_schema", None) or validate.Schema()
                Resolver._schema = schema
                _p, tname, key, _i, _x = ENTITIES[ent]
                f = schema.field(tname, D2R_KEY.get(ent, key)) or schema.field(tname, key)
                typ = (f or {}).get("type", {})
                if isinstance(typ, dict) and typ.get("type") == "text" and typ.get("dataLength"):
                    limit = typ["dataLength"]
            except (ImportError, OSError, ValueError):
                pass
            if ent in FORMULA_NAMES:
                limit = min(limit, 31)  # D2R's formula parser keeps 32 bytes of a quoted name
            self._limits[ent] = limit
        return self._limits[ent]

    def bare(self, ent: str, row: int) -> str:
        n = self.name(ent, row) or ""
        return n

    def assign_names(self) -> None:
        T = self.tables
        s = self.strings
        # Rows named in formulas that came out over 255 characters (tools/formula.py writes work/shorten.json):
        # compact names from the placeholder prefix, e.g. sk1820, is389. Vanilla rows keep D2R's names.
        shorten = ROOT / "work" / "shorten.json"
        if shorten.exists():
            for ent, rows in json.loads(shorten.read_text("utf-8")).items():
                for row in rows:
                    self.give(ent, row, f"{ENTITIES[ent][0]}{row}", "compact (formula length)")
        # Own text first.
        for row, label in self.skill_label.items():
            self.give("skills", row, label, "skill name")
        for ent, tname, col in (("monstats", "monstats", "NameStr"), ("levels", "levels", "LevelName"),
                                ("misc", "misc", "namestr"), ("superuniques", "superuniques", "Name")):
            for i, v in enumerate(T[tname].column(col)):
                self.give(ent, i, s.display(v) or v, col)
        for i, v in enumerate(T["overlay"].column("Filename")):
            self.give("overlay", i, v.replace("\\", "_"), "Filename")
        for i, v in enumerate(T["itemstatcost"].column("descstrpos")):
            self.give("itemstatcost", i, re.sub(r"%[+\-]?\d*[a-z%]", " ", s.display(v)), "descstrpos")
        for i, v in enumerate(T["sets"].column("name")):
            self.give("sets", i, v, "name") or None
        for e in self.idmap["sets"]:
            if e["name"] is None and T["sets"].rows[e["row"]][T["sets"].col("name")]:
                e["name"], e["source"] = T["sets"].rows[e["row"]][T["sets"].col("name")], "name"
        # Monster skills and missiles: named after the first monster that uses them.
        for col in [f"Skill{k}" for k in range(1, 9)]:
            for i, v in enumerate(T["monstats"].column(col)):
                if (r := self.cell_row("skills", v)) is not None:
                    self.give("skills", r, f"{self.bare('monstats', i)}_skill", f"monstats.{col}")
        # Properties: the first stat they set (stats are named by now).
        for i, v in enumerate(T["properties"].column("stat1")):
            if (r := self.cell_row("itemstatcost", v)) is not None:
                self.give("properties", i, self.bare("itemstatcost", r), "stat1")
        # States / missiles / skilldesc: the first skill that references them.
        by_skill = {"states": ["aurastate", "aurotherstate", "auratargetstate", "passivestate", "state1", "state2", "state3"],
                    "missiles": ["srvmissile", "cltmissile", "srvmissilea", "srvmissileb", "srvmissilec",
                                 "cltmissilea", "cltmissileb", "cltmissilec", "cltmissiled"],
                    "skilldesc": ["skilldesc"]}
        suffix = {"states": "_state", "missiles": "_missile", "skilldesc": ""}
        for ent, cols in by_skill.items():
            for col in cols:
                for i, v in enumerate(T["skills"].column(col)):
                    if (r := self.cell_row(ent, v)) is not None and self.name("skills", i):
                        self.give(ent, r, self.bare("skills", i) + suffix[ent], f"skills.{col}")
        # Sub-missiles: named after their parent missile; repeat until nothing changes.
        sub_cols = [c for c in T["missiles"].header if re.match(r"(Sub|HitSub|Explosion|Hit)Missile\d*$|^SubMissile\d$", c)]
        changed = True
        while changed:
            changed = False
            for col in sub_cols:
                for i, v in enumerate(T["missiles"].column(col)):
                    r = self.cell_row("missiles", v)
                    if r is not None and self.name("missiles", i):
                        changed |= self.give("missiles", r, f"{self.bare('missiles', i)}_{slug(col)}", f"missiles.{col}")
        # Monster missiles.
        for col in [c for c in T["monstats"].header if re.match(r"Miss[A-Z]\d|MissSQ|MissC|MissS\d", c)]:
            for i, v in enumerate(T["monstats"].column(col)):
                if (r := self.cell_row("missiles", v)) is not None:
                    self.give("missiles", r, f"{self.bare('monstats', i)}_{slug(col)}", f"monstats.{col}")
        # monstats2: the first monster that uses it.
        for i, v in enumerate(T["monstats"].column("MonStatsEx")):
            if (r := self.cell_row("monstats2", v)) is not None:
                self.give("monstats2", r, self.bare("monstats", i), "monstats.MonStatsEx")
        # Monster-side tables (monprop, monsounds, montype, monai, ...): the first monster that uses them;
        # pet types: the first skill that summons them.
        for (tname, col), (col_ent, _k) in self.columns.items():
            if tname not in ("monstats", "skills") or col_ent in ("*", "skills", "missiles", "states", "itemstatcost",
                                                                  "skilldesc", "monstats", "monstats2", "properties"):
                continue
            owner = "monstats" if tname == "monstats" else "skills"
            for i, v in enumerate(T[tname].column(col)):
                if (r := self.cell_row(col_ent, v)) is not None and self.name(owner, i):
                    self.give(col_ent, r, f"{self.bare(owner, i)}_{slug(col)}", f"{tname}.{col}")
        # Objects kept their own names; level presets are named after their map file.
        for i, v in enumerate(T["objects"].column("Name")):
            self.give("objects", i, v, "Name")
        for i, v in enumerate(T["lvlprest"].column("File1")):
            self.give("lvlprest", i, Path(v.replace("\\", "/")).stem if v and v != "0" else "", "File1")
        for ent in NUMERIC_KEY:
            for e in self.idmap[ent]:
                if e["name"] is None:
                    e["name"], e["source"] = str(e["row"]), "row number"
        # Whatever is left.
        for ent in ENTITIES:
            for e in self.idmap.get(ent, []):
                if e["name"] is None:
                    if ent == "cubemain":
                        e["name"], e["source"] = f"Median recipe {e['row']}", "generated"
                    else:
                        self.give(ent, e["row"], f"{ent}_{e['row']}", "fallback")

    # ---- rewrite ----------------------------------------------------------------------------------------
    def tc_item(self, cell: str) -> str:
        """Treasure class item: the first decoder wrote item display names ("Gold (gld)"), sometimes quoted with
        a modifier ('"Gold (gld),mul=2560"', D2R's own syntax is '"gld,mul=1280"'). Map the name to its code."""
        quoted = len(cell) > 1 and cell[0] == cell[-1] == '"'
        body = cell[1:-1] if quoted else cell
        name, sep, rest = body.partition(",")
        code = self.item_display.get(name, name)
        body = code + sep + rest
        return f'"{body}"' if quoted else body

    def rewrite(self, out: Path) -> tuple[Counter, dict]:
        replaced: Counter = Counter()
        unresolved: dict[str, set] = defaultdict(set)
        for (tname, c), (col_ent, _kind) in self.columns.items():
            t = self.tables[tname]
            ci = t.col(c)
            for r in t.rows:
                if not r[ci].strip():
                    continue
                ent = self.cell_entity(col_ent, r[ci])
                row = self.cell_row(ent, r[ci])
                if row is None:
                    continue
                if row < len(self.idmap[ent]):
                    r[ci] = self.idmap[ent][row]["name"]
                    replaced[ent] += 1
                else:
                    unresolved[ent].add(f"{tname}.{c}={r[ci]}")
        # Columns D2R reads as row numbers (data guide: int fields citing another table's Id/index). Median's new
        # rows moved after D2R's, so these numbers follow the ID map; placeholders above only covered names.
        for (tname, cols), ent in NUMERIC_REFS.items():
            t = self.tables.get(tname)
            if not t:
                continue
            for c in cols:
                if c not in t.header:
                    continue
                ci = t.col(c)
                for r in t.rows:
                    v = r[ci].strip()
                    if v.isdigit() and 0 < int(v) < len(self.idmap[ent]):
                        new = str(self.idmap[ent][int(v)]["id"])
                        if new != v:
                            r[ci] = new
                            replaced[f"{ent} (numeric)"] += 1
        for ent, (_p, tname, keycol, idcol, _x) in ENTITIES.items():
            if tname not in self.tables:
                continue
            t = self.tables[tname]
            for i, r in enumerate(t.rows):
                k = i
                if ent in GROUPED:  # key cell names the group, not the row
                    k = self.cell_row(ent, r[t.col(keycol)])
                    if k is None:
                        continue
                r[t.col(keycol)] = self.idmap[ent][k]["name"]
                if idcol and idcol in t.header:
                    r[t.col(idcol)] = str(self.idmap[ent][k]["id"])
        # The first decoder wrote 0 as blank; MonStatsEx is a required link, so blank means monstats2 row 0.
        ms = self.tables["monstats"]
        if "MonStatsEx" in ms.header:
            ci = ms.col("MonStatsEx")
            for r in ms.rows:
                if not r[ci].strip():
                    r[ci] = self.idmap["monstats2"][0]["name"]
        tc = self.tables.get("treasureclassex")
        if tc:
            for ci, cname in enumerate(tc.header):
                if re.fullmatch(r"Item\d+", cname):
                    for r in tc.rows:
                        r[ci] = self.tc_item(r[ci])
        for t in self.tables.values():
            write_tsv(out / f"{t.name}.txt", t.header, t.rows)
        return replaced, unresolved


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mxl", type=Path, default=DEFAULT_MXL)
    ap.add_argument("--vanilla", type=Path, default=None,
                    help="folder of classic 1.13c tables to use instead of the view tools/vanilla113.py builds from --d2r")
    ap.add_argument("--d2r", type=Path, default=DEFAULT_D2R)
    ap.add_argument("--out", type=Path, default=ROOT / "work" / "named")
    args = ap.parse_args()

    r = Resolver(args)
    r.classify_columns()
    notes = r.assign_ids(r.references())
    r.assign_names()
    replaced, unresolved = r.rewrite(args.out)
    (args.out / "idmap.json").write_text(json.dumps(r.idmap, indent=0, ensure_ascii=False), encoding="utf-8")

    lines = [f"{'entity':13} {'rows':>5} {'new':>5} {'first new id':>12}  name sources (new rows)"]
    for ent, entries in r.idmap.items():
        new = [e for e in entries if not e["source"].startswith("vanilla")]
        src = Counter(e["source"].split(".")[0] if "." in e["source"] else e["source"] for e in new)
        first = min((e["id"] for e in new), default="-")
        lines.append(f"{ent:13} {len(entries):5} {len(new):5} {first!s:>12}  {dict(src.most_common(5))}")
    lines += ["", f"reference columns: {len(r.columns)}", f"replaced references: {dict(replaced)}", *notes]
    if unresolved:
        lines.append(f"UNRESOLVED: { {k: sorted(v)[:5] for k, v in unresolved.items()} }")
    report = "\n".join(lines)
    (args.out / "report.txt").write_text(report + "\n", encoding="utf-8")
    print(report)
    return 1 if unresolved else 0


if __name__ == "__main__":
    sys.exit(main())

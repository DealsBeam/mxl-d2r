"""Validate the generated mod tables against D2R's own data schema.

Schema: D2R's data guide (extract/d2r/<build>/data/global/dataguide/data/files/*.js), which types every field:
reference {file, field}, string (string key), text (max length, optional list of allowed values), int, boolean,
parse (BBE formula or property reference, told apart by the description), plus enum validations in enums.js.

Tables: work/mod/data/global/excel/{base,rotw} (tools/txt_migrate.py); tables the mod does not ship fall back to
D2R's originals. Strings: work/mod/data/local/lng/strings (tools/tbl2json.py).

Findings are grouped by severity:
  error   the game would reject the value or it points at something that does not exist
  plugin  valid only once a D2RLoader plugin provides it (Median formula functions / calc codes, new level types)
  warn    suspicious, probably fine

Output: work/mod/validate_report.md, validate_findings.json. Exit code 1 if there are errors.
Usage: python tools/validate.py [--bank base|rotw|both] [--examples 3]
"""
import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
D2R = ROOT / "extract" / "d2r" / "93847" / "data"
GUIDE = D2R / "global" / "dataguide" / "data" / "files"
MOD = ROOT / "work" / "mod" / "data"

BBE_FUNCS = {"min", "max", "rand", "skill", "miss", "stat", "sklvl", "sksrc", "cond"}
CONDITIONS = {"IsType", "IsClass", "Desecrated", "Difficulty", "MonsterTestElite", "ItemIsType", "ItemIsModType",
              "MonsterHasMod"}
STAT_MODES = {"accr", "base", "mod"}
MAX_FORMULA_NAME = 31
MAX_ITEMS_PER_SET = 6
ENGINE_AI_ROWS = 155  # D2R build 93847 AI table 0x2396e90
AUTO_TC = re.compile(r"^(weap|armo|mele|bow|mag[a-z]*|rar[a-z]*)\d+$")
SCOPE_CODES = {"Skill": "skillcalc", "Missile": "misscalc", "Monster": "moncalc"}
TOKEN = re.compile(r"\s*(?:(?P<num>\d+)|(?P<str>'[^']*')|(?P<id>[A-Za-z_][A-Za-z0-9_]*)|(?P<op><=|>=|==|!=|[-+*/%^()?:<>,.]))")


def load_js(path: Path) -> dict:
    t = path.read_text("utf-8")
    return json.loads(t[t.index("=") + 1:].strip().rstrip(";"))


class Schema:
    def __init__(self):
        self.files = {p.stem.lower(): load_js(p) for p in GUIDE.glob("*.js")}
        self.enums = {f["name"]: f for f in self.files.get("enums", {}).get("fields", [])}

    def field(self, table: str, column: str) -> dict | None:
        f = self.files.get(table)
        if not f:
            return None
        # armor/weapons/misc share most fields through SharedItems, the affix tables through SharedItemMods
        fields = list(f["fields"])
        for extra in f.get("appendFiles", []):
            fields += self.files.get(extra.lower(), {}).get("fields", [])
        for fld in fields:
            names = [fld["name"], *fld.get("altNames", [])]
            for n in names:
                if n == column or ("#" in n and re.fullmatch(re.escape(n).replace("\\#", r"\d+"), column)):
                    return fld
        return None

    @staticmethod
    def allowed(spec_owner: dict) -> set[str] | None:
        v = spec_owner.get("d2rExcel", {}).get("validation", {})
        if v.get("type") == "table" and "table" in spec_owner:
            col = v.get("tableModelColumn", 0)
            return {row[col] for row in spec_owner["table"][v.get("tableStartRow", 1):] if col < len(row)}
        return None


class Data:
    """Tables of one bank: the mod's copy, else D2R's original."""

    def __init__(self, bank: str):
        self.bank = bank
        self.cache: dict[str, tuple[list[str], list[list[str]]] | None] = {}

    def table(self, name: str):
        name = name.lower()
        if name not in self.cache:
            sub = "base" if self.bank == "base" else ""
            hit = None
            for root in (MOD / "global" / "excel" / sub, D2R / "global" / "excel" / sub):
                p = root / f"{name}.txt"
                if p.exists():
                    lines = p.read_text("utf-8-sig", errors="replace").splitlines()
                    rows = [l.split("\t") for l in lines]
                    hit = (rows[0], [r for r in rows[1:] if r and r[0] != "Expansion"])
                    break
            self.cache[name] = hit
        return self.cache[name]

    def values(self, name: str, column: str) -> set[str] | None:
        t = self.table(name)
        if not t:
            return None
        header, rows = t
        idx = next((i for i, h in enumerate(header) if h.lower() == column.lower()), None)
        if idx is None:
            return None
        return {r[idx] for r in rows if idx < len(r) and r[idx] != ""}


class Validator:
    def __init__(self, bank: str, examples: int):
        self.schema = Schema()
        self.data = Data(bank)
        self.bank = bank
        self.examples = examples
        self.findings: dict[tuple, list] = defaultdict(list)
        self.strings = set()
        for p in (MOD / "local" / "lng" / "strings").glob("*.json"):
            self.strings |= {e["Key"] for e in json.loads(p.read_text("utf-8-sig"))}
        d2r_codes = lambda t: (self.data_d2r(t) or set())
        self.d2r_calc = {t: d2r_codes(t) for t in ("skillcalc", "misscalc", "moncalc")}

    def data_d2r(self, table: str) -> set[str] | None:
        p = D2R / "global" / "excel" / ("base" if self.bank == "base" else "") / f"{table}.txt"
        if not p.exists():
            return None
        return {l.split("\t")[0] for l in p.read_text("utf-8-sig").splitlines()[1:] if l}

    _vanilla: dict[tuple[str, str, str], set[str]] = {}

    def vanilla_values(self, table: str, column: str) -> set[str]:
        key = (self.bank, table, column)
        if key not in self._vanilla:
            p = D2R / "global" / "excel" / ("base" if self.bank == "base" else "") / f"{table}.txt"
            vals: set[str] = set()
            if p.exists():
                lines = p.read_text("utf-8-sig", errors="replace").splitlines()
                header = lines[0].split("\t")
                if column in header:
                    i = header.index(column)
                    vals = {l.split("\t")[i].strip() for l in lines[1:] if len(l.split("\t")) > i}
            self._vanilla[key] = vals
        return self._vanilla[key]

    def add(self, sev: str, table: str, column: str, problem: str, row: str, value: str) -> None:
        if sev == "error" and value in self.vanilla_values(table, column):
            sev, problem = "warn", f"{problem} (vanilla D2R has the same value)"
        self.findings[(sev, table, column, problem)].append((row, value))

    # ---- formulas ---------------------------------------------------------------------------------------
    def check_bbe(self, table: str, column: str, row: str, text: str, scope: str) -> None:
        tokens, pos = [], 0
        while pos < len(text):
            m = TOKEN.match(text, pos)
            if not m or m.end() == pos:
                self.add("error", table, column, "formula: unreadable text", row, text)
                return
            kind = m.lastgroup
            tokens.append((kind, m.group(kind)))
            pos = m.end()
        depth = 0
        for k, v in tokens:
            depth += (v == "(") - (v == ")") if k == "op" else 0
            if depth < 0:
                break
        if depth != 0:
            self.add("error", table, column, "formula: unbalanced parentheses", row, text)
        for k, v in tokens:
            if k == "str" and len(v) - 2 > MAX_FORMULA_NAME:
                self.add("error", table, column, f"name longer than {MAX_FORMULA_NAME} characters in a formula "
                         "(D2R's parser keeps 32 bytes)", row, v)
        codes_table = SCOPE_CODES.get(scope)
        scope_codes = (self.data.values(codes_table, "code") or set()) if codes_table else set()
        d2r_codes = {c for c in self.d2r_calc.get(codes_table, set())} if codes_table else set()
        skillcalc = self.data.values("skillcalc", "code") or set()
        misscalc = self.data.values("misscalc", "code") or set()
        for i, (k, v) in enumerate(tokens):
            if k != "id":
                continue
            nxt = tokens[i + 1][1] if i + 1 < len(tokens) else ""
            prev = tokens[i - 1][1] if i else ""
            if nxt == "(":
                if v.startswith("mxl_"):
                    self.add("plugin", table, column, f"Median formula function {v}()", row, text)
                elif v not in BBE_FUNCS:
                    self.add("error", table, column, f"unknown function {v}()", row, text)
                elif v in ("skill", "miss", "stat", "sklvl", "sksrc") and i + 2 < len(tokens) and tokens[i + 2][0] == "str":
                    name = tokens[i + 2][1][1:-1]
                    target = {"skill": ("skills", "skill"), "sksrc": ("skills", "skill"), "sklvl": ("skills", "skill"),
                              "miss": ("missiles", "Missile"), "stat": ("itemstatcost", "Stat")}[v]
                    if name not in (self.data.values(*target) or set()):
                        self.add("error", table, column, f"{v}(): unknown {target[0]} '{name}'", row, text)
                    if i + 4 < len(tokens) and tokens[i + 3][1] == "." and tokens[i + 4][0] in ("id", "num"):
                        sub = tokens[i + 4][1]
                        valid = STAT_MODES if v == "stat" else (misscalc if v == "miss" else skillcalc)
                        if sub not in valid:
                            if v == "stat" and sub.isdigit() and int(sub) < 100:
                                self.add("plugin", table, column, f"Median stat() mode {sub}", row, text)
                            else:
                                self.add("error", table, column, f"{v}(): unknown sub-code '{sub}'", row, text)
                        elif v != "stat" and sub not in self.d2r_calc["misscalc" if v == "miss" else "skillcalc"]:
                            self.add("plugin", table, column, f"Median calc code '{sub}'", row, text)
                elif v == "cond" and i + 2 < len(tokens) and tokens[i + 2][0] == "str":
                    if tokens[i + 2][1][1:-1] not in CONDITIONS:
                        self.add("error", table, column, "cond(): unknown condition", row, text)
                continue
            if prev == ".":
                continue  # sub-code inside skill('x'.code); checked above
            if not codes_table:
                self.add("error", table, column, f"{scope} scope has no identifiers: '{v}'", row, text)
            elif v not in scope_codes:
                self.add("error", table, column, f"unknown {codes_table} code '{v}'", row, text)
            elif v not in d2r_codes:
                self.add("plugin", table, column, f"Median calc code '{v}'", row, text)

    # ---- columns ----------------------------------------------------------------------------------------
    def check_table(self, table: str) -> None:
        t = self.data.table(table)
        if not t or table not in self.schema.files:
            return
        header, rows = t
        for ci, col in enumerate(header):
            if not col or col.startswith("*"):
                continue
            spec = self.schema.field(table, col)
            if spec is None:
                self.add("warn", table, col, "column not in D2R's schema", "-", "")
                continue
            typ = spec.get("type", {})
            kind = typ.get("type") if isinstance(typ, dict) else typ
            desc = re.sub(r"\s+", " ", (typ.get("description", "") if isinstance(typ, dict) else "") + " " + spec.get("description", ""))
            allowed = self.schema.allowed(spec)
            v_rule = spec.get("d2rExcel", {}).get("validation", {})
            if v_rule.get("type") == "foreign" and v_rule.get("foreignField") in self.schema.enums:
                allowed = self.schema.allowed(self.schema.enums[v_rule["foreignField"]])
            ref_set = None
            if kind == "reference" and isinstance(typ, dict) and typ.get("file"):
                ref_set = self.data.values(typ["file"], typ.get("field") or "")
            prop_set = None
            if kind == "parse" and "Properties#code" in desc:
                prop_set = (self.data.values("properties", "code") or set()) | (self.data.values("propertygroups", "code") or set())
            param_sets = None
            if kind == "parse" and desc.lstrip().startswith("A number or references"):
                param_sets = set()
                for f, fld in re.findall(r"\$!([^!#]+)#([^!]+)!\$", desc):
                    param_sets |= self.data.values(f, fld) or set()
            scope = None
            m = re.search(r"(Skill|Missile|Item|Monster|Treasure Class) scope BBE", desc)
            if kind == "parse" and m:
                scope = m.group(1)
            for r in rows:
                if ci >= len(r):
                    continue
                v = r[ci].strip()
                if v == "":
                    continue
                rid = r[0]
                if kind == "int" and not re.fullmatch(r"-?\d+", v):
                    self.add("error", table, col, "not an integer", rid, v)
                elif kind == "boolean" and v not in ("0", "1"):
                    self.add("error", table, col, "not 0/1", rid, v)
                elif kind in ("text", "parse") and isinstance(typ, dict) and typ.get("dataLength") and len(v) > typ["dataLength"]:
                    self.add("error", table, col, f"longer than {typ['dataLength']} characters", rid, v)
                elif kind == "string" and v not in self.strings and v != "0":
                    # the generated string files hold every Median and D2R key, so the key is missing from
                    # Median's own string tables too (D2Sigma may build such text in code)
                    self.add("warn", table, col, "string key in neither Median's nor D2R's strings", rid, v)
                if allowed is not None and v not in allowed:
                    nums = [int(a) for a in allowed if re.fullmatch(r"-?\d+", a)]
                    if v.startswith("mxl_lvltype_"):
                        self.add("plugin", table, col, "Median level type (D2R's are fixed)", rid, v)
                    elif re.fullmatch(r"-?\d+", v) and nums:
                        self.add("plugin", table, col, "Median value outside D2R's list (engine feature in D2Sigma)", rid, v)
                    else:
                        self.add("error", table, col, "value not in D2R's allowed list", rid, v)
                # SQ (sequence) is not a PlrMode row; D2R's skill loader takes it as a token (126 vanilla skills)
                if ref_set is not None and v not in ref_set and not (typ.get("file") == "PlrMode" and v == "SQ"):
                    self.add("error", table, col, f"no {typ['file']}.{typ.get('field')} '{v}'", rid, v)
                if prop_set is not None and v not in prop_set:
                    self.add("error", table, col, "unknown property code", rid, v)
                if param_sets is not None and not re.fullmatch(r"-?\d+", v) and v not in param_sets:
                    self.add("warn", table, col, "parameter is neither a number nor a known name", rid, v)
                if scope:
                    self.check_bbe(table, col, rid, v, scope)

    def run(self) -> None:
        names = {p.stem for p in (MOD / "global" / "excel" / ("base" if self.bank == "base" else "")).glob("*.txt")}
        for t in sorted(names):
            self.check_table(t)
        self.engine_rules()

    def engine_rules(self) -> None:
        """Rules D2R asserts on at load time that the field schema does not express."""
        d = self.data
        # treasure class items: item code, item type, treasure class, unique/set item index, or an auto class
        tc = d.table("treasureclassex")
        if tc:
            h, rows = tc
            known = set()
            for t, c in (("weapons", "code"), ("armor", "code"), ("misc", "code"), ("itemtypes", "Code"),
                         ("treasureclassex", "Treasure Class"), ("uniqueitems", "index"), ("setitems", "index")):
                known |= d.values(t, c) or set()
            pos = {r[0]: i for i, r in enumerate(rows) if r[0]}
            for ci, c in enumerate(h):
                if not re.fullmatch(r"Item\d+", c):
                    continue
                for i, r in enumerate(rows):
                    v = r[ci].strip() if ci < len(r) else ""
                    name = v.strip('"').split(",", 1)[0]
                    if name and name not in known and not AUTO_TC.match(name):
                        self.add("error", "treasureclassex", c, "not an item, item type, treasure class, unique or set item", r[0], v)
                    elif pos.get(name, -1) > i:
                        self.add("error", "treasureclassex", c, "names a treasure class defined further down", r[0], v)
        # sets: at most 6 items
        si = d.table("setitems")
        if si:
            h, rows = si
            counts = defaultdict(int)
            for r in rows:
                counts[r[h.index("set")]] += 1
            for name, n in counts.items():
                if n > MAX_ITEMS_PER_SET:
                    self.add("error", "setitems", "set", f"set has {n} items (D2R allows {MAX_ITEMS_PER_SET})", name, str(n))
        # monstats: MonStatsEx is required
        ms = d.table("monstats")
        if ms:
            h, rows = ms
            i = h.index("MonStatsEx")
            for r in rows:
                if not r[i].strip() and r[0] != "Expansion":
                    self.add("error", "monstats", "MonStatsEx", "blank (a required link to monstats2)", r[0], "")
            # AI: the engine's AI table has 155 entries (monai rows 0-154, docs/d2r-addresses.md section 12); a monster whose
            # AI is a later row (or no row) runs the "None" think function and stands still
            ai = d.table("monai")
            if ai:
                ai_row = {r[0]: n for n, r in enumerate(ai[1]) if r and r[0]}
                a = h.index("AI")
                for r in rows:
                    name = r[a].strip()
                    if name and ai_row.get(name, ENGINE_AI_ROWS) >= ENGINE_AI_ROWS:
                        self.add("error", "monstats", "AI", f"not in the engine's AI table (monai rows 0-{ENGINE_AI_ROWS - 1})", r[0], name)
        # hireling: the name range NameFirst..NameLast must go upwards in string IDs
        hl = d.table("hireling")
        if hl:
            h, rows = hl
            ids = self.string_ids()
            for r in rows:
                a, b = r[h.index("NameFirst")], r[h.index("NameLast")]
                if a and b and not (ids.get(a, -1) < ids.get(b, -1)):
                    self.add("error", "hireling", "NameLast", "string ID not above NameFirst's", r[0] or "-", f"{a}..{b}")
        # levels: a preset level (DrlgType 2) must be claimed by a lvlprest row's LevelId
        lv, lp = d.table("levels"), d.table("lvlprest")
        if lv and lp:
            (lh, lrows), (ph, prows) = lv, lp
            claimed = {r[ph.index("LevelId")] for r in prows}
            for r in lrows:
                if r[lh.index("DrlgType")] == "2" and r[lh.index("Id")] not in claimed:
                    self.add("error", "levels", "DrlgType", "preset level that no lvlprest row claims", r[0], r[lh.index("Id")])
        if lv:  # Depend places a level relative to another; a loop recurses until the stack overflows
            lh, lrows = lv
            dep = {r[lh.index("Id")]: r[lh.index("Depend")] for r in lrows}
            for start in dep:
                seen, u = [], start
                while dep.get(u, "") not in ("", "0") and u not in seen:
                    seen.append(u)
                    u = dep[u]
                if u == start and seen:
                    self.add("error", "levels", "Depend", "Depend chain loops back to this level", start, " -> ".join(seen))
        lm = d.table("lvlmaze")
        if lv and lm:
            (lh, lrows), (mh, mrows) = lv, lm
            have = {r[mh.index("Level")] for r in mrows}
            for r in lrows:
                if r[lh.index("DrlgType")] == "1" and r[lh.index("Id")] not in have:
                    self.add("error", "levels", "DrlgType", "maze level without a lvlmaze row", r[0], r[lh.index("Id")])
        # skills: class skills need a skilldesc
        sk = d.table("skills")
        if sk:
            h, rows = sk
            for r in rows:
                if r[h.index("charclass")] and not r[h.index("skilldesc")]:
                    self.add("error", "skills", "skilldesc", "class skill without a skilldesc", r[0], "")
        # inventory: D2R expects two rows (resolutions) per page
        inv = d.table("inventory")
        if inv:
            h, rows = inv
            ref = D2R / "global" / "excel" / ("base" if self.bank == "base" else "") / "inventory.txt"
            want = (len([l for l in ref.read_text("utf-8-sig").splitlines()[1:] if l and not l.startswith("Expansion")])
                    if ref.exists() else len(rows))
            if len(rows) != want:
                self.add("error", "inventory", "-", f"{len(rows)} rows; D2R's layout has {want} (2 per page)", "-", "")

    _string_ids: dict | None = None

    def string_ids(self) -> dict[str, int]:
        if self._string_ids is None:
            self._string_ids = {}
            for p in (MOD / "local" / "lng" / "strings").glob("*.json"):
                for e in json.loads(p.read_text("utf-8-sig")):
                    self._string_ids.setdefault(e["Key"], e["id"])
        return self._string_ids


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bank", choices=["base", "rotw", "both"], default="both")
    ap.add_argument("--examples", type=int, default=3)
    args = ap.parse_args()
    banks = ["base", "rotw"] if args.bank == "both" else [args.bank]
    out = ROOT / "work" / "mod"
    report, all_findings, errors = ["# Validation report", ""], {}, 0
    for bank in banks:
        v = Validator(bank, args.examples)
        v.run()
        sev_counts = defaultdict(int)
        for (sev, *_rest), items in v.findings.items():
            sev_counts[sev] += len(items)
        errors += sev_counts["error"]
        report += [f"## Bank: {bank}", "",
                   f"errors {sev_counts['error']}, plugin {sev_counts['plugin']}, warnings {sev_counts['warn']}", ""]
        for sev in ("error", "plugin", "warn"):
            items = sorted(((k, val) for k, val in v.findings.items() if k[0] == sev), key=lambda kv: -len(kv[1]))
            if not items:
                continue
            report += [f"### {sev}", "", "| Table | Column | Problem | Count | Examples (row: value) |", "|---|---|---|---|---|"]
            for (_s, table, col, problem), vals in items:
                ex = "; ".join(f"{r}: {val[:60]}" for r, val in vals[: args.examples]).replace("|", "\\|")
                report.append(f"| {table} | {col} | {problem} | {len(vals)} | {ex} |")
            report.append("")
        all_findings[bank] = {"|".join(k): vals for k, vals in v.findings.items()}
    (out / "validate_report.md").write_text("\n".join(report) + "\n", encoding="utf-8")
    (out / "validate_findings.json").write_text(json.dumps(all_findings, indent=0), encoding="utf-8")
    print("\n".join(line for line in report if line.startswith(("#", "errors"))))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())

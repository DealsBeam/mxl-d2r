"""Median XL formula (calc bytecode) decoder and D2R text renderer.

Bytecode (Fog.dll #10253, 1.13c), a stack machine:
  00 end            01 i   call function i (pops its args)
  04 b / 05 w / 06 d     variable (calc code) with 1/2/4-byte operand
  07 / 08 / 09      push signed 8/16/32-bit constant
  0a <  0b >  0c <=  0d >=  0e ==  0f !=  10 +  11 -  12 *  13 /  14 ^ (power)
  15 unary minus    16 ternary  (cond, then, else -> cond ? then : else)

Function tables differ per context. Indexes 0..N are vanilla; D2Sigma appends its own (found from its
patch records: skills table 0x101df3a8 count 14, items table 0x101d94b4, missiles table 0x101dc480):
  skills/skilldesc: 0 min 1 max 2 rand 3 skill('x'.code) 4 miss('x'.code) 5 stat('x'.mode) 6 sklvl(s,l,?)
                    7 hasstate(state) [mxl]  10 tabpoints(tab) [mxl]  11 statlayer(stat, mode, layer) [mxl]
                    13 itemstat(bodyloc, stat) [mxl]   (8, 9, 12 exist but Median never calls them)
  items:            0 min 1 max 2 rand 3 stat('x'.mode)  4 [mxl] 5 skilllevel(skill, flags) [mxl]
  missiles:         0 min 1 max 2 rand 3 skill('x'.code) 4 [mxl] 5 stat('x'.mode) [added by D2Sigma]
skill() sub-codes are always skillcalc codes; miss() sub-codes and missile-formula variables are misscalc codes.

Malformed bytecode follows Fog's rules: popping an empty stack gives 0, and the result is the top of the
stack when the expression ends (opcode 00 or any unknown opcode). Such formulas are reported as quirks.

Usage: python tools/formula.py check      # decode every formula, compare with the first decoder's text
       python tools/formula.py convert    # rewrite formula cells in work/named/*.txt with D2R-named text,
                                          # write work/formulas/<file>.txt listings and report.md
"""
import collections
import csv
import json
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TXT = ROOT.parent / "mxl_extracted" / "2_txt"

BINOPS = {0x0A: "<", 0x0B: ">", 0x0C: "<=", 0x0D: ">=", 0x0E: "==", 0x0F: "!=",
          0x10: "+", 0x11: "-", 0x12: "*", 0x13: "/", 0x14: "^"}


@dataclass
class Node:
    kind: str  # const | var | op | neg | cond | call
    value: object = None
    args: list = field(default_factory=list)


def read_codes(table: str) -> list[str]:
    lines = (TXT / f"{table}.txt").read_text("utf-8").splitlines()[1:]
    return [l.split("\t", 1)[0] for l in lines if l.strip()]


# function index -> (name, arity, kind): kind "ref" = first arg is a row reference + second a sub-code
CONTEXTS = {
    "skills": {"vars": "skillcalc", "funcs": {
        0: ("min", 2), 1: ("max", 2), 2: ("rand", 2), 3: ("skill", 2), 4: ("miss", 2), 5: ("stat", 2),
        6: ("sklvl", 3), 7: ("mxl_hasstate", 1), 8: ("mxl_fn8", 1), 9: ("mxl_fn9", 1),
        10: ("mxl_tabpoints", 1), 11: ("mxl_statlayer", 3), 12: ("mxl_fn12", 1), 13: ("mxl_itemstat", 2)}},
    "items": {"vars": "skillcalc", "funcs": {
        0: ("min", 2), 1: ("max", 2), 2: ("rand", 2), 3: ("stat", 2), 4: ("mxl_item_fn4", 2), 5: ("mxl_skilllevel", 2)}},
    "missiles": {"vars": "misscalc", "funcs": {
        0: ("min", 2), 1: ("max", 2), 2: ("rand", 2), 3: ("skill", 2), 4: ("mxl_miss_fn4", 2), 5: ("stat", 2)}},
}
FILE_CONTEXT = {"skillscode": "skills", "skills2code": "skills", "skilldesccode": "skills",
                "itemscode": "items", "misscode": "missiles"}


class DecodeError(Exception):
    pass


def decode(code: bytes, ctx: str, quirks: list | None = None) -> Node:
    """Bytecode -> tree, with Fog.dll's semantics for malformed code (see module docstring)."""
    funcs = CONTEXTS[ctx]["funcs"]
    stack: list[Node] = []
    i = 0
    quirks = quirks if quirks is not None else []

    def pop() -> Node:
        if not stack:
            quirks.append(f"stack underflow at byte {i} (Fog uses 0)")
            return Node("const", 0)
        return stack.pop()

    while i < len(code):
        op = code[i]
        if op == 0x01:
            idx = code[i + 1]
            if idx not in funcs:
                raise DecodeError(f"unknown function {idx}")
            name, arity = funcs[idx]
            args = [pop() for _ in range(arity)][::-1]
            stack.append(Node("call", (idx, name), args))
            i += 2
        elif op in (0x04, 0x05, 0x06):
            size = {0x04: 1, 0x05: 2, 0x06: 4}[op]
            stack.append(Node("var", int.from_bytes(code[i + 1:i + 1 + size], "little")))
            i += 1 + size
        elif op in (0x07, 0x08, 0x09):
            size = {0x07: 1, 0x08: 2, 0x09: 4}[op]
            v = int.from_bytes(code[i + 1:i + 1 + size], "little", signed=True)
            # keep the unsigned reading of 1-byte pushes too: the first decoder misread them that way
            stack.append(Node("const", (v, code[i + 1]) if size == 1 and v < 0 else v))
            i += 1 + size
        elif op in BINOPS:
            b, a = pop(), pop()
            stack.append(Node("op", BINOPS[op], [a, b]))
            i += 1
        elif op == 0x15:
            stack.append(Node("neg", None, [pop()]))
            i += 1
        elif op == 0x16:
            e, t, c = pop(), pop(), pop()
            stack.append(Node("cond", None, [c, t, e]))
            i += 1
        else:  # 00 and anything else end the expression
            if op != 0x00:
                quirks.append(f"opcode {op:#04x} at byte {i} ends the expression")
            break
    if len(stack) > 1:
        quirks.append(f"{len(stack) - 1} extra values left on the stack (top is used)")
    return stack[-1] if stack else Node("const", 0)


def entries(name: str) -> list[tuple[int, str, bytes]]:
    """(index, first decoder's text, bytecode) from mxl_extracted/2_txt/<name>.txt."""
    lines = (TXT / f"{name}.txt").read_text("utf-8").splitlines()
    out = []
    for k in range(0, len(lines) - 1, 2):
        idx, _, text = lines[k].partition("\t")
        out.append((int(idx), text, bytes(int(x, 16) for x in lines[k + 1].split())))
    return out


def _i32(v: int) -> int:
    return (v + 2**31) % 2**32 - 2**31


def _apply(op: str, a: int, b: int) -> int:
    """Fog.dll arithmetic on 32-bit ints."""
    if op == "/":
        return 0 if b == 0 else _i32(int(a / b))
    if op == "^":
        r = 1
        for _ in range(max(b, 0)):
            r = _i32(r * a)
        return r
    return _i32({"+": a + b, "-": a - b, "*": a * b, "<": a < b, ">": a > b, "<=": a <= b, ">=": a >= b,
                 "==": a == b, "!=": a != b}[op])


def has_rand(n: Node) -> bool:
    return (n.kind == "call" and n.value[1] == "rand") or any(has_rand(x) for x in n.args)


def fold(n: Node) -> Node:
    """Constant-fold with Fog's semantics: constant subtrees, ternaries with a constant condition,
    x*0, 0/x. Used on malformed formulas so they become clean D2R text."""
    args = [fold(a) for a in n.args]
    n = Node(n.kind, n.value, args)
    is_c = lambda x: x.kind == "const"
    if n.kind == "neg" and is_c(args[0]):
        return Node("const", _i32(-const(args[0])))
    if n.kind == "cond" and is_c(args[0]):
        return args[1] if const(args[0]) else args[2]
    if n.kind == "op":
        a, b = args
        if is_c(a) and is_c(b):
            return Node("const", _apply(n.value, const(a), const(b)))
        if n.value == "*" and ((is_c(a) and const(a) == 0 and not has_rand(b)) or (is_c(b) and const(b) == 0 and not has_rand(a))):
            return Node("const", 0)
        if n.value == "/" and is_c(a) and const(a) == 0 and not has_rand(b):
            return Node("const", 0)
    return n


def const(n: Node) -> int:
    return n.value[0] if isinstance(n.value, tuple) else n.value


PREC = {"?": 1, "<": 3, ">": 3, "<=": 3, ">=": 3, "==": 3, "!=": 3, "+": 4, "-": 4, "*": 5, "/": 5, "^": 6}


class Renderer:
    """Renders a tree as D2 formula text. `ref` resolves (kind, row) -> name for skill/stat/missile/state refs."""

    def __init__(self, ctx: str, ref=None, unsigned_bytes: bool = False):
        self.ctx = ctx
        self.unsigned_bytes = unsigned_bytes  # mimic the first decoder's bug (for comparison only)
        self.codes = read_codes(CONTEXTS[ctx]["vars"])
        self.skillcodes = read_codes("skillcalc")
        self.misscodes = read_codes("misscalc")
        self.ref = ref or (lambda kind, row: {"skill": "sk", "stat": "is", "miss": "ml", "state": "st"}[kind] + str(row))
        self.unsupported: set[str] = set()

    def var(self, n: int) -> str:
        return self.codes[n] if n < len(self.codes) else f"var{n}"

    def render(self, n: Node, parent: int = 0) -> str:
        if n.kind == "const":
            if self.unsigned_bytes and isinstance(n.value, tuple):
                return str(n.value[1])
            return str(n.value[0] if isinstance(n.value, tuple) else n.value)
        if n.kind == "var":
            return self.var(n.value)
        if n.kind == "neg":
            return f"-{self.render(n.args[0], 7)}"
        if n.kind == "cond":
            c = self.render(n.args[0], 2)
            if n.args[0].kind in ("op", "cond"):
                c = f"({c})" if not c.startswith("(") or not c.endswith(")") else c
            s = f"{c}?{self.render(n.args[1], 2)}:{self.render(n.args[2], 2)}"
            return f"({s})" if parent else s
        if n.kind == "op":
            p = PREC[n.value]
            # left-associative: the right operand needs parentheses at equal precedence
            s = f"{self.render(n.args[0], p)}{n.value}{self.render(n.args[1], p + 1)}"
            return f"({s})" if parent > p else s
        idx, name = n.value
        a = n.args
        if name in ("skill", "miss", "stat") and a[0].kind == "const" and a[1].kind == "const":
            row, sub = const(a[0]), const(a[1])
            if name == "skill":
                code = self.skillcodes[sub] if sub < len(self.skillcodes) else str(sub)
                return f"skill('{self.ref('skill', row)}'.{code})"
            if name == "miss":
                code = self.misscodes[sub] if sub < len(self.misscodes) else str(sub)
                return f"miss('{self.ref('miss', row)}'.{code})"
            mode = {0: "accr", 1: "base", 2: "mod"}.get(sub, str(sub))
            return f"stat('{self.ref('stat', row)}'.{mode})"
        if name == "mxl_hasstate" and a[0].kind == "const":
            self.unsupported.add(name)
            return f"{name}('{self.ref('state', const(a[0]))}')"
        if name == "mxl_statlayer" and a[0].kind == "const":
            self.unsupported.add(name)
            return f"{name}('{self.ref('stat', const(a[0]))}',{self.render(a[1])},{self.render(a[2])})"
        if name == "sklvl" and all(x.kind == "const" for x in a):
            codes = [self.skillcodes[const(x)] if const(x) < len(self.skillcodes) else str(const(x)) for x in a[1:]]
            return f"sklvl('{self.ref('skill', const(a[0]))}'.{codes[0]}.{codes[1]})"
        if name == "mxl_itemstat" and a[1].kind == "const":
            self.unsupported.add(name)
            return f"{name}({self.render(a[0])},'{self.ref('stat', const(a[1]))}')"
        if name == "mxl_skilllevel" and a[0].kind == "const":
            self.unsupported.add(name)
            return f"{name}('{self.ref('skill', const(a[0]))}',{self.render(a[1])})"
        if name.startswith("mxl_"):
            self.unsupported.add(name)
        return f"{name}({','.join(self.render(x) for x in a)})"


def normalize(text: str) -> str:
    """Comparison form: no spaces/parentheses, first decoder's names mapped to placeholders."""
    t = re.sub(r"[\s()]", "", text)
    return t


def old_skill_names() -> dict[str, str]:
    """First decoder's skill display names ("Blight", "Iron Golem (sk132)") -> placeholder."""
    out = {}
    for line in (TXT.parent / "skill_names.txt").read_text("utf-8").splitlines()[1:]:
        ph, _sd, _key, used = line.split("	")
        out[used] = ph
    return out


def cmd_check() -> int:
    """Decode every formula and compare with the first decoder's text (in its conventions: unsigned 1-byte
    pushes, skill display names, fnN for Sigma functions). Expected, explained differences are counted
    separately; anything left under "unexplained" needs a look."""
    names = old_skill_names()
    cats: collections.Counter = collections.Counter()
    unexplained = []
    back_rules = [(r"mxl_hasstate\('st(\d+)'\)", r"fn7(\1)"), (r"mxl_itemstat\(([^,]+),'is(\d+)'\)", r"fn13(\1,\2)"),
                  (r"mxl_tabpoints\(", "fn10("), (r"mxl_skilllevel\('sk(\d+)',", r"fn5(\1,")]
    for name, ctx in FILE_CONTEXT.items():
        for idx, text, code in entries(name):
            q: list[str] = []
            mine = Renderer(ctx, unsigned_bytes=True).render(decode(code, ctx, q))
            old = re.sub(r"skill\('(.*?)'\.", lambda m: f"skill('{names.get(m.group(1), m.group(1))}'.", text)
            if q:
                cats["malformed in Median's data (decoded with Fog's rules)"] += 1
            elif normalize(mine) == normalize(old):
                cats["identical"] += 1
            else:
                back = mine
                for pat, rep in back_rules:
                    back = re.sub(pat, rep, back)
                if normalize(back) == normalize(old):
                    cats["Sigma function naming only"] += 1
                elif "mxl_statlayer" in mine and "fn11" not in text:
                    cats["first decoder truncated at fn11 (statlayer)"] += 1
                elif re.search(r"stat\(\d+,", mine):
                    cats["non-constant stat() argument in Median's data"] += 1
                else:
                    unexplained.append((name, idx, mine, text))
    total = sum(cats.values()) + len(unexplained)
    print(f"{total} formulas")
    for k, v in cats.most_common():
        print(f"  {v:6}  {k}")
    print(f"  {len(unexplained):6}  unexplained")
    for m in unexplained[:25]:
        print(f"  {m[0]}[{m[1]}]\n     mine: {m[2][:140]}\n     old:  {m[3][:140]}")
    return 1 if unexplained else 0


# table -> (formula file, context). Formula cells in the .bin hold the byte offset of their bytecode in the
# concatenated formula buffer; the column -> record offset map is found by voting (see find_fields).
FORMULA_TABLES = {"skills": ("skillscode", "skills"), "skilldesc": ("skilldesccode", "skills"),
                  "missiles": ("misscode", "missiles"), "misc": ("itemscode", "items")}
BINS = ROOT / "extract" / "mxl" / "data" / "global" / "excel"
NAMED = ROOT / "work" / "named"
OUT = ROOT / "work" / "formulas"


def read_tsv(path: Path) -> tuple[list[str], list[list[str]]]:
    with path.open(encoding="utf-8", newline="") as f:
        rows = list(csv.reader(f, delimiter="\t", quoting=csv.QUOTE_NONE))
    width = len(rows[0])
    for r in rows[1:]:
        r.extend([""] * (width - len(r)))
    return rows[0], rows[1:]


def name_resolver():
    """Median row -> D2R-side name, from work/named/idmap.json (tools/mxl_names.py)."""
    idmap = json.loads((NAMED / "idmap.json").read_text("utf-8"))
    tables = {"skill": "skills", "stat": "itemstatcost", "miss": "missiles", "state": "states"}
    missing = set()

    def ref(kind: str, row: int) -> str:
        rows = idmap[tables[kind]]
        if 0 <= row < len(rows):
            return rows[row]["name"]
        missing.add(f"{kind}{row}")
        return f"{kind}{row}"
    return ref, missing


def formula_starts(ents) -> list[int]:
    starts, pos = [], 0
    for _i, _t, code in ents:
        starts.append(pos)
        pos += len(code)
    return starts


def read_bin(table: str) -> tuple[bytes, int]:
    b = (BINS / f"{table}.bin").read_bytes()
    n = struct.unpack_from("<I", b)[0]
    return b, (len(b) - 4) // n


def find_fields(table: str, ents, starts) -> dict[str, int]:
    """Column -> byte offset inside the .bin record, by vote over rows whose first-decoder text matches a
    formula's text. A column qualifies only when every such row agrees."""
    cand = collections.defaultdict(set)
    for (_i, text, _c), st in zip(ents, starts):
        cand[text].add(st)
    b, size = read_bin(table)
    header, rows = read_tsv(TXT / f"{table}.txt")
    candidates = []  # (rows tried, column, offsets every tried row agrees on)
    for ci, col in enumerate(header):
        votes, tried = collections.Counter(), 0
        for r, row in enumerate(rows):
            t = row[ci]
            if not t or t not in cand or t.lstrip("-").isdigit():
                continue
            tried += 1
            rec = b[4 + r * size: 4 + (r + 1) * size]
            for o in range(0, size - 3):
                if struct.unpack_from("<I", rec, o)[0] in cand[t]:
                    votes[o] += 1
        full = sorted(o for o, v in votes.items() if v == tried)
        if tried >= 2 and full:
            candidates.append((tried, col, full))
    # Columns with more evidence claim their offset first; a column whose rows happen to equal another
    # column's (e.g. prgcalc2 == prgcalc1) then takes its next fully agreeing offset.
    fields, taken = {}, set()
    for _tried, col, full in sorted(candidates, key=lambda c: (-c[0], c[1])):
        free = [o for o in full if all(abs(o - t) >= 4 for t in taken)]
        if free:
            fields[col] = free[0]
            taken.add(free[0])
    return fields


def cmd_convert() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    ref, missing = name_resolver()
    summary, details = [], []
    used_unsupported = collections.Counter()
    for table, (fname, ctx) in FORMULA_TABLES.items():
        ents = entries(fname)
        starts = formula_starts(ents)
        by_start = {st: k for k, st in enumerate(starts)}
        rendered, notes = [], []
        for _idx, _text, code in ents:
            q: list[str] = []
            r = Renderer(ctx, ref)
            tree = decode(code, ctx, q)
            if q:  # malformed in Median's data: fold to what Fog actually computes
                tree = fold(tree)
            rendered.append(r.render(tree))
            notes.append((sorted(r.unsupported), q))
        lines = ["index\tformula\tmxl_functions\tquirks\tfirst_decoder_text"]
        for (idx, text, _c), out, (uns, q) in zip(ents, rendered, notes):
            lines.append(f"{idx}\t{out}\t{','.join(uns)}\t{'; '.join(q)}\t{text}")
        (OUT / f"{fname}.txt").write_text("\r\n".join(lines) + "\r\n", encoding="utf-8")

        fields = find_fields(table, ents, starts)
        b, size = read_bin(table)
        oheader, orig = read_tsv(TXT / f"{table}.txt")
        header, rows = read_tsv(NAMED / f"{table}.txt")
        changed = differs = 0
        for col, off in fields.items():
            ci, oci = header.index(col), oheader.index(col)
            for r in range(len(rows)):
                if not orig[r][oci]:
                    continue  # no formula in this cell
                start = struct.unpack_from("<I", b, 4 + r * size + off)[0]
                k = by_start.get(start)
                if k is None:
                    details.append(f"- {table} row {r} {col}: offset {start} is not a formula start")
                    continue
                differs += rows[r][ci] != rendered[k]
                rows[r][ci] = rendered[k]
                changed += 1
                uns, q = notes[k]
                for u in uns:
                    used_unsupported[(u, f"{table}.{col}")] += 1
                if q:
                    details.append(f"- quirk {table} row {r} ({rows[r][0]}) {col}: `{rendered[k]}` ({'; '.join(q)})")
        with (NAMED / f"{table}.txt").open("w", encoding="utf-8", newline="") as f:
            f.writelines("\t".join(x) + "\r\n" for x in [header, *rows])
        cols = ", ".join(f"{c}@{o}" for c, o in sorted(fields.items(), key=lambda x: x[1]))
        summary.append(f"- {table}: {len(fields)} formula columns, {changed} cells rewritten ({differs} with new text). Columns: {cols}")
    report = ["# Formula conversion report", "", *summary, "",
              "## Median-only formula functions used in tables (need a D2R implementation)", ""]
    report += [f"- {u} in {where}: {n}" for (u, where), n in sorted(used_unsupported.items())]
    report += ["", "## Details", "", *details]
    if missing:
        report += ["", f"Unresolved references: {sorted(missing)[:20]}"]
    (OUT / "report.md").write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join(report[: 6 + len(used_unsupported) + 3]))
    print(f"... {len(details)} detail lines in {OUT / 'report.md'}")
    return 0


if __name__ == "__main__":
    sys.exit({"check": cmd_check, "convert": cmd_convert}[sys.argv[1]]() if len(sys.argv) > 1 else 0)

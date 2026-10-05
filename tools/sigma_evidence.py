"""Collect field-access evidence for D2Sigma's own tables from the Ghidra decompile.

For a table with rows at *(DAT_101f32ac + ARRAY) and a given row size, finds
  * getters: functions returning  idx * ROWSIZE + *(int *)(DAT_101f32ac + ARRAY)
  * users:   functions that call a getter or index the array directly
and, inside users, the variables that hold a row pointer and every  *(type *)(var + off)  read from them.

Usage: python tools/sigma_evidence.py <array-slot hex> <rowsize> [--show N]
Prints offset -> types seen, number of functions, sample lines.
"""
import argparse
import re
from collections import defaultdict
from pathlib import Path

DECOMP = Path(__file__).resolve().parents[2] / "mxl_extracted" / "3_ghidra" / "D2Sigma_decompiled.c"


def load_functions() -> dict[str, str]:
    src = DECOMP.read_text(encoding="utf-8", errors="replace")
    parts = re.split(r"\n//==== (FUN_[0-9a-f]+) @ [0-9a-f]+\n", src)
    return dict(zip(parts[1::2], parts[2::2]))


ELEM_SIZE = {"char": 1, "byte": 1, "undefined1": 1, "short": 2, "ushort": 2, "undefined2": 2,
             "int": 4, "uint": 4, "undefined4": 4}


def add(fields, name: str, body: str, m: re.Match, off: int, typ: str, rowsize: int) -> None:
    after = body[m.end(): m.end() + 4]
    if off >= rowsize or (after.startswith(" = ") and not after.startswith(" == ")):
        return
    f = fields[off]
    f["types"][typ] += 1
    f["funcs"].add(name)
    if len(f["lines"]) < 6:
        line = body[body.rfind("\n", 0, m.start()) + 1: body.find("\n", m.end())].strip()
        f["lines"].append(f"{name} [{source_file(body)}]: {line[:150]}")


def source_file(body: str) -> str:
    m = re.search(r'"\.\.\\([\w\\]+\.cpp)"', body)
    return m.group(1).replace("\\\\", "/") if m else "?"


def evidence(funcs: dict[str, str], array: int, rowsize: int):
    index_pat = re.compile(r"(\w+) \* (?:%#x|%d) \+ \*\(int \*\)\(DAT_101f32ac \+ %#x\)" % (rowsize, rowsize, array))
    getters = {n for n, b in funcs.items() if re.search(r"return \w+ \* (?:%#x|%d) \+ \*\(int \*\)\(DAT_101f32ac \+ %#x\)" % (rowsize, rowsize, array), b)}
    fields = defaultdict(lambda: {"types": defaultdict(int), "funcs": set(), "lines": []})
    users = set()
    for name, body in funcs.items():
        rowvars = set()
        # var = idx * size + array        /   var = getter(...)
        for m in re.finditer(r"(\w+) = (?:\(\w+ \*\))?\(?\w+ \* (?:%#x|%d) \+ \*\(int \*\)\(DAT_101f32ac \+ %#x\)" % (rowsize, rowsize, array), body):
            rowvars.add(m.group(1))
        # var = (type *)(*(int *)(DAT_101f32ac + array) + idx * size)   (operands the other way round)
        for m in re.finditer(r"(\w+) = (?:\([\w ]+\*\))?\(?\*\(int \*\)\(DAT_101f32ac \+ %#x\) \+ \w+ \* (?:%#x|%d)\)?" % (array, rowsize, rowsize), body):
            rowvars.add(m.group(1))
        for g in getters:
            for m in re.finditer(r"(\w+) = (?:\([\w ]+\*\))?%s\(" % g, body):
                rowvars.add(m.group(1))
        if not rowvars:
            continue
        users.add(name)
        for v in rowvars:
            for m in re.finditer(r"\*\((\w+(?: \w+)?) \*\)\(%s \+ (0x[0-9a-f]+|\d+)\)" % re.escape(v), body):
                off = int(m.group(2), 0)
                after = body[m.end(): m.end() + 4]
                if off >= rowsize or (after.startswith(" = ") and not after.startswith(" == ")):
                    continue  # tables are read-only: a write means the variable holds something else
                f = fields[off]
                f["types"][m.group(1)] += 1
                f["funcs"].add(name)
                line = body[body.rfind("\n", 0, m.start()) + 1: body.find("\n", m.end())].strip()
                if len(f["lines"]) < 6:
                    f["lines"].append(f"{name} [{source_file(body)}]: {line[:150]}")
            # typed row pointers: *(type *)((int)var + off), var[n], *var
            decl = re.search(r"^\s*(\w+(?: \w+)?) \*%s;" % re.escape(v), body, re.M)
            elem = decl.group(1) if decl else None
            for m in re.finditer(r"\*\((\w+(?: \w+)?) \*\)\(\(int\) ?%s \+ (0x[0-9a-f]+|\d+)\)" % re.escape(v), body):
                add(fields, name, body, m, int(m.group(2), 0), m.group(1), rowsize)
            if elem in ELEM_SIZE:
                for m in re.finditer(r"\b%s\[(\d+)\]" % re.escape(v), body):
                    add(fields, name, body, m, int(m.group(1)) * ELEM_SIZE[elem], elem, rowsize)
                for m in re.finditer(r"(?<![\w)])\*%s\b(?! \+| =[^=])" % re.escape(v), body):
                    add(fields, name, body, m, 0, elem, rowsize)
            # byte at offset 0: *(char *)var / *var
            for m in re.finditer(r"\*\((\w+(?: \w+)?) \*\)%s\b(?! \+)" % re.escape(v), body):
                f = fields[0]
                f["types"][m.group(1)] += 1
                f["funcs"].add(name)
    return getters, users, fields


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("array", nargs="?", type=lambda s: int(s, 16))
    ap.add_argument("rowsize", nargs="?", type=int)
    ap.add_argument("--show", type=int, default=1)
    ap.add_argument("--func", action="append", help="print these functions' bodies")
    ap.add_argument("--slot", help="list functions that read *(DAT_101f32ac + SLOT)")
    ap.add_argument("--max-lines", type=int, default=80)
    a = ap.parse_args()
    if a.func or a.slot:
        funcs = load_functions()
        for name in a.func or []:
            body = funcs.get(name, "(not found)")
            lines = [l for l in body.splitlines() if l.strip()]
            print(f"########## {name} [{source_file(body)}] ({len(lines)} lines)")
            print("\n".join(lines[: a.max_lines]))
        if a.slot:
            needle = f"DAT_101f32ac + {a.slot})"
            for name, body in funcs.items():
                if needle in body:
                    hits = [l.strip() for l in body.splitlines() if needle in l]
                    print(f"{name} [{source_file(body)}]: {hits[0][:150]}")
        return
    getters, users, fields = evidence(load_functions(), a.array, a.rowsize)
    print(f"getters: {sorted(getters)}\nusers: {len(users)}")
    for off in sorted(fields):
        f = fields[off]
        print(f"  +{off:#04x} ({off:3}) {dict(f['types'])}  in {len(f['funcs'])} fn")
        for line in f["lines"][: a.show]:
            print(f"        {line}")


if __name__ == "__main__":
    main()

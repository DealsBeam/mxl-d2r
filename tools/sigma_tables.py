"""D2Sigma-only tables: schema, evidence and decoding.

Median ships 30 tables that only D2Sigma.dll reads (skills2, monstats3, mysticorbs, npcstores, ...).
Their .bin files are a u32 row count followed by packed fixed-size rows, and D2Sigma keeps the same layout
in memory (rows at *(DAT_101f32ac + array), count at *(DAT_101f32ac + count)).

The schema lives in research/sigma_schema.json and is edited by hand as fields get identified:
  { "<table>": { "rowsize": 44, "rows": 3895, "parallel": "monstats", "array": "0x38", "count": "0xf0",
                 "fields": [ {"off": 6, "type": "i16", "name": "SoundNormal1", "note": "..."}, ... ] } }
Types: u8 i8 u16 i16 u32 i32 str<N> (zero-terminated text in N bytes), strid (u16 string id:
table * 10000 + index, 0 = string.tbl, 1 = patchstring, 2 = expansionstring; 5382 = no string), u16x<N> (N u16 values in a
row, printed as a comma list without the zeros). Bytes not covered by a field are shown as field_0xNN (u8).
The txt column names D2Sigma registers for each table (its field descriptors: name, type, offset, link; recovered in
mxl_extracted/4_sigma_source/tables) are the names to use; "columns" on a table is its descriptor count, "col" on a field the
txt column name when the schema name differs.

Commands:
  init      create/refresh the schema skeleton (keeps existing names and notes)
  evidence  write research/sigma_evidence/<table>.md: value statistics + code accesses per field
  decode    write work/sigma/<table>.txt with named columns; parallel tables get the vanilla row's key
            (--out DIR writes somewhere else, e.g. to compare after a schema change)
Optional field keys: "ref": "<entity>" (value is a row of that entity, printed by name; a Sigma table with a "key" field,
e.g. itemclasses, prints that field), "formula": "skills2code" (value is a byte offset into that formula table, -1 = none;
printed as skills2code#<entry> plus the formula text), "bits": {"<bit>": "<name>"} (flag word: decoded as one 0/1 column per
bit, named like the txt columns).
  coverage  named txt columns / all txt columns per table (the numbers of docs/sigma-tables.md)
"""
import argparse
import json
import re
import struct
import sys
from collections import Counter
from pathlib import Path

import sigma_evidence

ROOT = Path(__file__).resolve().parent.parent
SUGMA = ROOT.parent
BINS = ROOT / "extract" / "mxl" / "data" / "global" / "excel"
RAW_DUMPS = SUGMA / "mxl_extracted" / "2_txt" / "sigma_raw_dumps"
SCHEMA = ROOT / "research" / "sigma_schema.json"
EVIDENCE = ROOT / "research" / "sigma_evidence"
IDMAP = ROOT / "work" / "named" / "idmap.json"
NAMED = ROOT / "work" / "named"

# Sigma table -> vanilla table it extends row for row.
PARALLEL = {"armor2": "armor", "weapons2": "weapons", "misc2": "misc", "skills2": "skills",
            "missiles2": "missiles", "monstats3": "monstats", "uniqueitems2": "uniqueitems",
            "objects2": "objects", "propertiesext": "properties", "itemtypesext": "itemtypes",
            "cubemainext": "cubemain", "pettypeext": "pettype", "monumodexp": "monumod",
            "charstatsexp": "charstats", "hiredescext": "hiredesc", "npcext": "npc"}
# Where D2Sigma keeps the rows (offsets into the table container at DAT_101f32ac), found from the
# bounds-checked getters in the decompile (index * rowsize + *(DAT_101f32ac + array), count check).
SLOTS = {"hiredescext": (0x14, 0xD4), "monumodexp": (0x3C, 0xF4), "skilltree": (0x80, 0x130),
         "uniqueitems2": (0x8C, 0x13C), "hireclasses": (0x10, 0xD0), "infographics": (0x1C, None), "itemclasses": (0x24, None),
         "monstats3": (0x38, 0xF0), "mysticorbs": (0x40, 0xF8), "npcactions": (0x48, 0xFC),
         "objects2": (0x58, 0x10C), "pettypeext": (0x60, None), "qualities": (0x68, 0x11C),
         "requirements": (0x74, 0x124), "skills2": (0x78, None), "skilltabs": (0x7C, None),
         "stateicons": (0x84, 0x134), "statscreen": (0x88, 0x138)}
TYPES = {"u8": "<B", "i8": "<b", "u16": "<H", "i16": "<h", "u32": "<I", "i32": "<i", "strid": "<H"}
NO_STRING = 5382
STRINGS = SUGMA / "mxl_extracted" / "2_txt" / "strings"
_string_keys: list[list[str]] = []


def string_key(sid: int) -> str:
    """Median string id -> string key ("" for none)."""
    if not _string_keys:
        for name in ("string", "patchstring", "expansionstring"):
            lines = (STRINGS / f"{name}.txt").read_text("utf-8").splitlines()
            _string_keys.append([line.split("\t", 1)[0] for line in lines])
    if sid == NO_STRING:
        return ""
    table, index = divmod(sid, 10000)
    keys = _string_keys[table] if table < 3 else []
    if index >= len(keys):
        return f"#{sid}"
    key = keys[index]
    # strings Median only looks up by ID (key "x" or empty) are renamed mxl_<id> by tools/tbl2json.py
    return f"mxl_{sid}" if key in ("x", "") else key


_formulas: dict[str, dict[int, str]] = {}


def formula_text(table: str, start: int) -> str:
    """Byte offset into <table>.bin's formula bytecode (tools/formula.py: the u32 a record holds) ->
    "<table>#<entry>: <first decoder's text>"; "" for none. Entries are listed in mxl_extracted/2_txt/<table>.txt."""
    if table not in _formulas:
        lines = (SUGMA / "mxl_extracted" / "2_txt" / f"{table}.txt").read_text("utf-8").splitlines()
        by_start, pos = {}, 0
        for k in range(0, len(lines) - 1, 2):
            idx, _, text = lines[k].partition("	")
            by_start[pos] = f"{table}#{idx}: {text}"
            pos += len(lines[k + 1].split())
        _formulas[table] = by_start
    return "" if start < 0 else _formulas[table].get(start, f"{table}@{start}?")


_ref_names: dict[str, list[str]] = {}


def ref_name(entity: str, row: int) -> str:
    """Median row of <entity> -> resolved name: idmap.json (mxl_names.py), else the first column of
    work/named/<entity>.txt for tables whose names survived (objects, ...), else the "key" column of a Sigma table."""
    if entity not in _ref_names:
        idmap = json.loads(IDMAP.read_text("utf-8")) if IDMAP.exists() else {}
        if entity in idmap:
            _ref_names[entity] = [e["name"] for e in idmap[entity]]
        elif (NAMED / f"{entity}.txt").exists():
            lines = (NAMED / f"{entity}.txt").read_text("utf-8").splitlines()[1:]
            _ref_names[entity] = [line.split("\t", 1)[0] for line in lines]
        else:  # a Sigma table whose schema names a "key" string column (itemclasses: NameStr)
            entry = json.loads(SCHEMA.read_text("utf-8")).get(entity, {})
            key = next((f for f in entry.get("fields", []) if f["name"] == entry.get("key")), None)
            _ref_names[entity] = [string_key(value(r, key)) for r in read_bin(entity)[1]] if key else []
    names = _ref_names[entity]
    return names[row] if 0 <= row < len(names) else ""


def cell(row: bytes, f: dict) -> str:
    """A field's value as text. Fields with "ref": "<entity>" print the referenced row's name
    (negative values and 0xFFFF mean none)."""
    v = value(row, f)
    if isinstance(v, tuple):
        text = ",".join((ref_name(f["ref"], x) or f"#{x}") if f.get("ref") else str(x) for x in v if x)
    elif f["type"] == "strid":
        text = string_key(v)
    elif f.get("formula") and isinstance(v, int):
        text = formula_text(f["formula"], v)
    elif f.get("ref") and isinstance(v, int):
        text = "" if v < 0 or v == 0xFFFF else ref_name(f["ref"], v) or f"#{v}"
    else:
        text = str(v)
    return text.replace("\t", " ").replace("\n", "\\n")


def size_of(t: str) -> int:
    if t.startswith("u16x"):
        return 2 * int(t[4:])
    return struct.calcsize(TYPES[t]) if t in TYPES else int(t[3:])


def read_bin(table: str) -> tuple[int, list[bytes]]:
    b = (BINS / f"{table}.bin").read_bytes()
    n = struct.unpack_from("<I", b)[0]
    size = (len(b) - 4) // n if n else 0
    return size, [b[4 + i * size: 4 + (i + 1) * size] for i in range(n)]


def value(row: bytes, f: dict):
    t, off = f["type"], f["off"]
    if t.startswith("u16x"):
        return struct.unpack_from("<%dH" % int(t[4:]), row, off)
    if t not in TYPES:
        raw = row[off: off + size_of(t)]
        return raw.split(b"\0", 1)[0].decode("latin-1")
    return struct.unpack_from(TYPES[t], row, off)[0]


def layout(fields: list[dict], rowsize: int) -> list[dict]:
    """Fields sorted by offset, with gaps filled by u8 filler fields."""
    out, pos = [], 0
    for f in sorted(fields, key=lambda f: f["off"]):
        while pos < f["off"]:
            out.append({"off": pos, "type": "u8", "name": f"field_{pos:#04x}", "filler": True})
            pos += 1
        out.append(f)
        pos = max(pos, f["off"] + size_of(f["type"]))
    while pos < rowsize:
        out.append({"off": pos, "type": "u8", "name": f"field_{pos:#04x}", "filler": True})
        pos += 1
    return out


def raw_dump_fields(table: str, rowsize: int) -> list[dict]:
    """Starting point: the first extraction's auto-typed columns (str@0, u16@4, i32@8, ...)."""
    path = RAW_DUMPS / f"{table}.txt"
    if not path.exists():
        return []
    cols = path.read_text("utf-8").split("\n", 1)[0].rstrip("\r").split("\t")[1:]
    parsed = []
    for c in cols:
        m = re.fullmatch(r"(str|u8|u16|i32)@(\d+)", c)
        if m:
            parsed.append((m.group(1), int(m.group(2))))
    fields = []
    for i, (t, off) in enumerate(parsed):
        if t == "str":
            end = parsed[i + 1][1] if i + 1 < len(parsed) else rowsize
            fields.append({"off": off, "type": f"str{end - off}"})
        elif t != "u8":
            fields.append({"off": off, "type": t})
    return fields


def cmd_init(_args) -> None:
    schema = json.loads(SCHEMA.read_text("utf-8")) if SCHEMA.exists() else {}
    for dump in sorted(RAW_DUMPS.glob("*.txt")):
        t = dump.stem
        rowsize, rows = read_bin(t)
        entry = schema.setdefault(t, {})
        entry.update({"rowsize": rowsize, "rows": len(rows)})
        if t in PARALLEL:
            entry["parallel"] = PARALLEL[t]
        if t in SLOTS:
            arr, cnt = SLOTS[t]
            entry["array"] = hex(arr)
            if cnt:
                entry["count"] = hex(cnt)
        if "fields" not in entry:
            entry["fields"] = [dict(f, name=f"{f['type']}_{f['off']:#04x}") for f in raw_dump_fields(t, rowsize)]
        for f in entry["fields"]:
            f.pop("filler", None)
    SCHEMA.parent.mkdir(parents=True, exist_ok=True)
    SCHEMA.write_text(json.dumps(schema, indent=1), encoding="utf-8")
    print(f"{len(schema)} tables -> {SCHEMA}")


def stats(rows: list[bytes], f: dict) -> str:
    vals = [value(r, f) for r in rows]
    c = Counter(vals)
    if f["type"] not in TYPES:
        return f"{len(c)} distinct, e.g. {[v for v, _ in c.most_common(4)]}"
    nz = [v for v in vals if v not in (0, -1, 0xFFFF, 0xFF, 0xFFFFFFFF)]
    rng = f"{min(nz)}..{max(nz)}" if nz else "-"
    return f"{len(c)} distinct, non-trivial range {rng}, top {c.most_common(4)}"


def cmd_evidence(args) -> None:
    schema = json.loads(SCHEMA.read_text("utf-8"))
    funcs = sigma_evidence.load_functions()
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    for t, entry in schema.items():
        if args.table and t not in args.table:
            continue
        rowsize, rows = read_bin(t)
        code = {}
        getters, users = set(), set()
        if "array" in entry:
            getters, users, code = sigma_evidence.evidence(funcs, int(entry["array"], 16), rowsize)
        lines = [f"# {t}", "", f"rows {len(rows)}, row size {rowsize}"
                 + (f", extends `{entry['parallel']}` row for row" if "parallel" in entry else "")
                 + (f", memory: array +{entry['array']}" if "array" in entry else "")
                 + (f" count +{entry['count']}" if "count" in entry else ""),
                 f"getters: {sorted(getters) or '-'}; functions using rows: {len(users)}", "",
                 "| off | type | name | values | code |", "|---|---|---|---|---|"]
        offsets_seen = set()
        for f in layout(entry["fields"], rowsize):
            if f.get("filler") and all(r[f["off"]] == 0 for r in rows) and f["off"] not in code:
                continue
            offsets_seen.add(f["off"])
            ev = code.get(f["off"])
            ev_text = (f"{dict(ev['types'])} in {len(ev['funcs'])} fn" if ev else "")
            lines.append(f"| {f['off']:#04x} | {f['type']} | {f['name']} | {stats(rows, f)} | {ev_text} |")
        lines += ["", "## Code accesses", ""]
        for off in sorted(code):
            ev = code[off]
            lines.append(f"- `+{off:#04x}` {dict(ev['types'])}")
            lines += [f"  - `{l}`" for l in ev["lines"]]
        (EVIDENCE / f"{t}.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"evidence -> {EVIDENCE}")


def bit_columns(f: dict) -> list[tuple[int, str]]:
    """(bit, column name) of a flag word's named bits."""
    return [(int(bit), name) for bit, name in f["bits"].items()]


def vanilla_keys(table: str) -> list[str]:
    """Row keys of the vanilla table a Sigma table extends, after name resolution (work/named)."""
    path = NAMED / f"{table}.txt"
    if not path.exists():
        return []
    lines = path.read_text("utf-8").splitlines()[1:]
    header = path.read_text("utf-8").split("\n", 1)[0].split("\t")
    key = 0
    for i, h in enumerate(header):
        if h.lower() in ("code", "name", "index", "skill", "missile", "id", "uniquemod", "type", "class", "hireling", "pettype", "npc", "description"):
            key = i
            break
    return [l.split("\t")[key] for l in lines]


def cmd_decode(args) -> None:
    schema = json.loads(SCHEMA.read_text("utf-8"))
    out = Path(args.out) if args.out else ROOT / "work" / "sigma"
    out.mkdir(parents=True, exist_ok=True)
    for t, entry in schema.items():
        rowsize, rows = read_bin(t)
        fields = [f for f in layout(entry["fields"], rowsize)
                  if not (f.get("filler") and all(r[f["off"]] == 0 for r in rows))]
        keys = vanilla_keys(entry["parallel"]) if "parallel" in entry else []
        header = ["*row"] + ([f"*{entry['parallel']}"] if "parallel" in entry else [])
        for f in fields:
            header += [b for _, b in sorted(bit_columns(f))] if f.get("bits") else [f["name"]]
        lines = ["\t".join(header)]
        for i, r in enumerate(rows):
            cells = [str(i)] + ([keys[i] if i < len(keys) else ""] if "parallel" in entry else [])
            for f in fields:
                if f.get("bits"):
                    v = value(r, f)
                    cells += [str((v >> bit) & 1) for bit, _ in sorted(bit_columns(f))]
                else:
                    cells.append(cell(r, f))
            lines.append("\t".join(cells))
        (out / f"{t}.txt").write_text("\r\n".join(lines) + "\r\n", encoding="utf-8", newline="")
    print(f"{len(schema)} tables -> {out}")


def cmd_coverage(_args) -> None:
    """Named txt columns / all txt columns (the "columns" count of the schema = the field descriptors D2Sigma registers) per
    table, plus the bytes that are non-zero in some row and not covered by a field (a "done" table has none)."""
    schema = json.loads(SCHEMA.read_text("utf-8"))
    total_named = total = 0
    print("| Table | Rows | Row size | Named | Unnamed non-zero bytes |\n|---|---|---|---|---|")
    for t, entry in sorted(schema.items(), key=lambda kv: -kv[1]["rows"] * kv[1]["rowsize"]):
        rowsize, rows = read_bin(t)
        covered = set()
        named = 0
        for f in entry["fields"]:
            covered.update(range(f["off"], f["off"] + size_of(f["type"])))
            named += len(f["bits"]) if f.get("bits") else 1
        columns = entry.get("columns", named)
        loose = [o for o in range(rowsize) if o not in covered and any(r[o] for r in rows)]
        total_named += min(named, columns)
        total += columns
        print(f"| {t} | {len(rows)} | {rowsize} | {min(named, columns)}/{columns} | "
              f"{', '.join(f'{o:#04x}' for o in loose) or '-'} |")
    print(f"\n{total_named}/{total} columns named")


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("init")
    e = sub.add_parser("evidence")
    e.add_argument("table", nargs="*")
    sub.add_parser("coverage")
    d = sub.add_parser("decode")
    d.add_argument("--out", help="directory for the decoded tables (default work/sigma)")
    args = ap.parse_args()
    {"init": cmd_init, "evidence": cmd_evidence, "decode": cmd_decode, "coverage": cmd_coverage}[args.cmd](args)
    return 0


if __name__ == "__main__":
    sys.exit(main())

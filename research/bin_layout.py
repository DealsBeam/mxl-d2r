"""Map a D2R excel .bin record layout from its .txt: for each txt column, the offsets whose values equal the column
(numbers) or are non-zero exactly where the column is non-empty (compiled formulas, string refs).
Usage: python research/bin_layout.py skills [columns...]"""
import csv, re, struct, sys
from pathlib import Path

EXCEL = Path(__file__).resolve().parent.parent / "extract" / "d2r" / "93847" / "data" / "global" / "excel"
name = sys.argv[1]
want = set(sys.argv[2:])
rows = list(csv.reader(open(EXCEL / f"{name}.txt", encoding="latin1"), delimiter="\t", quoting=csv.QUOTE_NONE))
h, rows = rows[0], [r for r in rows[1:] if r and r[0] and r[0] != "Expansion"]
data = (EXCEL / f"{name}.bin").read_bytes()
n = struct.unpack_from("<I", data)[0]
size = (len(data) - 4) // n
print(f"{n} records of {size} bytes; txt rows {len(rows)}")
recs = [data[4 + i * size: 4 + (i + 1) * size] for i in range(min(n, len(rows)))]
def index(table, key):
    t = list(csv.reader(open(EXCEL / f"{table}.txt", encoding="latin1"), delimiter="	", quoting=csv.QUOTE_NONE))
    k = t[0].index(key)
    return {r[k]: i for i, r in enumerate(r for r in t[1:] if r and r[0] and r[0] != "Expansion")}


mis, mon = index("missiles", "Missile"), index("monstats", "Id")
REFS = {c: mis for c in h if re.match(r"(srv|clt)missile[abc]?$", c)}
REFS.update(summon=mon)
sts = index("states", "state")
REFS.update({c: sts for c in ("aurastate", "auratargetstate", "passivestate")})
fmts = {"<i": 4, "<h": 2, "<H": 2, "<b": 1, "<B": 1}
for ci, col in enumerate(h):
    if want and col not in want:
        continue
    vals = [r[ci] if ci < len(r) else "" for r in rows[:len(recs)]]
    if not any(vals):
        continue
    hits = []
    refs = REFS.get(col)
    if refs:
        vals = [str(refs[v]) if v in refs else ("-1" if v == "" else "?") for v in vals]
        vals = ["-1" if v == "?" else v for v in vals]
    numeric = all(v == "" or v.lstrip("-").isdigit() for v in vals) and any(v not in ("", "0") for v in vals)
    for off in range(size):
        for f, w in fmts.items():
            if off + w > size:
                continue
            got = [struct.unpack_from(f, rec, off)[0] for rec in recs]
            if numeric and all(g == int(v or 0) for g, v in zip(got, vals)):
                hits.append(f"0x{off:x}{f[1]}")
            elif not numeric and f == "<i" and all((g != 0) == (v not in ("", "0")) for g, v in zip(got, vals)):
                hits.append(f"0x{off:x}nz")
            elif not numeric and f == "<i" and all((g != -1) == (v not in ("",)) for g, v in zip(got, vals)):
                hits.append(f"0x{off:x}ne-1")
    print(f"{col}: {' '.join(hits[:6]) or '-'}")

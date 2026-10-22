r"""Print the Ghidra decompile of the function that contains an address.

  py -3 tools/sigma_fn.py sigma 0x100a4e50            D2Sigma.dll (base 0x10000000, build Sep 21 2026)
  py -3 tools/sigma_fn.py D2Game 0x6fc73470           1.13c D2Game.dll (VA, or --rva 0x53470 for module base + rva)
  py -3 tools/sigma_fn.py D2Common 0x6fd8c59c --max 200

The modules are the files of `..\mxl_extracted\3_ghidra\<module>.dll.c` (D2Sigma: `D2Sigma_decompiled.c`). Each
function is headed `//==== <name> @ <hex address>`. Also shows the source file attribution of the function
(`..\mxl_extracted\4_sigma_source\symbols.csv`, `5_game_source\<module>\symbols.csv`).
"""
import argparse
import bisect
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EX = ROOT.parent / "mxl_extracted"
HEAD = re.compile(r"^//==== (\S+) @ ([0-9a-fA-F]+)$", re.M)
_cache: dict[str, tuple[str, list[int], list[tuple[int, int, str]]]] = {}


def load(mod: str):
    if mod in _cache:
        return _cache[mod]
    name = "D2Sigma_decompiled.c" if mod.lower() == "sigma" else f"{mod}.dll.c"
    text = (EX / "3_ghidra" / name).read_text("utf-8", errors="replace")
    heads = [(int(m.group(2), 16), m.start(), m.group(1)) for m in HEAD.finditer(text)]
    heads.sort()
    _cache[mod] = (text, [h[0] for h in heads], heads)
    return _cache[mod]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("module", help="sigma, or a 1.13c module (D2Game, D2Client, D2Common, D2Win, D2Lang, Fog, ...)")
    ap.add_argument("addr", type=lambda s: int(s, 16))
    ap.add_argument("--rva", action="store_true", help="addr is an RVA of the module")
    ap.add_argument("--max", type=int, default=120, help="max lines printed")
    a = ap.parse_args()
    text, starts, heads = load(a.module)
    addr = a.addr
    if a.rva:
        addr += starts[0] & ~0xFFFF
    i = bisect.bisect_right(starts, addr) - 1
    if i < 0:
        raise SystemExit("address below the first function")
    a0, off, name = heads[i]
    end = heads[i + 1][1] if i + 1 < len(heads) else len(text)
    lines = text[off:end].rstrip().splitlines()
    sym = EX / "4_sigma_source" / "symbols.csv" if a.module.lower() == "sigma" else None
    src = ""
    if sym is None:
        d = {"D2Gfx": "D2gfx", "D2Sound": "D2sound"}.get(a.module, a.module)
        sym = EX / "5_game_source" / d / "symbols.csv"
    if sym.exists():
        for r in csv.DictReader(sym.open(encoding="utf-8")):
            if int(r["address"], 16) == a0:
                src = f'{r["new_name"]}  {r["file"]} [{r["confidence"]}]'
                break
    print(f"// {a.module} {name} @ {a0:#x}  (query {addr:#x}, +{addr - a0:#x})  {src}  [{len(lines)} lines]")
    print("\n".join(lines[: a.max]))
    if len(lines) > a.max:
        print(f"... {len(lines) - a.max} more lines (--max)")


if __name__ == "__main__":
    main()

"""Recover D2Sigma.dll's table of game-function pointers (DAT_101f1xxx etc.).

D2Sigma fills each pointer at startup with a tiny stub:
    push <offset | -ordinal> ; push <module index> ; call resolve(0x10042870) ; mov [ptr], eax ; ret
resolve() loads module <index> from the name table at 0x101d4014 (12-byte entries) and returns
base + offset (or GetProcAddress by ordinal when the value is negative).

This tool scans D2Sigma.dll for those stubs, reads the module names, and labels every pointer with the
1.13c function at that address from the Ghidra decompiles (Ordinal_NNNN for exported functions).

Output: research/sigma_imports.json  {"0x101f1124": {"module": "D2Common.dll", "offset": "0x6b300",
                                                     "va": "0x6fdbb300", "function": "Ordinal_10786"}}
Usage:  python tools/sigma_imports.py [--dll C:\\Games\\median-xl\\D2Sigma.dll]
"""
import argparse
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GHIDRA = ROOT.parent / "mxl_extracted" / "3_ghidra"
RESOLVE = 0x10042870
MODULE_TABLE = 0x101d4014


class PE:
    def __init__(self, data: bytes):
        self.b = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        nsec = struct.unpack_from("<H", data, pe + 6)[0]
        optsz = struct.unpack_from("<H", data, pe + 20)[0]
        self.base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
        self.secs = []
        for i in range(nsec):
            o = pe + 24 + optsz + i * 40
            vsz, va, rsz, raw = struct.unpack_from("<IIII", data, o + 8)
            self.secs.append((data[o:o + 8].rstrip(b"\0").decode(), self.base + va, vsz, raw, rsz))

    def va_to_off(self, va: int) -> int | None:
        for _n, sva, vsz, raw, rsz in self.secs:
            if sva <= va < sva + max(vsz, rsz):
                return raw + (va - sva) if va - sva < rsz else None
        return None

    def off_to_va(self, off: int) -> int | None:
        for _n, sva, _vsz, raw, rsz in self.secs:
            if raw <= off < raw + rsz:
                return sva + (off - raw)
        return None

    def cstr(self, va: int) -> str:
        o = self.va_to_off(va)
        return self.b[o:self.b.index(b"\0", o)].decode("latin-1") if o is not None else ""


def decompile_symbols(module: str) -> tuple[int, dict[int, str]]:
    """(image base, address -> function name) from the 1.13c Ghidra decompile of <module>."""
    path = GHIDRA / f"{module}.c"
    if not path.exists():
        return 0, {}
    names = {}
    for m in re.finditer(r"^//==== (\S+) @ ([0-9a-f]+)$", path.read_text("utf-8", errors="replace"), re.M):
        names[int(m.group(2), 16)] = m.group(1)
    base = (min(names) & ~0xFFFF) if names else 0
    return base, names


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default=r"C:\Games\median-xl\D2Sigma.dll")
    ap.add_argument("--out", type=Path, default=ROOT / "research" / "sigma_imports.json")
    args = ap.parse_args()
    pe = PE(Path(args.dll).read_bytes())

    modules = []
    for i in range(0x17):  # resolve() rejects index > 0x16
        name_ptr = struct.unpack_from("<I", pe.b, pe.va_to_off(MODULE_TABLE + i * 12))[0]
        modules.append(pe.cstr(name_ptr) if name_ptr else "")

    stub = re.compile(rb"\x68(.{4})\x6a(.)\xe8(.{4})\xa3(.{4})\xc3", re.S)
    symbols: dict[str, tuple[int, dict[int, str]]] = {}
    out = {}
    for m in stub.finditer(pe.b):
        va = pe.off_to_va(m.start())
        call_at = va + 1 + 4 + 2  # address of the call instruction
        if call_at + 5 + struct.unpack("<i", m.group(3))[0] != RESOLVE:
            continue
        value = struct.unpack("<i", m.group(1))[0]
        idx = m.group(2)[0]
        ptr = struct.unpack("<I", m.group(4))[0]
        module = modules[idx] if idx < len(modules) else f"module{idx}"
        entry = {"module": module}
        if value < 0:
            entry["ordinal"] = -value
        else:
            entry["offset"] = hex(value)
            stem = Path(module).name
            if stem not in symbols:
                symbols[stem] = decompile_symbols(stem)
            base, names = symbols[stem]
            if base:
                entry["va"] = hex(base + value)
                entry["function"] = names.get(base + value, "?")
        out[hex(ptr)] = entry
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(dict(sorted(out.items())), indent=1), encoding="utf-8")

    by_mod: dict[str, list] = {}
    for e in out.values():
        by_mod.setdefault(e["module"], []).append(e)
    print("modules:", [m for m in modules if m])
    for mod, es in sorted(by_mod.items()):
        named = sum(1 for e in es if e.get("function", "?") != "?" or "ordinal" in e)
        print(f"  {mod:14} {len(es):4} pointers, {named} matched to a decompiled function or ordinal")
    print(f"{len(out)} pointers -> {args.out}")


if __name__ == "__main__":
    main()

r"""List every run-time patch D2Sigma.dll applies to the original 1.13c game DLLs.

D2Sigma's patcher (FUN_10044750, called per module from FUN_10043d60 at start-up) takes ecx = module index
(name table at 0x101d4014), edx = process handle, stack = table pointer. A table is a dword header followed by
5-dword records {rva, size, value, relative, module}, ended by a record whose rva is 0. Each record is one write at
module base + rva: size 0 = a dword `value` (a 32-bit constant or pointer, or the single byte 0xe9 / 0xe8 of an
opcode followed by its own record), size N = N bytes filled with the low byte of `value` (0x90 nops, or one
byte like 0xeb); `relative` = the dword written is the rel32 `value - (address + 4)`, i.e. the operand of a
call/jmp redirected to D2Sigma code.

This tool groups the raw writes into logical patches (jmp hook, call redirect, pointer, constant, nop, byte),
names the 1.13c function that contains each site (Ghidra export `mxl_extracted/3_ghidra`, file attribution
`5_game_source/<module>/symbols.csv`), disassembles the original instruction from the installed 1.13c DLL, and
names the D2Sigma function it points to (`4_sigma_source/symbols.csv`).

Output: research/sigma_patches.json (all logical patches) and research/sigma_patches.tsv.
Usage:  py -3 tools/sigma_patches.py [--dll C:\Games\median-xl\D2Sigma.dll] [--game C:\Games\median-xl]
"""
import argparse
import bisect
import csv
import json
import re
import struct
from collections import Counter
from pathlib import Path

import capstone

from sigma_imports import MODULE_TABLE, PE

ROOT = Path(__file__).resolve().parent.parent
EXTRACT = ROOT.parent / "mxl_extracted"

# (module index, table VA) in the order FUN_10043d60 passes them; the last one only when `-noglide` is not given
CALLS = [(8, 0x10170fb8), (3, 0x101d4130), (5, 0x10173bb8), (10, 0x10175c90), (0x12, 0x101d7ff0), (0xc, 0x10175f28),
         (0x13, 0x10176128), (0x16, 0x101761a0), (0x10, 0x101761c8), (0xe, 0x101774a4), (0xd, 0x101748a0),
         (9, 0x10175eb0), (7, 0x10176020), (6, 0x10177418), (0x15, 0x10176070), (2, 0x101760b0), (0xf, 0x10176370),
         (0x11, 0x10177328), (0xb, 0x10175f90)]
SRC_DIR = {"Bnclient.dll": "Bnclient", "BnClient.dll": "Bnclient", "D2Game.dll": "D2Game", "D2Client.dll": "D2Client",
           "D2Common.dll": "D2Common", "D2Gfx.dll": "D2gfx", "D2Win.dll": "D2Win", "D2Lang.dll": "D2Lang",
           "Fog.dll": "Fog", "Storm.dll": "Storm", "D2Net.dll": "D2Net", "D2MCPClient.dll": "D2MCPClient",
           "D2Launch.dll": "D2Launch", "D2Gdi.dll": "D2Gdi", "D2Direct3D.dll": "D2Direct3D", "D2DDraw.dll": "D2DDraw",
           "D2Multi.dll": "D2Multi", "D2Sound.dll": "D2sound", "D2Glide.dll": "D2Glide", "SmackW32.dll": ""}


class Syms:
    """Function starts of one module: address -> (ghidra name, new name, source file)."""

    def __init__(self, module: str):
        d = SRC_DIR.get(module, "")
        self.starts: list[int] = []
        self.info: dict[int, tuple[str, str, str]] = {}
        self.exports: dict[int, int] = {}
        self.base = 0
        if not d:
            return
        p = EXTRACT / "5_game_source" / d / "symbols.csv"
        for r in csv.DictReader(p.open(encoding="utf-8")):
            a = int(r["address"], 16)
            self.info[a] = (r["ghidra_name"], r["new_name"], r["file"])
        e = EXTRACT / "5_game_source" / d / "exports.tsv"
        if e.exists():
            for line in e.read_text("utf-8").splitlines()[1:]:
                f = line.split("\t")
                self.exports[int(f[1], 16)] = int(f[0])
        self.starts = sorted(self.info)
        if self.starts:
            self.base = self.starts[0] & ~0xFFFF

    def at(self, va: int) -> tuple[int, str, str] | None:
        i = bisect.bisect_right(self.starts, va) - 1
        if i < 0:
            return None
        s = self.starts[i]
        g, n, f = self.info[s]
        return s, (f"export#{self.exports[s]} " if s in self.exports else "") + n, f


def sigma_syms() -> tuple[list[int], dict[int, tuple[str, str]]]:
    info = {}
    for r in csv.DictReader((EXTRACT / "4_sigma_source" / "symbols.csv").open(encoding="utf-8")):
        info[int(r["address"], 16)] = (r["new_name"], r["file"])
    return sorted(info), info


def imports_of(pe: PE) -> dict[int, str]:
    """IAT slot VA -> 'Dll#ordinal' or 'Dll!name' for a game DLL."""
    b = pe.b
    pe_off = struct.unpack_from("<I", b, 0x3C)[0]
    rva = struct.unpack_from("<I", b, pe_off + 24 + 104)[0]
    out = {}
    if not rva:
        return out
    o = pe.va_to_off(pe.base + rva)
    while o is not None:
        ilt, _t, _f, name, iat = struct.unpack_from("<5I", b, o)
        if not name:
            break
        dll = pe.cstr(pe.base + name)
        k = pe.va_to_off(pe.base + (ilt or iat))
        slot = pe.base + iat
        while True:
            v = struct.unpack_from("<I", b, k)[0]
            if not v:
                break
            if v & 0x80000000:
                out[slot] = f"{dll}#{v & 0xFFFF}"
            else:
                out[slot] = f"{dll}!{pe.cstr(pe.base + v + 2)}"
            k += 4
            slot += 4
        o += 20
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default=r"C:\Games\median-xl\D2Sigma.dll")
    ap.add_argument("--game", default=r"C:\Games\median-xl")
    ap.add_argument("--out", type=Path, default=ROOT / "research" / "sigma_patches.json")
    a = ap.parse_args()
    sg = PE(Path(a.dll).read_bytes())
    names = []
    for i in range(0x17):
        p = struct.unpack_from("<I", sg.b, sg.va_to_off(MODULE_TABLE + i * 12))[0]
        names.append(sg.cstr(p) if p else "")
    s_starts, s_info = sigma_syms()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    sig_imports = json.loads((ROOT / "research" / "sigma_imports.json").read_text("utf-8"))

    def sname(t: int) -> str:
        i = bisect.bisect_right(s_starts, t) - 1
        s0 = s_starts[i]
        return s_info[s0][0] if s0 == t else f"{s_info[s0][0]}+{t - s0:#x}"

    def stub_of(t: int) -> dict:
        """What a naked hook stub calls inside D2Sigma, which game code it resumes at, its size."""
        o = sg.va_to_off(t)
        calls, back, n = [], "", 0
        for ins in md.disasm(sg.b[o:o + 0x100], t):
            n = ins.address + ins.size - t
            if ins.mnemonic == "call":
                tok = ins.op_str
                if tok.startswith("0x"):
                    calls.append(sname(int(tok, 16)))
                elif tok in ("eax", "ecx", "edx", "ebx", "esi", "edi", "ebp"):
                    calls.append(f"indirect {tok}")
                else:
                    e = sig_imports.get(tok.split("[")[-1].rstrip("]"))
                    calls.append(f"{e['module']}+{e.get('offset', '')}" if e else tok)
            elif ins.mnemonic == "jmp" and "[" in ins.op_str:
                e = sig_imports.get(ins.op_str.split("[")[-1].rstrip("]"))
                back = f"{e['module']}+{e.get('offset', '')}" if e else ins.op_str
                break
            elif ins.mnemonic == "ret" or ins.mnemonic == "int3":
                break
        return {"calls": calls, "resume": back, "bytes": n}

    patches = []
    for idx, tva in CALLS:
        mod = names[idx]
        off = sg.va_to_off(tva)
        recs = []
        o = off + 4
        while True:
            rva, size, val, rel, _m = struct.unpack_from("<5I", sg.b, o)
            if rva == 0:
                break
            recs.append((rva, size, val, rel))
            o += 20
        if not recs:
            continue
        game = PE(Path(a.game, mod).read_bytes())
        syms = Syms(mod)
        base = syms.base or game.base
        iat = imports_of(game)
        by_rva = {}
        for r in recs:
            by_rva.setdefault(r[0], []).append(r)
        used = set()

        def ctx(va: int):
            """The original instruction that contains va (linear disassembly from the enclosing function)."""
            f = syms.at(va)
            start = f[0] if f and va - f[0] < 0x4000 else va
            o2 = game.va_to_off(start)
            if o2 is None:
                return ""
            for ins in md.disasm(game.b[o2:o2 + (va - start) + 16], start):
                if ins.address <= va < ins.address + ins.size:
                    txt = f"{ins.mnemonic} {ins.op_str}".strip()
                    return f"{ins.address:#x}: {txt}"
                if ins.address > va:
                    break
            return ""

        def sigma_target(t: int):
            if not (0x10000000 <= t < 0x10210000):
                return None
            i = bisect.bisect_right(s_starts, t) - 1
            if i < 0:
                return None
            s = s_starts[i]
            n, fl = s_info[s]
            r = {"va": hex(t), "func": n if s == t else f"{n}+{t - s:#x}", "file": fl}
            if 0x1009b4b0 <= t < 0x1009be50:  # block of tiny asm stubs (naked hooks); name what each one calls
                r["stub"] = stub_of(t)
            return r

        for rva, size, val, rel in sorted(recs):
            if (rva, size, val, rel) in used:
                continue
            va = base + rva
            rec = {"module": mod, "rva": hex(rva), "va": hex(va), "func113": None, "src113": None}
            f = syms.at(va)
            if f:
                rec["func113"] = f"{f[1]} @{f[0]:#x}"
                rec["src113"] = f[2]
            rec["sect"] = next((n for n, sva, vsz, _r, rsz in game.secs if sva <= va < sva + max(vsz, rsz)), "?")
            rec["orig"] = ctx(va)
            if size == 0 and val in (0xe9, 0xe8) and rel == 0:
                nxt = [r for r in by_rva.get(rva + 1, []) if r[3]]
                if nxt:
                    t = nxt[0][2]
                    nops = [r for r in by_rva.get(rva, []) if r[1] > 1 and r[2] == 0x90909090]
                    rec.update(kind="jmp" if val == 0xe9 else "call", target=sigma_target(t),
                               nop=nops[0][1] if nops else 0)
                    used.add((rva, size, val, rel))
                    used.add(nxt[0])
                    used.update(nops)
                    patches.append(rec)
                    continue
            if rel:
                # operand of an existing call/jmp: the instruction starts one byte (call/jmp) earlier
                rec["kind"] = "callfix"
                rec["target"] = sigma_target(val)
                rec["orig"] = ctx(va - 1) or rec["orig"]
                m = re.match(r"\S+: call (0x[0-9a-f]+)$", rec["orig"] or "")
                m2 = re.match(r"\S+: call dword ptr \[(0x[0-9a-f]+)\]$", rec["orig"] or "")
                if m:
                    tv = int(m.group(1), 16)
                    g = syms.at(tv)
                    rec["orig_callee"] = (f"{g[1]} @{g[0]:#x}" if g else hex(tv))
                elif m2:
                    rec["orig_callee"] = iat.get(int(m2.group(1), 16), m2.group(1))
            elif size > 0:
                rec["kind"] = "nop" if val & 0xff == 0x90 else "byte"
                rec["size"] = size
                rec["value"] = hex(val & 0xff)
            else:
                t = sigma_target(val)
                if t:
                    rec["kind"] = "ptr"
                    rec["target"] = t
                else:
                    rec["kind"] = "const"
                    rec["value"] = hex(val)
            used.add((rva, size, val, rel))
            patches.append(rec)
    a.out.write_text(json.dumps(patches, indent=1), encoding="utf-8")
    with a.out.with_suffix(".tsv").open("w", encoding="utf-8", newline="") as fh:
        w = csv.writer(fh, delimiter="\t", lineterminator="\n")
        w.writerow(["module", "rva", "kind", "func113", "src113", "orig", "orig_callee", "size_value_nop", "target_va",
                    "target_func", "target_file", "stub_calls", "stub_resume"])
        for p in patches:
            t = p.get("target") or {}
            w.writerow([p["module"], p["rva"], p["kind"], p["func113"], p["src113"], p["orig"], p.get("orig_callee", ""),
                        f'{p.get("size", "")}/{p.get("value", "")}/{p.get("nop", "")}', t.get("va", ""),
                        t.get("func", ""), t.get("file", ""),
                        ";".join((t.get("stub") or {}).get("calls", [])), (t.get("stub") or {}).get("resume", "")])
    c = Counter((p["module"], p["kind"]) for p in patches)
    for (m, k), n in sorted(c.items()):
        print(f"{m:16} {k:8} {n}")
    print(len(patches), "logical patches ->", a.out)


if __name__ == "__main__":
    main()

"""Placeholder monster graphics until Median's art is ported (Phase 5).

D2R draws a monster from two things: the classic animation set named by monstats.txt Code (COF files at
data/global/monsters/<code>/cof/<code><mode><weaponclass>.cof, used even in HD for timing) and the HD model that
data/hd/character/monsters.json assigns to the monstats Id. Median uses 254 graphics codes D2R does not have,
and D2R crashes on a missing COF ("Could not add cof file") and has no HD model for Median's monster Ids.

Phase 5: tools/install_mod.py ships Median's own monster animations (COF + DCC, files D2R lacks) and
tools/animdata_merge.py its animdata records. A monster whose Code or modes D2R lacks but Median's files complete keeps
its own Code and monstats2 row (Median's timing; Median's art in classic mode) and only borrows a D2R monster's HD
model in monsters.json. Monsters Median's files do not complete either (a few codes) still borrow a D2R monster's
graphics: its Code and its monstats2 visual columns (sizes, parts, modes, light...).
The donor is a D2R monster with the same MonType, else a zombie. monsters.json maps every monstats Id to the HD model
of its donor or of the Code it ends up with.

Writes into work/mod/data (after txt_migrate.py) and work/mod/gfx_placeholders.md.
Usage: python tools/gfx_placeholders.py
"""
import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from txt_migrate import read_tsv, write_tsv, is_record  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "work" / "mod" / "data"
D2R = ROOT / "extract" / "d2r" / "93847"
GAME = r"C:\Program Files (x86)\Diablo II Resurrected"
FILELIST = D2R / "filelist.txt"
MONSTERS_JSON = D2R / "data" / "hd" / "character" / "monsters.json"
MODES = ["DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "RN"]  # KB, SQ reuse others
KEEP = {"Id", "*hcIdx", "*eol"}  # monstats2 columns that stay the row's own
FALLBACK = "zombie1"
MXL_MONSTERS = ROOT / "assets" / "median" / "data" / "global" / "monsters"
NOT_DONORS = re.compile(r"^gorgon\d")  # enabled in monstats but never spawned in D2R


def d2r_files() -> tuple[set[str], dict]:
    """Classic COF names in D2R's CASC and D2R's monsters.json (both cached under extract/d2r)."""
    if not FILELIST.exists() or not MONSTERS_JSON.exists():
        import native
        storage = native.open_casc(GAME)
        if not FILELIST.exists():
            names = [f.replace("\\", "/").lower() for f, _, _ in native.casc_files(storage, "*")]
            FILELIST.write_text("\n".join(names), encoding="utf-8")
        if not MONSTERS_JSON.exists():
            MONSTERS_JSON.parent.mkdir(parents=True, exist_ok=True)
            MONSTERS_JSON.write_bytes(native.casc_read(storage, "data:data/hd/character/monsters.json"))
    cofs = set()
    for line in FILELIST.read_text("utf-8").splitlines():
        m = re.search(r"data/global/monsters/[^/]+/cof/([^/]+)\.cof$", line)
        if m:
            cofs.add(m.group(1))
    return cofs, json.loads(MONSTERS_JSON.read_text("utf-8-sig"))


def missing_cofs(code: str, h2: list[str], row2: list[str], cofs: set[str]) -> list[str]:
    w = row2[h2.index("BaseW")].lower() or "hth"
    # death modes fall back to the hand-to-hand animation (D2R's goatmen and fetishes rely on it)
    return [f"{code}{m}{w}".lower() for m in MODES
            if f"m{m}" in h2 and row2[h2.index(f"m{m}")] == "1" and f"{code}{m}{w}".lower() not in cofs
            and not (m in ("DT", "DD") and f"{code}{m}hth".lower() in cofs)]


def main() -> int:
    cofs, mj = d2r_files()
    mxl_cofs = {p.stem.lower() for p in MXL_MONSTERS.rglob("*.cof")} if MXL_MONSTERS.exists() else set()
    all_cofs = cofs | mxl_cofs
    hd = dict(mj)
    report = ["# Placeholder monster graphics", "",
              "Monsters below borrow a D2R monster's graphics (Code + monstats2 visuals) until Median's art is "
              "ported.", ""]
    for bank in ("base", ""):
        d = OUT / "global" / "excel" / bank
        vd = D2R / "data" / "global" / "excel" / bank
        if not (d / "monstats.txt").exists():
            continue
        h, rows = read_tsv(d / "monstats.txt")
        h2, rows2 = read_tsv(d / "monstats2.txt")
        vh, vrows = read_tsv(vd / "monstats.txt")
        vh2, vrows2 = read_tsv(vd / "monstats2.txt")
        ci, ti, xi = h.index("Code"), h.index("MonType"), h.index("MonStatsEx")
        ms2 = {r[0]: r for r in rows2 if is_record(r)}
        vms2 = {r[0]: r for r in vrows2 if is_record(r)}
        # D2R monsters whose own graphics are complete, as donors; the HD model of each Code
        model = {}
        donors: dict[str, list[str]] = {}
        for r in vrows:
            if not is_record(r) or r[0] not in mj:
                continue
            code, ex = r[vh.index("Code")], r[vh.index("MonStatsEx")]
            model.setdefault(code.lower(), mj[r[0]])
            if (ex in vms2 and code and r[vh.index("enabled")] == "1" and not NOT_DONORS.match(r[0])
                    and not missing_cofs(code, vh2, vms2[ex], cofs)):
                donors.setdefault(r[vh.index("MonType")], r)
                donors.setdefault("*" + r[0], r)
        fallback = donors["*" + FALLBACK]
        replaced: dict[str, list[str]] = {}  # monstats2 Id -> donor monstats row
        changed = Counter()
        own_art = Counter()  # (Median code, donor of the HD model) -> monsters keeping their own animations
        for r in rows:
            if not is_record(r) or not r[ci]:
                continue
            row2 = ms2.get(r[xi])
            if row2 is None:
                continue
            if r[xi] in replaced:
                donor = replaced[r[xi]]
            elif not missing_cofs(r[ci], h2, row2, cofs):
                continue
            elif not missing_cofs(r[ci], h2, row2, all_cofs):
                donor = donors.get(r[ti], fallback)
                hd[r[0]] = mj[donor[0]]
                own_art[(r[ci], donor[0])] += 1
                continue
            else:
                donor = donors.get(r[ti], fallback)
                dex = vms2[donor[vh.index("MonStatsEx")]]
                for c in h2:
                    if c not in KEEP and c in vh2:
                        row2[h2.index(c)] = dex[vh2.index(c)]
                replaced[r[xi]] = donor
            changed[(r[ci], donor[vh.index("Code")], donor[0])] += 1
            r[ci] = donor[vh.index("Code")]
        for r in rows:
            if is_record(r) and r[0] and r[ci].lower() in model:
                hd[r[0]] = model[r[ci].lower()]
        write_tsv(d / "monstats.txt", h, rows)
        write_tsv(d / "monstats2.txt", h2, rows2)
        report += [f"## Bank {bank or 'rotw'}: {sum(own_art.values())} monsters keep Median's animations "
                   f"({len(own_art)} code/model pairs), HD model borrowed", "",
                   "| Median code | HD model of | Monsters |", "|---|---|---|"]
        report += [f"| {a} | {b} | {n} |" for (a, b), n in sorted(own_art.items())]
        report += ["", f"Still borrowing graphics: {sum(changed.values())} monsters, {len(replaced)} monstats2 rows", "",
                   "| Median code | Placeholder code | Donor | Monsters |", "|---|---|---|---|"]
        report += [f"| {a} | {b} | {c} | {n} |" for (a, b, c), n in sorted(changed.items())]
        report.append("")
    target = OUT / "hd" / "character" / "monsters.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(hd, indent=2), encoding="utf-8")
    (OUT.parent / "gfx_placeholders.md").write_text("\n".join(report), encoding="utf-8")
    print(f"monsters.json: {len(hd)} Ids ({len(hd) - len(mj)} added); report -> work/mod/gfx_placeholders.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())

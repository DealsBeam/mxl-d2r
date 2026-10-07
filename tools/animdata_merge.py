"""Merge Median's animdata.d2 into D2R's (Phase 5: monster animations).

animdata.d2 holds one record per animation (COF name: token + mode + weapon class): frames per direction, speed and
the per-frame event table (attack/missile/sound frames). It is read in HD mode too, for gameplay timing.
Format: 256 buckets (bucket = sum of the upper-case name's characters % 256), each a uint32 count then 160-byte
records: char name[8], uint32 frames, uint32 speed, uint8 events[144].

Merge: D2R's records stay the base (its Warlock and other 3.x additions, 103 records Median lacks). Median's own
animations (its new monster codes and modes, about 2,500) are added. Where both have a record, Median's wins when only
the speed/events differ (Median's attack-speed balance, about 245 records); when the frame count differs, D2R's record
stays, so it keeps matching D2R's COF and HD animation. eanimdata.d2 (D2R's own extra table) is left alone.

Input:  extract/d2r/93847/data/global/animdata.d2, extract/mxl/data/global/animdata.d2
Output: work/mod/data/global/animdata.d2, work/mod/animdata_report.md
Usage: python tools/animdata_merge.py
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
D2R = ROOT / "extract" / "d2r" / "93847" / "data" / "global" / "animdata.d2"
MXL = ROOT / "extract" / "mxl" / "data" / "global" / "animdata.d2"
OUT = ROOT / "work" / "mod" / "data" / "global" / "animdata.d2"
REPORT = ROOT / "work" / "mod" / "animdata_report.md"
RECORD = 160


def bucket(name: bytes) -> int:
    return sum(name.split(b"\0")[0].upper()) % 256


def parse(data: bytes) -> dict[bytes, bytes]:
    """Upper-case name -> 160-byte record, in file order."""
    recs, o = {}, 0
    for _blk in range(256):
        count = struct.unpack_from("<I", data, o)[0]
        o += 4
        for _ in range(count):
            rec = data[o:o + RECORD]
            recs[rec[:8].split(b"\0")[0].upper()] = rec
            o += RECORD
    if o != len(data):
        raise SystemExit(f"animdata: {len(data) - o} trailing bytes")
    return recs


def main() -> int:
    if not D2R.exists():
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import native
        D2R.parent.mkdir(parents=True, exist_ok=True)
        D2R.write_bytes(native.casc_read(native.open_casc(r"C:\Program Files (x86)\Diablo II Resurrected"),
                                         "data:data/global/animdata.d2"))
    d2r, mxl = parse(D2R.read_bytes()), parse(MXL.read_bytes())
    merged = dict(d2r)
    added, retimed, kept = [], [], []
    for name, rec in mxl.items():
        if name not in d2r:
            merged[name] = rec
            added.append(name)
        elif rec[8:] != d2r[name][8:]:
            if rec[8:12] == d2r[name][8:12]:  # same frame count: Median's speed and events
                merged[name] = d2r[name][:8] + rec[8:]
                retimed.append(name)
            else:
                kept.append(name)
    buckets: list[list[bytes]] = [[] for _ in range(256)]
    for name, rec in merged.items():
        buckets[bucket(rec[:8])].append(rec)
    out = bytearray()
    for b in buckets:
        out += struct.pack("<I", len(b)) + b"".join(b)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(bytes(out))
    report = ["# animdata merge", "",
              f"- D2R records: {len(d2r)}; Median records: {len(mxl)}; merged: {len(merged)}",
              f"- Added from Median: {len(added)}",
              f"- Median timing taken (same frame count): {len(retimed)}",
              f"- D2R kept (frame count differs): {len(kept)}: {', '.join(n.decode() for n in sorted(kept))}",
              f"- D2R-only kept: {sum(1 for n in d2r if n not in mxl)}"]
    REPORT.write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join("   " + line for line in report[2:]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

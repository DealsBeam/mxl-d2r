"""Terror zones (D2R's "desecrated zones") off for the mod, for now (owner, 2026-10-21; docs/PLAN.md "Removed for now").

D2R schedules them offline from data/hd/global/excel/desecratedzones.json ("Corrupted tremors strike ..." messages,
boosted monster level, experience and drops). Every difficulty of every game version has ladder_enabled /
non_ladder_enabled flags; this writes D2R's file with all of them false into the mod, so no zone is ever active.

Usage: python tools/terror_zones.py   (reads extract/d2r/93847, writes work/mod/data/hd/global/excel/desecratedzones.json)
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "extract" / "d2r" / "93847" / "data" / "hd" / "global" / "excel" / "desecratedzones.json"
DST = ROOT / "work" / "mod" / "data" / "hd" / "global" / "excel" / "desecratedzones.json"


def disable(node, counts: dict) -> None:
    if isinstance(node, dict):
        for key in ("ladder_enabled", "non_ladder_enabled"):
            if key in node:
                counts[key] = counts.get(key, 0) + (1 if node[key] else 0)
                node[key] = False
        for value in node.values():
            disable(value, counts)
    elif isinstance(node, list):
        for value in node:
            disable(value, counts)


def main() -> int:
    if not SRC.exists():
        print(f"missing {SRC}: run python tools/casc_extract.py --set meta")
        return 1
    # D2R's file holds a UInt64 seed; json keeps Python ints exact
    data = json.loads(SRC.read_text(encoding="utf-8"))
    counts: dict = {}
    disable(data, counts)
    DST.parent.mkdir(parents=True, exist_ok=True)
    DST.write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")
    print(f"   terror zones off: {counts.get('ladder_enabled', 0)} ladder + {counts.get('non_ladder_enabled', 0)} "
          f"non-ladder difficulty flags cleared -> {DST.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

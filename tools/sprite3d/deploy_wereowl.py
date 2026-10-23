#!/usr/bin/env python
"""Development shortcut: run the Wereowl build step against a game slot's installed mod and copy its output back into the slot.

  py -3 tools/sprite3d/deploy_wereowl.py <slot 1..3> [--assets DIR]     (env MXL_RUN = the name that holds the slot's lock)

A full `tools/build.py` + `install_mod.py` takes long; the Wereowl step (tools/wereowl_models.py) only touches the files
of the unused vanilla units `horse`, `wargoatman` and `pig`, the two unit JSONs and `monsters.json`. This seeds a scratch
`work/mod` with the slot's own `monsters.json` and Corrupted Wereowl unit, runs the step and copies exactly those files
back. It stops when the slot's lock is not held by MXL_RUN (the rule: never touch a slot you do not hold). Not a
substitute for `install_mod.py` before a release.
"""
import json
import os
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import game_slots  # noqa: E402
import wereowl_models  # noqa: E402


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 1
    slot = int(a[0])
    me = os.environ.get("MXL_RUN", "")
    info = game_slots.lock_info(slot)
    if not info or not info[1] or info[1].split()[0] != me:
        print("slot %d: the lock is not held by MXL_RUN=%r (%s): stopping" % (slot, me, info))
        return 2
    data = Path(game_slots.mod_dir(slot)) / "MedianXL.mpq" / "data"
    hdc_slot = data / "hd" / "character"
    hdc = wereowl_models.ROOT / "work" / "mod" / "data" / "hd" / "character"
    (hdc / "enemy").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(hdc_slot / "monsters.json", hdc / "monsters.json")
    mj = json.loads((hdc / "monsters.json").read_text("utf-8"))
    corrupted = mj["corrupted_wereowl"] + ".json"
    shutil.copyfile(hdc_slot / "enemy" / corrupted, hdc / "enemy" / corrupted)
    rc = wereowl_models.main()
    if rc:
        return rc
    for rel in ("enemy/horse", "enemy/wargoatman", "enemy/pig"):
        src, dst = hdc / rel, hdc_slot / rel
        shutil.copytree(src, dst, dirs_exist_ok=True)
    for rel in ("enemy/mxl_wereowl3d.json", "enemy/" + corrupted, "monsters.json"):
        shutil.copyfile(hdc / rel, hdc_slot / rel)
    print("slot %d: Wereowl files copied" % slot)
    return 0


if __name__ == "__main__":
    sys.exit(main())

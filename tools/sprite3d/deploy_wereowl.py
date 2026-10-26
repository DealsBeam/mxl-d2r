#!/usr/bin/env python
"""Development shortcut: run the Wereowl build step against a game slot's installed mod and copy its output back into the slot.

  py -3 tools/sprite3d/deploy_wereowl.py <slot 1..3> [--assets DIR]     (env MXL_RUN = the name that holds the slot's lock)

A full `tools/build.py` + `install_mod.py` takes long; the Wereowl step (tools/wereowl_models.py) only touches the files at
`data/hd/character/mxl/{wereowl,corrupted_wereowl}` (with MXL_OLD_PATHS=1 the vanilla units `horse`, `wargoatman`, `pig`), the two
index files `data/hd/model_lod_desc.bin` / `texture_desc_cache.json`, the two unit JSONs and `monsters.json`. This seeds a scratch
`work/mod` with the slot's own `monsters.json` and Corrupted Wereowl unit, runs the step (the indexes start from the game's) and copies exactly
those files back. It stops when the slot's lock is not held by MXL_RUN (the rule: never touch a slot you do not hold). Not a
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

INDEXES = ("model_lod_desc.bin", "texture_desc_cache.json")


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
    data = Path(game_slots.mod_dir(slot)) / "D2R-XL.mpq" / "data"
    hd_slot = data / "hd"
    hdc_slot = hd_slot / "character"
    hd = wereowl_models.ROOT / "work" / "mod" / "data" / "hd"
    hdc = hd / "character"
    (hdc / "enemy").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(hdc_slot / "monsters.json", hdc / "monsters.json")
    mj = json.loads((hdc / "monsters.json").read_text("utf-8"))
    corrupted = mj["corrupted_wereowl"] + ".json"
    # the Corrupted's unit JSON from an earlier deploy already names its model files: back to the goatman1 donor's, the step patches it again
    unit_txt = (hdc_slot / "enemy" / corrupted).read_text("utf-8")
    for patched in ("enemy/wargoatman/torso.model", "mxl/corrupted_wereowl/torso.model"):
        unit_txt = unit_txt.replace("data/hd/character/" + patched, "data/hd/character/enemy/goatman1/torso.model")
    unit = json.loads(unit_txt)
    for kind in ("models", "textures"):  # dependencies on files of an earlier deploy (they may be gone or differ now)
        unit["dependencies"][kind] = [d for d in unit["dependencies"][kind] if "/mxl/" not in d["path"] and "/pig/" not in d["path"]]
    (hdc / "enemy" / corrupted).write_text(json.dumps(unit, separators=(",", ":")), encoding="utf-8")
    for rel in ("mxl/wereowl", "mxl/corrupted_wereowl"):  # files of earlier deploys that this one does not write
        shutil.rmtree(hdc_slot / rel, ignore_errors=True)
        shutil.rmtree(hdc / rel, ignore_errors=True)
    for n in INDEXES:  # start from the game's indexes: the slot's carry entries (and stale ones) of earlier deploys
        (hd / n).unlink(missing_ok=True)
        (hd_slot / n).unlink(missing_ok=True)
    rc = wereowl_models.main()
    if rc:
        return rc
    for rel in ("enemy/horse", "enemy/wargoatman", "enemy/pig", "mxl/wereowl", "mxl/corrupted_wereowl"):
        if (hdc / rel).exists():
            shutil.copytree(hdc / rel, hdc_slot / rel, dirs_exist_ok=True)
    for n in INDEXES:
        if (hd / n).exists():
            shutil.copyfile(hd / n, hd_slot / n)
    for rel in ("enemy/mxl_wereowl3d.json", "enemy/" + corrupted, "monsters.json"):
        shutil.copyfile(hdc / rel, hdc_slot / rel)
    print("slot %d: Wereowl files copied" % slot)
    return 0


if __name__ == "__main__":
    sys.exit(main())

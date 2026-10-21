"""Run the whole data pipeline in order (after the one-time extractions mpq_extract.py and casc_extract.py).

  mxl_names.py       names + IDs              -> work/named/ (+ idmap.json)
  formula.py convert formula cells            -> work/named/ (in place)
  sigma_tables.py    D2Sigma-only tables      -> work/sigma/
  tbl2json.py        strings                  -> work/mod/data/local/lng/
  txt_migrate.py     D2R table layout         -> work/mod/data/global/excel/{base,}
  cube_tables.py     cube conditions          -> plugin/src/mxl_cube.h (+ cubemain stat ids, ext item conditions)
  sigma_data.py      requirements, dungeon lockout and goblin data -> plugin/src/mxl_{requirements,dungeons,goblins}.h
  items_data.py      runeword properties 8-13 -> plugin/src/mxl_runewords.h
  npc_actions_data.py  npcext menu slots + npcactions -> plugin/src/mxl_npc_actions.h (string ids resolved through tbl2json output)
  npc_services_data.py npcext service flags + requirement rows -> plugin/src/mxl_npc_services.h
  gfx_placeholders.py  D2R graphics for Median-only monsters (until the art port) + hd/character/monsters.json
  animdata_merge.py  merge Median animation data into D2R's animdata
  hd_skill_anims.py  HD skill animation data for Median skills
  missile_hd.py      HD models for Median's missiles (by CelFile) -> hd/missiles/missiles.json
  item_hd.py         HD inventory pictures for Median's items/uniques/sets -> hd/items/*.json; also converts Median-owned item art via item_art.py (bug #18)
  skill_icons.py     Median's skill icons as D2R icon sheets (classic + HD) and skilldesc IconCel
  ui_panels.py       Median panel art as HD panel backgrounds (inventory, cube)
  skill_tree_art.py  Median skill tree look: tab titles, stone, arrows per class and tab (HD + classic)
  ui_layouts.py      UI layout overrides (8-tab skill tree, 15x10 inventory and cube)
  validate.py        D2R schema checks        -> work/mod/validate_report.md

Usage: python tools/build.py   (exit code = validate.py's: 1 if there are errors)
"""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STEPS = [["mxl_names.py"], ["formula.py", "convert"], ["sigma_tables.py", "decode"], ["tbl2json.py"],
         ["txt_migrate.py"], ["cube_tables.py"], ["sigma_data.py"], ["items_data.py"], ["npc_actions_data.py"], ["npc_services_data.py"], ["gfx_placeholders.py"], ["animdata_merge.py"], ["hd_skill_anims.py"], ["missile_hd.py"], ["item_hd.py"], ["skill_icons.py"], ["ui_panels.py"], ["skill_tree_art.py"], ["ui_layouts.py"],
         ["validate.py"]]


def run(step: list[str]) -> int:
    print(f"== {' '.join(step)}", flush=True)
    r = subprocess.run([sys.executable, str(ROOT / "tools" / step[0]), *step[1:]], cwd=ROOT,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print("\n".join(f"   {line}" for line in r.stdout.strip().splitlines()[-4:]))
    return r.returncode


def main() -> int:
    for d in (ROOT / "work" / "named", ROOT / "work" / "mod"):
        shutil.rmtree(d, ignore_errors=True)
    # Names and formulas: formula.py exits 3 when formulas exceed 255 characters and it asked for compact names
    # (work/shorten.json); repeat until stable.
    for _attempt in range(4):
        rc = run(STEPS[0]) or run(STEPS[1])
        if rc != 3:
            break
        shutil.rmtree(ROOT / "work" / "named", ignore_errors=True)
    if rc:
        print(f"step failed ({rc})")
        return rc
    for step in STEPS[2:]:
        rc = run(step)
        if rc and step[0] != "validate.py":
            print(f"step failed ({rc})")
            return rc
    return rc


if __name__ == "__main__":
    sys.exit(main())

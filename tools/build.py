"""Run the whole data pipeline in order (after the one-time extractions mpq_extract.py and casc_extract.py).

  mxl_names.py       names + IDs              -> work/named/ (+ idmap.json)
  formula.py convert formula cells            -> work/named/ (in place)
  sigma_tables.py    D2Sigma-only tables      -> work/sigma/
  tbl2json.py        strings                  -> work/mod/data/local/lng/
  txt_migrate.py     D2R table layout         -> work/mod/data/global/excel/{base,}
  art_fix.py         missile CelFiles and overlay Filenames that exist nowhere -> the closest existing art (both banks)
  waypoints_data.py  actinfo.txt (waypoint panel slots for Median's extra waypoints) -> work/mod, plugin/src/mxl_waypoints.h
  monster_ai_data.py checks the plugin's monster AI id (andariel_ai = monai row 155, served by plugin/src/monster_ai.cpp)
  cube_tables.py     cube conditions          -> plugin/src/mxl_cube.h (+ cubemain stat ids, ext item conditions)
  sigma_data.py      requirements, dungeon lockout and goblin data -> plugin/src/mxl_{requirements,dungeons,goblins}.h
  items_data.py      runeword properties 8-13 -> plugin/src/mxl_runewords.h
  npc_actions_data.py  npcext menu slots + npcactions -> plugin/src/mxl_npc_actions.h (string ids resolved through tbl2json output)
  npc_services_data.py npcext service flags + requirement rows -> plugin/src/mxl_npc_services.h
  gfx_placeholders.py  D2R graphics for Median-only monsters (until the art port) + hd/character/monsters.json
  art_provenance.py  Median's missile/overlay/monster DCCs matched to D2R's own by image (exact/recolour/rescale/mashup) -> work/mod/art_provenance.json (cached in work/art_provenance/; first run about 2 minutes)
  hd_monsters.py     donor, scale and colour per Median-only monster (HD units) -> hd/character
  wereowl_models.py  the generated 3D Wereowl as the Druid form and the Corrupted Wereowl (needs the local assets; MXL_NO_3D=1 skips) -> hd/character
  animdata_merge.py  merge Median animation data into D2R's animdata
  hd_skill_anims.py  HD skill animation data for Median skills
  missile_hd.py      HD models for Median's missiles (by CelFile) -> hd/missiles/missiles.json
  effects_hd.py      every Median missile/overlay row -> exact D2R HD effect, scaled variant or closest stand-in; overlay.txt Character -> hd/missiles, hd/overlays, work/mod/effects_hd.md
  terror_zones.py    D2R terror zones off for now (all difficulty flags false in desecratedzones.json)
  (missile_art.py    Median's own missile/overlay animations as HD flipbook effects; not a step until confirmed in game,
                     docs/hd-effects.md; run it by hand)
  item_hd.py         HD inventory pictures for Median's items/uniques/sets -> hd/items/*.json; also converts Median-owned item art via item_art.py (bug #18)
  skill_icons.py     Median's skill icons as D2R icon sheets (classic + HD) and skilldesc IconCel
  state_icons.py     D2Sigma's buff / debuff icon bar -> plugin/src/mxl_state_icons.h (+ the git-ignored mxl_state_icon_art.h)
  ui_panels.py       Median panel art as HD sprites and classic DC6 panels
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
         ["txt_migrate.py"], ["art_fix.py"], ["waypoints_data.py"], ["monster_ai_data.py"], ["cube_tables.py"], ["sigma_data.py"], ["items_data.py"], ["npc_actions_data.py"], ["npc_services_data.py"], ["gfx_placeholders.py"], ["art_provenance.py"], ["hd_monsters.py"], ["wereowl_models.py"], ["animdata_merge.py"], ["hd_skill_anims.py"], ["missile_hd.py"], ["effects_hd.py"], ["item_hd.py"], ["skill_icons.py"], ["state_icons.py"], ["ui_panels.py"], ["skill_tree_art.py"], ["ui_layouts.py"], ["terror_zones.py"],
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

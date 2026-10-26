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
  unique_limits_data.py  uniqueitems2 carry limits -> plugin/src/mxl_unique_limits.h
  stat_names_data.py  stat names of the built itemstatcost.txt -> plugin/src/mxl_stat_names.h (harness `item fireresist=155`)
  npc_actions_data.py  npcext menu slots + npcactions -> plugin/src/mxl_npc_actions.h (string ids resolved through tbl2json output)
  npc_services_data.py npcext service flags + requirement rows -> plugin/src/mxl_npc_services.h
  gfx_placeholders.py  D2R graphics for Median-only monsters (until the art port) + hd/character/monsters.json
  art_provenance.py  Median's missile/overlay/monster DCCs matched to D2R's own by image (exact/recolour/rescale/mashup) -> work/mod/art_provenance.json (cached in work/art_provenance/; first run about 2 minutes)
  hd_monsters.py     donor, scale and colour per Median-only monster (HD units) -> hd/character
  wereowl_models.py  the generated 3D Wereowl as the Druid form and the Corrupted Wereowl (assets/hd/wereowl from the asset pack; MXL_NO_3D=1 skips) -> hd/character
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

Before the steps it checks that assets/ (Median XL's files, HD art) is the pack pinned in assets.lock (tools/asset_pack.py):
a missing or different assets/ stops the build (exit 2) and says how to fetch it; a missing one is fetched here when a local
source is given (MXL_ASSETS) or with --fetch-assets (download); an existing but different one is never replaced by the build.
MXL_SKIP_ASSET_CHECK=1 builds with a modified assets/ (a pack in the making), with a warning.

Usage: python tools/build.py [--fetch-assets]   (exit code = validate.py's: 1 if there are errors)
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STEPS = [["mxl_names.py"], ["formula.py", "convert"], ["sigma_tables.py", "decode"], ["tbl2json.py"],
         ["txt_migrate.py"], ["item_special_data.py"], ["art_fix.py"], ["waypoints_data.py"], ["monster_ai_data.py"], ["cube_tables.py"], ["sigma_data.py"], ["items_data.py"], ["unique_limits_data.py"], ["stat_names_data.py"], ["npc_actions_data.py"], ["npc_services_data.py"], ["gfx_placeholders.py"], ["art_provenance.py"], ["hd_monsters.py"], ["wereowl_models.py"], ["animdata_merge.py"], ["hd_skill_anims.py"], ["missile_hd.py"], ["effects_hd.py"], ["item_hd.py"], ["skill_icons.py"], ["state_icons.py"], ["ui_panels.py"], ["skill_tree_art.py"], ["ui_layouts.py"], ["terror_zones.py"],
         ["validate.py"]]


def run(step: list[str]) -> int:
    print(f"== {' '.join(step)}", flush=True)
    r = subprocess.run([sys.executable, str(ROOT / "tools" / step[0]), *step[1:]], cwd=ROOT,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print("\n".join(f"   {line}" for line in r.stdout.strip().splitlines()[-4:]))
    return r.returncode


def ensure_assets(download: bool) -> int:
    """0 when assets/ is the pinned pack; otherwise fetch it if allowed, else say what to do. Never builds without it."""
    sys.path.insert(0, str(ROOT / "tools"))
    import asset_pack
    ok, why = asset_pack.assets_ok()
    if ok:
        print(f"== assets/: {why}", flush=True)
        return 0
    if os.environ.get("MXL_SKIP_ASSET_CHECK") == "1":
        print(f"WARNING: assets/ does not match assets.lock ({why}); building anyway because MXL_SKIP_ASSET_CHECK=1", flush=True)
        return 0
    print(f"== assets/ is not the pack pinned in assets.lock: {why}", flush=True)
    present = asset_pack.ASSETS.exists()
    if (os.environ.get("MXL_ASSETS") or download) and not present:
        if asset_pack.main(["fetch"]) == 0:
            return 0
    else:
        hint = ("assets/ exists, so add --force to replace it (changes in it are lost)." if present
                else "or run `tools/build.py --fetch-assets` to download it now.")
        if present and not (asset_pack.ASSETS / asset_pack.MANIFEST).exists():
            print("   assets/ from before the pack (a Git LFS checkout)? If it is the pinned pack: py -3 tools/asset_pack.py adopt")
        print("   fetch it:  py -3 tools/asset_pack.py fetch              (downloads the pack named in assets.lock)")
        print("              py -3 tools/asset_pack.py fetch --from PATH   (a local pack file, folder or unpacked tree; or set MXL_ASSETS)")
        print(f"   {hint} docs/setup.md has the details.")
    return 2


def main() -> int:
    rc = ensure_assets("--fetch-assets" in sys.argv[1:])
    if rc:
        return rc
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

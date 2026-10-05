"""Install the generated mod into D2R's mods folder as a D2RLoader-ready folder mod.

<game>/mods/MedianXL/
  MedianXL.mpq/modinfo.json         {"name": "MedianXL", "savepath": "MedianXL/"}  (own save folder; D2R reads it
                                    inside the folder MPQ; a copy also sits at the mod root)
  d2rloader/metadata.json           schemaVersion 1, no "namespace" (native string loading, see docs/strings.md)
  d2rloader/config/d2rloader.toml   [d2rcore.save_format] mode = "extended" (Median's wider item/stat IDs)
  MedianXL.mpq/data/...             copy of work/mod/data (folder-style MPQ) + global/dataversionbuild.txt
                                    + Median's classic tiles/maps (global/tiles)

Only MedianXL.mpq/data is replaced on reinstall; saves live elsewhere (Saved Games) and are never touched.
Usage: python tools/install_mod.py [--game "C:\\Program Files (x86)\\Diablo II Resurrected"] [--name MedianXL]
"""
import argparse
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "work" / "mod" / "data"
DEFAULT_GAME = r"C:\Program Files (x86)\Diablo II Resurrected"
BUILD = ROOT / "extract" / "d2r" / "93847" / "data" / "global" / "dataversionbuild.txt"
MXL_TILES = ROOT / "extract" / "mxl" / "data" / "global" / "tiles"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--name", default="MedianXL")
    args = ap.parse_args()

    if not (SRC / "global" / "excel").exists():
        print("work/mod/data is missing: run tools/build.py first")
        return 1
    game = Path(args.game)
    if not (game / "D2R.exe").exists() or not (game / "D2RLoader.exe").exists():
        print(f"{game} does not look like a D2R folder with D2RLoader installed")
        return 1

    mod = game / "mods" / args.name
    mpq = mod / f"{args.name}.mpq"
    data = mpq / "data"
    if data.exists():
        shutil.rmtree(data)
    shutil.copytree(SRC, data)
    shutil.copy2(BUILD, data / "global" / "dataversionbuild.txt")
    # Classic map tiles and presets (.dt1/.ds1): D2R loads them for map layout even in HD mode, and Median's
    # level types point at its own (e.g. tiles/mxl/quest/yshari/basewall.dt1). Note: Median's .ds1 maps still use
    # Median's object numbers; objects 573+ were renumbered (docs/txt-migrate.md) and need remapping later.
    shutil.copytree(MXL_TILES, data / "global" / "tiles")

    # D2R (and D2RLoader's mod detection) read modinfo.json inside the folder-style MPQ; the copy at the mod
    # root follows the D2RLoader guide's layout and is harmless.
    modinfo = json.dumps({"name": args.name, "savepath": f"{args.name}/"}, indent=2)
    for target in (mpq / "modinfo.json", mod / "modinfo.json"):
        target.write_text(modinfo, encoding="utf-8")
    d2rl = mod / "d2rloader"
    (d2rl / "config").mkdir(parents=True, exist_ok=True)
    (d2rl / "plugins").mkdir(exist_ok=True)
    (d2rl / "patches").mkdir(exist_ok=True)
    meta = {"schemaVersion": 1,
            "metadata": {"name": "Median XL (D2R port)", "modVersion": "0.1.0-dev", "author": "mxl-d2r",
                         "description": "Work-in-progress port of Median XL to D2R. Data only; plugins pending.",
                         "website": ""}}
    (d2rl / "metadata.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    toml = d2rl / "config" / "d2rloader.toml"
    if not toml.exists():  # never overwrite a config someone edited
        toml.write_text('[d2rcore.save_format]\nmode = "extended"\n', encoding="utf-8")

    files = sum(1 for _ in data.rglob("*") if _.is_file())
    size = sum(p.stat().st_size for p in data.rglob("*") if p.is_file()) / 1e6
    print(f"installed {files} files ({size:.0f} MB) -> {mod}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

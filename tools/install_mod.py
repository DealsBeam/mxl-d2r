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
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "work" / "mod" / "data"
DEFAULT_GAME = os.environ.get("MXL_D2R_GAME", r"C:\Program Files (x86)\Diablo II Resurrected")  # see game_ctl.py
BUILD = ROOT / "extract" / "d2r" / "93847" / "data" / "global" / "dataversionbuild.txt"
MXL_GLOBAL = ROOT / "extract" / "mxl" / "data" / "global"
MXL_TILES = MXL_GLOBAL / "tiles"
PLUGIN = ROOT / "build" / "plugin" / "Release" / "mxl-core.dll"
D2R_FILELIST =ROOT / "extract" / "d2r" / "93847" / "filelist.txt"  # written by gfx_placeholders.py


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--name", default="MedianXL")
    ap.add_argument("--slot", help="game slot 1..3 or 'all' (tools/game_slots.py); overrides --game")
    ap.add_argument("--savepath", help="save folder name written to modinfo.json (default <name>/, slots: <name>-sN/)")
    args = ap.parse_args()

    if not (SRC / "global" / "excel").exists():
        print("work/mod/data is missing: run tools/build.py first")
        return 1
    import game_slots
    if not args.slot:
        n = game_slots.slot_of_game_dir(args.game)  # --game / MXL_D2R_GAME pointing at a slot keeps its save folder
        default = f"{args.name}/" if n in (None, 1) else f"{args.name}-s{n}/"
        return install_one(Path(args.game), args.name, args.savepath or default)
    slots = [n for n in game_slots.DEFAULTS if (Path(game_slots.slot_dir(n)) / "D2RLoader.exe").exists()]         if args.slot == "all" else [int(args.slot)]
    rc, first = 0, None
    for n in slots:
        savepath = args.savepath or (f"{args.name}/" if n == 1 else f"{args.name}-s{n}/")
        print(f"== slot {n}: {game_slots.slot_dir(n)} (saves {savepath})")
        rc |= install_one(Path(game_slots.slot_dir(n)), args.name, savepath, first)
        first = first or Path(game_slots.slot_dir(n)) / "mods" / args.name / f"{args.name}.mpq" / "data"
        if n != 1:
            game_slots.setup(n)  # d2rloader.toml of the main install, own save folder
    return rc


def install_one(game: Path, name: str, savepath: str, copy_data_from: Path = None) -> int:
    """Install into one game folder. copy_data_from: the data folder of an install made a moment ago (skips the
    ds1 renumbering and the file merging, copies the finished tree)."""
    if not (game / "D2R.exe").exists() or not (game / "D2RLoader.exe").exists():
        print(f"{game} does not look like a D2R folder with D2RLoader installed")
        return 1

    mod = game / "mods" / name
    mpq = mod / f"{name}.mpq"
    data = mpq / "data"
    if data.exists():
        shutil.rmtree(data)
    if copy_data_from is not None:
        shutil.copytree(copy_data_from, data)
        return finish_install(game, name, savepath, mod, mpq, data)
    shutil.copytree(SRC, data)
    shutil.copy2(BUILD, data / "global" / "dataversionbuild.txt")
    # Classic map tiles and presets (.dt1/.ds1): D2R loads them for map layout even in HD mode, and Median's
    # level types point at its own (e.g. tiles/mxl/quest/yshari/basewall.dt1). Note: Median's .ds1 maps still use
    # Median's object numbers; objects 573+ were renumbered (docs/txt-migrate.md) and need remapping later.
    shutil.copytree(MXL_TILES, data / "global" / "tiles")
    # The maps name monsters and objects by Median's row numbers; D2R kept its own added rows, so Median's rows from
    # 734 (monstats) / 573 (objects) moved. Renumber them in the installed copy (tools/ds1_remap.py).
    from ds1_remap import remap_tree
    s = remap_tree(data / "global" / "tiles")
    print("ds1 ids: " + ", ".join(f"{k}={v}" for k, v in sorted(s.items())))
    # HD scenes for the maps D2R has none for (docs/maps-hd.md): closest vanilla biome, no new art.
    import maps_hd
    maps_hd.build(data, game)
    # Median-only missile and overlay animations (.dcc): D2R crashes on a missing classic graphics file. Median-only
    # inventory pictures (items/*.dc6, the invfile/uniqueinvfile/setinvfile names): classic mode (G) draws items from
    # them. Files D2R already has are left alone so its own effects and items keep matching their HD versions.
    d2r_files = set(D2R_FILELIST.read_text("utf-8").splitlines()) if D2R_FILELIST.exists() else set()
    # Monsters (Phase 5): Median's own animations (COF + DCC); gfx_placeholders.py lets monsters these complete keep
    # their own Code (HD still shows a borrowed model), and animdata_merge.py adds their timing.
    for sub in ("missiles", "overlays", "items", "monsters"):
        for p in (MXL_GLOBAL / sub).rglob("*"):
            rel = p.relative_to(MXL_GLOBAL.parent.parent).as_posix().lower()  # data/global/...
            if p.is_file() and f"data:{rel}" not in d2r_files:
                dest = data / "global" / p.relative_to(MXL_GLOBAL)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(p, dest)
    return finish_install(game, name, savepath, mod, mpq, data)


def finish_install(game: Path, name: str, savepath: str, mod: Path, mpq: Path, data: Path) -> int:
    # D2R (and D2RLoader's mod detection) read modinfo.json inside the folder-style MPQ; the copy at the mod
    # root follows the D2RLoader guide's layout and is harmless.
    modinfo = json.dumps({"name": name, "savepath": savepath}, indent=2)
    for target in (mpq / "modinfo.json", mod / "modinfo.json"):
        target.write_text(modinfo, encoding="utf-8")
    d2rl = mod / "d2rloader"
    (d2rl / "config").mkdir(parents=True, exist_ok=True)
    (d2rl / "plugins").mkdir(exist_ok=True)
    (d2rl / "patches").mkdir(exist_ok=True)
    meta = {"schemaVersion": 1,
            "metadata": {"name": "Median XL (D2R port)", "modVersion": "0.1.0-dev", "author": "mxl-d2r",
                         "description": "Work-in-progress port of Median XL to D2R.",
                         "website": ""}}
    (d2rl / "metadata.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    # The mxl-core plugin (plugin/, built by tools/build_plugin.ps1) loads only with this mod (mod-scoped)
    if PLUGIN.exists():
        shutil.copy2(PLUGIN, d2rl / "plugins" / PLUGIN.name)
        print(f"plugin {PLUGIN.name} ({PLUGIN.stat().st_size // 1024} KB)")
    else:
        print(f"no plugin at {PLUGIN} (run tools/build_plugin.ps1); installing data only")
    toml = d2rl / "config" / "d2rloader.toml"
    if not toml.exists():  # never overwrite a config someone edited
        toml.write_text('[d2rcore.save_format]\nmode = "extended"\n', encoding="utf-8")

    files = sum(1 for _ in data.rglob("*") if _.is_file())
    size = sum(p.stat().st_size for p in data.rglob("*") if p.is_file()) / 1e6
    print(f"installed {files} files ({size:.0f} MB) -> {mod}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

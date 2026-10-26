"""Install the generated mod into D2R's mods folder as a D2RLoader-ready folder mod.

<game>/mods/MedianXL/
  MedianXL.mpq/modinfo.json         {"name": "MedianXL", "savepath": "MedianXL/"}  (own save folder; D2R reads it
                                    inside the folder MPQ; a copy also sits at the mod root)
  d2rloader/metadata.json           schemaVersion 1 + "namespace": "medianxl" (Median-only strings, docs/strings-namespace.md;
                                    the folder MPQ needs no (listfile): it is a directory, not a packed archive)
  d2rloader/config/d2rloader.toml   [d2rcore.save_format] mode = "extended" (Median's wider item/stat IDs)
                                    [d2rcore.stash] add_shared_tabs = 7 (Median's stash has 10 pages, D2R 3 normal tabs)
  MedianXL.mpq/data/...             copy of work/mod/data (folder-style MPQ) + global/dataversionbuild.txt
                                    + Median's classic tiles/maps (global/tiles)

Only MedianXL.mpq/data is replaced on reinstall; saves live elsewhere (Saved Games) and are never touched.
tools/package_release.py builds the same folder for a player release (build_data + write_mod_files).
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
NAMESPACE = "medianxl"  # tbl2json.py writes the namespaced strings under lng/strings/<NAMESPACE>/
DEFAULT_GAME = os.environ.get("MXL_D2R_GAME", r"C:\Program Files (x86)\Diablo II Resurrected")  # see game_ctl.py
D2R_BUILD = "93847"  # the game build the data and the plugin are made for; data/global/dataversionbuild.txt holds it
MXL_GLOBAL = ROOT / "assets" / "median" / "data" / "global"
MXL_TILES = MXL_GLOBAL / "tiles"
PLUGIN = ROOT / "build" / "plugin" / "Release" / "mxl-core.dll"
D2R_FILELIST =ROOT / "extract" / "d2r" / "93847" / "filelist.txt"  # written by gfx_placeholders.py


STASH_PAGES = 10  # Median's stash panel counts "n / 10" in both modes (plugin/src/travel_logic.h MedianStashPages)
VANILLA_SHARED_TABS = 3  # normal shared tabs of D2R before D2RLoader adds any (travel_logic.h VanillaSharedTabs)


def ensure_toml_key(text: str, section: str, key: str, value: str) -> str:
    """Return `text` with `key = value` in [section]. An uncommented key already there is the user's and is kept; a
    commented template line (D2RLoader writes `# key = default`) is replaced; a missing section is appended."""
    lines = text.splitlines()
    head = f"[{section}]"
    start = next((i for i, l in enumerate(lines) if l.strip() == head), None)
    if start is None:
        return text.rstrip("\n") + f"\n\n{head}\n{key} = {value}\n"
    end = next((i for i in range(start + 1, len(lines)) if lines[i].lstrip().startswith("[")), len(lines))
    for i in range(start + 1, end):
        bare = lines[i].strip()
        if bare.startswith(key) and bare[len(key):].lstrip().startswith("="):
            return text  # set by hand: leave it
    for i in range(start + 1, end):
        bare = lines[i].strip().lstrip("#").strip()
        if lines[i].strip().startswith("#") and bare.startswith(key) and bare[len(key):].lstrip().startswith("="):
            lines[i] = f"{key} = {value}"
            return "\n".join(lines) + "\n"
    lines.insert(start + 1, f"{key} = {value}")
    return "\n".join(lines) + "\n"


# D2RLoader stores every registered action's key in <game>/d2rloader/config/input-bindings.toml the first time the plugin registers it,
# so changing a default in the plugin does not reach a player who already ran an older build: the old key stays. The first port
# registered Map Reveal on R and the cube transmute on C, which are D2R's run toggle and Character screen (a press did both). Those
# two stored entries, and only an entry that still holds the old default, are dropped here so the next start registers the new ones
# (travel_logic.h Hotkeys). A key someone chose by hand is not on the old default and is kept.
OLD_HOTKEY_DEFAULTS = {"mxl-core/mxl_map_reveal": 0x52, "mxl-core/mxl_transmute": 0x43}  # R, C


def migrate_hotkey_bindings(text: str) -> str:
    """`input-bindings.toml` text without the [bindings."<id>"] tables of OLD_HOTKEY_DEFAULTS whose primary key is still the old one."""
    blocks, cur = [], []
    for line in text.split("\n"):
        if line.startswith("[") and cur:
            blocks.append(cur)
            cur = []
        cur.append(line)
    blocks.append(cur)
    kept = []
    for blk in blocks:
        stale = any(blk[0].strip() == f'[bindings."{ident}"]' and f"primary_key = {key}" in (l.strip() for l in blk)
                    for ident, key in OLD_HOTKEY_DEFAULTS.items())
        if not stale:
            kept.append("\n".join(blk))
    return "\n".join(kept)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--name", default="MedianXL")
    ap.add_argument("--slot", default=os.environ.get("MXL_SLOT"), help="game slot 1..3 or 'all' (default MXL_SLOT); overrides --game")
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
        rc = install_one(Path(game_slots.slot_dir(n)), args.name, savepath, first)
        if rc:
            return rc
        first = first or Path(game_slots.slot_dir(n)) / "mods" / args.name / f"{args.name}.mpq" / "data"
        if n != 1:
            game_slots.setup(n)  # d2rloader.toml of the main install, own save folder
    return rc


def install_one(game: Path, name: str, savepath: str, copy_data_from: Path = None) -> int:
    """Install into one game folder. copy_data_from: the data folder of an install made a moment ago (skips the
    ds1 renumbering and the file merging, copies the finished tree)."""
    owner = os.environ.get("MXL_RUN")
    if owner:
        import game_slots
        slot = game_slots.slot_of_game_dir(str(game))
        info = game_slots.lock_info(slot) if slot is not None else None
        if not info or not info[1] or info[1].split()[0] != owner:
            print(f"refusing install: {game} is not locked by {owner}")
            return 1
        if game_slots.slot_processes(str(game)):
            print(f"refusing install: {game} still has a running game")
            return 1
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
    else:
        build_data(data, game)
    return finish_install(game, name, savepath, mod, mpq, data)


def build_data(data: Path, game: Path) -> None:
    """Write the mod's data folder (`data` must not exist yet): work/mod/data plus what is merged at install time.
    Reads D2R's own files from the game folder `game` (CASC, for maps_hd) but copies none of them."""
    shutil.copytree(SRC, data)
    (data / "global" / "dataversionbuild.txt").write_text(D2R_BUILD, encoding="ascii")
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


DEV_METADATA = {"modVersion": "0.1.0-dev", "description": "Work-in-progress port of Median XL to D2R.", "website": ""}


def write_mod_files(mod: Path, name: str, savepath: str, plugin: Path = PLUGIN, metadata: dict = None) -> None:
    """Everything of the mod folder except <name>.mpq/data: modinfo.json, d2rloader/{metadata.json, plugins, config, patches}.
    metadata: modVersion / description / website for d2rloader/metadata.json (default: the dev install's)."""
    mpq = mod / f"{name}.mpq"
    mpq.mkdir(parents=True, exist_ok=True)
    # D2R (and D2RLoader's mod detection) read modinfo.json inside the folder-style MPQ; the copy at the mod
    # root follows the D2RLoader guide's layout and is harmless.
    modinfo = json.dumps({"name": name, "savepath": savepath}, indent=2)
    for target in (mpq / "modinfo.json", mod / "modinfo.json"):
        target.write_text(modinfo, encoding="utf-8")
    d2rl = mod / "d2rloader"
    (d2rl / "config").mkdir(parents=True, exist_ok=True)
    (d2rl / "plugins").mkdir(exist_ok=True)
    (d2rl / "patches").mkdir(exist_ok=True)
    meta = {"schemaVersion": 1, "namespace": NAMESPACE,
            "metadata": {"name": "Median XL (D2R port)", "author": "mxl-d2r", **DEV_METADATA, **(metadata or {})}}
    (d2rl / "metadata.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    # The mxl-core plugin (plugin/, built by tools/build_plugin.ps1) loads only with this mod (mod-scoped)
    if plugin.exists():
        shutil.copy2(plugin, d2rl / "plugins" / "mxl-core.dll")
        print(f"plugin {plugin.name} ({plugin.stat().st_size // 1024} KB)")
    else:
        print(f"no plugin at {plugin} (run tools/build_plugin.ps1); installing data only")
    toml = d2rl / "config" / "d2rloader.toml"
    if not toml.exists():  # never overwrite a config someone edited
        toml.write_text('[d2rcore.save_format]\nmode = "extended"\n', encoding="utf-8")
    # only this key is ours to add; the rest of an edited config stays as it is
    text = toml.read_text(encoding="utf-8")
    added = ensure_toml_key(text, "d2rcore.stash", "add_shared_tabs", str(max(0, STASH_PAGES - VANILLA_SHARED_TABS)))
    if added != text:
        toml.write_text(added, encoding="utf-8")
        print(f"d2rloader.toml: add_shared_tabs = {STASH_PAGES - VANILLA_SHARED_TABS} (Median's {STASH_PAGES} stash pages)")


def finish_install(game: Path, name: str, savepath: str, mod: Path, mpq: Path, data: Path) -> int:
    write_mod_files(mod, name, savepath)
    bindings = game / "d2rloader" / "config" / "input-bindings.toml"
    if bindings.exists():
        old = bindings.read_text(encoding="utf-8")
        new = migrate_hotkey_bindings(old)
        if new != old:
            bindings.write_text(new, encoding="utf-8")
            print("input-bindings.toml: dropped the first port's stored hotkeys (Map Reveal R, transmute C); the plugin registers L and K")

    for rel in ("strings/%s/median.json", "strings/%s/overrides.json", "strings-legacy/%s/median.json"):
        ns_file = data / "local" / "lng" / (rel % NAMESPACE)
        if not ns_file.exists():
            print(f"warning: {ns_file.relative_to(data)} is missing: no Median strings (run tools/tbl2json.py)")
    files = sum(1 for _ in data.rglob("*") if _.is_file())
    size = sum(p.stat().st_size for p in data.rglob("*") if p.is_file()) / 1e6
    print(f"installed {files} files ({size:.0f} MB) -> {mod}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

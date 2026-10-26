"""Player release of the port: the launcher's feed (release-manifest.json) and a zip for manual / offline install.

From a finished build (tools/build.py -> work/mod) and the release plugin (cmake -DMXL_RELEASE=ON: no test harness, built
here unless --plugin is given) it writes into dist/:

  MedianXL-D2R-<version>.zip     top folder MedianXL/, the folder tools/install_mod.py writes into <D2R>\\mods:
                                   MedianXL.mpq/modinfo.json   {"name": "MedianXL", "savepath": "MedianXL/"}
                                   MedianXL.mpq/data/...       install_mod.build_data (work/mod/data + the install-time merges)
                                   d2rloader/plugins/mxl-core.dll, d2rloader/config/d2rloader.toml, d2rloader/patches/,
                                   d2rloader/metadata.json     (modVersion = <version>, website = the GitHub repo)
  release-manifest.json          schemaVersion 1: version, required D2R build, required D2RLoader + sha256, every file of
                                 the mod folder with size and sha256 (docs/release.md "Release manifest")
  RELEASE_NOTES-<version>.md     skeleton: requirements and install steps, to be filled in
  SHA256SUMS                     of the zip, the manifest and the notes

Nothing else goes in: no test harness files, test configs, saves or logs (checked), and no file that is a byte copy of one in
extract/ (Blizzard's own files: D2R reads those from its own install).

  py -3 tools/package_release.py [--version 0.70.0] [--plugin DLL] [--data-from DIR] [--game DIR] [--out dist]
  py -3 tools/package_release.py --no-data --plugin build/mxl-core.dll   (CI: no game files there; plugin + config only)
Version: --version, else PluginVersion of plugin/src/mxl_core.cpp.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import install_mod  # noqa: E402

ROOT = install_mod.ROOT
NAME = "MedianXL"
REPO_URL = "https://github.com/Nugwa628/mxl-d2r"
EXTRACT = ROOT / "extract" / "d2r" / install_mod.D2R_BUILD
RELEASE_BUILD = ROOT / "build" / "plugin-release"
D2R = {"build": install_mod.D2R_BUILD, "version": "3.3.0", "imageTimestamp": "0x6ab3782c"}  # plugin/src/build_guard_logic.h
# The pinned D2RLoader (docs/setup.md step 10)
D2RLOADER = {"version": "1.3.1-beta", "url": "https://d2rloader.net/download/D2RLoader-1.3.1-beta.zip", "size": 47652455,
             "sha256": "9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7",
             "signerThumbprint": "8001082E968FADAA58BF2F73EFA1810A11D0C28F"}
# What the game folder's own d2rloader/config/d2rloader.toml needs for this mod ([d2rloader] section of D2RLoader's config):
# the mod starts by itself, and -txt makes D2R read the mod's .txt tables (it ships no .bin for most of them).
LAUNCH = {"default_mod": NAME, "launch_arguments": "-txt"}
# The player's own settings: the launcher writes these only when they are missing (docs/release.md).
PRESERVE = ["d2rloader/config/d2rloader.toml"]
ZIP_TIME = (2026, 1, 1, 0, 0, 0)  # one timestamp for every entry: the same input gives the same zip

FORBIDDEN = re.compile(r"(^|/)(mxl-testharness[^/]*|mxl-core-off\.txt|input-bindings\.toml|imgui\.ini|debugsettings\.json)$"
                       r"|\.(d2s|d2i|d2x|d2rl|ctc|ctl|key|map|ma\d|log|dmp|pdb|orig)$", re.I)  # saves: D2R and D2RLoader sidecars


def plugin_version() -> str:
    m = re.search(r'PluginVersion = "([^"]+)"', (ROOT / "plugin" / "src" / "mxl_core.cpp").read_text("utf-8"))
    return m.group(1)


def build_release_plugin() -> Path:
    """cmake -DMXL_RELEASE=ON into build/plugin-release (the dev build in build/plugin stays as it is)."""
    subprocess.check_call(["git", "submodule", "update", "--init", "third_party/PluginSDK"], cwd=ROOT)
    subprocess.check_call(["cmake", "-S", "plugin", "-B", str(RELEASE_BUILD), "-A", "x64", "-DMXL_RELEASE=ON"], cwd=ROOT)
    subprocess.check_call(["cmake", "--build", str(RELEASE_BUILD), "--config", "Release", "--target", "mxl_core"], cwd=ROOT)
    return RELEASE_BUILD / "Release" / "mxl-core.dll"


def check_release_plugin(dll: Path, version: str) -> list:
    """Problems of a plugin DLL for a release: the test harness must be compiled out, and the version must be the release's."""
    b = dll.read_bytes()
    problems = []
    if b"mxl-testharness" in b:
        problems.append(f"{dll} has the test harness (build it with -DMXL_RELEASE=ON)")
    if version.encode() + b"\0" not in b:
        problems.append(f"{dll} does not carry PluginVersion {version}")
    return problems


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def stage(mod: Path, version: str, plugin: Path, game: Path, data_from: Path = None, no_data: bool = False) -> None:
    if mod.exists():
        shutil.rmtree(mod)
    data = mod / f"{NAME}.mpq" / "data"
    if data_from is not None:
        shutil.copytree(data_from, data)
    elif not no_data:
        install_mod.build_data(data, game)
    install_mod.write_mod_files(mod, NAME, f"{NAME}/", plugin, {
        "modVersion": version, "website": REPO_URL,
        "description": "Median XL 2.14.4 for Diablo II: Resurrected (build %s) through D2RLoader." % install_mod.D2R_BUILD})


def drop_blizzard_copies(mod: Path, extract: Path = EXTRACT) -> int:
    """Remove files that are byte copies of D2R's own (same path under extract/d2r/<build>/data): the game has them already.
    dataversionbuild.txt is ours (install_mod writes it). -> number removed."""
    data = mod / f"{NAME}.mpq" / "data"
    n = 0
    if not (extract / "data").is_dir() or not data.is_dir():
        return 0
    for p in sorted(data.rglob("*")):
        if not p.is_file() or p.name.lower() == "dataversionbuild.txt":
            continue
        twin = extract / "data" / p.relative_to(data)
        if twin.is_file() and twin.stat().st_size == p.stat().st_size and twin.read_bytes() == p.read_bytes():
            p.unlink()
            n += 1
    return n


def forbidden_files(mod: Path) -> list:
    return [p.relative_to(mod).as_posix() for p in mod.rglob("*") if p.is_file() and FORBIDDEN.search(p.relative_to(mod).as_posix())]


def file_list(mod: Path) -> list:
    return [{"path": p.relative_to(mod).as_posix(), "size": p.stat().st_size, "sha256": sha256(p)}
            for p in sorted(mod.rglob("*"), key=lambda q: q.relative_to(mod).as_posix()) if p.is_file()]


def write_zip(mod: Path, out: Path) -> None:
    """Deterministic zip: sorted entries, one timestamp, top folder MedianXL/ (empty folders kept: d2rloader/patches)."""
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(mod.rglob("*"), key=lambda q: q.relative_to(mod).as_posix()):
            arc = f"{NAME}/" + p.relative_to(mod).as_posix()
            if p.is_dir():
                if not any(p.iterdir()):
                    z.writestr(zipfile.ZipInfo(arc + "/", ZIP_TIME), b"")
                continue
            info = zipfile.ZipInfo(arc, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(p, "rb") as f, z.open(info, "w") as w:
                shutil.copyfileobj(f, w, 1 << 20)


def manifest(version: str, files: list, zip_path: Path, complete: bool) -> dict:
    return {
        "schemaVersion": 1,
        "name": NAME,
        "version": version,
        "pluginVersion": plugin_version(),
        "released": datetime.date.today().isoformat(),
        "complete": complete,  # false: a --no-data build (no MedianXL.mpq/data); not for players
        "d2r": D2R,
        "d2rloader": D2RLOADER,
        "launch": LAUNCH,
        "modFolder": NAME,  # the paths below are relative to <D2R>/mods/<modFolder>/
        "saveFolder": f"{NAME}/",  # under Saved Games/Diablo II Resurrected/mods/ (modinfo.json savepath)
        "preserve": PRESERVE,
        "zip": {"name": zip_path.name, "size": zip_path.stat().st_size, "sha256": sha256(zip_path)},
        "fileCount": len(files),
        "totalSize": sum(f["size"] for f in files),
        "files": files,
    }


NOTES = """# Median XL D2R {version}

Median XL 2.14.4 ported to Diablo II: Resurrected through the D2RLoader plugin mxl-core {plugin}.

## Requirements

- Diablo II: Resurrected **build {build}** (D2R {d2rver}). On any other build the plugin installs nothing and shows
  "Median XL D2R {plugin} needs D2R build {build}; this is <n>".
- D2RLoader **{loader}** ({loader_url}, sha256 `{loader_sha}`).

## Install

With the Median XL launcher (recommended): it keeps its own copy of D2R build {build} with D2RLoader and the mod, so your
normal D2R install and other mods stay as they are. It reads release-manifest.json and downloads only changed files.

By hand (offline fallback), into a D2R folder at build {build} that has D2RLoader {loader}:

1. Unzip `{zip}` into `<D2R folder>\\mods\\` so that `<D2R folder>\\mods\\MedianXL\\MedianXL.mpq\\modinfo.json` exists.
2. In `<D2R folder>\\d2rloader\\config\\d2rloader.toml` set `default_mod = "MedianXL"` and `launch_arguments = "-txt"`.
3. Start `D2RLoader.exe`. Characters are saved in `Saved Games\\Diablo II Resurrected\\mods\\MedianXL\\`.
   To update, delete `mods\\MedianXL\\MedianXL.mpq\\data` and unzip the new release; keep your
   `mods\\MedianXL\\d2rloader\\config\\d2rloader.toml` if you changed it.

## Changes

- TODO

## Known issues

- TODO (docs/unported.md lists what is not ported yet)

## Checksums

See SHA256SUMS.
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", help="release version (default: PluginVersion)")
    ap.add_argument("--plugin", type=Path, help="a release mxl-core.dll (default: build it with -DMXL_RELEASE=ON)")
    ap.add_argument("--data-from", type=Path, help="copy this finished MedianXL.mpq/data instead of building it")
    ap.add_argument("--no-data", action="store_true", help="no MedianXL.mpq/data (CI): manifest says complete=false")
    ap.add_argument("--game", type=Path, default=Path(install_mod.DEFAULT_GAME), help="D2R folder read for maps_hd (CASC)")
    ap.add_argument("--out", type=Path, default=ROOT / "dist")
    a = ap.parse_args()

    version = (a.version or plugin_version()).lstrip("v")
    if version != plugin_version():
        print(f"note: release {version}, plugin PluginVersion {plugin_version()}")
    if not a.no_data and a.data_from is None and not (install_mod.SRC / "global" / "excel").exists():
        print("work/mod/data is missing: run tools/build.py first")
        return 1
    plugin = a.plugin or build_release_plugin()
    if not plugin.is_file():
        print(f"no plugin at {plugin}")
        return 1
    problems = check_release_plugin(plugin, plugin_version())
    if problems:
        print("\n".join(problems))
        return 1

    a.out.mkdir(parents=True, exist_ok=True)
    mod = a.out / "stage" / NAME
    stage(mod, version, plugin, a.game, a.data_from, a.no_data)
    dropped = drop_blizzard_copies(mod)
    if dropped:
        print(f"left out {dropped} files that are byte copies of D2R's own (extract/d2r/{install_mod.D2R_BUILD})")
    bad = forbidden_files(mod)
    if bad:
        print("refusing to package test or player files: " + ", ".join(bad[:20]))
        return 1

    suffix = "-nodata" if a.no_data else ""
    zip_path = a.out / f"MedianXL-D2R-{version}{suffix}.zip"
    write_zip(mod, zip_path)
    files = file_list(mod)
    man_path = a.out / "release-manifest.json"
    man_path.write_text(json.dumps(manifest(version, files, zip_path, not a.no_data), indent=1) + "\n", encoding="utf-8")
    notes = a.out / f"RELEASE_NOTES-{version}.md"
    notes.write_text(NOTES.format(version=version, plugin=plugin_version(), build=D2R["build"], d2rver=D2R["version"],
                                  loader=D2RLOADER["version"], loader_url=D2RLOADER["url"], loader_sha=D2RLOADER["sha256"],
                                  zip=zip_path.name), encoding="utf-8")
    sums = "".join(f"{sha256(p)}  {p.name}\n" for p in (zip_path, man_path, notes))
    (a.out / "SHA256SUMS").write_text(sums, encoding="utf-8")
    shutil.rmtree(a.out / "stage")
    print(f"{zip_path.name}: {len(files)} files, {sum(f['size'] for f in files) / 1e6:.0f} MB unpacked, "
          f"{zip_path.stat().st_size / 1e6:.0f} MB zipped; {man_path.name}, {notes.name}, SHA256SUMS in {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

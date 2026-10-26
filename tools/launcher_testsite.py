#!/usr/bin/env python
"""Test releases for the launcher's end-to-end test (docs/launcher.md "Testing"): a new dist/ folder derived from a real
tools/package_release.py output, with a few files changed, added or removed. tools/publish_release.py then publishes it
into a local site folder in the download host's layout, and tools/serve_dir.py serves that folder with Range.

  py -3 tools/launcher_testsite.py derive --from dist --out dist-lt2 --version 0.70.0-lt2
        [--savepath MedianXL-lt/] [--put REL=FILE ...] [--append REL=TEXT ...] [--remove REL ...]

  --savepath  MedianXL.mpq/modinfo.json "savepath" and the manifest's saveFolder: a test release must never use a real
              save folder (package_release writes MedianXL/, the player's)
  --put       replace or add a file (e.g. the dev mxl-core.dll, so the game checks can drive the game)
  --append    add a line to a text file (a changed file)
  --remove    drop a file

Everything else in the manifest (d2r, d2rloader, launch, preserve, ...) is the source's. The zip is written the way
package_release writes it (sorted entries, one timestamp, top folder MedianXL/).
"""
import argparse
import datetime
import json
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package_release as pr  # noqa: E402


def derive(a) -> int:
    src = Path(a.src)
    man = json.loads((src / "release-manifest.json").read_text("utf-8"))
    zips = list(src.glob("MedianXL-D2R-*.zip"))
    if len(zips) != 1:
        print(f"{src}: expected one MedianXL-D2R-*.zip, found {len(zips)}")
        return 1
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        with zipfile.ZipFile(zips[0]) as z:
            z.extractall(tmp)
        mod = Path(tmp) / pr.NAME
        if a.savepath:
            info = mod / "MedianXL.mpq" / "modinfo.json"
            mi = json.loads(info.read_text("utf-8"))
            mi["savepath"] = a.savepath
            info.write_text(json.dumps(mi, indent=2) + "\n", encoding="utf-8")
            man["saveFolder"] = a.savepath
        for spec in a.put:
            rel, file = spec.split("=", 1)
            (mod / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(file, mod / rel)
        for spec in a.append:
            rel, text = spec.split("=", 1)
            with open(mod / rel, "a", encoding="utf-8", newline="") as f:
                f.write(text + "\n")
        for rel in a.remove:
            (mod / rel).unlink()
        zip_path = out / f"MedianXL-D2R-{a.version}.zip"
        pr.write_zip(mod, zip_path)
        files = pr.file_list(mod)
    man.update({
        "version": a.version,
        "released": datetime.date.today().isoformat(),
        "zip": {"name": zip_path.name, "size": zip_path.stat().st_size, "sha256": pr.sha256(zip_path)},
        "fileCount": len(files),
        "totalSize": sum(f["size"] for f in files),
        "files": files,
    })
    man_path = out / "release-manifest.json"
    man_path.write_text(json.dumps(man, indent=1) + "\n", encoding="utf-8")
    notes = out / f"RELEASE_NOTES-{a.version}.md"
    notes.write_text(f"# Median XL D2R {a.version}\n\nTest release derived from {man.get('version')} for the launcher tests.\n",
                     encoding="utf-8")
    (out / "SHA256SUMS").write_text("".join(f"{pr.sha256(p)}  {p.name}\n" for p in (zip_path, man_path, notes)), encoding="utf-8")
    print(f"{out}: {a.version}, {len(files)} files ({len(a.put)} put, {len(a.append)} changed, {len(a.remove)} removed)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("derive")
    d.add_argument("--from", dest="src", required=True, help="a package_release.py output folder")
    d.add_argument("--out", required=True)
    d.add_argument("--version", required=True)
    d.add_argument("--savepath")
    d.add_argument("--put", action="append", default=[])
    d.add_argument("--append", action="append", default=[])
    d.add_argument("--remove", action="append", default=[])
    a = ap.parse_args()
    return derive(a)


if __name__ == "__main__":
    sys.exit(main())

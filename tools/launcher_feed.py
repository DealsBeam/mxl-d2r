#!/usr/bin/env python
"""Release feed for the launcher, for tests (docs/launcher.md "Release feed"): a release-manifest.json in the format
of docs/release.md (schemaVersion 1; tools/package_release.py writes the real one), and a local server.

  py -3 tools/launcher_feed.py build --zip MedianXL-D2R-0.70.0.zip --out feed/ --version 0.70.0
      manifest for a release zip (one MedianXL/ folder inside); the zip is copied next to it unless it is already there
  py -3 tools/launcher_feed.py build --mod D:/D2R-93847/mods/MedianXL --out feed/ --version 0.69.0-test
      stand-in: zip an installed mod folder first (logs, crash reports and test-harness files left out)
  py -3 tools/launcher_feed.py serve --dir feed/ [--port 8765] [--no-range]
      HTTP server with byte ranges (GitHub release downloads have them); --no-range answers every request with the
      whole file, to test the launcher's fallback
"""
import argparse
import datetime
import hashlib
import http.server
import json
import os
import re
import shutil
import sys
import zipfile
from pathlib import Path

D2R = {"build": "93847", "version": "3.3.0", "imageTimestamp": "0x6ab3782c"}
D2RLOADER = {"version": "1.3.1-beta", "url": "https://d2rloader.net/download/D2RLoader-1.3.1-beta.zip", "size": 47652455,
             "sha256": "9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7",
             "signerThumbprint": "8001082E968FADAA58BF2F73EFA1810A11D0C28F"}
PRESERVE = ["d2rloader/config/d2rloader.toml"]
# never part of a release: run-time output and the test harness of tools/game_ctl.py
SKIP = re.compile(r"(^|/)(logs|crashes)/|(^|/)mxl-testharness[^/]*$|(^|/)mxl-core-off\.txt$|\.caps-bak$|\.mxlpart$|\.bak$", re.I)


def sha256_file(p, chunk=1 << 20):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                return h.hexdigest()
            h.update(b)


def zip_mod(mod: Path, out_zip: Path, root: str) -> int:
    files = sorted(p for p in mod.rglob("*") if p.is_file() and not SKIP.search(p.relative_to(mod).as_posix()))
    tmp = out_zip.with_suffix(".zip.part")
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in files:
            z.write(p, f"{root}/{p.relative_to(mod).as_posix()}")
    os.replace(tmp, out_zip)
    return len(files)


def manifest_for(zip_path: Path, version: str, url: str) -> dict:
    with zipfile.ZipFile(zip_path) as z:
        names = [i for i in z.infolist() if not i.is_dir()]
        roots = {i.filename.split("/", 1)[0] for i in names}
        if len(roots) != 1:
            raise SystemExit(f"{zip_path}: expected one top folder, found {sorted(roots)[:5]}")
        root = roots.pop()
        files = []
        savepath = root + "/"
        for i in names:
            rel = i.filename.split("/", 1)[1]
            h = hashlib.sha256()
            with z.open(i) as f:
                for b in iter(lambda: f.read(1 << 20), b""):
                    h.update(b)
            files.append({"path": rel, "size": i.file_size, "sha256": h.hexdigest()})
            if rel == f"{root}.mpq/modinfo.json":
                savepath = json.loads(z.read(i).decode("utf-8-sig")).get("savepath", savepath)
    files.sort(key=lambda f: f["path"])
    return {"schemaVersion": 1, "name": root, "version": version, "pluginVersion": version,
            "released": datetime.date.today().isoformat(), "complete": True, "d2r": D2R, "d2rloader": D2RLOADER,
            "launch": {"default_mod": root, "launch_arguments": "-txt"}, "modFolder": root, "saveFolder": savepath,
            "preserve": [k for k in PRESERVE if any(f["path"] == k for f in files)],
            "zip": {"name": url, "size": zip_path.stat().st_size, "sha256": sha256_file(zip_path)},
            "fileCount": len(files), "totalSize": sum(f["size"] for f in files), "files": files}


def build(a) -> int:
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    if a.mod:
        mod = Path(a.mod)
        if not (mod / "MedianXL.mpq").is_dir():
            print(f"{mod} has no MedianXL.mpq folder")
            return 1
        zip_path = out / f"MedianXL-D2R-{a.version}.zip"
        n = zip_mod(mod, zip_path, a.root)
        print(f"zipped {n} files of {mod} -> {zip_path} ({zip_path.stat().st_size >> 20} MB)")
    else:
        src = Path(a.zip)
        zip_path = out / src.name
        if src.resolve() != zip_path.resolve():
            shutil.copy2(src, zip_path)
    m = manifest_for(zip_path, a.version, a.url or zip_path.name)
    (out / "release-manifest.json").write_text(json.dumps(m, indent=1), encoding="utf-8")
    print(f"manifest: {len(m['files'])} files, version {a.version} -> {out / 'release-manifest.json'}")
    return 0


class RangeHandler(http.server.SimpleHTTPRequestHandler):
    no_range = False

    def send_head(self):
        path = self.translate_path(self.path)
        if os.path.isdir(path) or not os.path.exists(path):
            return super().send_head()
        size = os.path.getsize(path)
        rng = None if self.no_range else self.headers.get("Range")
        m = re.match(r"bytes=(\d+)-(\d*)$", rng or "")
        f = open(path, "rb")
        if m:
            start, end = int(m.group(1)), int(m.group(2)) if m.group(2) else size - 1
            end = min(end, size - 1)
            if start > end:
                f.close()
                self.send_error(416)
                return None
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.send_header("Content-Length", str(end - start + 1))
            f.seek(start)
            self._left = end - start + 1
        else:
            self.send_response(200)
            self.send_header("Content-Length", str(size))
            self._left = size
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Accept-Ranges", "none" if self.no_range else "bytes")
        self.end_headers()
        return f

    def copyfile(self, source, outputfile):
        left = getattr(self, "_left", None)
        while left is None or left > 0:
            b = source.read(min(1 << 20, left) if left is not None else 1 << 20)
            if not b:
                break
            outputfile.write(b)
            if left is not None:
                left -= len(b)


def serve(a) -> int:
    os.chdir(a.dir)
    RangeHandler.no_range = a.no_range
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", a.port), RangeHandler)
    print(f"serving {a.dir} on http://127.0.0.1:{a.port}/ ({'no ranges' if a.no_range else 'ranges'})", flush=True)
    srv.serve_forever()
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    g = b.add_mutually_exclusive_group(required=True)
    g.add_argument("--zip")
    g.add_argument("--mod")
    b.add_argument("--out", required=True)
    b.add_argument("--version", required=True)
    b.add_argument("--root", default="MedianXL", help="top folder inside a zip made with --mod")
    b.add_argument("--url", help="zip.name written to the manifest (default: the zip's file name, relative)")
    s = sub.add_parser("serve")
    s.add_argument("--dir", required=True)
    s.add_argument("--port", type=int, default=8765)
    s.add_argument("--no-range", action="store_true")
    a = ap.parse_args()
    return build(a) if a.cmd == "build" else serve(a)


if __name__ == "__main__":
    sys.exit(main())

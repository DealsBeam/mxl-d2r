"""Extract every Median XL MPQ into extract/mxl/ and write a manifest.

Usage: python tools/mpq_extract.py [--src C:\\Games\\median-xl] [--out extract/mxl]

Archive names are base64 ("bW9uenp6" = "monzzz"). The manifest records, for every file, the archive it
came from, so later tools can trace an asset back. Files present in more than one archive are listed
under "conflicts"; the copy from the archive processed last (sorted by name) is the one kept on disk.
"""
import argparse
import base64
import json
import re
import sys
from pathlib import Path

import native

ROOT = Path(__file__).resolve().parent.parent
SPECIAL = {"(listfile)", "(attributes)", "(signature)"}


def decode_name(mpq: Path) -> str:
    m = re.fullmatch(r"medianxl-([A-Za-z0-9+/]+?)(-\d+)?\.mpq", mpq.name)
    if not m:
        return mpq.stem
    b64 = m.group(1)
    try:
        label = base64.b64decode(b64 + "=" * (-len(b64) % 4)).decode("ascii")
    except Exception:
        label = b64
    return label + (m.group(2) or "")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=r"C:\Games\median-xl")
    ap.add_argument("--out", default=str(ROOT / "extract" / "mxl"))
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    files: dict[str, dict] = {}
    conflicts: dict[str, list[str]] = {}
    archives = []
    failed = []

    for mpq in sorted(Path(args.src).glob("medianxl-*.mpq")):
        label = decode_name(mpq)
        h = native.open_mpq(mpq)
        try:
            names = [(n, s) for n, s in native.mpq_files(h) if n not in SPECIAL]
            for name, size in names:
                rel = name.replace("\\", "/")
                dest = out / rel
                dest.parent.mkdir(parents=True, exist_ok=True)
                if not native.extract(h, name, dest):
                    failed.append(f"{mpq.name}:{name}")
                    continue
                if rel in files:
                    conflicts.setdefault(rel, [files[rel]["archive"]]).append(mpq.name)
                files[rel] = {"archive": mpq.name, "label": label, "size": size}
        finally:
            native.storm.SFileCloseArchive(h)
        archives.append({"file": mpq.name, "label": label, "count": len(names)})
        print(f"{mpq.name:32} {label:12} {len(names):5} files", flush=True)

    manifest = {"source": args.src, "archives": archives, "files": dict(sorted(files.items())),
                "conflicts": conflicts, "failed": failed}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")
    print(f"\n{len(files)} files, {len(conflicts)} conflicts, {len(failed)} failed -> {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

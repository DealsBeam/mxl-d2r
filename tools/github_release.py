#!/usr/bin/env python3
"""Create (or update) a GitHub release for a D2R-XL version from its CHANGELOG.md section (docs/release.md step 5).

  py -3 tools/github_release.py 0.69.6 --commit <sha on GitHub main> [--downloader build/launcher/Release/D2R-XL-Downloader.exe]
                                       [--repo Nugwa628/mxl-d2r] [--latest] [--dry-run]

The body is the "### <version>" section of CHANGELOG.md, plus the download links: the downloader (always the newest
installer) and the release's 7z on files.werdegames.com. The 7z itself is not attached; it lives on the download host,
because 355 MB over the developer's uplink is too slow. --downloader attaches the exe (2 MB) to the release. The tag
`v<version>` is created on --commit, a commit of the public (scrubbed) main: the forge's commit ids differ, see
docs/release.md. Needs the GitHub CLI (`gh`), logged in.
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SITE = "https://files.werdegames.com/mxl/"


def changelog_section(text: str, version: str) -> str:
    """The body of the first "### <version>" heading of CHANGELOG.md (the D2R-XL part comes first), without the heading."""
    m = re.search(rf"^### {re.escape(version)}\b[^\n]*\n(.*?)(?=^### |^## |\Z)", text, re.M | re.S)
    if not m:
        raise SystemExit(f"CHANGELOG.md has no '### {version}' section")
    return m.group(1).strip()


def release_body(version: str, section: str, downloader_version: str = "") -> str:
    section = section.replace("](docs/", "](https://github.com/Nugwa628/mxl-d2r/blob/main/docs/")  # release pages have no relative links
    lines = [section, "", "**Install or update:** run the D2R-XL Downloader"]
    if downloader_version:
        lines[-1] += f" ({SITE}launcher/{downloader_version}/D2R-XL-Downloader.exe)"
    lines[-1] += ". It installs the newest release into your D2R folder's `mods\\D2R-XL`."
    lines += ["", f"**Manual install:** [D2R-XL-{version}.7z]({SITE}releases/{version}/D2R-XL-{version}.7z), unpacked into "
              "your D2R `mods` folder (see the README).",
              "",
              "Needs Diablo II: Resurrected build 93847 (Battle.net) and D2RLoader 1.3.1. "
              "Known bugs: [docs/bugs.md](https://github.com/Nugwa628/mxl-d2r/blob/main/docs/bugs.md)."]
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("version")
    ap.add_argument("--commit", required=True, help="commit on the GitHub main to tag")
    ap.add_argument("--repo", default="Nugwa628/mxl-d2r")
    ap.add_argument("--downloader", type=Path, help="D2R-XL-Downloader.exe to attach")
    ap.add_argument("--downloader-version", default="", help="its version, for the link (default: read from the exe)")
    ap.add_argument("--latest", action="store_true", help="mark as the latest release")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    dv = a.downloader_version
    if a.downloader and not dv:
        sys.path.insert(0, str(ROOT / "tools"))
        import publish_release
        dv = publish_release.exe_version(a.downloader)
    body = release_body(a.version, changelog_section((ROOT / "CHANGELOG.md").read_text("utf-8"), a.version), dv)
    tag = f"v{a.version}"
    if a.dry_run:
        print(f"{tag} on {a.commit}\n\n{body}")
        return 0
    with tempfile.NamedTemporaryFile("w", suffix=".md", delete=False, encoding="utf-8") as f:
        f.write(body)
    exists = subprocess.run(["gh", "release", "view", tag, "--repo", a.repo], capture_output=True).returncode == 0
    if exists:
        cmd = ["gh", "release", "edit", tag, "--repo", a.repo, "--notes-file", f.name, "--title", f"D2R-XL {a.version}"]
    else:
        cmd = ["gh", "release", "create", tag, "--repo", a.repo, "--target", a.commit, "--title", f"D2R-XL {a.version}",
               "--notes-file", f.name]
    cmd += ["--latest"] if a.latest else (["--latest=false"] if not exists else [])
    subprocess.check_call(cmd)
    if a.downloader:
        subprocess.check_call(["gh", "release", "upload", tag, str(a.downloader), "--repo", a.repo, "--clobber"])
    print(f"https://github.com/{a.repo}/releases/tag/{tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

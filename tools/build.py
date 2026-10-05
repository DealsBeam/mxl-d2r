"""Run the whole data pipeline in order (after the one-time extractions mpq_extract.py and casc_extract.py).

  mxl_names.py       names + IDs              -> work/named/ (+ idmap.json)
  formula.py convert formula cells            -> work/named/ (in place)
  sigma_tables.py    D2Sigma-only tables      -> work/sigma/
  tbl2json.py        strings                  -> work/mod/data/local/lng/
  txt_migrate.py     D2R table layout         -> work/mod/data/global/excel/{base,}
  validate.py        D2R schema checks        -> work/mod/validate_report.md

Usage: python tools/build.py   (exit code = validate.py's: 1 if there are errors)
"""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STEPS = [["mxl_names.py"], ["formula.py", "convert"], ["sigma_tables.py", "decode"], ["tbl2json.py"],
         ["txt_migrate.py"], ["validate.py"]]


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

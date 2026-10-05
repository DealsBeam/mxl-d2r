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


def main() -> int:
    for d in (ROOT / "work" / "named", ROOT / "work" / "mod"):
        shutil.rmtree(d, ignore_errors=True)
    for step in STEPS:
        print(f"== {' '.join(step)}", flush=True)
        r = subprocess.run([sys.executable, str(ROOT / "tools" / step[0]), *step[1:]], cwd=ROOT,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        tail = r.stdout.strip().splitlines()[-4:]
        print("\n".join(f"   {line}" for line in tail))
        if r.returncode and step[0] != "validate.py":
            print(f"step failed ({r.returncode})")
            return r.returncode
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())

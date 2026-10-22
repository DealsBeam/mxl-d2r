#!/usr/bin/env python
"""Column survey for docs/strings-namespace-research.md. For each (table, column) the caller's prep script puts a valid
`medianxl:` key in row 1 and `medianxl:NoSuchKey` in row 2; the loader stops at the first bad reference, so every column
gets its own start. Prints the loader's complaint per column: an error naming NoSuchKey means the column is a localization
column; none means the cell is read as plain text.
  MXL_RUN=<slot lock name> py -3 tools/ns_cols.py --slot N PREP_CMD RESTORE_CMD txt:col [txt:col ...]
Holds the slot lock between starts (game_ctl.stop would release it)."""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_checks as gc  # noqa: E402
import game_ctl as g  # noqa: E402

prep_cmd, restore_cmd, cols = sys.argv[1], sys.argv[2], sys.argv[3:]
d = gc.new_run()
real_release = g.game_slots.release
g.game_slots.release = lambda *a, **k: None
out = open(os.path.join(d, "ns_cols.txt"), "w", encoding="utf-8")
try:
    for col in cols:
        subprocess.run(restore_cmd, shell=True, capture_output=True)
        env = dict(os.environ, NS_ONLY=col)
        subprocess.run(prep_cmd, shell=True, capture_output=True, env=env)
        off = g._size(g.LOADER_LOG)
        g.start(timeout=30)
        text = g._read_from(g.LOADER_LOG, off)
        g.stop()
        hits = [ln[:260] for ln in text.splitlines() if re.search(r"String reference error|could not be loaded|NoSuchKey|NsLong|pool", ln)]
        line = "%s -> %s" % (col, hits if hits else "no complaint (menu reached: %s)" % ("D2R startup complete" in text))
        print(line)
        out.write(line + "\n")
        out.flush()
finally:
    subprocess.run(restore_cmd, shell=True, capture_output=True)
    real_release(g.SLOT, g.LOCK_OWNER)

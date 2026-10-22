#!/usr/bin/env python
"""Which cube OUTPUT form does D2R compile? (docs/strings-namespace.md, "Item references"; lane cube-outputs.)
Background mode, one game slot, the caller holds its lock.

  MXL_RUN=<slot lock name> py -3 tools/cube_out_probe.py --slot N VARIANT [VARIANT ...]

Per variant the installed cubemain.txt (and uniqueitems.txt / setitems.txt) of the slot are edited (originals are
restored at the end), the game is started (D2RLoader recompiles the changed tables), the test character enters a game
and the probe prints the commands of MXL_PROBE_CMDS (`;` separated, default: `cuberec`/`cubeslots` of the Dulra recipes).
Variants: see VARIANTS: (file, whole-cell old, new) edit lists."""
import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_checks as gc  # noqa: E402
import game_ctl as g  # noqa: E402

DEFAULT_CMDS = ["cuberec 4950", "cuberec 4961", "cuberec 5083", "cubeslots 4940 40"]
FILES = ("cubemain.txt", "uniqueitems.txt", "setitems.txt")


def excel(game):
    return os.path.join(game, "mods", "MedianXL", "MedianXL.mpq", "data", "global", "excel")


def patch(path, old, new, rows=None, cols=None):
    """Replace every cell equal to `old` by `new` (whole cell; optionally only rows (0 = first data row) and column
    names); returns how many."""
    lines = open(path, "rb").read().decode("utf-8").split("\n")
    head = lines[0].split("\t")
    n = 0
    for k in range(1, len(lines)):
        if rows is not None and (k - 1) not in rows:
            continue
        cells = lines[k].split("\t")
        for i, c in enumerate(cells):
            if c == old and (cols is None or head[i] in cols):
                cells[i] = new
                n += 1
        lines[k] = "\t".join(cells)
    open(path, "wb").write("\n".join(lines).encode("utf-8"))
    return n


def set_cells(path, rows, col, value):
    lines = open(path, "rb").read().decode("utf-8").split("\n")
    i = lines[0].split("\t").index(col)
    for r in rows:
        cells = lines[r + 1].split("\t")
        cells[i] = value
        lines[r + 1] = "\t".join(cells)
    open(path, "wb").write("\n".join(lines).encode("utf-8"))


DULRA = range(4950, 4962)
Q = "medianxl:DulraCatalyst"


def v_base(ex):
    return 0


def v_bare_both(ex):
    """Pre-switch shape for the Dulra catalyst: bare input and bare unique index."""
    return patch(os.path.join(ex, "cubemain.txt"), Q, "DulraCatalyst") + patch(os.path.join(ex, "uniqueitems.txt"), Q, "DulraCatalyst")


def v_bare_input(ex):
    return patch(os.path.join(ex, "cubemain.txt"), Q, "DulraCatalyst")


def v_in1_misc(ex):
    """Input 1 of the Dulra rows is a plain misc code (not a unique)."""
    return patch(os.path.join(ex, "cubemain.txt"), Q, "gpw", rows=DULRA)


VARIANTS = {"base": v_base, "bare-both": v_bare_both, "bare-input": v_bare_input, "in1-misc": v_in1_misc}


def main():
    names = sys.argv[1:]
    d = gc.new_run()
    out = open(os.path.join(d, "cube_out_probe.txt"), "w", encoding="utf-8")

    def say(*a):
        s = " ".join(str(x) for x in a)
        print(s)
        out.write(s + "\n")
        out.flush()

    ex = excel(g.GAME_DIR)
    keep = os.path.join(d, "orig")
    os.makedirs(keep, exist_ok=True)
    for f in FILES:
        if not os.path.exists(os.path.join(keep, f)):
            shutil.copy(os.path.join(ex, f), os.path.join(keep, f))
    g.game_slots.release = lambda *a, **k: None   # keep the lock between starts
    gc.WD = gc.Watchdog()
    gc.WD.start()
    try:
        for name in names:
            for f in FILES:
                shutil.copy(os.path.join(keep, f), os.path.join(ex, f))
            n = VARIANTS[name](ex)
            say("== variant %s: %s cells edited" % (name, n))
            off = g._size(g.LOADER_LOG)
            if g.game_pids():
                g.stop()
                time.sleep(2)
            g.snapshot_marks()
            if not gc.to_menu():
                say("BLOCKED: no main menu")
                continue
            for ln in g._read_from(g.LOADER_LOG, off).splitlines():
                if re.search(r"error|ERROR|compiled mod.*(cubemain|uniqueitems)|warn", ln):
                    say("  loader:", ln[:230])
            if not gc.enter_game():
                say("BLOCKED: not in game")
                continue
            time.sleep(4)
            gc.goto(1)
            if not gc.items(gc.hstate(), "box"):
                say("no Horadric Cube yet:", gc.H("item box inv"))
            if os.environ.get("MXL_PROBE_STRESS"):
                # the reply of `cubeslots` must hold one line per 50 recipes; count the replies that lack one
                bad = 0
                n = int(os.environ["MXL_PROBE_STRESS"])
                for k in range(n):
                    first = (k * 2500) % 7500
                    ok, lines = gc.H("cubeslots %d 2500" % first, timeout=60)
                    got = [int(m.group(1)) for m in (re.match(r"cubeslots (\d+) \d+$", ln.strip()) for ln in lines) if m]
                    if got != list(range(first, first + 2500, 50)):
                        bad += 1
                        say("reply %d (first %d): %d lines, missing %s" % (k, first, len(got), sorted(set(range(first, first + 2500, 50)) - set(got))))
                say("stress: %d of %d replies incomplete" % (bad, n))
            for cmd in (os.environ["MXL_PROBE_CMDS"].split(";") if os.environ.get("MXL_PROBE_CMDS") else DEFAULT_CMDS):
                ok, lines = gc.H(cmd, timeout=30)
                say(cmd, "->", ok)
                for ln in lines:
                    if cmd == "state" and not re.match(r"item .* page=[34] ", ln):
                        continue    # `state`: only the cube / stash items
                    say("    " + ln[:200])
            g.stop()
            time.sleep(2)
    finally:
        for f in FILES:
            shutil.copy(os.path.join(keep, f), os.path.join(ex, f))
        gc.WD.stop_flag = True
        try:
            g.stop()
        finally:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())

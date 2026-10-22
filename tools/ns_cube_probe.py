#!/usr/bin/env python
"""How does D2R resolve a cube input that names a unique by index once the index cell is namespaced?
(docs/strings-namespace.md, "Item references".) Background mode, one game slot, the caller holds its lock.

  MXL_RUN=<slot lock name> py -3 tools/ns_cube_probe.py --slot N VARIANT [VARIANT ...]

Per variant: the installed cubemain.txt / uniqueitems.txt of the slot are edited (originals are restored at the end), the
game is started (D2RLoader recompiles the changed tables), the test character enters a game and the probe prints `cuberec`
of the goldcraft recipes (what the input compiled to) and the result of `cubecraft` with the HotTJ unique (+ rune) in
the cube. Variants: see VARIANTS (cube cell form, uniqueitems index cell form)."""
import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_checks as gc  # noqa: E402
import game_ctl as g  # noqa: E402

KEY = "HotTJ"
Q = "medianxl:" + KEY
# name -> (cubemain cell, uniqueitems index cell)
VARIANTS = {
    "base": (Q, Q),               # as shipped
    "cube-bare": (KEY, Q),        # reference without the namespace, index qualified
    "cube-quoted": ('"%s"' % Q, Q),
    "cube-d2r": ("d2r:" + KEY, Q),
    "nocarry": (Q, Q),            # base with carry1 cleared on the unique (is a HotTJ already carried: stash, bank?)
    "both-bare": (KEY, KEY),      # index bare too (display name then reads d2r:HotTJ: none)
    "cube-q-idx-bare": (Q, KEY),
}


def excel(game):
    return os.path.join(game, "mods", "MedianXL", "MedianXL.mpq", "data", "global", "excel")


def patch(path, old, new):
    """Replace every cell equal to `old` (whole cell, tab separated) by `new`; returns how many."""
    raw = open(path, "rb").read().decode("utf-8")
    n = 0
    out = []
    for line in raw.split("\n"):
        cells = line.split("\t")
        for i, c in enumerate(cells):
            if c == old:
                cells[i] = new
                n += 1
        out.append("\t".join(cells))
    open(path, "wb").write("\n".join(out).encode("utf-8"))
    return n


def main():
    names = sys.argv[1:]
    d = gc.new_run()
    out = open(os.path.join(d, "ns_cube_probe.txt"), "w", encoding="utf-8")

    def say(*a):
        s = " ".join(str(x) for x in a)
        print(s)
        out.write(s + "\n")
        out.flush()

    ex = excel(g.GAME_DIR)
    keep = os.path.join(d, "orig")
    os.makedirs(keep, exist_ok=True)
    for f in ("cubemain.txt", "uniqueitems.txt", "treasureclassex.txt"):
        if not os.path.exists(os.path.join(keep, f)):
            shutil.copy(os.path.join(ex, f), os.path.join(keep, f))
    g.game_slots.release = lambda *a, **k: None   # keep the lock between starts
    gc.WD = gc.Watchdog()
    gc.WD.start()
    try:
        for name in names:
            cube_cell, idx_cell = VARIANTS[name]
            for f in ("cubemain.txt", "uniqueitems.txt"):
                shutil.copy(os.path.join(keep, f), os.path.join(ex, f))
            nc = patch(os.path.join(ex, "cubemain.txt"), Q, cube_cell)
            ni = patch(os.path.join(ex, "uniqueitems.txt"), Q, idx_cell)
            if name == "nocarry":
                up = os.path.join(ex, "uniqueitems.txt")
                lines = open(up, "rb").read().decode("utf-8").split("\n")
                col = lines[0].split("\t").index("carry1")
                for k, ln in enumerate(lines):
                    c = ln.split("\t")
                    if c[0] == Q:
                        c[col] = ""
                        lines[k] = "\t".join(c)
                open(up, "wb").write("\n".join(lines).encode("utf-8"))
            say("== variant %s: cube cell %r (%d cells), index cell %r (%d cells)" % (name, cube_cell, nc, idx_cell, ni))
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
            say("items:", [(i.get("code"), i.get("page"), i.get("unique"), i.get("quality")) for i in gc.hstate()["items"]])
            if not gc.items(gc.hstate(), "box"):
                say("no Horadric Cube yet:", gc.H("item box inv"))
            for cmd in (os.environ["MXL_PROBE_CMDS"].split(";") if os.environ.get("MXL_PROBE_CMDS") else ["str key medianxl:" + KEY, "str key d2r:" + KEY, "cuberec 6327", "cuberec 6328", "cuberec 4555",
                        "cubeclear", "stat player 532 set 140", "gold set 400000 100000",
                        "item 0u1 cube uniq=30 532=1", "item f667 cube", "cubecraft", "gold get", "cubeclear",
                        "stat player 532 set 0"]):
                ok, lines = gc.H(cmd, timeout=30)
                say(cmd, "->", ok)
                for ln in lines:
                    say("    " + ln[:200])
            g.stop()
            time.sleep(2)
    finally:
        for f in ("cubemain.txt", "uniqueitems.txt"):
            shutil.copy(os.path.join(keep, f), os.path.join(ex, f))
        gc.WD.stop_flag = True
        try:
            g.stop()
        finally:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())

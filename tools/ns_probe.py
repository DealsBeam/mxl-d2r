#!/usr/bin/env python
"""Strings namespace experiment (docs/strings-namespace-research.md): background mode, one game slot.

Precondition (done by the caller): the slot's installed mod carries `"namespace": "medianxl"`, the file
data/local/lng/strings/medianxl/median.json with records whose text starts "NSTEST_", table cells `medianxl:<Key>`, and
the plugin built with the `h str` verb. The caller holds the slot lock (game_slots.py lock N ...).

  py -3 tools/ns_probe.py --slot 3

Starts the game, enters the test character's game, then asks the plugin (`h str ...`) for: the localization service's
answer for each full key, D2R's numeric lookup for every id whose text contains NSTEST_ (the ids D2R sees), and
creates a Healing Potion / spawns a zombie (cells pointing at the namespaced keys) for screenshots. Writes ns_probe.txt and
PNGs into the run folder; stops the game.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_checks as gc  # noqa: E402
import game_ctl as g  # noqa: E402

KEYS = ["NsItem", "NsItemDe", "NsMon", "NsLvlName", "NsLvlWarp", "NsLvlEntry", "NsSkName", "NsSkShort", "NsSkLong", "NsSkAlt",
        "NsStatPos", "NsStatNeg", "NsStat2", "NsGrpPos"]


def main():
    d = gc.new_run()
    out = open(os.path.join(d, "ns_probe.txt"), "w", encoding="utf-8")

    def say(*a):
        s = " ".join(str(x) for x in a)
        print(s)
        out.write(s + "\n")
        out.flush()

    gc.WD = gc.Watchdog()
    gc.WD.start()
    try:
        if g.game_pids():
            g.stop()
            time.sleep(2)
        g.snapshot_marks()
        if not gc.to_menu():
            say("BLOCKED: no main menu")
            return 1
        if not gc.enter_game():
            say("BLOCKED: not in game")
            return 1
        time.sleep(4)
        for cmd in ["str key d2r:strCancel", "str key mxl-core:NsItem", "str key NsItem"] + ["str key medianxl:" + k for k in KEYS] + [
                "str id 3344", "str id 28106", "str native 3344", "str native 28106",
                # group 1 check (Q4): Median's text for D2R's own key bhm (id 1973; vanilla "Spiny Shield", Median "Bone Shield")
                "str key d2r:bhm", "str key medianxl:bhm", "str key bhm", "str id 1973", "str native 1973",
                "str key medianxl:strCancel", "str key d2r:mxl_0", "str key medianxl:mxl_0", "str native 28106", "str id 28106",
                "str key d2r:Deadly", "str key medianxl:Deadly", "str native 1742"]:
            ok, lines = gc.H(cmd)
            say(cmd, "->", ok, lines)
        ok, lines = gc.H("str find NSTEST", timeout=60)
        say("str find NSTEST ->", ok)
        for ln in lines:
            say("   ", ln)
        for i in range(728, 752):
            ok, lines = gc.H("str native %d" % i)
            say("   ", lines[0] if lines else lines)
        ok, lines = gc.H("item hpo inv")
        say("item hpo inv ->", ok, lines)
        ok, lines = gc.H("item crn inv")
        say("item crn inv ->", ok, lines)
        g.press("i")
        time.sleep(1.5)
        g.shot("ns_inventory")
        for n, (fx, fy) in enumerate(((0.738, 0.696), (0.740, 0.690))):
            g.click(fx, fy, move_only=True)
            time.sleep(1.0)
            g.shot("ns_hover%d" % n)
        g.press("i")
        ok, lines = gc.H("spawn 5 1")
        say("spawn 5 ->", ok, lines)
        time.sleep(2)
        g.shot("ns_zombie")
        g.click(0.478, 0.529, move_only=True)
        time.sleep(1.0)
        g.shot("ns_zombie_hover")
        g.click(0.50, 0.30, move_only=True)
        gc.goto(2)
        time.sleep(1.5)
        g.shot("ns_bloodmoor")
        gc.goto(1)
        time.sleep(1.5)
        g.shot("ns_rogue")
        g.press("g")  # classic (legacy) graphics: do the namespaced strings still resolve?
        time.sleep(6)
        for cmd in ["str key medianxl:NsItem", "str native 737", "str native 513"]:
            ok, lines = gc.H(cmd)
            say("legacy", cmd, "->", ok, lines)
        g.press("i")
        time.sleep(1.5)
        g.click(0.738, 0.696, move_only=True)
        time.sleep(1.0)
        g.shot("ns_legacy_inv")
        ok, lines = gc.H("state")
        for ln in lines:
            say("   state:", ln[:200])
    finally:
        gc.WD.stop_flag = True
        say(gc.WD.summary())
        say(g.logscan()["text"])
        g.stop()
        out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

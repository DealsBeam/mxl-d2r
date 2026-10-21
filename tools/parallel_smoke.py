#!/usr/bin/env python
"""Run the smoke scenario (game_ctl.py smoke) on several game slots at the same time, all in background mode, and
record the evidence that the user's mouse and focus were left alone.

  py -3 tools/parallel_smoke.py [--slots 1,2,3] [--name NAME]

For every slot: take its run lock (game.lock.N; a slot whose lock is held, e.g. slot 1 by another lane, is skipped),
run `game_ctl.py smoke --slot N` with MXL_RUN=<NAME>-sN. Meanwhile
  - tools/input_watch.py samples the real cursor and the foreground window every 25 ms; if the cursor enters a game
    window or a game window becomes the foreground window, all games are stopped at once (abort),
  - tools/resource_sample.ps1 samples CPU, RAM and GPU of the machine and of every game process every few seconds.
Writes %USERPROFILE%\\mxl-d2r-runs\\<NAME>\\parallel-summary.md (steps per slot, timings, resources, cursor evidence).
"""
import json
import os
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import game_slots  # noqa: E402
from input_watch import Watcher  # noqa: E402


def read_steps(run_dir):
    rows = []
    try:
        for ln in open(os.path.join(run_dir, "report.md"), encoding="utf-8"):
            p = [c.strip() for c in ln.strip().strip("|").split("|")]
            if len(p) >= 3 and p[1] in ("PASS", "FAIL"):
                rows.append((p[0], p[1], p[3] if len(p) > 3 else ""))
    except OSError:
        pass
    return rows


def main():
    a = sys.argv[1:]
    slots = [int(x) for x in a[a.index("--slots") + 1].split(",")] if "--slots" in a else [1, 2, 3]
    name = a[a.index("--name") + 1] if "--name" in a else time.strftime("parallel-%Y%m%d-%H%M%S")
    runs = game_slots.RUNS
    out = os.path.join(runs, name)
    os.makedirs(out, exist_ok=True)
    active, notes = [], []
    for n in slots:
        owner = f"{name}-s{n}"
        ok, msg = game_slots.acquire(n, owner)
        notes.append(f"slot {n}: {msg}")
        if ok:
            active.append(n)
    print("\n".join(notes), flush=True)
    if not active:
        print("no slot available")
        return 1
    folders = [game_slots.slot_dir(n) for n in active]
    watcher = Watcher(os.path.join(out, "input-watch.json"), folders=folders).start()
    sampler = subprocess.Popen(["powershell", "-NoProfile", "-File", os.path.join(HERE, "resource_sample.ps1"),
                                "-Folders", ",".join(folders), "-Interval", "3"], stdout=subprocess.PIPE, text=True)
    samples = []

    def read_samples():
        for ln in sampler.stdout:
            try:
                samples.append(json.loads(ln))
            except ValueError:
                pass

    threading.Thread(target=read_samples, daemon=True).start()
    t0 = time.time()
    procs, ends = {}, {}
    for n in active:
        env = dict(os.environ, MXL_RUN=f"{name}-s{n}")
        env.pop("MXL_D2R_GAME", None)
        procs[n] = subprocess.Popen([sys.executable, os.path.join(HERE, "game_ctl.py"), "--slot", str(n), "smoke"], env=env,
                                    stdout=open(os.path.join(out, f"smoke-s{n}.out"), "w"), stderr=subprocess.STDOUT)
        time.sleep(4)  # stagger the launches a little so the three loaders do not compile at the same instant
    aborted = None
    while len(ends) < len(procs):
        for n, p in procs.items():
            if n not in ends and p.poll() is not None:
                ends[n] = time.time() - t0
        if os.path.exists(os.path.join(out, "input-watch.json.abort")) and not aborted:
            aborted = open(os.path.join(out, "input-watch.json.abort")).read()
            print("ABORT:", aborted, flush=True)
            for n, p in procs.items():
                p.terminate()
            for n in active:
                subprocess.run([sys.executable, os.path.join(HERE, "game_ctl.py"), "--slot", str(n), "stop"],
                               env=dict(os.environ, MXL_RUN=f"{name}-s{n}"), capture_output=True)
            break
        time.sleep(1)
    wall = time.time() - t0
    cursor = watcher.stop()
    sampler.terminate()
    for n in active:
        game_slots.release(n, f"{name}-s{n}")

    lines = [f"# Parallel smoke {name}", "", f"slots {active}, wall time {wall:.0f} s" + (f", ABORTED: {aborted}" if aborted else ""), ""]
    lines += ["## Steps per slot", "", "| Slot | Time | Steps | Detail |", "|---|---|---|---|"]
    for n in active:
        rows = read_steps(os.path.join(runs, f"{name}-s{n}"))
        passed = sum(1 for r in rows if r[1] == "PASS")
        bad = "; ".join(f"{r[0]}: {r[1]} {r[2]}" for r in rows if r[1] != "PASS") or "-"
        lines.append(f"| {n} | {ends.get(n, wall):.0f} s | {passed}/{len(rows)} passed | {bad} |")
    c = cursor
    lines += ["", "## Real cursor and focus (every 25 ms)", "",
              f"- samples {c['samples']} over {c.get('seconds')} s, longest gap {c['max_gap_ms']} ms, game windows seen {c['game_windows_seen']}",
              f"- cursor before {c['cursor_first']}, after {c['cursor_last']}, x range {c['x_range']}, y range {c['y_range']}, "
              f"{c['cursor_moves']} samples where it moved (the user's own mouse use)",
              f"- cursor over a game window rectangle: {c['cursor_in_game_window']} samples ({c['cursor_moved_in_game_window']} of them while it moved); "
              f"teleports into a game window: {c['teleports_into_game_window']}; a game window as foreground window: {c['game_foreground']} samples",
              f"- first violation: {c['first_violation'] or 'none'}"]
    lines += ["", "## What the games asked Windows for (plugin trace, calls the harness kept away from the desktop)", ""]
    for n in active:
        trace = os.path.join(game_slots.mod_dir(n), "d2rloader", "plugins", "mxl-testharness-trace.log")
        try:
            calls = {}
            for ln in open(trace, encoding="utf-8", errors="replace"):
                for fn in ("NtUserSetCursorPos", "NtUserClipCursor", "NtUserSetForegroundWindow", "NtUserSetActiveWindow"):
                    if ln.startswith(fn + " calls="):
                        calls[fn] = ln.split("calls=")[1].split()[0]
            lines.append(f"- slot {n}: " + ", ".join(f"{k} {v}" for k, v in calls.items()) + " (all answered by the plugin, none reached the system)")
        except OSError:
            lines.append(f"- slot {n}: no trace log")
    if samples:
        cpu = [s["cpu"] for s in samples]
        avail = [s["availMB"] for s in samples]
        gall = [s["gpuAll"] for s in samples]
        lines += ["", "## Resources (3 s samples)", "",
                  f"- machine CPU {sum(cpu) / len(cpu):.0f}% mean, {max(cpu):.0f}% peak ({samples[0]['cores']} logical cores); "
                  f"free RAM min {min(avail) / 1024:.1f} GB; GPU engines sum mean {sum(gall) / len(gall):.0f}%, peak {max(gall):.0f}%"]
        per = {}
        for s in samples:
            for p in s["procs"]:
                per.setdefault(p["pid"], []).append(p)
        for pid, ps in sorted(per.items()):
            lines.append(f"- pid {pid}: CPU {sum(x['cpu'] for x in ps) / len(ps):.0f}% of one core mean ({max(x['cpu'] for x in ps):.0f}% peak), "
                         f"RAM {max(x['ws'] for x in ps)} MB peak, GPU {sum(x['gpu'] for x in ps) / len(ps):.0f}% mean ({max(x['gpu'] for x in ps):.0f}% peak), "
                         f"VRAM {max(x['gpumem'] for x in ps)} MB peak")
        json.dump(samples, open(os.path.join(out, "resources.json"), "w"))
    open(os.path.join(out, "parallel-summary.md"), "w", encoding="utf-8").write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 2 if aborted else 0


if __name__ == "__main__":
    sys.exit(main())

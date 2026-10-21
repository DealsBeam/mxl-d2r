#!/usr/bin/env python
"""Scripted in-game checks for the untested rows of docs/testing.md, in game_ctl's BACKGROUND input mode.

Everything goes through tools/game_ctl.py's background path: console lines run through the plugin test harness
(`g.console_bg`, `g.console_text`), keys and mouse are window messages plus the plugin's virtual cursor, screenshots
are PrintWindow captures. This script never presses Ctrl+`, never calls SetForegroundWindow / SetCursorPos and never
uses ImageGrab. A watchdog thread samples GetCursorPos and GetForegroundWindow every 25 ms for the whole run and
aborts the run when the real cursor enters the game window or the game window becomes the foreground window.

Run with `py -3 tools/game_checks.py all [check ...]` (writes report.md, screenshots and logs into
%USERPROFILE%\\mxl-d2r-runs\\checks-<time>\\). Checks: mxl goblins respec cain heart catalyst lockout lockclock cow
goldcraft. See docs/dev-console.md for the console commands. D2R's own cheat output cannot be read in background
mode (the console panel stays closed): a check verifies a cheat by its effect in the screenshots and by the plugin's
`mxl` output and mxl-core.log.
"""
import ctypes
import ctypes.wintypes as wt
import datetime
import hashlib
import os
import shutil
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_ctl as g  # noqa: E402

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

LOCK = os.path.join(g.RUNS, "game.lock")
SAVES = g.SAVES
user32 = ctypes.windll.user32

# ------------------------------------------------------------------ run folder, lock, watchdog


def new_run():
    """Own run folder for this process (runs/current.txt is shared between lanes)."""
    name = "checks-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    d = os.path.join(g.RUNS, name)
    os.makedirs(d, exist_ok=True)
    g.current_run = lambda create=False: d
    return d


def lock(wait_s=3600):
    t0 = time.time()
    while True:
        try:
            os.mkdir(LOCK)
            return True
        except FileExistsError:
            if time.time() - t0 > wait_s:
                return False
            g.log("game.lock is held, waiting 30 s")
            time.sleep(30)


def unlock():
    try:
        os.rmdir(LOCK)
    except OSError:
        pass


def backup_saves():
    dst = os.path.join(g.RUNS, "save-backup-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    shutil.copytree(SAVES, dst)
    return dst


class Watchdog(threading.Thread):
    """Samples the real cursor and the foreground window every 25 ms."""

    def __init__(self):
        super().__init__(daemon=True)
        self.stop_flag = False
        self.abort = None
        self.first = self.last = None
        self.min = [10 ** 9, 10 ** 9]
        self.max = [-10 ** 9, -10 ** 9]
        self.fg_changes = 0
        self.samples = 0
        self.cursor_moves = 0
        self._fg = None
        self.inside_prev = False
        self.rest_inside = None

    def run(self):
        pt = wt.POINT()
        prev = None
        while not self.stop_flag:
            user32.GetCursorPos(ctypes.byref(pt))
            cur = (pt.x, pt.y)
            fg = user32.GetForegroundWindow()
            self.samples += 1
            if self.first is None:
                self.first = cur
            self.last = cur
            self.min = [min(self.min[0], cur[0]), min(self.min[1], cur[1])]
            self.max = [max(self.max[0], cur[0]), max(self.max[1], cur[1])]
            if prev is not None and cur != prev:
                self.cursor_moves += 1
            prev = cur
            if self._fg is not None and fg != self._fg:
                self.fg_changes += 1
            self._fg = fg
            h = g.find_window()
            if h:
                l, t, r, b = g.client_rect(h)
                inside = l <= cur[0] < r and t <= cur[1] < b
                if inside and not self.inside_prev and prev is not None and cur != prev and not self.abort:
                    # the real cursor MOVED into the game window. A cursor the user left resting over the window
                    # (never moved by this run) is not an abort reason; a move into it is, whoever made it.
                    self.abort = "real cursor moved %s -> %s into the game window %s" % (prev, cur, (l, t, r, b))
                self.inside_prev = inside
                if fg == h and not self.abort:
                    self.abort = "the game window became the foreground window"
            time.sleep(0.025)

    def summary(self):
        return ("%d samples at 25 ms; cursor before %s, after %s, range x %s..%s y %s..%s, %d cursor changes (the user's own "
                "mouse); foreground window changed %d times; abort: %s" % (
                    self.samples, self.first, self.last, self.min[0], self.max[0], self.min[1], self.max[1],
                    self.cursor_moves, self.fg_changes, self.abort))


WD = None


def guard():
    if WD is not None and WD.abort:
        raise RuntimeError("ABORT: " + WD.abort)


# ------------------------------------------------------------------ images (PrintWindow captures)


def grab():
    import numpy as np
    return np.asarray(g.grab_client().convert("RGB")).astype(int)


def clusters(a, b, region, thr=120, minblocks=3):
    """Changed regions between two captures inside region (fractions): [(fx, fy, blocks)] sorted top to bottom."""
    import numpy as np
    h, w, _ = a.shape
    m = np.abs(a - b).sum(axis=2) > thr
    mask = np.zeros_like(m)
    mask[int(region[1] * h):int(region[3] * h), int(region[0] * w):int(region[2] * w)] = True
    m &= mask
    S = 8
    gh, gw = h // S, w // S
    blocks = m[:gh * S, :gw * S].reshape(gh, S, gw, S).sum(axis=(1, 3)) >= 8
    seen = np.zeros_like(blocks)
    out = []
    for y in range(gh):
        for x in range(gw):
            if blocks[y, x] and not seen[y, x]:
                st, pts = [(y, x)], []
                seen[y, x] = True
                while st:
                    cy, cx = st.pop()
                    pts.append((cy, cx))
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            ny, nx = cy + dy, cx + dx
                            if 0 <= ny < gh and 0 <= nx < gw and blocks[ny, nx] and not seen[ny, nx]:
                                seen[ny, nx] = True
                                st.append((ny, nx))
                if len(pts) >= minblocks:
                    ys = [p[0] for p in pts]
                    xs = [p[1] for p in pts]
                    out.append((((min(xs) + max(xs) + 1) * S / 2) / w, ((min(ys) + max(ys) + 1) * S / 2) / h, len(pts)))
    return sorted(out, key=lambda t: (t[1], t[0]))


# ------------------------------------------------------------------ console (plugin harness) and logs

DEBUG_ON = {"v": False}


def con(cmd):
    """Run a console line. D2R's output is not readable in background mode."""
    guard()
    return g.console_bg(cmd)


def cheat(args):
    return con("cheat " + args)


def enable_debug():
    """`debug_mode` TOGGLES debug mode for the whole process; send it once per launch (the state cannot be read back:
    the effect of the next cheat is the check)."""
    if not DEBUG_ON["v"]:
        con("debug_mode")
        time.sleep(0.8)
        DEBUG_ON["v"] = True


def mxl_text():
    """The plugin's `mxl` command output (feature lines with their counters)."""
    return g.console_text("mxl", settle=1.0)


def mxl_mark():
    return g._size(g.MXL_LOG)


def mxl_lines_since(off):
    return [ln for ln in g._read_from(g.MXL_LOG, off).splitlines() if ln.strip()]


def counter(text, key, pattern):
    import re
    for ln in text.splitlines():
        if key in ln:
            m = re.search(pattern, ln)
            if m:
                return int(m.group(1))
    return None


# ------------------------------------------------------------------ menus / entering the game

DIFF_Y = {"normal": 0.42, "nightmare": 0.482, "hell": 0.542}
CARD_Y, CARD_DY = 0.076, 0.095


def to_menu():
    if g.game_pids():
        g.stop()
        time.sleep(2)
    DEBUG_ON["v"] = False
    if not g.start():
        return False
    if not g.clear_intro():
        return False
    time.sleep(2)
    g.click(0.62, 0.40)  # the menu swallows the first click
    time.sleep(1)
    return True


def wait_in_game(timeout=45):
    t0 = time.time()
    while time.time() - t0 < timeout:
        guard()
        if g.in_game():
            return True
        time.sleep(2)
    return False


def play(difficulty="normal"):
    g.click(*g.UI["menu_play"])
    if wait_in_game(8):
        return True
    time.sleep(2)
    g.click(0.5, DIFF_Y[difficulty])
    if wait_in_game(45):
        return True
    g.click(*g.UI["menu_play"])
    time.sleep(3)
    g.click(0.5, DIFF_Y[difficulty])
    return wait_in_game(45)


STATE = {"in_game": False}


def save_exit():
    for _ in range(3):  # close panels / dialogs; the last Esc opens the pause menu
        g.key("esc")
        time.sleep(1.0)
    g.click(*g.UI["save_exit"])
    time.sleep(9)


def leave():
    if STATE["in_game"]:
        save_exit()
        STATE["in_game"] = False


def _checksum(d):
    d = bytearray(d)
    d[8:12] = len(d).to_bytes(4, "little")
    d[12:16] = b"\0\0\0\0"
    c = 0
    for b in d:
        c = ((c << 1) | (c >> 31)) & 0xffffffff
        c = (c + b) & 0xffffffff
    d[12:16] = c.to_bytes(4, "little")
    return bytes(d)


def patch_quests(name, quests):
    """Set 16-bit quest words {(difficulty, quest): word} in a closed character ('Woo!' section, 48 words per
    difficulty after 10 header bytes), fix the checksum and D2RLoader's `.d2rl` sidecar (docs/save-format.md)."""
    p = os.path.join(SAVES, name + ".d2s")
    s = bytearray(open(p, "rb").read())
    i = bytes(s).find(b"Woo!")
    for (diff, q), w in quests.items():
        struct.pack_into("<H", s, i + 10 + diff * 96 + q * 2, w)
    s = bytearray(_checksum(bytes(s)))
    open(p, "wb").write(bytes(s))
    sp = os.path.join(SAVES, name + ".d2rl")
    r = bytearray(open(sp, "rb").read())
    struct.pack_into("<I", r, 0x34, struct.unpack_from("<I", s, 12)[0])
    struct.pack_into("<I", r, 0x38, len(s))
    r[0x3c:0x5c] = hashlib.sha256(bytes(s)).digest()
    r[0x0c:0x2c] = hashlib.sha256(bytes(r[0x2c:])).digest()
    open(sp, "wb").write(bytes(r))


CARDS = {"MxlQ": 0, "MxlBarb": 1}


def enter(name, difficulty="normal", quests=None, god=True):
    """Leave any running game, optionally patch the closed save, play the character, switch debug mode on."""
    leave()
    if quests:
        patch_quests(name, quests)
    g.click(0.87, CARD_Y + CARD_DY * CARDS[name])
    time.sleep(0.8)
    ok = play(difficulty)
    STATE["in_game"] = ok
    if not ok:
        return False
    time.sleep(2)
    enable_debug()
    if god:
        cheat("god 1")
    return True


# ------------------------------------------------------------------ helpers for checks

INV_REGION = (0.645, 0.42, 0.925, 0.72)
CUBE_REGION = (0.08, 0.29, 0.35, 0.60)


def attack(fx, fy, n=1):
    """Left click on an adjacent monster: the character attacks it (no real input, virtual cursor)."""
    for _ in range(n):
        g.click(fx, fy)


def find_spawn(cmd, region=(0.2, 0.15, 0.8, 0.78), fallback=(0.5, 0.35)):
    """Run a spawn cheat and return the centre of what appeared (fractions), by diffing two captures around it."""
    A = grab()
    time.sleep(0.5)
    B = grab()
    cheat(cmd)
    time.sleep(1.5)
    C = grab()
    import numpy as np
    h, w, _ = C.shape
    d = np.abs(C - B).sum(axis=2) > 90
    n = np.abs(B - A).sum(axis=2) > 30
    for dy in range(-6, 7, 3):
        for dx in range(-6, 7, 3):
            n = n | np.roll(np.roll(np.abs(B - A).sum(axis=2) > 30, dy, 0), dx, 1)
    m = d & ~n
    mask = np.zeros_like(m)
    mask[int(region[1] * h):int(region[3] * h), int(region[0] * w):int(region[2] * w)] = True
    m &= mask
    ys, xs = np.nonzero(m)
    if len(xs) < 200:
        return fallback
    # densest 120 x 160 window
    best, bx, by = 0, 0, 0
    for yy in range(int(region[1] * h), int(region[3] * h) - 160, 20):
        for xx in range(int(region[0] * w), int(region[2] * w) - 120, 20):
            v = int(m[yy:yy + 160, xx:xx + 120].sum())
            if v > best:
                best, bx, by = v, xx, yy
    sub = np.nonzero(m[by:by + 160, bx:bx + 120])
    return (bx + float(sub[1].mean())) / w, (by + float(sub[0].mean())) / h


def open_inventory():
    g.key("i")
    time.sleep(1.0)


def spawn_inv(codes):
    """Inventory open: spawn the items straight into it (`spawn <code>,inv`); returns the clusters that appeared."""
    before = grab()
    for code in codes:
        cheat("spawn %s,inv" % code)
    time.sleep(1.2)
    return clusters(before, grab(), INV_REGION)


def cube_try(codes, tag):
    """Fresh inventory (`itemgridclear`, a new cube), cube panel open, spawn `codes` into the inventory, move them into
    the cube, Transmute. Returns (cube items before, after, shots)."""
    open_inventory()
    cheat("itemgridclear")
    cheat("spawn box,inv")
    time.sleep(1.0)
    g.click(0.677, 0.452, "right")  # the Horadric Cube at the inventory's top left cell
    time.sleep(1.2)
    empty = grab()
    items = spawn_inv(codes)
    if len(items) < len(codes):
        return None, None, [sh(tag + "_spawn_failed")]
    for k, (fx, fy, _) in enumerate(items):
        g.click(fx, fy)
        time.sleep(0.5)
        g.click(0.12 + 0.07 * k, 0.33)
        time.sleep(0.5)
    s1 = sh(tag + "_loaded")
    n0 = len(clusters(empty, grab(), CUBE_REGION))
    g.click(0.209, 0.697)  # Transmute
    time.sleep(2.5)
    s2 = sh(tag + "_after")
    n1 = len(clusters(empty, grab(), CUBE_REGION))
    return n0, n1, [s1, s2]


class Result:
    def __init__(self, name, status, note, shots=(), log=()):
        self.name, self.status, self.note, self.shots, self.log = name, status, note, list(shots), list(log)


RESULTS = []


def add(name, status, note, shots=(), log=()):
    RESULTS.append(Result(name, status, note, shots, log))
    g.log("CHECK %s: %s %s" % (name, status, note))


def sh(name):
    return os.path.basename(g.shot(name))


# ------------------------------------------------------------------ checks


def check_mxl():
    if not enter("MxlQ", "normal"):
        return add("mxl console command", "BLOCKED", "could not enter the game")
    txt = mxl_text()
    wanted = ["states fix", "level types", "calc codes", "skill functions", "item spells", "missile hits", "mana costs",
              "cube conditions", "craft flags", "act 1 quests", "dungeon lockout", "goblins on",
              "npc actions on", "skill tree on"]
    missing = [w for w in wanted if w not in txt]
    add("`mxl` console command output", "PASS" if not missing else "FAIL",
        "console_text('mxl') returned %d lines with all %d feature lines; first: %s" % (
            len(txt.splitlines()), len(wanted), txt.splitlines()[0][:90] if txt else "-") if not missing
        else "missing: %s" % missing, [sh("mxl_town")], txt.splitlines())


def check_goblins():
    name = "Treasure goblin spawns in the Blood Moor after a kill (MXL_GOBLINS_FORCE=1)"
    if not enter("MxlQ", "normal"):
        return add(name, "BLOCKED", "could not enter the game")
    cheat("goto 2")
    time.sleep(7)
    m = mxl_mark()
    lines = []
    for _ in range(4):
        x, y = find_spawn("mspawn fallen1")
        attack(x, y, 6)
        lines = [ln for ln in mxl_lines_since(m) if "goblin" in ln and "spawned" in ln]
        if lines:
            break
        time.sleep(16)
    time.sleep(2)
    shots = [sh("goblin_scene")]
    txt = mxl_text()
    gl = [ln for ln in txt.splitlines() if "goblins on" in ln]
    add(name, "PASS" if lines else "FAIL",
        "log: %s; mxl: %s" % (lines[0][-85:] if lines else "no goblin line in 4 kills / 70 s", gl[0][:140] if gl else "-"),
        shots, lines)


def stat_crop():
    return grab()[620:670, 140:460]


def check_respec():
    name = "Akara respec after Den of Evil (Reset Stat/Skill Points)"
    if not enter("MxlQ", "normal", quests={(0, 41): 2}):
        return add(name, "BLOCKED", "could not enter the game")
    import numpy as np
    cheat("level 12")
    time.sleep(1)
    ax, ay = find_spawn("mspawn akara")
    g.key("c")
    time.sleep(1.2)
    A = stat_crop()
    sA = sh("respec_1_sheet_before")
    for _ in range(10):
        g.click(0.186, 0.573)
    time.sleep(0.5)
    B = stat_crop()
    sB = sh("respec_2_after_spending")
    g.key("esc")
    time.sleep(0.6)
    g.click(ax, ay)  # click Akara; her menu opens over her head
    time.sleep(3)
    sM = sh("respec_3_menu")
    m = mxl_mark()
    g.click(ax, ay - 0.12)  # menu lines (measured from the click on Akara): the plugin's Reset line is 0.12 above her
    time.sleep(2.5)
    lines = [ln for ln in mxl_lines_since(m) if "npc actions" in ln]
    g.key("c")
    time.sleep(1.2)
    C = stat_crop()
    sC = sh("respec_4_sheet_after")
    d_ab = float(np.abs(A - B).mean())
    d_ac = float(np.abs(A - C).mean())
    done = any("done" in ln for ln in lines)
    add(name, "PASS" if (done and d_ab > 0.5 and d_ac < 0.5) else "FAIL",
        "log %s; stat text change by spending %.1f, difference to the start after the reset %.2f" % (
            "`npc actions: done (48, 148)`" if done else "has no done line: %s" % [ln[-60:] for ln in lines], d_ab, d_ac),
        [sA, sB, sM, sC], lines)


def check_cain():
    name = "Cain crafting materials line (before the rescue)"
    if not enter("MxlQ", "normal"):
        return add(name, "BLOCKED", "could not enter the game")
    m = mxl_mark()
    cx, cy = find_spawn("mspawn deckard_cain")
    g.click(cx, cy)
    time.sleep(3)
    s = sh("cain_menu")
    lines = [ln for ln in mxl_lines_since(m) if "npc actions" in ln]
    full = [ln for ln in lines if "menu record is full" in ln]
    add(name, "FAIL" if full else "PASS",
        "log: %s (known bug #23: Cain's menu record has no room for the line; look at cain_menu.png: Talk and Cancel only)" % (
            full[0][-90:] if full else "no 'record is full' line"), [s], lines)


def check_heart():
    name = "Griswold's Heart from Akara after Cain (quest 4 bit 1)"
    if not enter("MxlQ", "normal", quests={(0, 4): 2}):
        return add(name, "BLOCKED", "could not enter the game")
    open_inventory()
    base = grab()
    g.key("i")
    time.sleep(0.5)
    ax, ay = find_spawn("mspawn akara")
    g.click(ax, ay)
    time.sleep(4)
    s1 = sh("heart_talk1")
    g.key("esc")
    time.sleep(1)
    g.key("esc")
    time.sleep(0.6)
    g.click(ax, ay)
    time.sleep(4)
    s2 = sh("heart_talk2")
    g.key("esc")
    time.sleep(1)
    open_inventory()
    after = grab()
    s3 = sh("heart_inventory")
    new = clusters(base, after, INV_REGION)
    add(name, "PASS" if new else "FAIL",
        "%d new inventory item(s) after talking to Akara twice with quest 4 bit 1 set (patched save). Unique name/stats "
        "not read: check heart_inventory.png" % len(new), [s1, s2, s3])


def kill_andariel(tries=30):
    x, y = find_spawn("mspawn 156")
    attack(x, y + 0.03, tries)


def check_catalyst():
    name = "Andariel catalyst of disenchantment (Normal), not twice; Warriv re-issue"
    if not enter("MxlQ", "normal"):
        return add(name, "BLOCKED", "could not enter the game")
    cheat("easykill 1")
    open_inventory()
    base = grab()
    g.key("i")
    time.sleep(0.5)
    cheat("goto 37")
    time.sleep(7)
    kill_andariel()
    time.sleep(4)
    shots = [sh("catalyst_after_kill1")]
    open_inventory()
    one = grab()
    shots.append(sh("catalyst_inv_after_kill1"))
    g.key("i")
    time.sleep(0.5)
    new1 = clusters(base, one, INV_REGION)
    kill_andariel()
    time.sleep(4)
    open_inventory()
    two = grab()
    shots.append(sh("catalyst_inv_after_kill2"))
    g.key("i")
    new2 = clusters(one, two, INV_REGION)
    ok = len(new1) >= 1 and len(new2) == 0
    add(name, "PASS" if ok else "BLOCKED",
        "new inventory items after kill 1: %d, after kill 2: %d. If Andariel is still alive in catalyst_after_kill1.png "
        "the kill did not happen: no cheat found that kills her. Needs a harness command `kill <monster id>` (or `damage "
        "unit N`) and `quest <id> <state>` to put quest 6 in progress" % (len(new1), len(new2)), shots)


def portal_scroll(code, tag):
    """Spawn a scroll into the inventory, use it, return the portal position (fraction) or None."""
    open_inventory()
    items = spawn_inv([code])
    if not items:
        return None, [sh(tag + "_nopickup")]
    A = grab()
    g.click(items[0][0], items[0][1], "right")
    time.sleep(0.5)
    g.key("i")
    time.sleep(2.5)
    B = grab()
    cl = clusters(A, B, (0.2, 0.15, 0.8, 0.8), thr=150, minblocks=6)
    return ((cl[0][0], cl[0][1]) if cl else None), [sh(tag + "_portal")]


def lock_counters(txt):
    return (counter(txt, "dungeon lockout", r"refused (\d+)"), counter(txt, "dungeon lockout", r"clocks started (\d+)"))


def check_lockout():
    name = "Dungeon lockout: entry refused by level/difficulty (Normal level 12, level 251 portal)"
    if not enter("MxlQ", "normal"):
        return add(name, "BLOCKED", "could not enter the game")
    cheat("goto 1")
    time.sleep(6)
    r0, c0 = lock_counters(mxl_text())
    pos, shots = portal_scroll("b61X", "lock_normal")
    if pos is None:
        return add(name, "BLOCKED", "no portal visible after using `spawn b61X` (scroll not picked up or the portal "
                   "position not detected); counters before: refused %s clocks %s" % (r0, c0), shots)
    g.click(*pos)
    time.sleep(6)
    shots.append(sh("lock_normal_after_click"))
    r1, c1 = lock_counters(mxl_text())
    ok = r0 is not None and r1 == r0 + 1
    add(name, "PASS" if ok else "FAIL", "`mxl` dungeon-lockout counters: refused %s -> %s, clocks started %s -> %s" % (r0, r1, c0, c1), shots)


def check_lockout_clock():
    n1 = "Dungeon lockout: clock starts on entry; re-entry inside the lock time (Hell, level 251, 60 s)"
    n2 = "Dungeon lockout: level sealed after the lock time"
    if not enter("MxlBarb", "hell"):
        add(n1, "BLOCKED", "could not enter MxlBarb in Hell")
        return add(n2, "BLOCKED", "could not enter MxlBarb in Hell")
    cheat("goto 109")  # Harrogath: the scroll needs the matching act? the portal opens in any act
    time.sleep(6)
    r0, c0 = lock_counters(mxl_text())
    pos, shots = portal_scroll("b61X", "lock_hell")
    if pos is None:
        add(n1, "BLOCKED", "no portal visible after `spawn b61X`", shots)
        return add(n2, "BLOCKED", "no portal", shots)
    g.click(*pos)
    time.sleep(8)
    t0 = time.time()
    shots.append(sh("lock_hell_inside"))
    r1, c1 = lock_counters(mxl_text())
    cheat("goto 109")
    time.sleep(6)
    g.click(*pos)
    time.sleep(7)
    shots.append(sh("lock_hell_reenter"))
    r2, c2 = lock_counters(mxl_text())
    add(n1, "PASS" if (c1 == (c0 or 0) + 1 and r2 == r1) else "FAIL",
        "counters (refused, clocks): start %s/%s, after entry %s/%s, after re-entry %s/%s" % (r0, c0, r1, c1, r2, c2), shots)
    cheat("goto 109")
    time.sleep(6)
    while time.time() - t0 < 75:
        guard()
        time.sleep(3)
    g.click(*pos)
    time.sleep(7)
    s3 = sh("lock_hell_sealed")
    r3, c3 = lock_counters(mxl_text())
    lines = [ln for ln in mxl_lines_since(0) if "lockout" in ln and "sealed" in ln]
    add(n2, "PASS" if (r3 or 0) > (r2 or 0) else "FAIL", "after 75 s: refused %s -> %s; sealed log lines: %d" % (r2, r3, len(lines)),
        [s3], lines[-3:])


def check_cow():
    for diff, town, who in (("hell", True, "MxlBarb"), ("hell", False, "MxlBarb"), ("normal", True, "MxlQ")):
        nm = "Cow portal recipe (%s, %s)" % (diff, "town" if town else "field")
        tag = "cow_%s_%s" % (diff, "town" if town else "field")
        if not (STATE.get("cow") == diff and STATE["in_game"]):
            if not enter(who, diff):
                add(nm, "BLOCKED", "could not enter the game")
                continue
            STATE["cow"] = diff
        if not town:
            cheat("goto 2")
            time.sleep(7)
        n0, n1, shots = cube_try(["leg", "tbk"], tag)
        if n0 is None:
            add(nm, "BLOCKED", "the spawned Wirt's Leg / Tome were not picked up into the inventory (inventory full or the "
                "pickup cheat missed)", shots)
            continue
        ran = n1 < n0
        want = diff == "hell" and town
        add(nm, "PASS" if ran == want else "FAIL",
            "cube items %d -> %d after Transmute; expected %s (recipe row 3: min difficulty Hell, op 36 town only)" % (
                n0, n1, "the recipe to run" if want else "nothing"), shots)
        g.key("esc")
        time.sleep(0.5)


def check_goldcraft():
    add("Cube gold payment (rows 6327-6329) and craft flags (7100, 6369 -> 6350)", "BLOCKED",
        "needs items no cheat creates: stat 532 on an item (6327-6329), UpgradedBossKillerRing + erun + f700 (7100), "
        "RathmasSupremacy2 as an assassin (6369), and a way to read carried/banked gold exactly. Harness commands "
        "needed: `item <code> [stat=value ...]` to create an item with stats in the inventory, `flag player|game N "
        "get|set|clear`, `gold get` (write into mxl-testharness-console.txt), `class` change is already a cheat "
        "(`cheat class ass`).")


CHECKS = [("mxl", check_mxl), ("goblins", check_goblins), ("respec", check_respec), ("cain", check_cain),
          ("heart", check_heart), ("catalyst", check_catalyst), ("lockout", check_lockout), ("cow", check_cow),
          ("lockclock", check_lockout_clock), ("goldcraft", check_goldcraft)]


def write_report(extra=""):
    p = os.path.join(g.current_run(), "report.md")
    with open(p, "w", encoding="utf-8") as f:
        f.write("# Game checks (background input) %s\n\n" % os.path.basename(g.current_run()))
        f.write("| Check | Result | Note | Evidence |\n|---|---|---|---|\n")
        for r in RESULTS:
            f.write("| %s | %s | %s | %s |\n" % (r.name, r.status, r.note.replace("|", "/"), ", ".join(r.shots)))
        f.write("\n" + extra + "\n")
        for r in RESULTS:
            if r.log:
                f.write("\n### log: %s\n\n```\n%s\n```\n" % (r.name, "\n".join(x[:230] for x in r.log)))
    g.log("report " + p)
    return p


def run_all(only=None):
    global WD
    new_run()
    if not lock():
        print("game.lock held for 20 min, giving up")
        return 2
    os.environ["MXL_GOBLINS_FORCE"] = "1"
    WD = Watchdog()
    WD.start()
    try:
        backup_saves()
        g.snapshot_marks()
        if not to_menu():
            add("start game", "BLOCKED", "game did not reach the main menu")
        else:
            for name, fn in CHECKS:
                if only and name not in only:
                    continue
                try:
                    guard()
                    fn()
                except Exception as e:
                    add(name, "BLOCKED", "script error: %r" % (e,))
                    if WD.abort:
                        break
    finally:
        WD.stop_flag = True
        try:
            g.stop()
        finally:
            unlock()
            r = g.logscan()
            write_report("## Real input watchdog\n\n" + WD.summary() + "\n\n## logscan\n\n```\n" + r["text"] + "\n```")
            print(WD.summary())
    return 0


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    if a[0] == "all":
        sys.exit(run_all(a[1:] or None))
    print(__doc__)


if __name__ == "__main__":
    main()

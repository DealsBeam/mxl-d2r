#!/usr/bin/env python
"""Scripted in-game checks for the untested rows of docs/testing.md, in game_ctl's BACKGROUND input mode.

Everything goes through tools/game_ctl.py's background path: console lines run through the plugin test harness
(`g.console_bg`, `g.console_text`), keys and mouse are window messages plus the plugin's virtual cursor, screenshots
are PrintWindow captures. This script never presses Ctrl+`, never calls SetForegroundWindow / SetCursorPos and never
uses ImageGrab. A watchdog thread samples GetCursorPos and GetForegroundWindow every 25 ms for the whole run and
aborts the run when the real cursor enters the game window or the game window becomes the foreground window.

Run with `py -3 tools/game_checks.py all [check ...]` (writes report.md, screenshots and logs into
%USERPROFILE%\\mxl-d2r-runs\\checks-<time>\\); `--slot N` picks a game slot. Checks: mxl goblins respec cain heart
catalyst lockout cow goldcraft craftflags lockclock reenter areacycle. What a click in the world cannot do in background mode (kill a
boss, talk to an NPC, set quest bits, create items with stats, transmute, open a portal) is done by the plugin's
harness gameplay commands (plugin/src/harness_cmds.cpp, `g.hcmd`, docs/testing.md "Harness commands"): they call the
game's own functions, no simulated input, and write their answers to mxl-testharness-console.txt. Only menu / UI clicks
and D2R's own `cheat goto` (area change inside an act) still go through the window.
"""
import ctypes
import ctypes.wintypes as wt
import datetime
import hashlib
import os
import re
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


def enter_game():
    """From the main menu: play the selected character (the first card: the slot's most recently played one) in Normal."""
    g.click(0.87, 0.076)
    time.sleep(0.8)
    ok = play("normal")
    STATE["in_game"] = ok
    return ok


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


# ------------------------------------------------------------------ the plugin's gameplay commands (tools/game_ctl.hcmd)

def H(cmd, timeout=15):
    """One harness gameplay command (plugin/src/harness_cmds.cpp): (ok, lines). No mouse, no keyboard."""
    guard()
    return g.hcmd(cmd, timeout)


def kv(line):
    return {k: v for k, v in re.findall(r"(\w+)=(\S+)", line)}


def hstate():
    """`h state` parsed: player dict, items (dicts), mons, objs, plugin counters, npc menu."""
    ok, lines = H("state")
    st = {"ok": ok, "player": {}, "items": [], "mons": [], "objs": [], "plugin": "", "menu": {}, "lines": lines}
    for ln in lines:
        if ln.startswith("player "):
            st["player"] = {k: (int(v) if re.fullmatch(r"-?\d+", v) else v) for k, v in kv(ln).items()}
        elif ln.startswith("item "):
            st["items"].append({k: (int(v) if re.fullmatch(r"-?\d+", v) else v) for k, v in kv(ln).items()})
        elif ln.startswith("mon "):
            st["mons"].append({k: int(v) for k, v in kv(ln).items() if re.fullmatch(r"-?\d+", v)})
        elif ln.startswith("obj "):
            st["objs"].append({k: int(v) for k, v in kv(ln).items() if re.fullmatch(r"-?\d+", v)})
        elif ln.startswith("plugin "):
            st["plugin"] = ln
        elif ln.startswith("npcmenu "):
            st["menu"] = {k: int(v) for k, v in kv(ln).items()}
    return st


def items(st, code, page=None):
    return [i for i in st["items"] if str(i.get("code")) == code and (page is None or i.get("page") == page)]


def ground_count(code):
    """Items of this code lying on the ground (`h dump ground` lists every item unit; mode 3 / 5 = on the ground): the quest
    bonus drops land there (one per player)."""
    ok, lines = H("dump ground")
    return len([l for l in lines if l.startswith("ground ") and ("code=%s " % code) in l and re.search(r"mode=[35] ", l)])


def stat_of(lines):
    m = re.search(r"= (-?\d+)", lines[0]) if lines else None
    return int(m.group(1)) if m else None


def lockout_counts(st):
    m = re.search(r"refused=(\d+) clocks=(\d+)", st["plugin"])
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def goblins_counts(st):
    m = re.search(r"(\d+) spawned .*?(\d+) rolls", st["plugin"])
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def goto(area, timeout=40):
    """Move the character to an area: D2R's own `cheat goto` (debug mode) for the same act, the plugin's `level`
    (the NPC travel) across acts. Waits until `h state` says so."""
    for attempt in range(2):
        enable_debug()
        con("cheat goto %d" % area) if attempt == 0 else H("level %d" % area)
        t0 = time.time()
        while time.time() - t0 < timeout:
            time.sleep(2)
            st = hstate()
            if st["player"].get("area") == area:
                time.sleep(2)
                return st
    return hstate()


def wait_menu(timeout=60):
    """The NPC menu opens when the NPC's speech has played; returns the `menu list` lines or None."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        guard()
        ok, lines = g.hcmd("menu list", 5)
        if ok:
            return lines
        time.sleep(3)
    return None


def npc_menu(npc_class, spawn=True):
    """Stand next to an NPC (spawn it when it is not loaded), talk, wait for the menu. Returns the menu lines."""
    ok, lines = H("interact %d" % npc_class)  # the town's own NPC: its speech ends with the menu
    if not ok and spawn:
        H("spawn %d" % npc_class)
        ok, lines = H("interact %d" % npc_class)
    if not ok:
        return None
    menu = wait_menu(40)
    if menu is None:  # the first click of a speech can be lost: talk again
        close_menu()
        H("interact %d" % npc_class)
        menu = wait_menu(40)
    return menu


def close_menu():
    g.hcmd("menu cancel", 5)


def kill_all_near():
    for _ in range(6):
        ok, _l = H("kill nearest")
        if not ok:
            break


# ------------------------------------------------------------------ results


class Result:
    def __init__(self, name, status, note, shots=(), log=()):
        self.name, self.status, self.note, self.shots, self.log = name, status, note, list(shots), list(log)


RESULTS = []


def add(name, status, note, shots=(), log=()):
    RESULTS.append(Result(name, status, note, shots, log))
    g.log("CHECK %s: %s %s" % (name, status, note))


def sh(name):
    return os.path.basename(g.shot(name))


def need_game():
    if not STATE["in_game"]:
        return False
    st = hstate()
    return st["ok"]


# ------------------------------------------------------------------ checks


def check_mxl():
    if not need_game():
        return add("mxl console command", "BLOCKED", "not in a game")
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
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    st = goto(2)
    if st["player"].get("area") != 2:
        return add(name, "BLOCKED", "could not reach the Blood Moor (area %s)" % st["player"].get("area"))
    s0, r0 = goblins_counts(st)
    m = mxl_mark()
    log = []
    for rnd in range(4):
        H("spawn 18 2")
        H("kill nearest 18")
        ok, l = H("kill nearest 18")
        log += l
        time.sleep(18)  # the tick runs every 375 frames (15 s)
        st = hstate()
        s1, r1 = goblins_counts(st)
        if s1 is not None and s0 is not None and s1 > s0:
            break
    gob = [mm for mm in st["mons"] if mm.get("class", 0) >= 2700]
    lines = [ln for ln in mxl_lines_since(m) if "goblin" in ln and "spawned" in ln]
    shots = [sh("goblin_scene")]
    add(name, "PASS" if (s1 or 0) > (s0 or 0) else "FAIL",
        "`h state` counters: spawned %s -> %s, rolls %s -> %s; goblin monsters alive near the player: %s; log: %s" % (
            s0, s1, r0, r1, [(mm["class"], mm["dist"]) for mm in gob], lines[0][-80:] if lines else "-"),
        shots, lines + [st["plugin"]])
    goto(1)
    kill_all_near()


def check_respec():
    name = "Akara respec after Den of Evil (Reset Stat/Skill Points)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    H("quest 1 0 set")   # Den of Evil done
    H("quest 41 1 set")  # its respec reward is waiting
    base0 = stat_of(H("stat player 0")[1])
    base4 = stat_of(H("stat player 4")[1])
    H("stat player 0 set %d" % (base0 + 60))   # 60 points "spent" on strength
    menu = npc_menu(148)
    if not menu:
        return add(name, "BLOCKED", "Akara's menu did not open (menu list: %s)" % menu)
    shot = sh("respec_menu")
    plugin_lines = [ln for ln in menu if "handler=plugin" in ln]
    if not plugin_lines:
        return add(name, "FAIL", "no plugin line in Akara's menu: %s" % menu, [shot], menu)
    idx = int(re.search(r"line (\d+)", plugin_lines[0]).group(1))
    m = mxl_mark()
    ok, ml = H("menu %d" % idx)
    time.sleep(1)
    _, after0 = H("stat player 0")
    _, after4 = H("stat player 4")
    lines = [ln for ln in mxl_lines_since(m) if "npc actions" in ln]
    s0 = stat_of(after0)
    s4 = stat_of(after4)
    _, q = H("quest 41 dump")
    done = any("done" in ln for ln in lines)
    # the character's strength goes back to the class start (Barbarian 30) and every point spent comes back as unused points
    refunded = (base0 + 60) - s0 if s0 is not None else None
    add(name, "PASS" if (ok and done and s0 == 30 and s4 == base4 + refunded) else "FAIL",
        "menu lines %s; strength %d -> %s (Barbarian start 30), unused stat points %d -> %s (%s refunded); log `%s`; quest 41 %s" % (
            [ln.strip() for ln in menu[1:]], base0 + 60, s0, base4, s4, refunded, lines[-1][-60:] if lines else "none", q[0]),
        [shot], menu + ml + lines)
    close_menu()


def check_cain():
    name = "Cain crafting materials line (bug #23)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    m = mxl_mark()
    menu = npc_menu(265)
    time.sleep(1)
    s = sh("cain_menu")
    lines = [ln for ln in mxl_lines_since(m) if "npc actions" in ln]
    full = [ln for ln in lines if "full" in ln]
    # The popup is read from the screenshot (top left of the window: DECKARD CAIN / TALK / CRAFTING MATERIALS / CANCEL);
    # the script can only assert the log and that the menu opened.
    add(name, "PASS" if (menu and not full) else "FAIL",
        "menu opened by `h interact 265`: %s; no `menu record is full` log line: %s. Popup text must be read from the "
        "screenshot (cain_menu.png)" % (bool(menu), not full), [s], (menu or []) + lines)
    close_menu()


def check_heart():
    name = "Griswold's Heart from Akara after Cain (quest 4 bit 1)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    close_menu()
    H("quest 4 1 set")
    H("packclear")  # room in the backpack: the Heart would be dropped on the floor otherwise
    H("remove 600 6")   # earlier runs' hearts
    H("remove rin 6")
    st0 = hstate()
    n0 = len(items(st0, "600"))
    ok0, _l0 = H("interact 148")
    if not ok0:
        H("spawn 148")
        H("interact 148")
    ok, ml = H("msg 148 0x76")
    for _ in range(15):  # (Akara's own speech sends the same message when it ends)
        time.sleep(2)
        st1 = hstate()
        if items(st1, "600"):
            break
    new = [i for i in items(st1, "600") if i["id"] not in [j["id"] for j in items(st0, "600")]]
    ring = [i for i in items(st1, "rin") if i["id"] not in [j["id"] for j in items(st0, "rin")]]
    _, q = H("quest 4 dump")
    add(name, "PASS" if (new and new[0].get("unique") == 69) else "FAIL",
        "after `h msg 148 0x76`: new item(s) with base code 600: %s (unique index %s, Griswold's Heart = 69), vanilla ring "
        "reward: %s; quest 4 bits %s" % (len(new), new[0].get("unique") if new else "-", bool(ring), q[0]),
        [sh("heart")], ["before: %s" % [(i["id"], i["page"], i.get("unique")) for i in items(st0, "600")], "after: %s" % [(i["id"], i["page"], i.get("unique")) for i in items(st1, "600")]] + ml + [x for x in mxl_lines_since(0)[-6:]])
    close_menu()


def check_catalyst():
    name = "Andariel catalyst of disenchantment (Normal), not twice; Warriv re-issue"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    close_menu()
    H("remove 3o[ 5")
    H("quest 47 0 clear")  # the plugin's "catalyst handed out" flag lives in the character's quest record: clear it
    H("difficulty 0")
    H("spawn 156")
    ok1, l1 = H("kill nearest 156")
    time.sleep(1)
    st1 = hstate()
    n1 = len(items(st1, "3o[")) + ground_count("3o[")
    H("spawn 156")
    H("kill nearest 156")
    time.sleep(1)
    st2 = hstate()
    n2 = len(items(st2, "3o[")) + ground_count("3o[")
    H("remove 3o[ 5")
    H("spawn 155")
    ok3, l3 = H("interact 155")
    time.sleep(2)
    st3 = hstate()
    n3 = len(items(st3, "3o["))
    _, q = H("quest 47 dump")
    ok = ok1 and n1 == 1 and n2 == 1 and n3 == 1
    add(name, "PASS" if ok else "FAIL",
        "catalysts in the inventory or on the ground: after kill 1 %d, after kill 2 %d (must stay 1), after removing it and talking to Warriv "
        "%d (re-issued); flag quest 47 %s" % (n1, n2, n3, q[0]), [sh("catalyst")], l1 + l3)
    close_menu()


def check_lockout():
    name = "Dungeon lockout: entry refused by level/difficulty (Normal level 1, portal to level 251)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    H("difficulty 0")
    st = hstate()
    r0, c0 = lockout_counts(st)
    H("item b60X")
    ok, l = H("use b60X")
    time.sleep(1.5)
    ok2, l2 = H("enter nearest")
    time.sleep(1.5)
    st1 = hstate()
    r1, c1 = lockout_counts(st1)
    add(name, "PASS" if (ok and r1 == (r0 or 0) + 1 and st1["player"]["area"] == 1) else "FAIL",
        "`use b60X` opened the portal (%s); `enter nearest`: %s; counters in `h state`: refused %s -> %s, clocks %s -> %s, "
        "still in area %s" % (ok, l2[0] if l2 else "-", r0, r1, c0, c1, st1["player"]["area"]), [sh("lock_normal")], l + l2)


def check_lockout_clock():
    n1 = "Dungeon lockout: clock starts on entry; re-entry inside the lock time (Hell rules, level 253 infernal_cave_2, 60 s)"
    n2 = "Dungeon lockout: level sealed after the lock time"
    if not need_game():
        add(n1, "BLOCKED", "not in a game")
        return add(n2, "BLOCKED", "not in a game")
    goto(1)
    lvl0 = hstate()["player"]["level"]
    H("difficulty 2")
    H("stat player 12 set 125")
    H("stat player 7 set 20000000")
    H("stat player 6 set 20000000")
    H("item b62X")
    ok, l = H("use b62X")
    r0, c0 = lockout_counts(hstate())

    def enter_portal():
        for _ in range(3):  # the portal object needs a frame or two before it works
            time.sleep(2)
            ok, ll = H("enter nearest")
            if ll and "moved" in ll[0]:
                return True, ll
        return False, ll

    moved, ll = enter_portal()
    t0 = time.time()
    time.sleep(4)
    st1 = hstate()
    r1, c1 = lockout_counts(st1)
    a1 = st1["player"].get("area")
    shots = [sh("lock_hell_inside")]
    goto(1)
    moved2, ll2 = enter_portal()
    time.sleep(3)
    st2 = hstate()
    r2, c2 = lockout_counts(st2)
    a2 = st2["player"].get("area")
    goto(1)
    ok1 = moved and a1 == 253 and c1 == (c0 or 0) + 1 and moved2 and r2 == r1
    add(n1, "PASS" if ok1 else "FAIL",
        "Hell rules (`h difficulty 2`, level 125): entry %s (area %s), counters (refused, clocks) %s/%s -> %s/%s; "
        "re-entry after %.0f s: %s (area %s), counters %s/%s" % (moved, a1, r0, c0, r1, c1, time.time() - t0, moved2, a2, r2, c2),
        shots, l + ll + ll2)
    while time.time() - t0 < 68:
        guard()
        time.sleep(3)
    for _ in range(2):
        ok3, ll3 = H("enter nearest")
        time.sleep(2)
    st3 = hstate()
    r3, c3 = lockout_counts(st3)
    a3 = st3["player"].get("area")
    add(n2, "PASS" if (r3 or 0) > (r2 or 0) and a3 == 1 else "FAIL",
        "68 s after the first entry: `enter nearest` -> %s; refused %s -> %s, still in area %s (sealed: %s)" % (
            ll3[0] if ll3 else "-", r2, r3, a3, (r3 or 0) > (r2 or 0)), [sh("lock_hell_sealed")], ll3)
    H("difficulty 0")
    H("stat player 12 set %d" % lvl0)


def check_cow():
    nm = "Cow portal recipe (Wirt's Leg): Normal town, Hell town, Hell field"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    goto(1)
    H("cubeclear")
    H("item leg cube")

    def cow(diff, area):
        H("difficulty %d" % diff)
        H("quest 40 0 set")  # the cow portal handler wants Baal killed in the game's difficulty
        before = {o["id"] for o in hstate()["objs"]}
        ok, l = H("cubecraft")
        time.sleep(1.5)
        st = hstate()
        new = [o for o in st["objs"] if o["id"] not in before and o.get("class") in (60, 600)]
        return ok, l, new

    ok1, l1, new1 = cow(0, 1)
    ok2, l2, new2 = cow(2, 1)
    goto(2)
    H("difficulty 2")
    H("cubeclear")
    H("item leg cube")
    ok3, l3, new3 = cow(2, 2)
    H("difficulty 0")
    goto(1)
    good = (not ok1) and ok2 and bool(new2) and (not ok3) and not new3
    add(nm, "PASS" if good else "FAIL",
        "cubecraft with the leg: Normal town -> %s (recipe has min difficulty Hell); Hell town -> %s, new portal object %s; "
        "Hell field (Blood Moor) -> %s, new portal object %s" % (
            l1[-1][:60], l2[0][:70], [(o['class']) for o in new2], l3[-1][:60], [(o['class']) for o in new3]),
        [sh("cow")], l1 + l2 + l3)
    H("cubeclear")


def check_goldcraft():
    nm = "Cube gold payment (rows 6327/6328): carried gold first, then the bank"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    goto(1)
    H("cubeclear")
    H("stat player 532 set 140")
    H("gold set 400000 100000")
    H("item 0u1 cube uniq=30 532=1")   # HotTJ; item stat 532 == 1 is the cubemain row's own condition (op 18)
    H("item f667 cube")
    ok1, l1 = H("cubecraft")
    _, g1 = H("gold get")
    H("cubeclear")
    H("gold set 20000 100000")
    H("item 0u1 cube uniq=30 532=1")
    H("item f667 cube")
    H("item f667 cube")
    ok2, l2 = H("cubecraft")
    _, g2 = H("gold get")
    H("cubeclear")
    c1 = re.search(r"carried=(\d+) bank=(\d+)", g1[0])
    c2 = re.search(r"carried=(\d+) bank=(\d+)", g2[0])
    good = ok1 and ok2 and c1 and c2 and (int(c1.group(1)), int(c1.group(2))) == (350000, 100000) and \
        (int(c2.group(1)), int(c2.group(2))) == (0, 20000)
    add(nm, "PASS" if good else "FAIL",
        "row 6327 (cost 50,000): %s, gold 400000 / 100000 -> %s; row 6328 (cost 100,000) with 20000 carried: %s, gold "
        "20000 / 100000 -> %s (carried first, the rest from the bank)" % (l1[0][:50], g1[0], l2[0][:50], g2[0]),
        [sh("goldcraft")], l1 + l2)
    H("stat player 532 set 0")


def check_craftflags():
    n1 = "Cube craft flag (player): row 6369 sets it, row 6350 is offered afterwards"
    n2 = "Cube craft flag (game): dungeon key row 7102 sets game flag 50, a second try is refused"
    if not need_game():
        add(n1, "BLOCKED", "not in a game")
        return add(n2, "BLOCKED", "not in a game")
    goto(1)
    H("cubeclear")
    cls0 = hstate()["player"]["class"]
    H("class 6")  # the recipe is for assassins
    try:
        H("stat player 378 set 214100")
        H("item voXX cube uniq=55")
        _, f0 = H("flag player 1 get")
        ok1, l1 = H("cubecraft")
        _, f1 = H("flag player 1 get")
        ok2, l2 = H("cubecraft")
    finally:
        H("class %d" % cls0)
        H("stat player 378 set 0")
    H("cubeclear")
    r1 = re.search(r"recipe (\d+)", l1[0]) if l1 else None
    r2 = re.search(r"recipe (\d+)", l2[0]) if l2 else None
    add(n1, "PASS" if (ok1 and r1 and r1.group(1) == "6369" and f0[0].endswith("= 0") and f1[0].endswith("= 1") and ok2
                       and r2 and r2.group(1) == "6350") else "FAIL",
        "assassin with the quest item: before %s; first transmute %s; after %s; second transmute %s (6350 only offered "
        "with the flag)" % (f0[0], l1[0][:40], f1[0], l2[0][:40] if l2 else "-"), [sh("craftflags")], l1 + l2)
    H("stat player 536 set 1000")
    H("item rx01 cube")
    H("item f700 cube")
    _, g0 = H("flag game 50 get")
    ok3, l3 = H("cubecraft")
    _, g1 = H("flag game 50 get")
    H("cubeclear")
    H("item rx01 cube")
    H("item f700 cube")
    ok4, l4 = H("cubecraft")
    H("cubeclear")
    H("stat player 536 set 0")
    r3 = re.search(r"recipe (\d+)", l3[0]) if l3 else None
    add(n2, "PASS" if (ok3 and r3 and r3.group(1) == "7102" and g0[0].endswith("= 0") and g1[0].endswith("= 1") and not ok4)
        else "FAIL",
        "key recipe: before %s; transmute %s; after %s; second try in the same game: %s" % (
            g0[0], l3[0][:50] if l3 else "-", g1[0], l4[-1][:60]), [sh("gameflag")], l3 + l4)
    H("flag game 50 clear")


def check_dungeon_reenter():
    """Bug #24b: a Median dungeon entered, left and entered again in one game. The client's missile tick called D2R's
    client "do" table through an empty slot for missiles whose pCltDoFunc is empty (row 5507 warp_crash_missile is fired by
    the monsters of level 251 Butcher's Lair 2 a few seconds after the arrival) and the game died with a call to address 0.
    The check enters level 251 (`b60X`, Hell rules, the 60 s lock) three times, fires row 5507 in each visit (`h missile`,
    the same call the monsters use) and needs the game alive and the player in the level every time. It runs after the
    checks that use `b60X` themselves (a second `use` while the portal stands fails), and 253 is sealed by lockclock by now."""
    name = "Median dungeon (251) entered three times in one game (bug #24b: empty client missile function slot)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    lvl0 = hstate()["player"]["level"]
    H("difficulty 2")
    H("stat player 12 set 125")
    notes, log, shots, ok = [], [], [], True
    off = g._size(g.MXL_LOG)

    def heal():
        H("stat player 7 set 2000000000")
        H("stat player 6 set 2000000000")

    heal()
    H("item b60X")
    _, l = H("use b60X")
    log += l
    time.sleep(1.5)
    ports = [o for o in hstate()["objs"] if o.get("class") in (59, 60, 600)]
    pid = max(ports, key=lambda o: o["id"])["id"] if ports else None
    t0 = time.time()
    for visit in (1, 2, 3):
        guard()
        heal()
        moved = False
        for _ in range(3):
            time.sleep(2)
            _, ll = H("enter %d" % pid) if pid is not None else (False, [])
            log += ll
            if ll and "moved" in ll[0]:
                moved = True
                break
        time.sleep(3)
        area = hstate()["player"].get("area") if g.game_pids() else None
        fired = []
        if area == 251:
            fired = [H("missile 5507")[0], H("missile 5507 -10 8")[0]]
            time.sleep(4)  # the client ticks the missiles in these seconds
        alive = bool(g.game_pids())
        if alive:
            shots.append(sh("reenter_%d" % visit))
        notes.append("visit %d at +%.0f s: portal %s moved %s, area %s, missiles %s, alive %s" % (
            visit, time.time() - t0, pid, moved, area, fired, alive))
        if not (alive and moved and area == 251 and all(fired)):
            ok = False
        if not alive:
            break
        heal()
        goto(1)
    skipped = [ln for ln in mxl_lines_since(off) if "which D2R leaves empty" in ln]
    if g.game_pids():
        H("difficulty 0")
        H("stat player 12 set %d" % lvl0)
    add(name, "PASS" if ok and g.game_pids() else "FAIL",
        "; ".join(notes) + "; plugin log: %s" % ("; ".join(x.split("mxl-core: ")[-1][:80] for x in skipped[:3]) or "no empty-slot call logged"),
        shots[-2:], log)


# lockclock goes first after mxl: entering the Median dungeon is a heavy level load and D2R's client asserts "asset cache
# exceeded" (512 assets) after a few area changes, then crashes (see docs/bugs.md)
def check_areacycle():
    """Bug #24: area changes in one game. MXL_AREAS (comma list, default 2..17) are visited in order with D2R's
    `cheat goto` / the plugin's `level`; the loader log is searched for the asset cache and biome assertions and the
    game process for being alive."""
    name = "Area changes in one game (bug #24: asset cache, empty biome)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    areas = [int(x) for x in os.environ.get("MXL_AREAS", "2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17").split(",")]
    off = g._size(g.LOADER_LOG)
    n_cache = n_biome = 0
    notes = []
    reached = 0
    for a in areas:
        guard()
        st = goto(a)
        at = st["player"].get("area")
        txt = g._read_from(g.LOADER_LOG, off)
        n_cache = txt.count("AssetCache.h")
        n_biome = txt.count("has no biome") + txt.count("empty biome filename")
        alive = bool(g.game_pids())
        notes.append("%d->%s cache=%d biome=%d%s" % (a, at, n_cache, n_biome, "" if alive else " DEAD"))
        if not alive:
            break
        if at == a:
            reached += 1
    ok = bool(g.game_pids()) and n_cache == 0 and n_biome == 0
    add(name, "PASS" if ok else "FAIL", "%d of %d areas reached; asset cache asserts %d, biome asserts %d; %s" % (
        reached, len(areas), n_cache, n_biome, "; ".join(notes)))


CHECKS = [("mxl", check_mxl), ("lockclock", check_lockout_clock), ("goblins", check_goblins), ("respec", check_respec),
          ("cain", check_cain), ("heart", check_heart), ("catalyst", check_catalyst), ("lockout", check_lockout),
          ("cow", check_cow), ("goldcraft", check_goldcraft), ("craftflags", check_craftflags),
          ("reenter", check_dungeon_reenter), ("areacycle", check_areacycle)]


def write_report(extra=""):
    p = os.path.join(g.current_run(), "report.md")
    with open(p, "w", encoding="utf-8") as f:
        f.write("# Game checks (background input, harness commands) %s\n\n" % os.path.basename(g.current_run()))
        f.write("| Check | Result | Note | Evidence |\n|---|---|---|---|\n")
        for r in RESULTS:
            f.write("| %s | %s | %s | %s |\n" % (r.name, r.status, r.note.replace("|", "/"), ", ".join(r.shots)))
        f.write("\n" + extra + "\n")
        for r in RESULTS:
            if r.log:
                f.write("\n### log: %s\n\n```\n%s\n```\n" % (r.name, "\n".join(x[:230] for x in r.log)))
    g.log("report " + p)
    return p


def lock(wait_s=1800):
    """The slot's run lock (tools/game_slots.py): wait, retry every 30 s."""
    t0 = time.time()
    while True:
        ok, msg = g.game_slots.acquire(g.SLOT, g.LOCK_OWNER)
        if ok:
            return True
        if time.time() - t0 > wait_s:
            g.log("slot %d: %s; giving up" % (g.SLOT, msg))
            return False
        g.log("slot %d lock is held (%s), waiting 30 s" % (g.SLOT, msg))
        time.sleep(30)


def unlock():
    g.game_slots.release(g.SLOT, g.LOCK_OWNER)


def run_all(only=None):
    global WD
    new_run()
    if not lock():
        print("game lock held for 30 min, giving up")
        return 2
    os.environ["MXL_GOBLINS_FORCE"] = "1"
    WD = Watchdog()
    WD.start()
    try:
        backup_saves()
        if g.game_pids():
            g.stop()
            time.sleep(2)
        # Quest state of the test character: the kill / dialog checks need quests 4 (Cain rescued, bit 1) and 6 (Andariel)
        # active when the game is created, and the plugin's catalyst flag (quest 47) clear; the earlier run's rewards
        # completed them. Closed save, checksum and sidecar fixed (docs/save-format.md).
        for name in sorted({f[:-4] for f in os.listdir(SAVES) if f.endswith(".d2s")}):
            patch_quests(name, {(0, 1): 0, (0, 4): 2, (0, 6): 0, (0, 41): 0, (0, 47): 0})
        g.snapshot_marks()
        if not to_menu():
            add("start game", "BLOCKED", "game did not reach the main menu")
        elif not enter_game():
            add("enter game", "BLOCKED", "the character did not enter the game")
        else:
            p0 = hstate()["player"]
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
            try:  # the test character is saved on exit: put the numbers the checks changed back
                H("difficulty 0")
                H("gold set %d %d" % (p0.get("gold", 0), p0.get("bank", 0)))
                H("cubeclear")
            except Exception:
                pass
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

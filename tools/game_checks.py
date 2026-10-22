#!/usr/bin/env python
"""Scripted in-game checks for the untested rows of docs/testing.md, in game_ctl's BACKGROUND input mode.

Everything goes through tools/game_ctl.py's background path: console lines run through the plugin test harness
(`g.console_bg`, `g.console_text`), keys and mouse are window messages plus the plugin's virtual cursor, screenshots
are PrintWindow captures. This script never presses Ctrl+`, never calls SetForegroundWindow / SetCursorPos and never
uses ImageGrab. A watchdog thread samples GetCursorPos and GetForegroundWindow every 25 ms for the whole run and
aborts the run when the real cursor enters the game window or the game window becomes the foreground window.

Run with `py -3 tools/game_checks.py all [check ...]` (writes report.md, screenshots and logs into
%USERPROFILE%\\mxl-d2r-runs\\checks-<time>\\); `--slot N` picks a game slot. Checks: mxl goblins respec cain heart
catalyst cainsub tower lockout lockquest cow goldcraft craftflags lockclock reenter areacycle trophy; the old "fix to test" rows of docs/bugs.md: skilltree (#3, #9) keybinds (#5)
strings (#7) automap (#15) scrollportals (#17) vendors (#18) monhover (#22). What a click in the world cannot do in background mode (kill a
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
    name = "checks-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-s%d" % g.SLOT  # two slots may start in the same second
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
    dst = os.path.join(g.RUNS, "save-backup-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-s%d" % g.SLOT)
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
            mon = {k: int(v) for k, v in kv(ln).items() if re.fullmatch(r"-?\d+", v)}
            lm = re.search(r"life=(\d+)/(\d+)", ln)
            if lm:
                mon["life"], mon["maxlife"] = int(lm.group(1)), int(lm.group(2))
            st["mons"].append(mon)
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


def revive_if_dead(timeout=20):
    """A dead test character (player mode 0 / 17, life 0) closes the game on `cheat goto`: press Esc (D2R respawns
    the character in town with full life, as a window message) and wait for it. Returns True when it was dead."""
    def dead(p):
        life = str(p.get("life", "1")).split("/")[0]
        return p.get("mode") in (0, 17) or life == "0"
    p = hstate()["player"]
    if not p or not dead(p):
        return False
    g.log("the test character is dead (mode %s): Esc to respawn in town" % p.get("mode"))
    t0 = time.time()
    while time.time() - t0 < timeout:
        g.press("esc")
        time.sleep(3)
        p = hstate()["player"]
        if p and not dead(p):
            break
    return True


def goto(area, timeout=40):
    """Move the character to an area: D2R's own `cheat goto` (debug mode) for the same act, the plugin's `level`
    (the NPC travel) across acts. Waits until `h state` says so."""
    revive_if_dead()
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
              "cube conditions", "craft flags", "act 1 quests", "dungeon lockout", "goblins on", "item events on",
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
    # npcext PromptConfirm: the click opens D2R's Yes / No page and sends nothing; No ends it with the stats untouched
    ok, ml = H("menu %d" % idx)
    time.sleep(1.5)
    _, c1 = H("menu confirm")
    confirm_shot = sh("respec_confirm")
    asked = ok and any("open" in ln for ln in c1)
    _, held0 = H("stat player 0")
    ok_no, no_out = H("menu no")
    time.sleep(1)
    _, held1 = H("stat player 0")
    declined_lines = [ln for ln in mxl_lines_since(m) if "npc actions" in ln]
    declined = (ok_no and stat_of(held0) == base0 + 60 and stat_of(held1) == base0 + 60
                and any("declined" in ln for ln in declined_lines) and not any("done" in ln for ln in declined_lines))
    menu = npc_menu(148)
    plugin_lines = [ln for ln in (menu or []) if "handler=plugin" in ln]
    if not plugin_lines:
        return add(name, "FAIL", "after No, the menu did not come back with the plugin line: %s" % menu, [shot, confirm_shot], ml + no_out)
    idx = int(re.search(r"line (\d+)", plugin_lines[0]).group(1))
    m = mxl_mark()
    ok, ml2 = H("menu %d" % idx)
    time.sleep(1.5)
    _, c2 = H("menu confirm")
    ok, yes_out = H("menu yes")
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
    asked2 = any("open" in ln for ln in c2)
    add(name, "PASS" if (asked and declined and asked2 and ok and done and s0 == 30 and s4 == base4 + refunded) else "FAIL",
        "menu lines %s; the click opens the Yes / No page (%s) and No leaves strength at %d (%s); after Yes strength %d -> %s "
        "(Barbarian start 30), unused stat points %d -> %s (%s refunded); log `%s`; quest 41 %s" % (
            [ln.strip() for ln in menu[1:]], asked, base0 + 60, declined, base0 + 60, s0, base4, s4, refunded,
            lines[-1][-60:] if lines else "none", q[0]),
        [shot, confirm_shot], menu + ml + c1 + no_out + ml2 + c2 + yes_out + lines)
    close_menu()
    H("stat player 4 set %d" % base4)  # the refund raised the unused points: the saved test character keeps the old number


def messages_sent():
    m = re.search(r"client messages .*\(sent (\d+), failed (\d+)\)", mxl_text())
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def check_trophy():
    """Median's trophies (misc2 InteractString): the tooltip says "Right click to reveal trophy challenge" and the right
    click prints the challenge as chat lines (plugin/src/item_spells.cpp, mxl_items.h Trophies)."""
    name = "Trophy items: hover text, right click reveals the challenge (misc2 InteractString)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    H("remove a56 5")
    ok, made = H("item a56 inv")
    if not ok:
        return add(name, "BLOCKED", "could not create the Heart of Azmodan (a56): %s" % made)
    sent0, failed0 = messages_sent()
    g.press("i")  # inventory (background key message)
    time.sleep(1.5)
    shots = [sh("trophy_inventory")]
    # the item lands in the first free cell of the 15 x 10 inventory grid (a free cell right of the tall items: the HD panel puts it near 0.841, 0.580): hover it with the plugin's virtual cursor
    for n, (fx, fy) in enumerate(((0.841, 0.580), (0.841, 0.592))):
        g.click(fx, fy, move_only=True)
        time.sleep(1.0)
        shots.append(sh("trophy_hover%d" % n))
    g.press("i")
    time.sleep(0.5)
    ok, used = H("use a56")
    time.sleep(1.5)
    shots.append(sh("trophy_revealed"))
    sent1, failed1 = messages_sent()
    ok2, again = H("use a56")  # the trophy is kept: a second reveal works
    time.sleep(0.5)
    sent2, failed2 = messages_sent()
    _, held = H("state")
    kept = any("a56" in ln for ln in held)
    lines = [ln for ln in used if "pSpell 17" in ln]
    add(name, "PASS" if (ok and ok2 and lines and "kept" in lines[0] and sent0 is not None and sent1 - sent0 >= 8
                        and failed1 == failed0 and sent2 - sent1 >= 8 and kept) else "FAIL",
        "`use a56`: %s; client messages sent %s -> %s -> %s (failed %s -> %s), the item is kept: %s. Hover text and the "
        "printed lines are read from the screenshots (trophy_hover*.png, trophy_revealed.png)" % (
            used[0] if used else "-", sent0, sent1, sent2, failed0, failed2, kept), shots, made + used + again)
    H("remove a56 5")


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


def check_cain_submenu():
    """Median's Cain page: a click on 'crafting materials' opens the submenu of the materials whose requirement holds
    (the server's plugin message 'N' makes the client rebuild the list; plugin/src/npc_actions.cpp)."""
    name = "Cain crafting materials: the click opens a submenu with the open entries"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    H("stat player 7 set 20000000")
    H("stat player 6 set 20000000")
    H("quest 4 0 set")  # Search for Cain done: the cube entry's requirement (the page needs one open entry)
    H("remove box 10")  # the entry shows only while the player lacks the cube
    menu = npc_menu(265)
    if not menu:
        return add(name, "FAIL", "Cain's menu did not open")
    # the menu flag is set when the talk starts; the client builds the list (and the plugin adds its line) when the speech ends (a first talk takes up to ~30 s)
    t0 = time.time()
    while time.time() - t0 < 240 and not any("handler=plugin" in x for x in menu):
        time.sleep(1)
        ok0, again = g.hcmd("menu list", 5)
        menu = again if ok0 else menu
    plugin_idx = [int(re.search(r"line (\d+)", x).group(1)) for x in menu if "handler=plugin" in x]
    if not plugin_idx:
        return add(name, "FAIL", "no plugin line in Cain's menu after the speech: %s" % menu, [sh("cain_submenu")], menu)
    before = len([l for l in menu if "line " in l])
    shot0 = sh("cain_menu_page")
    ok, l = H("menu %d" % plugin_idx[0])
    time.sleep(2.5)
    ok2, after = g.hcmd("menu list", 5)
    s = sh("cain_submenu")
    n_after = len([x for x in (after or []) if "line " in x])
    same = after == menu
    if ok2 and n_after >= 1 and not same:
        ok3, l3 = H("menu 0")  # the first entry: the Horadric Cube
        time.sleep(2)
        got = len(items(hstate(), "box"))
        add(name + " (entry gives the item)", "PASS" if got >= 1 else "FAIL",
            "`menu 0` in the submenu: %s; Horadric Cube (box) items now: %d" % (ok3, got), [sh("cain_entry")], l3 or [])
    add(name, "PASS" if (ok and ok2 and n_after >= 1 and not same) else "FAIL",
        "menu lines before %d; click on the plugin line: %s; menu open after: %s with %d lines (the list changed: %s). Entry texts must be "
        "read from the screenshots (cain_menu_page.png: Talk / crafting materials, cain_submenu.png: the entries)" % (before, ok, ok2, n_after, not same),
        [shot0, s], (menu or []) + (l or []) + (after or []))
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


def _qbits(q):
    """Bits 0-15 of quest q as a string of 0 / 1 (`h quest <q> dump`)."""
    ok, l = H("quest %d dump" % q)
    m = re.search(r"bits0-15 ([01]{16})", l[0]) if l else None
    return m.group(1) if m else None


def _qrestore(q, bits):
    for i, ch in enumerate(bits or ""):
        H("quest %d %d %s" % (q, i, "set" if ch == "1" else "clear"))


def check_lockquest():
    """Median's quest-table condition of the warp test (D2Sigma 0x5d810; leveldefs QuestFlagEx): level 73 (Duriel's Lair)
    names quest 13. `h lockcheck` asks the plugin's warp test for the verdict without moving anybody."""
    name = "Dungeon lockout: quest-table condition (level 73 needs quest 13: any of bits 1, 0, 0xe, 0xf)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    H("difficulty 0")
    saved = _qbits(13)
    if saved is None:
        return add(name, "BLOCKED", "could not read quest 13")
    notes, ok_all = [], True

    def verdict(level):
        ok, l = H("lockcheck %d" % level)
        m = re.search(r"= (\w+)", l[0]) if l else None
        return m.group(1) if m else "?"

    try:
        for b in (0, 1, 0xe, 0xf):
            H("quest 13 %d clear" % b)
        v0 = verdict(73)
        ok_all &= v0 == "quest"
        notes.append("no bits: %s (want quest)" % v0)
        for b in (1, 0, 0xe, 0xf):
            H("quest 13 %d set" % b)
            v = verdict(73)
            ok_all &= v == "allow"
            notes.append("bit %#x: %s" % (b, v))
            H("quest 13 %d clear" % b)
        v1 = verdict(157)  # butcher_s_lair: names no quest, must never be the quest verdict
        ok_all &= v1 != "quest"
        notes.append("level 157 (no quest): %s" % v1)
    finally:
        _qrestore(13, saved)
    add(name, "PASS" if ok_all else "FAIL", "; ".join(notes))


def check_tower():
    """Median's a1q5 guard (D2Sigma 0x9e8b0) on D2R's quest 5 kill handler: for a killer with quest 5 bit 0, 0xe or 0xf set
    the hook skips the vanilla handler. A Countess spawned by the harness is not on quest 5's unit list (the kill event
    never reaches the handler), so `h towerprobe` asks the hook's decision directly; the hook itself is only reached by the
    natural Countess in the Tower Cellar (docs/unported.md)."""
    name = "Forgotten Tower: quest 5 guard decides skip once quest 5 is done (towerprobe)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    close_menu()
    H("difficulty 0")
    saved = _qbits(5)
    if saved is None:
        return add(name, "BLOCKED", "could not read quest 5")

    def probe():
        ok, l = H("towerprobe")
        m = re.search(r"skip=(\d)", l[0]) if l else None
        return int(m.group(1)) if m else None

    notes, ok_all = [], True
    try:
        H("spawn 45")
        for c in (0, 0xe, 0xf):
            H("quest 5 %d clear" % c)
        v = probe()
        ok_all &= v == 0
        notes.append("quest 5 open: skip=%s (want 0)" % v)
        for b in (0, 0xe, 0xf):
            for c in (0, 0xe, 0xf):
                H("quest 5 %d clear" % c)
            H("quest 5 %d set" % b)
            v = probe()
            ok_all &= v == 1
            notes.append("bit %#x set: skip=%s (want 1)" % (b, v))
        H("kill nearest 45")
    finally:
        _qrestore(5, saved)
    add(name, "PASS" if ok_all else "FAIL", "; ".join(notes))


def check_lockout_clock():
    """Hell rules in a Hell dungeon: god mode so the test character survives (see god())."""
    god(True)
    try:
        _check_lockout_clock()
    finally:
        god(False)


def _check_lockout_clock():
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


def cast_and_watch(skill, level, target, tries=6, shot=None, approach=False):
    """setskill, then cast up to `tries` times and watch the missiles the server frames saw. A melee cast walks the player
    up to the monster first, and the Fallen of the Blood Moor wander and flee, so a cast at a monster 5-8 sub-tiles away only
    walks (mode 3) and the next one repeats it (regression hunt 2026-10-22: the same Bash passed or failed from run to run
    on exactly this): with `approach` the player is moved next to the monster before every try (`moveto nearest`) and the
    cast follows at once. Returns (cast_ok, cast_lines, mana_dropped, cast_mode_seen, missiles_seen, screenshot or None)."""
    H("setskill %d %d" % (skill, level))
    H("missiles reset")
    ok, lines, dropped, cast_mode, seen, shot_taken = False, [], False, False, 0, None
    for _ in range(tries):
        if approach:
            H("moveto nearest")
            time.sleep(0.4)
        ok, lines = H("cast %d %s" % (skill, target))
        m = re.search(r"mana (\d+) -> (\d+)", " ".join(lines))
        dropped = dropped or (bool(m) and int(m.group(2)) < int(m.group(1)))
        cast_mode = cast_mode or bool(re.search(r"mode now (7|10)", " ".join(lines)))
        t0 = time.time()
        while time.time() - t0 < 1.6:
            time.sleep(0.4)
            _ok, ml = H("missiles")
            m = [re.search(r"seen=(\d+)", ln) for ln in ml if ln.startswith("missiles ")]
            seen = int(m[0].group(1)) if m and m[0] else 0
            cast_mode = cast_mode or hstate()["player"].get("mode") in (7, 10)
            if shot and seen > 0 and shot_taken is None:  # the effect is on screen now (window-only capture)
                shot_taken = sh(shot)
        if seen > 0 and (dropped or cast_mode):
            break
        if not ok:
            H("spawn 18 1")  # the monster died or is gone
    return ok, lines, dropped, cast_mode, seen, shot_taken


def check_casts():
    nm = "Server-side skill casts (harness cast): Median Barbarian skills and a Sorceress spell, cast animation, missiles seen"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    goto(2)  # Blood Moor: skills do not work in town
    kill_all_near()
    notes, shots, log, good = [], [], [], True
    # earthquake_2 (Median, melee: the cast walks the player to the monster first) fires earthquake missiles;
    # Fire Bolt is a Sorceress spell, cast by the Barbarian (the game does not check the class here)
    cases = [("earthquake_2 (Median Barbarian)", 1271, 5, True, True), ("Bash (vanilla Barbarian)", 126, 1, False, True),
             ("Fire Bolt (Sorceress spell)", 36, 5, True, False)]
    for label, skill, level, want_missiles, melee in cases:
        kill_all_near()
        H("spawn 18 1")
        if melee:
            H("moveto nearest")
        ok, lines, dropped, cast_mode, seen, shot = cast_and_watch(skill, level, "nearest", tries=10 if melee else 6,
                                                                       shot="cast_%d" % skill, approach=melee)
        passed = ok and (dropped or cast_mode) and (seen > 0 or not want_missiles)
        good = good and passed
        notes.append("%s: cast %s, cast animation %s, mana drop at the call %s, missiles seen %d" % (
            label, "OK" if ok else "FAIL", cast_mode, dropped, seen))
        shots.append(shot or sh("cast_%d" % skill))
        log += lines
        kill_all_near()
    add(nm, "PASS" if good else "FAIL", "; ".join(notes), shots, log)


def spawn_id(cls=18):
    """Spawns a monster and returns its unit id (the `spawned class N id=M` line; the monster exists once that line is
    written, even when the closing "spawn OK" line is late)."""
    ok, lines = H("spawn %d 1" % cls)
    m = re.search(r"spawned class \d+ id=(\d+)", " ".join(lines))
    return int(m.group(1)) if m else None


def check_item_events():
    nm = "Item events (D2RCore registry): thorns 397, life on kill 265, cast on kill 376, open wounds / crushing blow skip immune monsters"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    goto(2)
    close_menu()
    kill_all_near()
    H("unequip lring")
    log, notes = [], []
    H("stat player 6 0 set 700000")  # no god mode: it refills life, which hides the life-on-kill gain; the checks take a second

    def life_of(mid):
        ok, l = H("stat m%d 6" % mid)
        log.extend(l)
        return stat_of(l)

    # thorns: stat 397 (func 11, events 2 / 3): 50 fire damage is taken off the monster per event (life is stat 6, 1/256 points)
    ok, l = H("equip rin 397=50 lring")
    mid = spawn_id()
    log += l
    if mid is None:
        return add(nm, "BLOCKED", "no monster could be spawned")
    a = life_of(mid)
    H("evtrace fire 3 player m%d" % mid)
    b = life_of(mid)
    thorns = ok and a is not None and b is not None and a - b == 50 * 256
    notes.append("thorns 50: monster life %s -> %s (%s)" % (a, b, "-50 points" if thorns else "WRONG"))
    H("kill nearest")
    # life on kill: stat 265 = 50 (D2RCore function 33 -> D2R function 28), +50 points to a character at 100 life
    # (the character's own gear may heal on a kill too: the gain is measured against a kill without the ring)
    H("unequip lring")
    mid = spawn_id()
    H("stat player 6 0 set 25600")
    H("evtrace fire 9 player m%d" % mid)
    base = stat_of(H("stat player 6")[1])
    ok, l = H("equip rin 265=50 lring")
    H("stat player 6 0 set 25600")
    ok2, l2 = H("evtrace fire 9 player m%d" % mid)
    life = stat_of(H("stat player 6")[1])
    heal = ok and ok2 and base is not None and life is not None and 50 * 256 <= life - base < 50 * 256 + 2000  # plus a little regeneration
    notes.append("life on kill 50: player life 25600 -> %s without the ring, %s with it (%s)" % (base, life, "+50 points" if heal else "WRONG"))
    H("kill nearest")
    # cast on kill: stat 376 layer 36 * 64 + 5 (Fire Bolt level 5, 100 %): a missile (class 58) appears, the skill need not be known
    H("unequip lring")
    ok, l = H("equip rin 376:2309=100 lring")
    mid = spawn_id()
    time.sleep(2)  # a monster that was just spawned is not a valid target yet
    H("missiles reset")
    life0 = life_of(mid)
    H("evtrace fire 9 player m%d" % mid)
    ok2, l2 = H("missiles")
    time.sleep(1.5)
    life1 = life_of(mid)
    log += l2 + H("evtrace calls")[1]
    # the Fire Bolt is a missile (class 58) that may be gone again before `missiles` looks; it burns the monster
    cast = ok and (any("class 58" in x for x in l2) or (life0 is not None and life1 is not None and life1 < life0))
    notes.append("cast on kill (Fire Bolt 5): %s; monster life %s -> %s" % (" ".join(x for x in l2 if "missiles live" in x)[:60] or "-", life0, life1))
    H("kill nearest")
    # cast on kill with the skills Median's monsters cast (monprop: 855 a nova, 861 a missile volley) and the other rows that are
    # not a plain bolt: the missiles tracker counts every missile that outlives a server frame (`seen=`). Row 47 "Fire Ball" is a
    # Median stub (srvdofunc 18 self buff, srvmissile fire_ball_missile = missiles row 5540 with no velocity or range, no damage):
    # its missile dies within the frame it is created in, which a direct `missile 5540` shows as well (`missile 3456` lives).
    def cast_case(skill, level):
        # the event needs a live spawned monster and the ring in place; a try that never reached the handler (the last cast on kill
        # line still names the previous case) is repeated, up to 3 times
        for _try in range(3):
            res = cast_case_once(skill, level)
            if res[2] is not None and res[2][:2] == (skill, level):
                break
        return res

    def cast_case_once(skill, level):
        H("unequip lring")
        okc, _l = H("equip rin 376:%d=100 lring" % (skill * 64 + level))
        m = spawn_id()
        time.sleep(2)
        H("missiles reset")
        H("evtrace fire 9 player m%d" % m)
        okm, lm = H("missiles")
        okk, lk = H("evtrace calls")
        log.extend(lm + lk)
        seen = re.search(r"seen=(\d+)", " ".join(lm))
        last = re.search(r"last cast on kill: skill (-?\d+) level (-?\d+) warp (-?\d+)", " ".join(lk))
        H("kill nearest")
        return okc and okm and okk, int(seen.group(1)) if seen else None, tuple(int(x) for x in last.groups()) if last else None

    def direct_missile(row):
        H("missiles reset")
        H("missile %d 12 0" % row)
        ok1, l1 = H("missiles")
        log.extend(l1)
        seen = re.search(r"seen=(\d+)", " ".join(l1))
        time.sleep(1.5)
        return int(seen.group(1)) if seen else None

    cases = {}
    for skill, level in ((861, 50), (855, 2), (47, 10), (54, 1)):
        cases[skill] = cast_case(skill, level)
    kill_all_near()
    direct = {row: direct_missile(row) for row in (5540, 3456)}
    kill_all_near()
    c861, c855, c47, c54 = cases[861], cases[855], cases[47], cases[54]
    missiles_ok = (c861[0] and c861[1] and c861[1] > 0 and c861[2] and c861[2][:2] == (861, 50)
                   and c855[0] and c855[1] and c855[1] > 0 and c855[2] and c855[2][:2] == (855, 2))
    fire_ball = (c47[0] and c47[2] is not None and c47[2][:2] == (47, 10) and c47[1] == 0 and direct[5540] == 0
                 and direct[3456] is not None and direct[3456] > 0)
    warp = c54[0] and c54[2] is not None and c54[2][:2] == (54, 1) and c54[2][2] == 1 and c861[2] is not None and c861[2][2] == 0
    notes.append("cast on kill missiles seen: skill 861 L50 %s (class 3456), skill 855 L2 %s (class 3124) (%s)" % (
        c861[1], c855[1], "missiles" if missiles_ok else "WRONG"))
    notes.append("Fire Ball 47 L10: handler reached %s, missiles seen %s; direct `missile 5540` %s, `missile 3456` %s (%s: Median's row is a stub whose missile expires within the frame)" % (
        c47[2], c47[1], direct[5540], direct[3456], "as expected" if fire_ball else "WRONG"))
    notes.append("warp flag (Median 0x40): Teleport 54 %s, skill 861 %s (%s)" % (c54[2], c861[2], "read" if warp else "WRONG"))
    # open wounds (135) / crushing blow (136): lose life on a hit; a monster with stat 373 (Median 189) skips them
    H("unequip lring")
    ok, l = H("equip rin 135=100 136=100 lring")
    plain, immune = spawn_id(), spawn_id()
    H("stat m%d 373 0 set 1" % immune)
    res = {}
    for who in (plain, immune):
        a = life_of(who)
        H("evtrace fire 5 player m%d" % who)
        H("evtrace fire 6 player m%d" % who)
        res[who] = (a, life_of(who))
    wounds = ok and res[plain][1] < res[plain][0] and res[immune][1] == res[immune][0]
    notes.append("open wounds / crushing blow: plain monster life %s -> %s, immune (373) %s -> %s" % (
        res[plain][0], res[plain][1], res[immune][0], res[immune][1]))
    H("unequip lring")
    kill_all_near()
    H("stat player 6 0 set 2000000")  # the character is saved with its life: a dark orb is not taken for being in game on the next run
    txt = mxl_text()
    on = "item events on" in txt
    notes.append("`mxl`: %s" % ("item events on" if on else "item events not on"))
    add(nm, "PASS" if thorns and heal and cast and missiles_ok and fire_ball and warp and wounds and on else "FAIL", "; ".join(notes), [sh("item_events")], log)


def god(on):
    """D2R's god cheat around the checks that could kill the test character (Hell dungeon, bosses): a dead character
    makes later warps close the game. Off everywhere else: it also gives infinite mana and blocks damage."""
    enable_debug()
    con("cheat god %d" % (1 if on else 0))
    time.sleep(0.5)


def check_median_ai():
    """Native andariel_ai (plugin monster_ai.cpp, AI id 155) on Andariel (156), Maghda (1693) and Radament the Fallen (1897):
    spawned next to the player outside town, each must act (change mode or distance) and the player must take damage."""
    name = "Median andariel_ai: Andariel, Maghda, Radament the Fallen act (move / attack)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    god(True)
    goto(2)
    close_menu()
    notes, shots, ok_all = [], [], True
    for cls in (156, 1693, 1897):
        st0 = hstate()
        life0 = st0["player"].get("life")
        H("spawn %d" % cls)
        modes, dists = set(), set()
        for _ in range(12):
            time.sleep(0.5)
            st = hstate()
            for m in st["mons"]:
                if m.get("class") == cls:
                    modes.add(m.get("mode"))
                    dists.add(m.get("dist", m.get("distance")))
        life1 = hstate()["player"].get("life")
        shots.append(sh("medianai_%d" % cls))
        acted = len(modes) > 1 or len(dists) > 1  # the player is in god mode for the run: judged by the monster's modes
        ok_all = ok_all and acted
        notes.append("%d: modes %s, distances %s, player life %s -> %s" % (cls, sorted(x for x in modes if x is not None),
                     sorted(x for x in dists if x is not None)[:6], life0, life1))
        H("kill nearest %d" % cls)
        time.sleep(1)
    god(False)
    add(name, "PASS" if ok_all else "FAIL", "; ".join(notes), shots)


# ------------------------------------------------------------------ the "verify" rows of docs/unported.md (lane verify-checks)
# Deterministic where the engine allows it: the harness verbs hit / avoid / itemroll / colour / req / cstat (docs/testing.md)
# call the game's own damage, avoid, item generator, colour and requirement functions on units set up with `stat`.

def hv(cmd, verb=None, timeout=15):
    """One harness line -> (ok, first output line starting with the verb, all lines)."""
    ok, lines = H(cmd, timeout)
    verb = verb or cmd.split()[0]
    first = next((l for l in lines if l.startswith(verb + " ") or l.startswith(verb + "=")), lines[0] if lines else "")
    return ok, first, lines


def rd(ref, stat, layer=None):
    """Value of a stat of player / merc / mon:<id> (the base the checks restore afterwards)."""
    _, lines = H("stat %s %d%s" % (ref, stat, (" %d" % layer) if layer is not None else ""))
    return stat_of(lines)


def setstat(ref, stat, value, layer=None):
    H("stat %s %d%s set %d" % (ref, stat, (" %d" % layer) if layer is not None else "", value))


BIG_LIFE = 2097151  # 8191 life points in D2's 8.8 fixed point


def hit(ref, elem, amount, attacker=None):
    """One `hit`: the target at full life, returns (lost raw, absorbed raw, result flags) or None."""
    setstat(ref, 7, BIG_LIFE)
    setstat(ref, 6, BIG_LIFE)
    for _ in range(8):  # result 0: the engine ignored the hit (just arrived in the area, hit recovery); try again
        ok, line, _l = hv("hit %s %s %d%s" % (ref, elem, amount, (" " + attacker) if attacker else ""), "hit")
        m = re.search(r"life=(-?\d+)->(-?\d+) lost=\S+ absorbed=(-?\d+) result=0x(\w+)", line)
        if not ok or not m:
            return None
        if int(m.group(4), 16) != 0:
            break
        time.sleep(1)
    return int(m.group(1)) - int(m.group(2)), int(m.group(3)), int(m.group(4), 16)


def spawn_attacker(cls=1):
    ok, lines = H("spawn %d" % cls)
    m = re.search(r"spawned class \d+ id=(\d+)", " ".join(lines))
    return ("mon:%s" % m.group(1)) if m else None


def fight_ready():
    """Outside the town (damage and avoidance run there), no god mode, no leftovers of the earlier check."""
    god(False)
    st = hstate()
    if st["player"].get("area") != 2:
        goto(2)
    close_menu()
    kill_all_near()


def have_merc():
    _, lines = H("merc")
    return any(l.startswith("merc id=") for l in lines)


def ensure_merc():
    """A hired mercenary (Kashya, act 1 Rogue) standing next to the player in the field."""
    if not have_merc():
        goto(1)
        close_menu()
        H("gold set 20000 0")
        H("quest 2 0 set")  # Kashya hires once Blood Raven is dead (Median's gate is the quest alone)
        for _ in range(2):
            ok, _l = H("hire 150 0")
            if ok:
                break
            H("spawn 150")  # the hirer is not loaded yet
            time.sleep(1)
    if not have_merc():
        return False
    if hstate()["player"].get("area") != 2:
        goto(2)
    for _ in range(30):  # the mercenary follows a few seconds after the player's area change
        if H("stat merc 7")[0]:
            setstat("merc", 7, BIG_LIFE)  # a spawned monster would kill a level-4 mercenary before the check starts
            setstat("merc", 6, BIG_LIFE)
            return True
        time.sleep(1)
    return False


def merc_boost():
    """Strength / dexterity / level high enough that the requirement check judges the gate under test only."""
    setstat("merc", 0, 900)
    setstat("merc", 2, 900)
    setstat("merc", 12, 90)


def check_resistcap():
    name = "Max resistance cap 90: player and mercenary (fire / lightning / cold / magic, max-resist 0 / +10 / +50)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    fight_ready()  # first: it kills everything near, the mercenary too
    if not ensure_merc():
        return add(name, "BLOCKED", "no mercenary could be hired")
    atk = spawn_attacker()
    if atk is None:
        return add(name, "BLOCKED", "no attacker monster")
    elems = [("fire", 39, 40), ("ltng", 41, 42), ("cold", 43, 44), ("magic", 37, 38)]
    cases = [(0, 0.25), (10, 0.15), (50, 0.10)]  # resist 200; the cap is 75 + max-resist, at most 90 (95 would be 5%)
    amount, bad, rows = 100000, [], []
    for ref, attacker in (("player", atk), ("merc", atk)):  # never the nearest monster: that is the hired mercenary
        saved = {s: rd(ref, s) for e in elems for s in e[1:]}
        for elem, res, mx in elems:
            for add_max, want in cases:
                setstat(ref, res, 200)
                setstat(ref, mx, add_max)
                r = hit(ref, elem, amount, attacker)
                got = r[0] / amount if r else None
                rows.append("%s %s max+%d: %s%%" % (ref, elem, add_max, "-" if got is None else "%.1f" % (got * 100)))
                if got is None or abs(got - want) > 0.006:
                    bad.append("%s %s max+%d took %s (want %d%%)" % (ref, elem, add_max, rows[-1].split(": ")[1], round(want * 100)))
        for s, v in saved.items():
            setstat(ref, s, v or 0)
    H("kill nearest 1")
    add(name, "PASS" if not bad else "FAIL",
        "resist 200 against a 100000-unit hit (damage taken): %s" % ("; ".join(rows) if not bad else "; ".join(bad)),
        [sh("resistcap")], rows)


def _reduce(amount, pct):
    if amount == 0 or pct == 0:
        return amount
    return max(amount - amount * pct // 100, 256)


def _credit(amount, divisor, pct, tpct):
    if amount != 0 and divisor != 0:
        amount = max(amount // divisor, 256)
    return _reduce(_reduce(amount, pct), tpct)


def check_absorb():
    name = "Absorb credit stats 184 / 189 / 417 (D2R 369 / 373 / 446): the healed amount"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    fight_ready()
    elem_res = [37, 39, 41, 43]
    saved = {s: rd("player", s) for s in elem_res + [142, 143, 146, 148, 369, 373]}
    saved_t = {l: rd("player", 446, l) for l in (1, 3, 4)}
    for s in elem_res:
        setstat("player", s, 0)
    setstat("player", 369, 0)  # `stat set` writes the base value; what is left is what other sources give
    base184 = rd("player", 369)
    amount, rows, bad = 100000, [], []

    def probe(label, elem, want):
        r = hit("player", elem, amount)
        got = r[1] if r else None
        rows.append("%s: healed %s (want %s)" % (label, got, want))
        if got is None or abs(got - want) > 2:
            bad.append(rows[-1])

    setstat("player", 142, 30)  # 30% of the fire damage is absorbed: 30000
    probe("fire absorb 30%", "fire", _credit(30000, 0, base184, 0))
    setstat("player", 369, 50)
    p = rd("player", 369)
    probe("+ stat 184 -> %d%%" % p, "fire", _credit(30000, 0, p, 0))
    setstat("player", 369, 0)
    setstat("player", 373, 4)
    probe("+ stat 189 divisor 4", "fire", _credit(30000, 4, base184, 0))
    setstat("player", 373, saved[373] or 0)
    setstat("player", 446, 50, 1)
    probe("+ stat 417 layer 1 (fire) 50%", "fire", _credit(30000, 0, base184, 50))
    setstat("player", 148, 30)
    probe("cold with 417 layer 1 only (unchanged)", "cold", _credit(30000, 0, base184, 0))
    setstat("player", 446, 0, 1)
    setstat("player", 146, 30)
    setstat("player", 446, 50, 3)
    probe("magic with 417 layer 3 50%", "magic", _credit(30000, 0, base184, 50))
    setstat("player", 446, 0, 3)
    setstat("player", 142, 60)
    probe("percent limited to 40%", "fire", _credit(40000, 0, base184, 0))
    setstat("player", 142, 30)
    setstat("player", 143, 100)
    probe("percent 30 + flat 100", "fire", _credit(30000, 0, base184, 0) + _credit(25600, 0, base184, 0))
    for s, v in saved.items():
        setstat("player", s, 0 if (v is None or s == 369) else v)
    for l, v in saved_t.items():
        setstat("player", 446, v or 0, l)
    add(name, "PASS" if not bad else "FAIL",
        "100000-unit hits on the player (stat 184 from other sources: %s%%); healed amount = D2R's cut run through the three stats: %s"
        % (base184, "; ".join(rows)), [sh("absorb")], rows)


def _avoid_rate(ref, kind, stat, value, max_avoid, n=2000):
    setstat(ref, stat, value)
    setstat(ref, 380, max_avoid)
    for _ in range(6):
        ok, line, _l = hv("avoid %s %s %d" % (ref, kind, n), "avoid", 60)
        m = re.search(r"n=(\d+) .* success=(\d+)", line)
        if ok and m and int(m.group(2)) == 0 and kind != "evade" and ref != "player" and "mode=1 " not in line:
            time.sleep(1.5)  # a walking mercenary can neither dodge nor avoid: wait until it stands
            continue
        break
    return (int(m.group(2)) / int(m.group(1))) if ok and m else None


def check_avoidcaps():
    name = "Avoid / dodge / evade caps (min(75, 60 + stat 380)): player and mercenary"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    fight_ready()  # first: it kills everything near, the mercenary too
    if not ensure_merc():
        return add(name, "BLOCKED", "no mercenary could be hired")
    kinds = [("dodge", "melee", 338), ("avoid", "missile", 339), ("evade", "evade", 340)]
    cases = [(100, 0, 0.60), (100, 10, 0.70), (100, 30, 0.75), (50, 0, 0.50)]  # (stat, stat 380, expected success rate)
    rows, bad = [], []
    for ref in ("player", "merc"):
        saved = {s: rd(ref, s) for s in (338, 339, 340, 380)}
        for label, kind, stat in kinds:
            for value, mx, want in cases:
                r = _avoid_rate(ref, kind, stat, value, mx)
                rows.append("%s %s %d (380=%d): %s" % (ref, label, value, mx, "-" if r is None else "%.1f%%" % (r * 100)))
                if r is None or abs(r - want) > 0.04:
                    bad.append("%s (want %d%%)" % (rows[-1], round(want * 100)))
            setstat(ref, stat, 0)
        for s, v in saved.items():
            setstat(ref, s, v or 0)
    add(name, "PASS" if not bad else "FAIL",
        "success rate of 2000 checks of the game's avoid function: %s" % ("; ".join(rows) if not bad else "; ".join(bad)),
        [sh("avoidcaps")], rows)


def _make_ring(lock):
    ok, lines = H("item rin 492=%d inv" % lock)
    m = re.search(r"item id=(\d+)", " ".join(lines))
    return int(m.group(1)) if ok and m else None


def _req(ref, cls=None):
    ok, lines = H("req %s%s" % (ref, (" as %d" % cls) if cls is not None else ""), 30)
    rows = {}
    for l in lines:
        if l.startswith("req id="):
            d = kv(l)
            rows[int(d["id"])] = {"usable": int(d["usable"]), "lock": int(d["lock"]), "weapon": int(d["weapon"]),
                                  "shield": int(d["shield"]), "types": [] if d["types"] == "-" else [int(x) for x in d["types"].split(",")],
                                  "code": d["code"]}
    return rows


MERC_LISTS = {271: (0, (27,), ()), 338: (3, (33, 34), (112,)), 359: (1, (30, 2), (212,)), 561: (4, (30,), (198,))}
# monster class -> (the character class it equips as, weapon / shield types allowed, types refused again): hireclasses (mxl_hirelings.h)


def check_classlock():
    name = "Class-locked items (stat 492): player of each class and each mercenary type"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    if not ensure_merc():
        return add(name, "BLOCKED", "no mercenary could be hired")
    merc_boost()
    H("packclear")  # `req` lists a limited number of items: earlier checks' leftovers pushed the rings off the list
    rings = {lock: _make_ring(lock) for lock in range(1, 9)}
    if not all(rings.values()):
        return add(name, "BLOCKED", "could not create the locked rings: %s" % rings)
    cls0 = hstate()["player"]["class"]
    rows, bad = [], []
    try:
        for cls in range(7):
            r = _req("player", cls)
            got = "".join(str(r[rings[l]]["usable"]) for l in range(1, 9))
            want = "".join("1" if (l == 8 or l - 1 == cls) else "0" for l in range(1, 9))
            rows.append("player class %d: locks 1-8 usable %s" % (cls, got))
            if got != want:
                bad.append("player class %d: usable %s, want %s" % (cls, got, want))
        for mcls, (equiv, _a, _d) in MERC_LISTS.items():
            r = _req("merc", mcls)
            got = "".join(str(r[rings[l]]["usable"]) for l in range(1, 9))
            want = "".join("1" if (l == 8 or l - 1 == equiv) else "0" for l in range(1, 9))
            rows.append("merc class %d (equips as class %d): locks 1-8 usable %s" % (mcls, equiv, got))
            if got != want:
                bad.append("merc class %d: usable %s, want %s" % (mcls, got, want))
    finally:
        H("class %d" % cls0)
    add(name, "PASS" if not bad else "FAIL",
        "`req` (the engine's requirement check, hooked for stat 492) of rings with lock 1-8 (lock = class + 1, 8 = nobody): %s"
        % ("; ".join(rows) if not bad else "; ".join(bad)), [], rows)


def check_hiregates():
    name = "Mercenary equip gates by hire class (weapon / shield types), all four hire classes"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    if not ensure_merc():
        return add(name, "BLOCKED", "no mercenary could be hired")
    merc_boost()
    for code in ("163", "139", "156", "104", "110", "158", "!1!", "tow", "l53", "115"):
        H("item %s inv" % code)  # best effort: the backpack may be full of the earlier ones
    rows, bad = [], []
    for mcls, (equiv, allow, deny) in MERC_LISTS.items():
        r = _req("merc", mcls)
        allowed = refused = 0
        for iid, it in r.items():
            if not (it["weapon"] or it["shield"]):
                continue
            want = (not any(t in deny for t in it["types"])) and any(t in allow for t in it["types"])
            if want:
                allowed += 1
            else:
                refused += 1
            if bool(it["usable"]) != want:
                bad.append("class %d item %s (%s types %s): usable %d, want %d" % (mcls, it["code"], iid, it["types"], it["usable"], want))
        rows.append("class %d: %d weapons / shields allowed, %d refused" % (mcls, allowed, refused))
        if allowed == 0 or refused == 0:
            bad.append("class %d: no %s item in the backpack to judge" % (mcls, "allowed" if allowed == 0 else "refused"))
    add(name, "PASS" if not bad else "FAIL",
        "`req merc as <class>` over the backpack weapons / shields: %s" % ("; ".join(rows) if not bad else "; ".join(bad[:6])), [], rows)


ETH_RATE = 0.026  # Median: rand(1000) <= 25


def check_itemrolls():
    n1 = "Socket roll (quality 1-3): rand() % 7, clipped to the base"
    n2 = "Ethereal roll: 2.6%, +25% defence, 75% durability"
    if not need_game():
        add(n1, "BLOCKED", "not in a game")
        return add(n2, "BLOCKED", "not in a game")
    tried = []
    for code in ("tow", "plt", "hax", "bsd"):  # a shield, body armour, an axe and a sword: the first base the ethereal gate lets through
        ok, line, _l = hv("itemroll %s 4000 q=2 ilvl=110" % code, "itemroll", 120)
        lines = _l
        m0 = re.search(r"ethereal=(\d+)", line)
        tried.append("%s %s" % (code, m0.group(1) if m0 else "-"))
        if ok and m0 and int(m0.group(1)) > 0:
            break
    m = re.search(r"n=(\d+) failed=(\d+) ilvl=(\d+) ethereal=(\d+) sockets=\[([^\]]*)\]", line)
    d = next((l for l in lines if l.startswith("itemroll def")), "")
    md = re.search(r"normal n=(\d+) sum=(\d+), ethereal n=(\d+) sum=(\d+); maxdur normal (\d+)\.\.(\d+), ethereal (\d+)\.\.(-?\d+)", d)
    if not (ok and m and md):
        add(n1, "BLOCKED", "itemroll answered: %s" % lines[:2])
        return add(n2, "BLOCKED", "itemroll answered: %s" % lines[:2])
    n = int(m.group(1))
    hist = {int(a): int(b) for a, b in (p.split(":") for p in m.group(5).split())}
    top = max(hist)
    exp = {k: n / 7.0 for k in range(top)}
    exp[top] = n * (7 - top) / 7.0
    bad = []
    for k in range(top + 1):
        sd = (exp[k] * (1 - exp[k] / n)) ** 0.5
        if abs(hist.get(k, 0) - exp[k]) > 4 * sd:
            bad.append("%d sockets: %d (want %.0f +- %.0f)" % (k, hist.get(k, 0), exp[k], 4 * sd))
    add(n1, "PASS" if not bad and top >= 3 else "FAIL",
        "%d items of the last base (quality 2): sockets histogram %s; expected 1/7 for 0..%d sockets and %d/7 for %d (D2R's own: a third of the items, rand(max)+1): %s"
        % (n, m.group(5), top - 1, 7 - top, top, "; ".join(bad) or "all within 4 sigma"), [], [line])
    eth = int(m.group(4))
    rate = eth / float(n)
    nn, sn, ne, se, dmin, dmax, emin, emax = (int(x) for x in md.groups())
    avg_n = sn / float(nn) if nn else 0
    avg_e = se / float(ne) if ne else 0
    ratio = avg_e / avg_n if avg_n else 0
    bad2 = []
    mean, sd = n * ETH_RATE, (n * ETH_RATE * (1 - ETH_RATE)) ** 0.5
    if abs(eth - mean) > 4 * sd:
        bad2.append("%d ethereal outside 4 sigma of Median's 2.6%% (%.0f +- %.0f; D2R's own would be 5%%)" % (eth, mean, 4 * sd))
    if ne and not 1.17 <= ratio <= 1.33:
        bad2.append("defence ratio %.2f (want 1.25; D2R 1.5)" % ratio)
    want_dur = dmax * 75 // 100
    if ne and (emin != want_dur or emax != want_dur):
        bad2.append("ethereal max durability %d..%d (want %d = 75%% of %d)" % (emin, emax, want_dur, dmax))
    add(n2, "PASS" if not bad2 and ne >= 40 else "FAIL",
        "%d items at item level %s (ethereal count per base tried: %s): %d ethereal (%.2f%%); defence average %.1f -> %.1f (x%.2f); max durability %d -> %d..%d%s"
        % (n, m.group(3), ", ".join(tried), eth, rate * 100, avg_n, avg_e, ratio, dmax, emin, emax, ("; " + "; ".join(bad2)) if bad2 else ""),
        [], [line, d])
    # quality 1 (low): D2R's own gate never lets it through, Median's roll does (qualities word2 = 25 for quality 1 too)
    n3 = "Ethereal roll, quality 1 (low): 2.6%, +25%, 75% durability"
    ok, line, _l = hv("itemroll %s 4000 q=1 ilvl=110" % (tried[-1].split()[0]), "itemroll", 120)
    m3 = re.search(r"n=(\d+) failed=(\d+) ilvl=(\d+) ethereal=(\d+)", line)
    d3 = next((l for l in _l if l.startswith("itemroll def")), "")
    md3 = re.search(r"normal n=(\d+) sum=(\d+), ethereal n=(\d+) sum=(\d+); maxdur normal (\d+)\.\.(\d+), ethereal (\d+)\.\.(-?\d+)", d3)
    if not (ok and m3 and md3):
        return add(n3, "BLOCKED", "itemroll answered: %s" % _l[:2])
    n_, e_ = int(m3.group(1)), int(m3.group(4))
    mean, sd = n_ * ETH_RATE, (n_ * ETH_RATE * (1 - ETH_RATE)) ** 0.5
    bad3 = []
    if abs(e_ - mean) > 4 * sd:
        bad3.append("%d ethereal outside 4 sigma of 2.6%% (%.0f +- %.0f)" % (e_, mean, 4 * sd))
    dmax3 = int(md3.group(6))
    if e_ and (int(md3.group(7)) != dmax3 * 75 // 100 or int(md3.group(8)) != dmax3 * 75 // 100):
        bad3.append("ethereal max durability %s..%s (want %d)" % (md3.group(7), md3.group(8), dmax3 * 75 // 100))
    add(n3, "PASS" if not bad3 and e_ >= 40 else "FAIL",
        "%d items at item level %s: %d ethereal (%.2f%%), max durability %d -> %s..%s%s"
        % (n_, m3.group(3), e_, 100.0 * e_ / n_, dmax3, md3.group(7), md3.group(8), ("; " + "; ".join(bad3)) if bad3 else ""), [], [line, d3])


def check_dye():
    name = "Dye colour (stat 368): the item colour byte and palette"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    H("cubeclear")
    ok, lines = H("item tow cube")
    m = re.search(r"item id=(\d+)", " ".join(lines))
    if not ok or not m:
        return add(name, "BLOCKED", "no item to dye: %s" % lines[:1])
    iid = m.group(1)
    rows, bad = [], []
    for v in list(range(0, 22)) + [-5]:
        setstat(iid, 368, v)
        ok, line, _l = hv("colour %s" % iid, "colour")
        d = kv(line)
        byte = int(d.get("byte", "0xee"), 16) if ok else None
        dyed = v >= 1
        want_idx = v - 1
        good = ok and ((d.get("palette") == "set" and byte is not None and (byte & 0x1f) == want_idx) if dyed else d.get("palette") == "none")
        rows.append("%d->%s" % (v, ("0x%02x" % byte) if byte is not None else "-"))
        if not good:
            bad.append("stat 368 = %d gave %s" % (v, line[:70]))
    H("cubeclear")
    add(name, "PASS" if not bad else "FAIL",
        "item colour function on a tower shield with stat 368 = 0..21 and -5: index = stat - 1 with a palette entry, 0 / negative = undyed; bytes: %s%s"
        % (" ".join(rows), ("; " + "; ".join(bad[:4])) if bad else ""), [], rows)


def check_goldclient():
    name = "Gold payment: the client's gold follows the server after a cube payment"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    close_menu()
    H("cubeclear")
    H("stat player 532 set 140")
    H("gold set 400000 100000")
    H("item 0u1 cube uniq=30 532=1")
    H("item f667 cube")
    ok1, l1 = H("cubecraft")
    time.sleep(1.5)
    _, s1 = H("gold get")
    _, c14 = H("cstat 14")
    _, c15 = H("cstat 15")
    H("cubeclear")
    H("gold set 20000 100000")
    H("item 0u1 cube uniq=30 532=1")
    H("item f667 cube")
    H("item f667 cube")
    ok2, l2 = H("cubecraft")
    time.sleep(1.5)
    _, s2 = H("gold get")
    _, d14 = H("cstat 14")
    _, d15 = H("cstat 15")
    H("cubeclear")
    H("stat player 532 set 0")
    a = (stat_of(c14), stat_of(c15))
    b = (stat_of(d14), stat_of(d15))
    good = ok1 and ok2 and a == (350000, 100000) and b == (0, 20000)
    add(name, "PASS" if good else "FAIL",
        "row 6327: server %s, client carried/bank %s (want 350000/100000); row 6328 from 20000/100000: server %s, client %s (want 0/20000)"
        % (s1[0], a, s2[0], b), [], l1 + l2)


def leave_and_reenter():
    """Esc menu -> Save and Exit -> character select -> Play (the same character)."""
    g.key("esc")
    time.sleep(1.5)
    g.click(*g.UI["save_exit"])
    time.sleep(9)
    STATE["in_game"] = False
    left = not g.in_game()
    if not left:
        return False
    time.sleep(2)
    return enter_game()


def check_bonusdrops():
    name = "Summoner / Izual bonus catalysts (Nightmare): dropped once on the ground, flag set"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    fight_ready()
    H("difficulty 1")
    H("quest 47 1 clear")
    H("quest 47 2 clear")
    out = []
    ok = True
    for cls, code, bit in ((250, "0o[", 1), (256, "jcat", 2)):
        before = ground_count(code)
        H("spawn %d" % cls)
        H("kill nearest %d" % cls)
        time.sleep(1)
        n1 = ground_count(code) - before
        _, q = H("quest 47 %d get" % bit)
        H("spawn %d" % cls)
        H("kill nearest %d" % cls)
        time.sleep(1)
        n2 = ground_count(code) - before
        out.append("class %d: %s on the ground after kill 1: %d, after kill 2: %d; flag bit %d: %s" % (cls, code, n1, n2, bit, q[0]))
        ok = ok and n1 == 1 and n2 == 1
    H("difficulty 0")
    add(name, "PASS" if ok else "FAIL", "; ".join(out) + " (one per player: the multiplayer count is manual)", [sh("bonusdrops")], out)


def check_summoner():
    n1 = "Summoner AI (first skill branch by the aip3 roll) and MosquitoNest init"
    if not need_game():
        return add(n1, "BLOCKED", "not in a game")
    fight_ready()
    god(True)
    notes, ok_all = [], True
    for cls, label in ((250, "Summoner"), (2053, "MosquitoNest")):
        H("spawn %d" % cls)
        per = {}
        t0 = time.time()
        while time.time() - t0 < 14:
            time.sleep(0.5)
            for m in hstate()["mons"]:
                if m.get("npc") or m.get("class") in (2, 271, 150):
                    continue
                d = per.setdefault(m["class"], {"modes": set(), "dists": set()})
                d["modes"].add(m.get("mode"))
                d["dists"].add(m.get("dist"))
        mine = per.get(cls, {"modes": set(), "dists": set()})
        others = sorted(k for k in per if k != cls)
        acted = len(mine["modes"]) > 1 or len(mine["dists"]) > 1
        if cls == 2053:
            acted = acted and bool(others)  # the nest's init sets up the spawn of mosquitoes
        ok_all = ok_all and acted
        notes.append("%s %d: modes %s, distances %s, other monsters that appeared: %s" % (
            label, cls, sorted(x for x in mine["modes"] if x is not None), sorted(x for x in mine["dists"] if x is not None)[:5], others))
        for _ in range(8):
            H("kill nearest")
    god(False)
    add(n1, "PASS" if ok_all else "FAIL", "; ".join(notes), [sh("summoner")], notes)


def check_tooltip():
    name = "Item tooltip text after the descfunc change (65 stats): screenshots of hovered items"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    close_menu()
    pos = []
    # stats whose Median text became D2R printf text (descfunc 19): strength 0, dexterity 2, vitality 3, energy 1, life 7 / mana 9
    # (8.8 fixed point), attack rating 19, resists 39 / 41, increased attack speed 93
    for stats in ("0=30 2=20 3=15", "1=12 7=2560 9=1536", "19=120 39=15 41=20 93=10"):
        ok, lines = H("item rin q=4 %s inv" % stats)
        m = re.search(r"item id=(\d+) .* x=(\d+) y=(\d+)", " ".join(lines[:1])) if ok else None
        if m:
            pos.append((int(m.group(2)), int(m.group(3)), stats))
    g.key("i")
    time.sleep(1.2)
    shots = [sh("inventory")]
    for i, (x, y, stats) in enumerate(pos):
        fx, fy = (1055 + (x + 0.5) * 27.6) / 1600.0, (381 + (y + 0.5) * 26.0) / 900.0  # the 15 x 10 backpack grid of the 1600 x 900 client
        g.click(fx, fy, move_only=True)
        time.sleep(1.0)
        g.click(fx + 0.002, fy + 0.002, move_only=True)  # the tooltip wants a second mouse move over the item and a dwell
        time.sleep(2.0)
        shots.append(sh("tooltip%d" % i))
    g.key("esc")
    add(name, "REVIEW", "rare rings with %s hovered in the inventory (read the screenshots by hand): %s" % (
        " / ".join(p[2] for p in pos), ", ".join(shots)), shots)


def check_gameend():
    n1 = "Craft per-game flags reset when the game ends (dungeon key recipe sets game flag 50)"
    if not need_game():
        return add(n1, "BLOCKED", "not in a game")
    goto(1)
    close_menu()
    H("cubeclear")
    H("stat player 536 set 1000")
    H("item rx01 cube")
    H("item f700 cube")
    ok0, l0 = H("cubecraft")
    _, g0 = H("flag game 50 get")
    H("cubeclear")
    H("stat player 536 set 0")
    t0 = mxl_text()
    e0 = counter(t0, "game end", r"games ended (\d+)")
    if not leave_and_reenter():
        return add(n1, "BLOCKED", "could not leave and re-enter the game (flag before: %s)" % g0[0])
    time.sleep(3)
    _, g1 = H("flag game 50 get")
    t1 = mxl_text()
    e1 = counter(t1, "game end", r"games ended (\d+)")
    ok = ok0 and g0[0].endswith("= 1") and g1[0].endswith("= 0") and e0 is not None and e1 is not None and e1 > e0
    add(n1, "PASS" if ok else "FAIL",
        "game flag 50 set by the recipe (%s): %s; after Save and Exit and Play again (a new game): %s; `mxl` games ended %s -> %s"
        % (l0[0][:40] if l0 else "-", g0[0], g1[0], e0, e1), [sh("gameend")], [x for x in t1.splitlines() if "game end" in x])


def _weapon_bonus(code):
    """(StrBonus, DexBonus) of a weapons.txt row of the built mod (blank = 0): the independent source for the formula."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "mod", "data", "global", "excel", "base", "weapons.txt")
    with open(path, encoding="utf-8-sig") as f:
        rows = [l.rstrip("\n").split("\t") for l in f]
    h = rows[0]
    for r in rows[1:]:
        if r[h.index("code")] == code:
            return int(r[h.index("StrBonus")] or 0), int(r[h.index("DexBonus")] or 0)
    return None


def _panelrow(stat):
    """`panelrow <stat>` -> (displayed value, text, weapon (str bonus, dex bonus) or None)."""
    ok, lines = H("panelrow %d" % stat)
    row = next((l for l in lines if l.startswith("panelrow %d " % stat)), "")
    m = re.search(r"value=(-?\d+) text=\[(.*)\]", row)
    w = next((l for l in lines if l.startswith("panelrow weapon")), "")
    mw = re.search(r"strbonus=(-?\d+) dexbonus=(-?\d+)", w)
    return (int(m.group(1)), m.group(2), (int(mw.group(1)), int(mw.group(2))) if mw else None) if ok and m else None


def check_statcols():
    name = "Stat pages: Str / Dex damage bonus rows and the total / % bonus / base columns of the Advanced Stats panel"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    close_menu()
    rows, bad = [], []
    base = {s: rd("player", s) for s in (0, 2)}
    try:
        # no weapon: both damage bonus rows are 0 (Median's handler returns 0 without a weapon)
        for slot in ("rhand", "lhand"):
            H("unequip " + slot)
        time.sleep(1)
        for stat in (299, 300):
            r = _panelrow(stat)
            rows.append("no weapon, stat %d: %s" % (stat, r))
            if r is None or r[0] != 0 or r[2] is not None:
                bad.append("no weapon: stat %d row %s (want value 0, no weapon)" % (stat, r))
        for code in ("hfh", "hst"):  # strength-only (16 / 0) and dexterity-only (0 / 6) in weapons.txt
            want_b = _weapon_bonus(code)
            if want_b is None:
                bad.append("%s is not in weapons.txt" % code)
                continue
            for sv, dv in ((50, 30), (123, 77), (300, 9)):
                setstat("player", 0, sv)
                setstat("player", 2, dv)
                ok, _l = H("equip %s rhand" % code)
                if not ok:
                    bad.append("equip %s failed: %s" % (code, _l[:1]))
                    continue
                time.sleep(1.2)
                tot_s, tot_d = stat_of(H("cstat 0")[1]), stat_of(H("cstat 2")[1])
                r299, r300 = _panelrow(299), _panelrow(300)
                if r299 is None or r300 is None:
                    bad.append("%s str %d dex %d: panelrow failed" % (code, sv, dv))
                    H("unequip rhand")
                    continue
                want_s = tot_s * want_b[0] // 100
                want_d = tot_d * want_b[1] // 100
                rows.append("%s (bonus %d/%d), Str %s Dex %s: str row %d [%s], dex row %d [%s], weapon %s"
                            % (code, want_b[0], want_b[1], tot_s, tot_d, r299[0], r299[1], r300[0], r300[1], r299[2]))
                if r299[0] != want_s or r300[0] != want_d:
                    bad.append("%s Str %s Dex %s: rows %d / %d, want %d / %d" % (code, tot_s, tot_d, r299[0], r300[0], want_s, want_d))
                if r299[2] != want_b:
                    bad.append("%s: weapon bonuses read %s, weapons.txt %s" % (code, r299[2], want_b))
                for v, t, lbl in ((want_s, r299[1], "str"), (want_d, r300[1], "dex")):
                    if v != 0 and str(v) not in t:
                        bad.append("%s %s row text [%s] lacks %d" % (code, lbl, t, v))
                H("unequip rhand")
        # the columns: attribute rows carry the % stat and the base after the total
        setstat("player", 0, 100)
        setstat("player", 2, 60)
        time.sleep(1.2)
        for stat in (0, 2):
            r = _panelrow(stat)
            tot = stat_of(H("cstat %d" % stat)[1])
            rows.append("stat %d: total %s -> %s" % (stat, tot, r))
            if r is None:
                bad.append("stat %d: panelrow failed" % stat)
                continue
            if r[0] != tot:
                bad.append("stat %d: displayed value %d, total %s" % (stat, r[0], tot))
            if "%d base" % (rd("player", stat) or 0) not in r[1]:
                bad.append("stat %d: text [%s] lacks base %s" % (stat, r[1], rd("player", stat)))
        # a % stat on gear shows as the column (the stat has no row of its own)
        ok, _l = H("equip rin 388=25 389=-10 rring")
        time.sleep(1.2)
        rs, rdx = _panelrow(0), _panelrow(2)
        rows.append("ring with +25%% str / -10%% dex: %s / %s" % (rs, rdx))
        if not ok or rs is None or rdx is None or "+25%" not in rs[1] or "-10%" not in rdx[1]:
            bad.append("ring %% columns: %s / %s (equip ok %s)" % (rs, rdx, ok))
    finally:
        for s, v in base.items():
            if v is not None:
                setstat("player", s, v)
        for slot in ("rhand", "rring"):
            H("unequip " + slot)
    add(name, "PASS" if not bad else "FAIL",
        "panel row functions on the client's player copy (harness `panelrow`), formula Str * StrBonus / 100 and Dex * DexBonus / 100 of the "
        "wielded weapon vs weapons.txt: %s" % ("; ".join(rows[:6]) if not bad else "; ".join(bad)), [], rows)

# ------------------------------------------------------------------ lane bug-backlog-b: the old "fix to test" rows of docs/bugs.md
# Each check drives the game the way a player does (bg mouse / keys, panels) and reads the answer from the harness or the
# files the game writes. Harness verbs added for them: skilltab, str, hover, automap, shop, levelact (docs/testing.md).

MOD_DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "mod", "data")
ACT_TOWNS = {0: 1, 1: 40, 2: 75, 3: 103, 4: 109}  # act index (levels.txt Act column) -> town level


def log_mark():
    return g._size(g.LOADER_LOG)


def log_since(off):
    return g._read_from(g.LOADER_LOG, off)


def assertions_since(off):
    """The messages of the assertion boxes d2rloader.log wrote since `off` (D2R's own asserts continue after logging)."""
    return re.findall(r"Message : (.*?)\s*\|", log_since(off))


def _crop_hash(path, box):
    from PIL import Image
    im = Image.open(path).convert("RGB").crop(box)
    return hashlib.md5(im.tobytes()).hexdigest()[:10]


def _shot_path(name):
    return os.path.join(g.current_run(), name + ".png")


def _red_pixels(path, box):
    """Pixels of the red "None" text of an unbound key in the Options controls list."""
    from PIL import Image
    im = Image.open(path).convert("RGB").crop(box)
    return sum(1 for r, gr, b in im.getdata() if r > 150 and gr < 90 and b < 90)


# --- #3 skill tree tabs, #9 skill tooltips -----------------------------------------------------------------------------

TREE_X = (1148, 1258, 1368)  # skill icon columns of the HD tree panel (1600 x 900 client)
TREE_Y0, TREE_DY, TREE_ROWS = 232, 75, 6
TREE_CLOSE = (1485 / 1600.0, 103 / 900.0)


def open_skill_tree():
    """T toggles the panel; the tab setter hook (`h skilltab`) counts the clicks the panel takes, so a click on tab 2 tells if the
    panel is open."""
    x0, y0 = g.UI["skill_tab0"]
    for _ in range(2):
        g.key("t")
        time.sleep(1.5)
        ok, lines = g.hcmd("skilltab", 5)
        calls0 = int(re.search(r"setter_calls=(\d+)", " ".join(lines)).group(1)) if ok else -1
        g.click(x0 + g.UI["skill_tab_dx"] * 2, y0)
        time.sleep(1.0)
        ok, lines = g.hcmd("skilltab", 5)
        m = re.search(r"current=(\d+) setter_calls=(\d+)", " ".join(lines))
        if ok and m and int(m.group(2)) > calls0 and int(m.group(1)) == 2:
            return True
    return False


def check_skilltree():
    name = "Skill tree: 8 tabs open and show their own pages (#3); hovering every icon raises no skill tooltip assert (#9)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    revive_if_dead()
    goto(1)
    close_menu()
    off = log_mark()
    if not open_skill_tree():
        return add(name, "BLOCKED", "the skill tree did not open / take a tab click")
    x0, y0 = g.UI["skill_tab0"]
    dx = g.UI["skill_tab_dx"]
    tabs, hashes, shots, bad = [], [], [], []
    for i in range(8):
        g.click(x0 + dx * i, y0)
        time.sleep(1.2)
        ok, lines = g.hcmd("skilltab", 5)
        cur = int(re.search(r"current=(\d+)", " ".join(lines)).group(1)) if ok else -1
        tabs.append(cur)
        if cur != i:
            bad.append("tab %d selected page %d" % (i, cur))
        shots.append(sh("skilltab%d" % (i + 1)))
        hashes.append(_crop_hash(_shot_path("skilltab%d" % (i + 1)), (1075, 195, 1445, 650)))  # the icon area of the panel
    distinct = len(set(hashes))
    if distinct < 7:
        bad.append("only %d of 8 tabs look different (%s)" % (distinct, hashes))
    # #9: the mouse over every icon cell of every tab; D2R builds the tooltip lines (the assert sits in that renderer)
    hovers = 0
    for i in range(8):
        g.click(x0 + dx * i, y0)
        time.sleep(0.8)
        for row in range(TREE_ROWS):
            for fx in TREE_X:
                fy = TREE_Y0 + TREE_DY * row
                g.click(fx / 1600.0, fy / 900.0, move_only=True)
                time.sleep(0.3)
                g.click((fx + 3) / 1600.0, (fy + 3) / 900.0, move_only=True)  # the tooltip wants a second move over the icon
                time.sleep(0.4)
                hovers += 1
        if i == 0:
            shots.append(sh("skilltip"))
    g.click(*TREE_CLOSE)
    time.sleep(0.8)
    asserts = assertions_since(off)
    dummy = [a for a in asserts if "strdummy" in a or "stringIndex1" in a]
    new_asserts = sorted(set(asserts))
    if dummy:
        bad.append("%d skill tooltip assert(s) `stringIndex1 != strdummy`" % len(dummy))
    status = "PASS" if not bad else "FAIL"
    add(name, status, "page of each tab click (harness `skilltab`): %s; distinct panel pictures %d / 8; %d icon cells hovered, assert boxes in "
        "d2rloader.log: %s%s" % (tabs, distinct, hovers, new_asserts or "none", ("; " + "; ".join(bad)) if bad else ""), shots)


# --- #5 keybinds survive Save and Exit ---------------------------------------------------------------------------------

OPT_OPTIONS = (0.5, 0.375)
OPT_CONTROLS = (967 / 1600.0, 141 / 900.0)
OPT_SKILL9_KEY1 = (705 / 1600.0, 607 / 900.0)
OPT_APPLY = (591 / 1600.0, 775 / 900.0)
OPT_DEFAULT = (800 / 1600.0, 775 / 900.0)
OPT_LEGACY = (800 / 1600.0, 380 / 900.0)
OPT_CLOSE = (1189 / 1600.0, 69 / 900.0)
POPUP_SKILL = (910 / 1600.0, 740 / 900.0)  # the second icon of the lower row of the skill speed bar (S)


def _key_file():
    d2s = max((f for f in os.listdir(SAVES) if f.endswith(".d2s")), key=lambda f: os.path.getmtime(os.path.join(SAVES, f)))
    return os.path.join(SAVES, d2s[:-4] + ".key")


def _md5(p):
    return hashlib.md5(open(p, "rb").read()).hexdigest()[:8] if os.path.exists(p) else "(none)"


def open_controls():
    g.key("esc")
    time.sleep(1.5)
    g.click(*OPT_OPTIONS)
    time.sleep(1.5)
    g.click(*OPT_CONTROLS)
    time.sleep(1.5)


def close_options():
    g.click(*OPT_CLOSE)  # Options -> pause menu
    time.sleep(1.2)
    g.click(0.5, 0.505)  # Return to Game
    time.sleep(1.2)


def restore_legacy_keys():
    open_controls()
    g.click(*OPT_DEFAULT)
    time.sleep(1.5)
    g.click(*OPT_LEGACY)
    time.sleep(1.5)
    g.click(*OPT_APPLY)
    time.sleep(2.0)
    close_options()


def bind_skill_hotkey():
    """S opens the skill speed bar; with the mouse over an icon an F key binds that skill. Returns the (left, right) ids the key
    selects afterwards, or None."""
    g.key("s")
    time.sleep(1.5)
    g.click(*POPUP_SKILL, move_only=True)
    time.sleep(0.8)
    g.click(POPUP_SKILL[0] + 0.002, POPUP_SKILL[1] + 0.002, move_only=True)
    time.sleep(1.2)
    g.key("f6")
    time.sleep(1.0)
    g.key("s")
    time.sleep(1.0)
    return hotkey_skill()


def hotkey_skill(tries=3):
    """The (left, right) skill ids after F6 (retried: right after entering the game a key can arrive before the skill bar is ready)."""
    ids = None
    for _ in range(tries):
        H("skill left 0")
        H("skill right 0")
        g.key("f6")
        time.sleep(1.5)
        ok, lines = H("skill")
        m = re.search(r"left=(\d+) right=(\d+)", " ".join(lines))
        ids = (int(m.group(1)), int(m.group(2))) if ok and m else None
        if ids and ids != (0, 0):
            break
        time.sleep(2)
    return ids


def check_keybinds():
    name = "Keybinds survive Save and Exit and a restart (#5): a rebound key in Options > Controls and an F6 skill hotkey"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    revive_if_dead()
    goto(1)
    close_menu()
    keyfile = _key_file()
    shots, bad = [], []
    restore_legacy_keys()  # a failed earlier run may have left Skill 9 bound: start from the Legacy layout
    h0 = _md5(keyfile)
    # 1. Options > Controls: Skill 9 -> F9, Apply (D2R writes the character's .key file)
    open_controls()
    g.click(*OPT_SKILL9_KEY1)
    time.sleep(1.0)
    g.key("f9")
    time.sleep(1.0)
    shots.append(sh("keys_bound"))
    g.click(*OPT_APPLY)
    time.sleep(2.0)
    h1 = _md5(keyfile)
    if h1 == h0:
        bad.append("Apply did not change %s (%s)" % (os.path.basename(keyfile), h0))
    close_options()
    # 2. skill hotkey F6 through the speed bar
    ids0 = bind_skill_hotkey()
    if not ids0 or ids0 == (0, 0):
        bad.append("F6 bound no skill (the speed bar icon %s)" % (POPUP_SKILL,))
    # 3. Save and Exit, play again
    if not leave_and_reenter():
        return add(name, "BLOCKED", "could not leave and re-enter the game: %s" % "; ".join(bad))
    time.sleep(6)  # the town is still loading when the harness first answers
    for _ in range(10):
        if hstate()["ok"] and hstate()["player"].get("area") == 1:
            break
        time.sleep(2)
    h2 = _md5(keyfile)
    if h2 != h1:
        bad.append("the .key file changed on exit / restart (%s -> %s)" % (h1, h2))
    ids1 = hotkey_skill()
    if ids0 and ids1 != ids0:
        bad.append("F6 selected %s after the restart, %s before" % (ids1, ids0))
    open_controls()
    shots.append(sh("keys_after_restart"))
    red = _red_pixels(_shot_path("keys_after_restart"), (680, 598, 730, 616))
    if red > 5:
        bad.append("Skill 9 shows None after the restart (%d red pixels)" % red)
    # 4. put the bindings back: Default > Legacy is the layout the file started with
    g.click(*OPT_DEFAULT)
    time.sleep(1.5)
    g.click(*OPT_LEGACY)
    time.sleep(1.5)
    g.click(*OPT_APPLY)
    time.sleep(2.0)
    h3 = _md5(keyfile)
    close_options()
    if h3 != h0:
        bad.append("Default > Legacy > Apply did not restore the key file (%s, was %s)" % (h3, h0))
    add(name, "PASS" if not bad else "FAIL",
        ".key file md5 %s (before) -> %s (Skill 9 = F9 applied) -> %s (after Save and Exit + restart, Skill 9 still F9 in the list); "
        "F6 skill hotkey selects (left, right) %s before and %s after the restart; layout restored (%s, was %s)%s" % (
            h0, h1, h2, ids0, ids1, h3, h0, ("; " + "; ".join(bad)) if bad else ""), shots)


# --- #7 NPC / string texts ---------------------------------------------------------------------------------------------


def _strings_entries():
    import glob
    import json
    ents = []
    for f in sorted(glob.glob(os.path.join(MOD_DATA, "local", "lng", "strings", "*.json"))):
        for e in json.load(open(f, encoding="utf-8-sig")):
            ents.append((os.path.basename(f), e["id"], e["Key"], e["enUS"]))
    return ents


def _norm(text):
    """Letters only, lower case, without D2R's colour codes (the verb leaves the code's own character in the text)."""
    return re.sub(r"[^a-z]", "", re.sub("ÿc.", "", text).lower())[:200]


def check_strings():
    name = "String ids by low 16 bits: no Median key shares a D2R string's id, so NPCs speak their own text (#7)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    ents = _strings_entries()
    by_low = {}
    for f, i, k, t in ents:
        by_low.setdefault(i & 0xFFFF, {}).setdefault((i, k), t)
    clashes = {low: sorted(v) for low, v in by_low.items() if len({k for _i, k in v}) > 1}  # different keys on one 16-bit value
    # texts to read back from the running game: every NPC intro / gossip line and a seeded sample of Median's own strings
    import difflib
    import random
    rnd = random.Random(7)
    npc = [e for e in ents if re.search(r"Intro|Gossip|Greet", e[2]) and e[1] < 0x10000]
    new = [e for e in ents if 28106 <= e[1] < 0x10000]
    pick = {}
    for f, i, k, t in npc + rnd.sample(new, min(250, len(new))):
        if len(by_low[i & 0xFFFF]) == 1 and not (i & 0xFFFF) in clashes:
            pick[i & 0xFFFF] = (k, t)
    ids = sorted(pick)
    wrong, checked = [], 0
    for n in range(0, len(ids), 10):
        batch = ids[n:n + 10]
        ok, lines = H("str " + " ".join(str(i) for i in batch))
        got = {}
        for ln in lines:
            m = re.match(r"\s*str (\d+) = \[(.*)\]\s*$", ln)
            if m:
                got[int(m.group(1))] = m.group(2)
        for i in batch:
            checked += 1
            want = _norm(pick[i][1])
            have = _norm(got.get(i, "(no answer)"))
            if (have or want) and difflib.SequenceMatcher(None, have, want).ratio() < 0.9:
                wrong.append("%d %s: game '%s' data '%s'" % (i, pick[i][0], have[:40], want[:40]))
    shots = []
    d2r_own = {27956, 28082}
    unexpected = {low: v for low, v in clashes.items() if low not in d2r_own}
    bad = []
    if unexpected:
        bad.append("%d 16-bit ids shared by different keys, e.g. %s" % (len(unexpected), list(unexpected.items())[:2]))
    if wrong:
        bad.append("%d of %d strings differ from the data: %s" % (len(wrong), checked, wrong[:3]))
    add(name, "PASS" if not bad else "FAIL",
        "%d keys in the string tables, %d 16-bit values hold two different keys (D2R ships %s itself: %s); the game's own string lookup (harness `str`) "
        "answered %d NPC intro / Median strings, %d wrong%s. What an NPC's subtitle looks like on screen stays a manual look" % (
            len(ents), len(clashes), sorted(d2r_own), sorted(set(clashes) & d2r_own), checked, len(wrong), ("; " + "; ".join(bad)) if bad else ""),
        shots, wrong[:20])


# --- #15 automap in Median-only level types ----------------------------------------------------------------------------

AUTOMAP_AREAS = {36: (138,), 37: (174, 175), 38: (191,), 39: (144,), 40: (247,), 41: (142,), 42: (152,), 43: (250,), 44: (243,),
                 51: (220,)}  # level type -> areas of it, tried in turn until the game's lookups hit a Median row (type 37 has wall rows only)


def check_automap():
    name = "Automap in Median-only areas: level types 36-51 resolve their Median automap rows (#15)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    revive_if_dead()
    ok, lines = H("automap")
    table = {}
    for ln in lines:
        m = re.search(r"automap type=(\d+) rows=(\d+)(?: resolved=(\d+) bad=(\d+))?", ln)
        if m:
            table[int(m.group(1))] = (int(m.group(2)), int(m.group(3) or 0), int(m.group(4) or 0))
    rows, bad, shots = [], [], []
    god(True)
    try:
        for typ, areas in AUTOMAP_AREAS.items():
            for area in areas:
                st = goto(area, timeout=50)
                ok2 = st["player"].get("area") == area
                _ok, lines = H("automap %d" % typ)
                m = re.search(r"game_lookups=(\d+) hits=(\d+)", " ".join(lines))
                lookups, hits = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
                if ok2 and hits > 0:
                    break
            n, resolved, badrows = table.get(typ, (0, 0, 0))
            g.key("tab")
            time.sleep(1.2)
            shots.append(sh("automap%d" % typ))
            g.key("tab")
            time.sleep(0.4)
            miss = ""
            if hits == 0:  # which (tile type, style) pairs the area asked for and the type's rows do not list
                txt = " ".join(lines)
                asked = ["%s/%s" % t for t in re.findall(r"miss tile=(\d+) style=(\d+)", txt)][:6]
                listed = ["%s/%s" % t for t in re.findall(r"row \d+ tile=(\d+) style=(\d+)", txt)]
                miss = " (it asked for tile/style %s, the rows list %s)" % (", ".join(asked), ", ".join(listed))
            rows.append("type %d (area %d%s): %d rows, %d resolved to a cel, game made %d lookups, %d found a Median row%s" % (
                typ, area, "" if ok2 else " NOT REACHED", n, resolved, lookups, hits, miss))
            if not ok2:
                bad.append("type %d: area %d not reached" % (typ, area))
            elif n >= 30 and (hits == 0 or lookups == 0):
                bad.append("type %d: the game's lookups found no Median row (%d lookups)" % (typ, lookups))
            if n and resolved < n * 0.99 - 1:
                bad.append("type %d: only %d of %d rows resolve to a cel the atlas has" % (typ, resolved, n))
    finally:
        god(False)
        goto(1)
    add(name, "PASS" if not bad else "FAIL",
        "%s. Types 45-50 (e.g. Heart of Darkness) have no Median rows, so they keep D2R's lookup (docs/plugin-spec.md)%s" % (
            "; ".join(rows), ("; " + "; ".join(bad)) if bad else ""), shots, rows)


# --- #17 levels above 255 through the summoning scrolls ------------------------------------------------------------------

SCROLLS = ["b60X", "b61X", "b62X", "b63X", "b64X", "b65X", "b66X", "b67X", "b68X", "b70X", "b71X", "b72X", "b73X"]


def _scroll_targets():
    """code -> Median level of the scroll's calc1 (misc.txt: `(stat(...)>0)?0:N`) -> D2R level id (mxl_items.h LevelIds: Median level
    n >= 137 is D2R's n + 1) and the act town the portal can be opened from (the portal links levels of one act)."""
    import csv
    rows = list(csv.reader(open(os.path.join(MOD_DATA, "global", "excel", "misc.txt"), encoding="latin-1"), delimiter="\t"))
    h = rows[0]
    ci = {n: i for i, n in enumerate(h)}
    lv = list(csv.reader(open(os.path.join(MOD_DATA, "global", "excel", "levels.txt"), encoding="latin-1"), delimiter="\t"))
    lci = {n: i for i, n in enumerate(lv[0])}
    act = {int(r[lci["Id"]]): int(r[lci["Act"]] or 0) for r in lv[1:] if len(r) > lci["Act"] and r[lci["Id"]].isdigit()}
    out = {}
    for r in rows[1:]:
        code = r[ci["code"]] if len(r) > ci["code"] else ""
        if code in SCROLLS:
            nums = re.findall(r"\?0:(\d+)$", r[ci["calc1"]].strip())
            median = int(nums[0]) if nums else 0
            did = median + 1 if median >= 137 else median
            out[code] = (median, did, ACT_TOWNS.get(act.get(did, 0), 1) if did else None)
    return out


def check_scrollportals():
    name = "Summoning scrolls open portals that lead into the levels above 255 (#17)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    revive_if_dead()
    targets = _scroll_targets()
    rows, bad, shots = [], [], []
    god(True)
    base = {}
    try:
        goto(1)
        base = {n: stat_of(H("stat player %d" % n)[1]) for n in (12, 6, 7)}  # level, life, max life: the character is saved on exit
        H("difficulty 2")  # Hell rules and a high level: the dungeon lockout refuses a low character (docs/plugin-spec.md)
        H("stat player 12 set 150")  # the highest entry requirement is 147 (mxl_dungeons.h)
        H("stat player 7 set 20000000")
        H("stat player 6 set 20000000")
        for code in SCROLLS:
            median, want, town = targets.get(code, (0, 0, None))
            if not want:
                # b73X: calc1 is 0 by Median's formula (no destination yet): the scroll must refuse
                goto(1)
                H("packclear")
                H("item %s inv" % code)
                ok, _l = H("use " + code)
                rows.append("%s: no destination in the data, %s" % (code, "refused" if not ok else "OPENED A PORTAL"))
                if ok:
                    bad.append("%s opened a portal without a destination" % code)
                continue
            st = goto(town, timeout=50)
            H("packclear")
            H("item %s inv" % code)
            before = {o["id"] for o in hstate()["objs"]}
            ok, lines = H("use " + code)
            time.sleep(1.5)
            new = [o for o in hstate()["objs"] if o["id"] not in before and o.get("class") in (59, 60, 600)]
            if not ok or not new:
                rows.append("%s -> level %d: the use was refused / no portal (%s)" % (code, want, lines[-1][:60] if lines else ""))
                bad.append("%s: no portal to level %d from town %d" % (code, want, town))
                continue
            moved = False
            for _ in range(3):  # the portal object needs a frame or two before it works
                time.sleep(2)
                ok, ll = H("enter %d" % new[0]["id"])
                if ll and "moved" in ll[0]:
                    moved = True
                    break
            area = None
            for _ in range(8):
                time.sleep(2)
                area = hstate()["player"].get("area")
                if area == want:
                    break
            if want > 255:
                shots.append(sh("portal%d" % want))
            rows.append("%s -> level %d%s: portal class %d, %s, entered area %s" % (code, want, " (>255)" if want > 255 else "", new[0]["class"], "moved" if moved else "REFUSED", area))
            if area != want:
                bad.append("%s: entered area %s, wanted %d" % (code, area, want))
    finally:
        god(False)
        H("difficulty 0")
        goto(1)
        for n, v in base.items():
            if v is not None:
                H("stat player %d set %d" % (n, v))
    add(name, "PASS" if not bad else "FAIL",
        "use the scroll in its act's town (Hell rules, level 150), `enter <portal id>`, read the area: %s%s" % ("; ".join(rows), ("; " + "; ".join(bad)) if bad else ""), shots, rows)


# --- #18 vendors ---------------------------------------------------------------------------------------------------------

SHOP_TABS = [(185 / 1600.0, 222 / 900.0), (289 / 1600.0, 222 / 900.0), (497 / 1600.0, 222 / 900.0)]  # Armor / Weapons / Misc
SHOP_GRID = (135, 248, 27.6)  # x0, y0, cell of the vendor panel's item grid (1600 x 900 client)
TRADE_CLOSE = (564 / 1600.0, 103 / 900.0)


def trade_panel_open():
    """The vendor panel's red close button (X) is on screen."""
    im = g.grab_client(g.need_window()).convert("RGB").crop((552, 92, 576, 114))
    return sum(1 for r, gr, b in im.getdata() if r > 140 and gr < 80 and b < 80) > 8


def check_vendors():
    name = "Vendors Charsi, Gheed, Akara: open the trade panel, browse the tabs, buy by right click; no crash, no [default_unit] flood (#18)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    revive_if_dead()
    goto(1)
    close_menu()
    off = log_mark()
    rows, bad, shots = [], [], []
    for cls, nm in ((148, "Akara"), (154, "Charsi"), (147, "Gheed")):
        H("packclear")  # a bought item needs room in the backpack
        for _ in range(5):  # the NPC's menu opens when the character stands next to it
            ok, _l = H("moveto nearest %d" % cls)
            if not ok:
                break  # not loaded
            time.sleep(4)
            near = [m for m in hstate()["mons"] if m.get("class") == cls]
            if near and near[0].get("dist", 99) <= 6:
                break
        near = [m for m in hstate()["mons"] if m.get("class") == cls]
        if not near or near[0].get("dist", 99) > 6:
            H("spawn %d" % cls)  # out of reach (behind a wall) or not loaded: a copy next to the character
            time.sleep(4)  # the client has to learn the new unit before it can talk to it
        H("interact %d" % cls)
        menu = wait_menu(60)
        if menu is None:
            bad.append("%s: no menu" % nm)
            continue
        ok, _l = H("menu 1")  # Trade
        for _ in range(6):
            time.sleep(2)
            if trade_panel_open():
                break
        else:
            bad.append("%s: the trade panel did not open" % nm)
        g.log("trade panel open before `shop`: %s" % trade_panel_open())
        ok, lines = H("shop %d" % cls)
        g.log("trade panel open after `shop`: %s" % trade_panel_open())
        stock = [kv(l) for l in lines if l.startswith("item ")]
        n = len(stock)
        for i, (fx, fy) in enumerate(SHOP_TABS):
            if cls == 148 and i == 0:
                continue  # Akara has no Armor tab: a click on its empty place closes the panel
            g.click(fx, fy)
            time.sleep(1.0)
            g.log("trade panel open after tab %d click: %s" % (i + 1, trade_panel_open()))
            shots.append(sh("shop_%s_tab%d" % (nm.lower(), i + 1)))
        # buy: an ammo-like stack on the Misc tab (page 3) when the vendor has one, else any Misc item
        g.click(*SHOP_TABS[2])
        time.sleep(0.8)
        misc = [s for s in stock if s.get("page") == "3" and s.get("code") not in ("key",)]
        misc.sort(key=lambda s: -int(s.get("qty", 0)))
        bought = "nothing on the Misc tab"
        tried = []
        H("gold set 40000 8888")  # inside the level cap on carried gold
        for s in misc[:3]:  # the first stack that the right click buys (a throwing weapon bought replaces the weapon in hand)
            st0 = hstate()
            x, y = int(s["x"]), int(s["y"])
            fx = (SHOP_GRID[0] + (x + 0.5) * SHOP_GRID[2]) / 1600.0
            fy = (SHOP_GRID[1] + (y + 0.5) * SHOP_GRID[2]) / 900.0
            g.click(fx, fy, button="right")
            time.sleep(2.0)
            st1 = hstate()
            gained = len(st1["items"]) - len(st0["items"])
            gold0, gold1 = st0["player"].get("gold", 0), st1["player"].get("gold", 0)
            tried.append("%s qty %s: items %+d, gold %d -> %d" % (s["code"], s.get("qty", "0"), gained, gold0, gold1))
            if gold1 < gold0:
                break
        bought = "right click buys: " + "; ".join(tried) if misc else bought
        if not misc:
            bad.append("%s: no stock on the Misc tab to buy" % nm)
        elif hstate()["player"].get("gold", 0) >= 40000:
            bad.append("%s: the right click bought nothing (%s)" % (nm, "; ".join(tried)))
        g.click(*TRADE_CLOSE)
        time.sleep(1.0)
        close_menu()
        alive = hstate()["ok"]
        rows.append("%s: %d stock items, %s, game %s" % (nm, n, bought, "alive" if alive else "GONE"))
        if not alive:
            bad.append("%s: the game stopped answering" % nm)
            break
    new_log = log_since(off)
    flood = new_log.count("[default_unit]")
    notfound = new_log.count("File not found")
    asserts = sorted(set(assertions_since(off)))
    if flood or notfound:
        bad.append("%d `[default_unit]` / %d `File not found` lines in d2rloader.log" % (flood, notfound))
    H("packclear")
    add(name, "PASS" if not bad else "FAIL",
        "%s; log since the first vendor: %d `[default_unit]`, %d `File not found`, assert boxes: %s%s. Whether every vendor picture looks right "
        "stays a manual look (the screenshots show each tab)" % ("; ".join(rows), flood, notfound, asserts or "none", ("; " + "; ".join(bad)) if bad else ""),
        shots, rows)


# --- #22 monsters can be hovered and attacked --------------------------------------------------------------------------


def _mon_screen(player, mon, k=1.3):
    """The isometric projection of a world offset onto the 1600 x 900 client (the player stands at the centre)."""
    dx, dy = mon["mx"] - player["x"], mon["my"] - player["y"]
    return 800 + (dx - dy) * 16 * k, 450 + (dx + dy) * 8 * k - 20


def check_monster_hover():
    name = "Monsters can be hovered and attacked through the player's own click path (#22)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    revive_if_dead()
    god(False)
    goto(2)
    close_menu()
    kill_all_near()
    H("skill left 0")  # the basic attack
    mid = spawn_id(5)  # a slow melee monster (zombie1)
    if mid is None:
        return add(name, "BLOCKED", "could not spawn a monster")
    time.sleep(1.5)
    hovered, life0, lives = False, None, []
    for step in range(24):
        st = hstate()
        mons = [m for m in st["mons"] if m["id"] == mid]
        if not mons:
            lives.append("gone")
            break
        m = mons[0]
        if life0 is None:
            life0 = m["life"]
        lives.append(m["life"])
        sx, sy = _mon_screen(st["player"], m)
        g.click(sx / 1600.0, sy / 900.0, move_only=True)
        time.sleep(0.4)
        g.click((sx + 2) / 1600.0, (sy + 2) / 900.0, move_only=True)
        time.sleep(0.5)
        ok, lines = g.hcmd("hover", 5)
        hm = re.search(r"hover id=(\d+) type=(\d+) class=(\d+) age_ms=(\d+)", " ".join(lines))
        if hm and int(hm.group(1)) == mid and int(hm.group(4)) < 1500:
            hovered = True
        g.click((sx + 2) / 1600.0, (sy + 2) / 900.0)  # left click = the basic attack on the hovered monster
        time.sleep(1.5)
    shot = sh("monster_fight")
    st = hstate()
    left = [m for m in st["mons"] if m["id"] == mid]
    dead = not left or left[0]["life"] == 0
    bad = []
    if not hovered:
        bad.append("the game never reported the spawned monster (id %d) as hovered" % mid)
    if life0 is None or not (dead or (left and left[0]["life"] < life0)):
        bad.append("the clicks did not hurt the monster (life %s)" % lives)
    kill_all_near()
    add(name, "PASS" if not bad else "FAIL",
        "spawned zombie1 id %d next to the player; the mouse moved over its projected position (harness `hover` reports the unit under "
        "the cursor): %s; left clicks on it: life %s, %s%s" % (mid, "hovered" if hovered else "NOT hovered", lives, "killed" if dead else "alive",
                                                           ("; " + "; ".join(bad)) if bad else ""), [shot], [str(lives)])


CHECKS = [("mxl", check_mxl), ("lockclock", check_lockout_clock), ("goblins", check_goblins), ("respec", check_respec),
          ("cain", check_cain), ("cainsub", check_cain_submenu), ("heart", check_heart), ("catalyst", check_catalyst), ("tower", check_tower), ("lockout", check_lockout), ("lockquest", check_lockquest),
          # reenter before cow: the cow check leaves a Cow Level portal in town that `enter nearest` would take
          ("reenter", check_dungeon_reenter), ("cow", check_cow), ("goldcraft", check_goldcraft), ("craftflags", check_craftflags),
          ("areacycle", check_areacycle), ("casts", check_casts), ("itemevents", check_item_events), ("medianai", check_median_ai),
          # the verify rows of docs/unported.md
          ("resistcap", check_resistcap), ("absorb", check_absorb), ("avoidcaps", check_avoidcaps), ("classlock", check_classlock),
          ("hiregates", check_hiregates), ("itemrolls", check_itemrolls), ("dye", check_dye), ("goldclient", check_goldclient),
          ("bonusdrops", check_bonusdrops), ("summoner", check_summoner), ("tooltip", check_tooltip), ("gameend", check_gameend),
          ("trophy", check_trophy), ("statcols", check_statcols),
          # the old "fix to test" rows of docs/bugs.md
          ("skilltree", check_skilltree), ("keybinds", check_keybinds), ("strings", check_strings), ("automap", check_automap),
          ("scrollportals", check_scrollportals), ("vendors", check_vendors), ("monhover", check_monster_hover)]


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
        # active when the game is created (quest 5 open for the tower check), and the plugin's catalyst flag (quest 47) clear; the earlier run's rewards
        # completed them. Closed save, checksum and sidecar fixed (docs/save-format.md).
        for name in sorted({f[:-4] for f in os.listdir(SAVES) if f.endswith(".d2s")}):
            patch_quests(name, {(0, 1): 0, (0, 4): 2, (0, 5): 0, (0, 6): 0, (0, 41): 0, (0, 47): 0})
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
                    revive_if_dead()
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

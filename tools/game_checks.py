#!/usr/bin/env python
"""Scripted in-game checks for the untested rows of docs/testing.md, in game_ctl's BACKGROUND input mode.

Everything goes through tools/game_ctl.py's background path: console lines run through the plugin test harness
(`g.console_bg`, `g.console_text`), keys and mouse are window messages plus the plugin's virtual cursor, screenshots
are PrintWindow captures. This script never presses Ctrl+`, never calls SetForegroundWindow / SetCursorPos and never
uses ImageGrab. A watchdog thread samples GetCursorPos and GetForegroundWindow every 25 ms for the whole run and
aborts the run when the real cursor enters the game window or the game window becomes the foreground window.

Run with `py -3 tools/game_checks.py all [check ...]` (writes report.md, screenshots and logs into
%USERPROFILE%\\mxl-d2r-runs\\checks-<time>\\); `--slot N` picks a game slot.
Checks: mxl lockclock goblins respec cain cainsub heart catalyst tower lockout lockquest reenter cow goldcraft
craftflags areacycle mapmem casts itemevents medianai medianai_ctl tyrael resistcap absorb damagered avoidcaps classlock
hiregates itemrolls dye goldclient bonusdrops summoner tooltip gameend trophy statcols skilltree keybinds npcstrings
automap scrollportals vendors monhover levelcap skillcaps townportal potions itemskills cowportals deathcrash nocorpse
nslong strings goblinname rewardsound statorder infopanel sprint steadyaim
heat trophytimer mapreveal wpauto hotkeys stashpages.
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


def ensure_cube():
    """The cube checks put items in the Horadric Cube (`item <code> cube`), which fails with "placing failed" when the
    character carries no cube (a fresh test character does not; an earlier check that removed it leaves none): give it one."""
    if not items(hstate(), "box"):
        H("item box inv")


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
    # the read-back can differ from the value set when the test character wears strength items left by earlier runs:
    # compare against what the game reports, not against fixed numbers
    spent = stat_of(H("stat player 0")[1])
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
    declined = (ok_no and stat_of(held0) == spent and stat_of(held1) == spent
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
    refunded = spent - s0 if s0 is not None else None
    asked2 = any("open" in ln for ln in c2)
    add(name, "PASS" if (asked and declined and asked2 and ok and done and s0 is not None and s0 < spent and refunded >= 60 and s4 == base4 + refunded) else "FAIL",
        "menu lines %s; the click opens the Yes / No page (%s) and No leaves strength at %s (%s); after Yes strength %s -> %s "
        "(Barbarian start 30), unused stat points %d -> %s (%s refunded); log `%s`; quest 41 %s" % (
            [ln.strip() for ln in menu[1:]], asked, spent, declined, spent, s0, base4, s4, refunded,
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


def skill_usable(skill):
    """The game's usability code (`h skills`, 0x33f360) of one skill of the player, None when he has not got it."""
    ok, lines = H("skills")
    for ln in lines:
        m = re.match(r"skill id=(\d+) level=\d+ mode=\d+ owner=-?\d+ usable=(\d+)", ln)
        if m and int(m.group(1)) == skill:
            return int(m.group(2))
    return None


def check_steadyaim():
    """Hunter's Steady Aim (docs/median-rules-audit.md SK10): Steady Shot (D2R 1368) builds stat 220 layer 5 on state
    sk1296_state one per shot up to the cap; Heartseeker (352, restrict 5: needs the state) is refused without it
    (plugin/src/skill_restrict.cpp), spends one charge per cast and drops the state at the last one."""
    nm = "Steady Aim: Steady Shot builds the counter, Heartseeker needs it and spends it (restrict 5)"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    goto(2)  # Blood Moor
    kill_all_near()
    st0 = hstate()["player"]
    cls0 = st0.get("class")
    log = []
    for cmd in ("class 5", "stat player 0 set 400", "stat player 2 set 400", "unequip rhand", "equip 163 rhand",
                "unequip lhand", "equip aqv lhand", "setskill 1368 5", "setskill 352 5"):
        ok, lines = H(cmd)
        log += lines
    counter = lambda: stat_of(H("stat player 220 5")[1])
    before = skill_usable(352)
    first = counter()
    H("spawn 3 1")
    seen = []
    for _ in range(4):
        H("cast 1368 nearest")
        time.sleep(2.5)
        seen.append(counter())
    during = skill_usable(352)
    ok, lines = H("cast 352 nearest")
    log += lines
    time.sleep(2.5)
    after_one = counter()
    for _ in range(6):
        if not counter():
            break
        H("cast 352 nearest")
        time.sleep(2.5)
    end = counter()
    after = skill_usable(352)
    kill_all_near()
    if cls0 is not None:
        H("class %d" % cls0)
    good = (before == 4 and first == 0 and seen == [1, 2, 3, 4] and during == 0 and after_one is not None
            and 0 < after_one < seen[-1] + 1 and end == 0 and after in (1, 4))  # 1 = Heartseeker's cooldown answers before the restriction
    add(nm, "PASS" if good else "FAIL",
        "Heartseeker usability (4 = restricted) before the first shot %s, counter %s; after Steady Shot x4 the counter reads %s "
        "and Heartseeker %s; one Heartseeker leaves %s; spent to %s and Heartseeker %s (4 restricted, or 1 while its cooldown runs)" % (
            before, first, seen, during, after_one, end, after), (), log)


def check_heat():
    """The Fire Sorceress's Heat (docs/median-rules-audit.md SK11): Molten Core (D2R 1682, a class skill) puts the heat loss
    rate in stat 297; with it set the mana bar drains outside a town instead of regenerating (plugin/src/heat.cpp), Flamefront
    (482) puts a negative rate on the caster for a moment, which builds heat, and a town stops the drain."""
    nm = "Heat: stat 297 drains the mana bar outside town, Flamefront builds it, a town holds it"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    goto(2)
    kill_all_near()
    cls0 = hstate()["player"].get("class")
    log = []
    for cmd in ("class 1", "setskill 482 5", "setskill 1682 5"):
        ok, lines = H(cmd)
        log += lines
    mana = lambda: stat_of(H("stat player 8")[1])
    rate = stat_of(H("stat player 297")[1])
    H("stat player 8 set 25600")
    top = mana()
    time.sleep(2.5)
    mid = mana()
    time.sleep(2.5)
    low = mana()
    for _ in range(12):  # run it dry (the floor is 0)
        if mana() == 0:
            break
        time.sleep(2)
    empty = mana()
    H("spawn 3 1")
    peak = empty
    H("cast 482 nearest")
    seen_negative = False
    for _ in range(8):
        peak = max(peak, mana() or 0)
        seen_negative = seen_negative or (stat_of(H("stat player 297")[1]) or 0) < 0
    kill_all_near()
    goto(1)  # Rogue Encampment
    H("stat player 8 set 12800")
    time.sleep(3)
    town = mana()
    goto(2)
    H("stat player 8 set 25600")
    if cls0 is not None:
        H("class %d" % cls0)
    good = (rate is not None and rate > 0 and top is not None and top >= 25000 and mid is not None and mid < top and low is not None and low < mid
            and empty == 0 and peak > 0 and seen_negative and town == 12800)
    add(nm, "PASS" if good else "FAIL",
        "stat 297 = %s; mana x256 from full: %s -> %s (2.5 s) -> %s (5 s) -> %s (empty, floor 0); Flamefront at empty: peak %s, "
        "stat 297 went negative: %s; in town (set to 12800, 3 s later): %s" % (rate, top, mid, low, empty, peak, seen_negative, town), (), log)


def check_trophytimer():
    """Trophy challenge windows (docs/median-rules-audit.md CR4, DC6; plugin/src/trophy_timer.cpp). The Heart of Sin recipe
    (assassin row 6377: Azmodan's Heart a56, unique 27, challenge code 167166 in stat 378) must match within 10 s of the code
    appearing, be refused later, and be refused after the player left the area or died. Control: the stat still holds the
    code in every refused case, so the refusal is the window, not the stat compare."""
    nm = "Trophy timer: cube in time passes, too late / another zone / dead is refused"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    goto(1)
    if not items(hstate(), "box"):  # the character may not have found its cube yet
        H("item box inv")
    H("cubeclear")
    cls0 = hstate()["player"]["class"]
    H("class 6")  # the recipe is for assassins
    log, res = [], {}

    def attempt(label):
        H("flag player 9 clear")  # the recipe sets it and the follow-up recipe 6358 (same item) opens with it
        H("cubeclear")
        H("item a56 cube uniq=27")
        ok, lines = H("cubecraft")
        r = re.search(r"recipe (\d+)", lines[0]) if lines else None
        res[label] = (ok and r is not None and r.group(1) == "6377", lines[0][:60] if lines else "-")
        log.extend(lines)
        H("cubeclear")

    def start():
        H("stat player 378 set 0")
        time.sleep(1)
        H("stat player 378 set 167166")
        time.sleep(1.5)  # the frame hook samples the players every 5 frames

    try:
        start()
        attempt("in time")
        start()
        time.sleep(11.5)
        held_late = stat_of(H("stat player 378")[1])
        attempt("too late")
        start()
        goto(2)
        goto(1)
        held_zone = stat_of(H("stat player 378")[1])
        attempt("other zone")
        goto(2)  # a town does not let a hit kill
        atk = spawn_attacker(1)
        start()
        H("stat player 6 set 25600")
        H("hit player phys 2000000000 %s" % (atk or ""))  # from a spawned monster; 1/256 life points
        for _ in range(12):  # the death animation runs a second or two
            time.sleep(1)
            if hstate()["player"].get("mode") in (0, 17):
                break
        died = hstate()["player"].get("mode") in (0, 17)
        time.sleep(3)  # the sampler sees the dead player
        revive_if_dead()
        goto(1)
        held_dead = stat_of(H("stat player 378")[1])
        attempt("died")
    finally:
        H("stat player 378 set 0")
        H("class %d" % cls0)
        H("cubeclear")
    held = (held_late, held_zone, held_dead)
    good = (res["in time"][0] and not res["too late"][0] and not res["other zone"][0] and not res["died"][0]
            and died and all(v == 167166 for v in held))
    add(nm, "PASS" if good else "FAIL",
        "recipe 6377 matched: in time %s; 12 s later %s; after goto 2 and back %s; after dying %s (the character died: %s; stat 378 "
        "still %s in the refused cases)" % (res["in time"][0], res["too late"][0], res["other zone"][0], res["died"][0], died, held),
        (), log)


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
    snd = reward_stats()  # Akara's reward message 0x61 plays item_lightarmor (230) on the client
    if not (snd and snd[3] == 230):
        time.sleep(2)
        snd = reward_stats()
    add(name, "PASS" if (new and new[0].get("unique") == 69 and snd and snd[0] and snd[2] == 0x61 and snd[3] == 230) else "FAIL",
        "after `h msg 148 0x76`: new item(s) with base code 600: %s (unique index %s, Griswold's Heart = 69), vanilla ring "
        "reward: %s; quest 4 bits %s; client reward sound (hook, played, message, row) %s" % (len(new), new[0].get("unique") if new else "-", bool(ring), q[0], snd),
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
    ensure_cube()
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
    ensure_cube()
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
    ensure_cube()
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


def mapmem_probe():
    """`h mapmem` (client verb) -> dict of the numbers, or None."""
    ok, lines = H("mapmem")
    ln = next((x for x in lines if x.startswith("mapmem ")), "")
    d = kv(ln)
    if not ok or "layer" not in d:
        return None
    cells = [int(x) for x in d.get("cells", "0").split(",")]
    return {"layer": int(d["layer"]), "bucket": int(d["bucket"]), "cells": sum(cells), "file": d.get("file") == "read",
            "nodes": int(d.get("nodes", 0)), "foreign": int(d.get("foreign", 0)), "clean": d.get("clean") == "1",
            "loads": int(d.get("loads", 0)), "saves": int(d.get("saves", 0)), "views": int(d.get("views", 0)),
            "resets": int(d.get("resets", 0))}


def check_mapmem():
    """Per-level map memory of Levels.txt layers above 99 (docs/subsystems/areas-drlg.md section 6, bug #24). D2R's map file
    has 100 chains indexed by the layer; the plugin indexes it by layer % 100 and shows the loader only the nodes of the
    layer it loads. MXL_MAPMEM (comma list; default 134,147,184,12,139: layers 101, 110, 140, 5 and 105, the last two share
    chain 5) is visited in order (walking a little in each level to explore it), then again in reverse order. Every visit
    must reach the level in a living game; every return must find a clean chain holding saved nodes of the level's own layer,
    room sets not smaller than when the level was left, and (layer 100 or more) a view counter that says the loader was served
    a filtered file."""
    name = "Map memory of levels with Layer above 99 (bug #24: 100-chain table, per-level rooms kept)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    areas = [int(x) for x in os.environ.get("MXL_MAPMEM", "134,147,184,12,139").split(",")]
    off = g._size(g.LOADER_LOG)
    notes, shots, bad = [], [], []
    left = {}
    shared_seen = False
    plan = [(a, "first") for a in areas] + [(a, "back") for a in reversed(areas)]
    for a, phase in plan:
        guard()
        st = goto(a)
        at = st["player"].get("area")
        if not g.game_pids():
            bad.append("%d %s: the game is gone" % (a, phase))
            break
        if at != a:
            bad.append("%d %s: stayed in %s" % (a, phase, at))
            continue
        time.sleep(2)
        arrive = mapmem_probe()
        if arrive is None:
            bad.append("%d %s: no mapmem line" % (a, phase))
            continue
        p = st["player"]
        for dx, dy in ((14, 0), (0, 14), (-14, 0), (0, -14)):  # a short walk reveals the rooms around the arrival
            H("move %d %d" % (int(p.get("x", 0)) + dx, int(p.get("y", 0)) + dy))
            time.sleep(1.5)
        now = mapmem_probe() or arrive
        notes.append("%s %d: layer %d cells %d -> %d nodes %d foreign %d clean %d views %d saves %d" % (
            phase, a, now["layer"], arrive["cells"], now["cells"], now["nodes"], now["foreign"], now["clean"], now["views"], now["saves"]))
        if phase == "first":
            left[a] = max(arrive["cells"], now["cells"])
            if a == areas[0]:
                shots.append(sh("mapmem_%d" % a))
        else:
            if not arrive["clean"] or not now["clean"]:
                bad.append("%d back: the chain of bucket %d is not clean" % (a, now["bucket"]))
            if now["layer"] >= 100 and now["views"] == 0:
                bad.append("%d back: layer %d never got a view" % (a, now["layer"]))
            if not arrive["file"] or arrive["nodes"] < 1:
                bad.append("%d back: no saved node of layer %d in the map file (file read %s, nodes %d)" % (a, now["layer"], arrive["file"], arrive["nodes"]))
            if arrive["cells"] < left[a]:
                bad.append("%d back: %d rooms on arrival, %d when it was left" % (a, arrive["cells"], left[a]))
        if now["foreign"] > 0:
            shared_seen = True
    txt = g._read_from(g.LOADER_LOG, off)
    reads = txt.count("Reading From")  # a few lines per level change; thousands of them is the loop of bug #24
    alive = bool(g.game_pids())
    if reads > 2000:
        bad.append("%d 'Reading From' lines in the loader log (a self-loop in the chain?)" % reads)
    nodes_back = [n for n in notes if n.startswith("back")]
    add(name, "PASS" if alive and not bad and len(nodes_back) == len(areas) else "FAIL",
        "%s%s; %s" % ("; ".join(bad) + "; " if bad else "", "chain of two layers shared %s" % ("seen" if shared_seen else "not seen"), " | ".join(notes)), shots)


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


def _andariel_ai_users():
    """(class id, name) of every monstats row whose AI column is andariel_ai in the built mod (the 47 users of AI 155)."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "mod", "data", "global", "excel", "monstats.txt")
    out = []
    with open(path, encoding="utf-8-sig") as f:
        head = f.readline().rstrip("\r\n").split("\t")
        ai = head.index("AI")
        for cls, line in enumerate(f):
            cols = line.rstrip("\r\n").split("\t")
            if len(cols) > ai and cols[ai] == "andariel_ai":
                out.append((cls, cols[0]))
    return out


AI_FIELDS = ("thinks", "wait", "attack", "enemy", "charge", "walk", "s1", "s2", "s3", "s4", "fallback", "controlled")


def ai_counters(text):
    """The `monster ai` line of `mxl`: counters of the native andariel_ai think (monster_ai.cpp MonsterAiStatus), or None."""
    for ln in text.splitlines():
        m = re.search(r"(\d+) thinks \(wait (\d+), attack (\d+) \+ (\d+) on the enemy, charge (\d+), walk (\d+), skills 1-4: (\d+) (\d+) (\d+) (\d+)\)"
                      r", fallback thinks (\d+), controlled thinks (\d+)", ln)
        if m:
            return dict(zip(AI_FIELDS, (int(x) for x in m.groups())))
    return None


def ai_delta(a, b):
    return {k: b[k] - a[k] for k in AI_FIELDS}


def clear_mons(rounds=3):
    """Kill every live monster `h state` lists (it lists at most 60 within 40 units). Ids that survive one kill are unkillable
    (town NPCs, Malthael's dark nephalems) and are not tried again."""
    skip = set()
    for _ in range(rounds):
        st = hstate()
        ids = [m.get("id") for m in st["mons"] if m.get("id") is not None and m.get("id") not in skip and not m.get("npc")]
        if not st["ok"] or not ids:
            return
        for i in ids:
            H("kill %d" % i)
        skip.update(ids)
        time.sleep(0.2)


def check_median_ai():
    """Native andariel_ai (plugin monster_ai.cpp, AI id 155) on every monstats row that uses it (47 classes, read from the built
    mod): each is spawned next to the god-mode player outside town and watched for ~3 s. PASS: the think ran (`mxl` counter
    `thinks` rose), the monster changed mode or distance or the counters show a wait / walk / attack / skill, and the game
    is still running with the monster listed. MXL_AI_CHUNK=a:b limits the run to users a..b-1 of the list."""
    name = "Median andariel_ai: all users act (think runs, mode or distance changes, no crash)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    users = _andariel_ai_users()
    lo, _, hi = os.environ.get("MXL_AI_CHUNK", "").partition(":")
    if lo or hi:
        users = users[int(lo or 0):int(hi or len(users))]
    god(True)
    goto(2)
    close_menu()
    kill_all_near()
    notes, shots, bad = [], [], []
    prev = ai_counters(mxl_text())
    if prev is None:
        god(False)
        return add(name, "FAIL", "the `mxl` monster ai line has no counters (medianai off, or the line changed)")
    skipped = []
    for cls, nm in users:
        clear_mons()
        ok, lines = H("spawn %d 1" % cls)
        mid = re.search(r"spawned class \d+ id=(\d+)", " ".join(lines))
        if not mid:
            skipped.append(nm)  # the harness cannot place it (e.g. a clone that needs its own room), not a failure of the AI
            notes.append("%d %s: NOT SPAWNABLE (%s)" % (cls, nm, " ".join(lines)[:60]))
            continue
        mid = int(mid.group(1))
        prev = ai_counters(mxl_text()) or prev  # counters from the monster alone (leftover summons are killed above)
        modes, dists, seen = set(), set(), 0
        for _ in range(6):
            time.sleep(0.5)
            st = hstate()
            if not st["ok"]:
                break
            for m in st["mons"]:
                if m.get("id") == mid:
                    seen += 1
                    modes.add(m.get("mode"))
                    dists.add(m.get("dist", m.get("distance")))
        cur = ai_counters(mxl_text())
        alive = hstate()["ok"] or (time.sleep(2) or hstate()["ok"])  # one retry: a `state` line can be late
        d = ai_delta(prev, cur) if cur else None
        prev = cur or prev
        acts = (d["wait"] + d["attack"] + d["enemy"] + d["charge"] + d["walk"] + d["s1"] + d["s2"] + d["s3"] + d["s4"]) if d else 0
        moved = len(modes) > 1 or len(dists) > 1
        # seen == 0 (it died within the first half second, or the listing is full of its summons) is judged on the counters alone
        good = alive and d is not None and d["thinks"] > 0 and (moved or acts > 0)
        if not good:
            bad.append(nm)
        notes.append("%d %s: %s, thinks %s (wait %s walk %s attack %s+%s charge %s skills %s), modes %s, %d distances, listed %d times%s" % (
            cls, nm, "PASS" if good else "FAIL", d and d["thinks"], d and d["wait"], d and d["walk"], d and d["attack"], d and d["enemy"],
            d and d["charge"], d and [d["s1"], d["s2"], d["s3"], d["s4"]], sorted(x for x in modes if x is not None), len(dists), seen,
            "" if alive else ", GAME GONE"))
        if cls in (156, 1244, 1729):
            shots.append(sh("medianai_%d" % cls))
        H("kill %d" % mid)  # by id: the listing can be full of unkillable summons (Malthael's dark nephalems, class 1239)
        if not alive:
            break
    clear_mons()
    god(False)
    add(name, "PASS" if not bad and users else "FAIL", "%d of %d users acted%s%s | %s" % (
        len(users) - len(bad) - len(skipped), len(users), (" (FAILED: %s)" % ", ".join(bad)) if bad else "",
        (" (not spawnable by the harness: %s)" % ", ".join(skipped)) if skipped else "", " | ".join(notes)), shots)


def check_median_ai_controlled():
    """The controlled branch of andariel_ai: Median's gate is the monster's "last mode" field (monster data +0x68 in D2R, set by
    the unit-mode setter 0x4475xx to the mode being left, unless that is 1): 3 or 0x13 once a hit recovery ended. A spawned
    Maghda (1693) is hit through the game's damage path with `hit ... stagger` (GetHit forced); the `mxl` counter
    `controlled thinks` must stay 0 before the first hit, rise after it, and the game must keep running."""
    name = "Median andariel_ai: controlled branch runs after a hit recovery (last mode GetHit)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    cls = 1693
    god(True)
    goto(2)
    close_menu()
    kill_all_near()
    c0 = ai_counters(mxl_text())
    if c0 is None:
        god(False)
        return add(name, "FAIL", "the `mxl` monster ai line has no counters")
    mid = spawn_id(cls)
    if mid is None:
        god(False)
        return add(name, "BLOCKED", "could not spawn Maghda")
    time.sleep(1.0)
    c1 = ai_counters(mxl_text())
    log, seq, modes = [], [], []
    for i in range(6):
        ok, lines = H("hit mon:%d phys 1280 stagger" % mid)  # 5 life points; GetHit forced
        log += lines[:1]
        time.sleep(0.15)
        modes += [m.get("mode") for m in hstate()["mons"] if m.get("class") == cls]
        time.sleep(1.0)
        seq.append(ai_counters(mxl_text())["controlled"] - c1["controlled"])
        if seq[-1] > 0 and i >= 2:
            break
    time.sleep(1.5)
    c2 = ai_counters(mxl_text())
    alive = hstate()["ok"]
    before = c1["controlled"] - c0["controlled"]
    after = c2["controlled"] - c1["controlled"]
    god(False)
    shot = sh("medianai_controlled")
    kill_all_near()
    good = alive and before == 0 and after > 0 and c2["thinks"] > c1["thinks"]
    add(name, "PASS" if good else "FAIL",
        "controlled thinks before the first hit +%d (must be 0), after the hits +%d (must be > 0); thinks %d -> %d; controlled thinks after each staggering hit %s; monster modes right after the hits %s; game alive %s" % (
            before, after, c1["thinks"], c2["thinks"], seq, modes, alive), [shot], log[:8])


def check_tyrael():
    """Tyrael's travel line (Act 4, class 367) is gated by Median's quest 28 bit 0 instead of D2R's quest 26
    (npc_services.cpp FixTravelGates). The `mxl` npc actions line must say `travel gates moved 1`; the NPC menu (`menu list`)
    is read with quest 26 done / quest 28 clear, both clear, and quest 28 set: the travel line may only be there with
    quest 28. The quest bits are put back afterwards."""
    name = "Tyrael travel line follows quest 28 (Median), not quest 26"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    moved = counter(mxl_text(), "npc actions", r"travel gates moved (\d+)")
    god(True)
    goto(103)
    close_menu()
    saved = {}
    for q in (26, 28):
        _, l = H("quest %d 0 get" % q)
        saved[q] = bool(l) and l[0].rstrip().endswith("= 1")
    seen = {}
    for q, v in ((26, 0), (28, 0)):  # the first talk plays Tyrael's long speech and can miss the menu: talk once for nothing
        H("quest %d 0 %s" % (q, "set" if v else "clear"))
    npc_menu(367)
    close_menu()
    time.sleep(2)
    for q26, q28 in ((1, 0), (0, 0), (0, 1), (1, 1)):
        for q, v in ((26, q26), (28, q28)):
            H("quest %d 0 %s" % (q, "set" if v else "clear"))
        menu = npc_menu(367)
        seen[(q26, q28)] = menu
        close_menu()
        time.sleep(1)
    for q, v in saved.items():
        H("quest %d 0 %s" % (q, "set" if v else "clear"))
    god(False)
    if any(v is None for v in seen.values()):
        return add(name, "BLOCKED", "Tyrael's menu did not open in every state: %s" % {k: (v is not None) for k, v in seen.items()},
                   [sh("tyrael")], [("%s: %s" % (k, v)) for k, v in seen.items() if v is not None][:6])
    # the travel line is the one whose handler is 0x1162f0 (string 0x58d2); the other lines (talk, mercenary resurrect) have
    # their own conditions and are not looked at
    has = {k: any("handler=image+0x1162f0" in ln for ln in v) for k, v in seen.items()}
    gated = all(has[(q26, q28)] == bool(q28) for q26, q28 in has)
    log = []
    for k, v in seen.items():
        log += ["quest 26 %d, quest 28 %d:" % k] + v
    add(name, "PASS" if gated and moved == 1 else "FAIL",
        "`mxl` travel gates moved %s; travel line shown by (quest 26, quest 28): %s; it must appear with quest 28 only (bits restored to %s)" % (
            moved, ", ".join("%s -> %s" % (k, v) for k, v in has.items()), saved), [sh("tyrael")], log)


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
    """One `hit`: the target at full life, returns (lost raw, absorbed raw, result flags, the element's field after the step) or None."""
    setstat(ref, 7, BIG_LIFE)
    setstat(ref, 6, BIG_LIFE)
    for _ in range(8):  # result 0: the engine ignored the hit (just arrived in the area, hit recovery); try again
        ok, line, _l = hv("hit %s %s %d%s" % (ref, elem, amount, (" " + attacker) if attacker else ""), "hit")
        m = re.search(r"life=(-?\d+)->(-?\d+) lost=\S+ absorbed=(-?\d+) result=0x(\w+)(?: field=(-?\d+))?", line)
        if not ok or not m:
            return None
        if int(m.group(4), 16) != 0:
            break
        time.sleep(1)
    return int(m.group(1)) - int(m.group(2)), int(m.group(3)), int(m.group(4), 16), int(m.group(5)) if m.group(5) else None


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


def _field_cut(field, pct):
    """Median's percent step on a damage field (D2Sigma 0x10057f10): no floor, the cut truncates."""
    return field - field * pct // 100 if field > 0 and pct else field


def _field_reduce(field, divisor, pct, tpct, duration=False):
    """What Median leaves of a field once D2R's step is done: 189 (divide, or halve a duration to 5 at least), 184, 417."""
    if field <= 0:
        return field
    if divisor:
        field = max(field >> 1, 5) if duration else field // divisor
    if duration:
        return field
    return _field_cut(_field_cut(field, pct), tpct)


def check_damagered():
    name = "Damage reduction stats 184 / 189 / 417 (D2R 369 / 373 / 446) on the incoming damage itself (D2Sigma 0x10057c10)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    fight_ready()
    elems = [("fire", 39, 40, 1), ("ltng", 41, 42, 2), ("cold", 43, 44, 4), ("magic", 37, 38, 3)]
    absorb = [142, 143, 144, 145, 146, 147, 148, 149]
    stats = [s for e in elems for s in e[1:3]] + absorb + [369, 373]
    saved = {s: rd("player", s) for s in stats}
    saved_t = {l: rd("player", 446, l) for l in range(0, 6)}
    for e in elems:  # resist 200 with no max-resist: the cap leaves 25% of every hit, whatever the difficulty's penalty
        setstat("player", e[1], 200)
        setstat("player", e[2], 0)
    for s in absorb:  # no absorb unless a probe asks for it
        setstat("player", s, 0)
    setstat("player", 369, 0)  # `stat set` writes the base value; what is left is what other sources give
    setstat("player", 373, 0)
    for l in range(0, 6):
        setstat("player", 446, 0, l)
    p0 = rd("player", 369) or 0
    amount, pre, rows, bad = 100000, 25000, [], []

    def note(label, got, want, ok):
        rows.append("%s: %s (want %s)" % (label, got, want))
        if not ok:
            bad.append(rows[-1])

    def probe(label, elem, want, amt=amount):
        r = hit("player", elem, amt)
        note(label, "field %s lost %s" % (r and r[3], r and r[0]), want, r is not None and r[3] == want and abs(r[0] - want) <= 2)

    probe("baseline, 184 from other sources %d%%" % p0, "fire", _field_reduce(pre, 0, p0, 0))
    setstat("player", 373, 4)
    probe("189 divisor 4", "fire", _field_reduce(pre, 4, p0, 0))
    setstat("player", 373, 0)
    setstat("player", 369, 50)
    p = rd("player", 369)
    probe("184 -> %d%%" % p, "fire", _field_reduce(pre, 0, p, 0))
    setstat("player", 369, 0)
    setstat("player", 446, 50, 1)
    probe("417 layer 1 (fire) 50%", "fire", _field_reduce(pre, 0, p0, 50))
    probe("417 layer 1 on cold (unchanged)", "cold", _field_reduce(pre, 0, p0, 0))
    setstat("player", 446, 0, 1)
    for elem, _res, _mx, layer in elems:
        setstat("player", 446, 40, layer)
        probe("417 layer %d (%s) 40%%" % (layer, elem), elem, _field_reduce(pre, 0, p0, 40))
        setstat("player", 446, 0, layer)
    # all three on odd numbers: the order (divide, 184, 417) and the truncation of every step show in the exact field
    setstat("player", 373, 3)
    setstat("player", 369, 33)
    p = rd("player", 369)
    setstat("player", 446, 17, 1)
    probe("all three on 25001 (189 3, 184 %d%%, 417 17%%)" % p, "fire", _field_reduce(25001, 3, p, 17), amt=100005)
    setstat("player", 373, 0)
    setstat("player", 369, 0)
    setstat("player", 446, 0, 1)
    # the absorb credit is computed before these stats: 30% of the 25000 left by the resist is 7500, healed through 189 (and 184)
    setstat("player", 142, 30)
    setstat("player", 373, 4)
    r = hit("player", "fire", amount)
    want_heal, want_field = _credit(7500, 4, p0, 0), _field_reduce(17500, 4, p0, 0)
    note("fire absorb 30% + 189 divisor 4", "healed %s field %s" % (r and r[1], r and r[3]), "healed %s field %s" % (want_heal, want_field),
         r is not None and abs(r[1] - want_heal) <= 2 and r[3] == want_field)
    setstat("player", 142, 0)
    setstat("player", 373, 0)
    # durations: 189 halves a freeze / cold length (at least 5 frames); 184 / 417 leave it alone
    for elem in ("flen", "clen"):
        r0 = hit("player", elem, 400)
        base = r0[3] if r0 else None
        if base is None:
            bad.append("%s: no hit" % elem)
            continue
        for label, divisor, pct, tpct in (("189 divisor 4", 4, 0, 0), ("189 divisor 1000", 1000, 0, 0), ("184 50%, 417 50%", 0, 50, 50)):
            setstat("player", 373, divisor)
            setstat("player", 369, pct)
            setstat("player", 446, tpct, 0)
            r = hit("player", elem, 400)
            want = _field_reduce(base, divisor, 0, 0, duration=True)
            note("%s %s (base %s)" % (elem, label, base), r and r[3], want, r is not None and r[3] == want)
            setstat("player", 373, 0)
            setstat("player", 369, 0)
            setstat("player", 446, 0, 0)
    setstat("player", 373, 2)
    r = hit("player", "flen", 16)
    note("flen 16 with 189 (the floor of 5 frames)", r and r[3], 5, r is not None and r[3] == 5)
    setstat("player", 373, 0)
    # physical damage takes stat 417 with layer 0 (the layer Median passes for it)
    r0 = hit("player", "phys", amount)
    setstat("player", 446, 50, 0)
    r1 = hit("player", "phys", amount)
    want = _field_cut(r0[3], 50) if r0 and r0[3] is not None else None
    note("phys 417 layer 0 50%% of %s" % (r0 and r0[3]), r1 and r1[3], want, r1 is not None and want is not None and r1[3] == want)
    for s, v in saved.items():
        setstat("player", s, 0 if (v is None or s in (369, 373)) else v)
    for l, v in saved_t.items():
        setstat("player", 446, v or 0, l)
    add(name, "PASS" if not bad else "FAIL",
        "hits on the player at resist 200 (25%% of 100000 left) and freeze / cold lengths; the damage field after D2R's step must equal Median's "
        "formula exactly (divide, then 184, then 417, no floor): %s" % ("; ".join(rows) if not bad else "; ".join(bad)),
        [sh("damagered")], rows)


def _avoid_rate(ref, kind, stat, value, max_avoid, n=2000):
    setstat(ref, stat, value)
    setstat(ref, 380, max_avoid)
    for _ in range(6):
        ok, line, _l = hv("avoid %s %s %d" % (ref, kind, n), "avoid", 60)
        m = re.search(r"n=(\d+) .* success=(\d+)", line)
        if ok and not m:
            time.sleep(0.5)  # the result line was lost (console output race): ask again
            continue
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
    ensure_cube()
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
    ensure_cube()
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
    ensure_cube()
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
    # hst (weapons.txt quest=10, the Horadric Staff) and hfh (quest=25, the Hell Forge Hammer) are quest items: D2R's place
    # function refuses them (`equip`: "does not fit body location 4") once the character's quest state says the quest is
    # done: quest 10 bit 0 for hst, quest 27 bits 0 / 1 for hfh (measured on a character whose quests the earlier checks had
    # completed; not a data or namespace matter: the same build equips both on a fresh character). Clear the bits for the
    # check, put them back afterwards.
    qsaved = {q: _qbits(q) for q in (10, 27)}
    for q in qsaved:
        for i in range(16):
            H("quest %d %d clear" % (q, i))
    try:
        # no weapon: both damage bonus rows are 0 (Median's handler returns 0 without a weapon)
        H("packclear")  # room in the backpack: an unequip into a full backpack fails and the hand stays occupied
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
                # the totals the panel reads include the weapon's own bonus (the quest uniques carry properties that scale
                # with the character level: stat 2 reads 40 after `set 30` with hst on a level 20 character, `cstat` 30)
                tot_s, tot_d = rd("player", 0), rd("player", 2)
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
        for q, bits in qsaved.items():
            _qrestore(q, bits)
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


def check_npcstrings():
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
        ok, lines = H("strids " + " ".join(str(i) for i in batch))
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

def check_nocorpse():
    """Median's death rules (docs/subsystems/death.md; audit rows DC1-DC4): a dying player leaves no corpse, keeps the equipment
    (ring, amulet, belt), and loses neither gold nor experience, and the respawn request is held back for 5 s (125 frames).
    The character gets three equipped items, gold and a big experience value, is killed by a monster's hit, asks to respawn
    at once (Esc: must be refused, still dead), and again after 125 frames (must work, in town); before and after the respawn
    there must be no second player-type unit (D2's corpse), the same items on the same body locations, the same carried
    gold and bank, the same experience, and no gold pile on the ground.
    The plugin counts the calls it swallowed (`h state`: skipped corpse calls / penalty calls): they must have risen, or the
    death never reached the two functions and nothing was tested."""
    nm = "No corpse on death (Median): no corpse unit, gear and belt kept, gold and xp unchanged, respawn after 5 s in town"

def _excel(name):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "mod", "data", "global", "excel", "base", name + ".txt")
    with open(path, encoding="utf-8-sig") as f:
        rows = [l.rstrip(chr(10)).split(chr(9)) for l in f]
    return rows[0], rows[1:]


CLASS_NAMES = ["Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin"]


def check_levelcap():
    name = "Level cap 150 (experience.txt MaxLvl): XP near levels 99 / 120 / 149, stat and skill points, save and reload"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    cls = hstate()["player"].get("class")
    if not isinstance(cls, int) or cls >= len(CLASS_NAMES):
        return add(name, "BLOCKED", "player class %r" % (cls,))
    col = CLASS_NAMES[cls]
    xh, xrows = _excel("experience")
    maxlvl = int(next(r for r in xrows if r[0] == "MaxLvl")[xh.index(col)])
    lv = {int(r[0]): int(r[xh.index(col)] or 0) for r in xrows if r[0].isdigit()}  # row L = experience at which level L + 1 starts

    def start(level):  # experience at which `level` starts
        return lv[level - 1]

    ch, crows = _excel("charstats")
    crow = next(r for r in crows if r[0] == col)
    per_stat, per_skill = int(crow[ch.index("StatPerLevel")] or 0), int(crow[ch.index("SkillsPerLevel")] or 0)
    keep = {s: rd("player", s) for s in (4, 5, 6, 7, 8, 9, 10, 11, 12, 13)}
    log, bad = [], []

    def now():
        return {"level": rd("player", 12), "xp": rd("player", 13) & 0xffffffff, "stat": rd("player", 4), "skill": rd("player", 5)}

    def climb(label, from_level, to_level, expect_level, expect_gain):
        """Put the character at the start of `from_level` and award the experience that ends at the start of `to_level` (D2R's own
        award function runs the level-ups): the level and the points granted must match the table."""
        setstat("player", 12, from_level)
        setstat("player", 13, start(from_level))
        a = now()
        H("xp %d" % max(start(to_level) - start(from_level), 0))
        b = now()
        gained = b["level"] - a["level"]
        ok = b["level"] == expect_level and gained == expect_gain and b["stat"] - a["stat"] == gained * per_stat and b["skill"] - a["skill"] == gained * per_skill
        log.append("%s: level %d -> %d (want %d), stat points %+d (want %+d), skill points %+d (want %+d), experience %d" % (
            label, a["level"], b["level"], expect_level, b["stat"] - a["stat"], expect_gain * per_stat, b["skill"] - a["skill"],
            expect_gain * per_skill, b["xp"]))
        if not ok:
            bad.append(log[-1])
        return b

    if maxlvl != 150:
        bad.append("built experience.txt MaxLvl is %d for %s" % (maxlvl, col))
    climb("level 98 -> 99", 98, 99, 99, 1)
    climb("level 99 -> 100 (the old cap)", 99, 100, 100, 1)
    climb("level 119 -> 120", 119, 120, 120, 1)
    climb("level 148 -> 149", 148, 149, 149, 1)
    climb("level 149 -> 150", 149, 150, 150, 1)
    b = now()
    H("xp 100000000")  # past the last level: the level and the points stay
    c = now()
    log.append("150 + 100000000 experience: level %d, stat points %+d, skill points %+d, experience %d" % (
        c["level"], c["stat"] - b["stat"], c["skill"] - b["skill"], c["xp"]))
    if c["level"] != 150 or c["stat"] != b["stat"] or c["skill"] != b["skill"]:
        bad.append(log[-1])
    top = climb("one award from level 99 to the end of the table", 99, 150, 150, 51)
    time.sleep(1)
    _, c12 = H("cstat 12")
    log.append("client copy of the level (cstat 12): %s" % (c12[0] if c12 else None))
    if not c12 or not c12[0].rstrip().endswith("= 150"):
        bad.append("client level: %s" % (c12[0] if c12 else None))
    shots = []
    g.key("c")
    time.sleep(1.5)
    shots.append(sh("levelcap_charsheet"))
    g.key("c")
    time.sleep(0.5)
    # save and reload: Save and Exit writes the character, Play loads it again
    ok = leave_and_reenter()
    if not ok:
        bad.append("could not leave and re-enter the game")
    else:
        time.sleep(3)
        d = now()
        saved = None
        sp = os.path.join(SAVES, g.CHAR_NAME + ".d2s")
        if os.path.exists(sp):
            with open(sp, "rb") as f:
                saved = f.read(0x1c)[0x1b]
        log.append("after Save and Exit and Play: level %d (saved header byte %s), experience %d (was %d), stat points %d (was %d), "
                   "skill points %d (was %d)" % (d["level"], saved, d["xp"], top["xp"], d["stat"], top["stat"], d["skill"], top["skill"]))
        if d["level"] != 150 or d["xp"] != top["xp"] or d["stat"] != top["stat"] or d["skill"] != top["skill"] or saved != 150:
            bad.append(log[-1])
    # the test character goes back to the numbers it had
    for s_, v in keep.items():
        setstat("player", s_, v or 0)
    add(name, "PASS" if not bad else "FAIL",
        "%s (class %s, %d stat / %d skill points per level, MaxLvl %d): %s" % (
            "all steps hold" if not bad else "; ".join(bad), col, per_stat, per_skill, maxlvl, "; ".join(log)), shots, log)

# ------------------------------------------------------------------ the old "fix to test" rows of docs/bugs.md (4, 6, 10, 12, 16, 19)
# Background mode, harness verbs only. Expected values come from sources other than the migration being tested: Median's
# own cubemain.bin / skills2 caps / named tables (names, not ids) and the built levels.txt.

import json  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILT_EXCEL = os.path.join(REPO, "work", "mod", "data", "global", "excel")
TOWNS = [1, 40, 75, 103, 109]  # the town of act 1-5


def _tsv(path):
    import csv
    with open(path, encoding="utf-8-sig", errors="replace", newline="") as f:
        return list(csv.reader(f, delimiter="\t"))


def _main_checkout(path):
    """A worktree keeps no extract/: fall back to the main checkout's copy."""
    if os.path.exists(path):
        return path
    alt = os.path.join(r"%USERPROFILE%\mxl-d2r", os.path.relpath(path, REPO))
    return alt if os.path.exists(alt) else path


def _level_acts():
    t = _tsv(os.path.join(BUILT_EXCEL, "levels.txt"))
    h = t[0]
    return {int(r[h.index("Id")]): int(r[h.index("Act")] or 0) for r in t[1:] if len(r) > 4 and r[h.index("Id")].isdigit()}


def go(area):
    """Move to an area: D2R's `cheat goto` inside an act, the plugin's `level` (NPC travel) across acts."""
    acts = _level_acts()
    now = hstate()["player"].get("area")
    if now is not None and acts.get(now) != acts.get(area):
        revive_if_dead()
        H("level %d" % area)
        t0 = time.time()
        while time.time() - t0 < 40:
            time.sleep(2)
            st = hstate()
            if st["player"].get("area") == area:
                time.sleep(2)
                return st
    return goto(area)


def new_portals(before):
    st = hstate()
    return [o for o in st["objs"] if o["id"] not in before and o.get("class") in (59, 60, 600)]


def check_cowportals():
    """Bug #19: the five 'Cow Portal' output recipes (rows 2, 3, 85, 4962, 7453) open the level Median's cubemain names, and
    the 256 cleaned empty output cells are empty in D2R's records."""
    n1 = "Cube portal outputs (bug #19): each Cow Portal recipe opens its own level"
    n2 = "Cube outputs (bug #19): no stray quotes, D2R's empty slots match the txt"
    if not need_game():
        add(n1, "BLOCKED", "not in a game")
        return add(n2, "BLOCKED", "not in a game")
    # expected: Median's special byte 1 slots (cubemain.bin: 4-byte count, 0x148 records, slots at +0x4c step 0x54, byte +8,
    # word +6 = Median level number), level numbers -> D2R ids by idmap.json
    data = open(_main_checkout(os.path.join(REPO, "extract", "mxl", "data", "global", "excel", "cubemain.bin")), "rb").read()
    count = struct.unpack_from("<I", data, 0)[0]
    idmap = json.load(open(_main_checkout(os.path.join(REPO, "work", "named", "idmap.json")), encoding="utf-8"))
    level_id = {e["row"]: e["id"] for e in idmap["levels"]}
    wants = []
    for row in range(count):
        for slot in range(3):
            s = 4 + row * 0x148 + 0x4c + 0x54 * slot
            if data[s + 8] == 1:
                median = struct.unpack_from("<H", data, s + 6)[0]
                wants.append((row, slot, median, level_id.get(median)))
    acts = _level_acts()
    rows, bad = [], []
    log = []
    H("difficulty 2")
    H("quest 40 0 set")  # the Cow Level's own handler wants Baal killed in the game's difficulty
    try:
        for row, slot, median, want in wants:
            town = TOWNS[acts.get(want, 0)] if want is not None else 1
            go(town)
            before = {o["id"] for o in hstate()["objs"]}
            ok, lines = H("cubeout %d %d" % (row, slot))
            time.sleep(1.5)
            new = new_portals(before)
            dests = [o.get("dest") for o in new]
            log.append("row %d slot %d: Median level %d -> D2R %s, town %d: ok=%s new portal dest %s" % (row, slot, median, want, town, ok, dests))
            rows.append("row %d -> %s" % (row, dests))
            if not ok or want not in dests:
                bad.append("row %d wants level %s, got %s" % (row, want, dests))
        sh("cowportals")
    finally:
        H("difficulty 0")
    add(n1, "PASS" if wants and not bad else "FAIL",
        "%d portal outputs from cubemain.bin (Median level numbers -> idmap.json -> D2R ids): %s%s" % (
            len(wants), "; ".join(rows), ("; WRONG: " + "; ".join(bad)) if bad else ""), [sh("cowportals")], log)

    # --- the empty output cells
    txt = _tsv(os.path.join(BUILT_EXCEL, "cubemain.txt"))
    h = txt[0]
    cols = [h.index("output"), h.index("output b"), h.index("output c")]
    quotes = sum(1 for r in txt[1:] for c in cols if c < len(r) and '"' in r[c])
    want_mask = []
    for r in txt[1:]:
        m = 0
        for i, c in enumerate(cols):
            if c < len(r) and r[c].strip():
                m |= 1 << i
        want_mask.append(m)
    got = ""
    for first in range(0, len(want_mask), 2500):
        ok, lines = H("cubeslots %d %d" % (first, min(2500, len(want_mask) - first)), 60)
        got += "".join(m.group(2) for m in (re.match(r"cubeslots (\d+) (\d+)$", l.strip()) for l in lines) if m)
    diffs = [(i, want_mask[i], int(got[i])) for i in range(min(len(got), len(want_mask))) if want_mask[i] != int(got[i])]
    add(n2, "PASS" if quotes == 0 and len(got) == len(want_mask) and not diffs else "FAIL",
        "%d recipes: %d output cells with a quote in the built cubemain.txt; D2R's records (slot +0x5a/0x5b/0x5b) hold %d of %d "
        "slots the txt names and %d empty ones; differences (row, txt mask, D2R mask): %s" % (
            len(want_mask), quotes, sum(bin(m).count("1") for m in want_mask), len(want_mask) * 3,
            len(want_mask) * 3 - sum(bin(m).count("1") for m in want_mask), diffs[:12]), [],
        ["recipe %d: txt %d D2R %d" % d for d in diffs[:40]])


def _caps_sources():
    """Median row -> (mlvl, skcn) from work/formulas/skill_caps.txt, Median row -> D2R id (idmap.json), D2R id -> name."""
    caps = {}
    for line in open(_main_checkout(os.path.join(REPO, "work", "formulas", "skill_caps.txt")), encoding="utf-8").read().splitlines()[1:]:
        f = (line.split("\t") + ["", ""])[:3]
        if f[0].isdigit():
            caps[int(f[0])] = (int(f[1] or 0), f[2])
    idmap = json.load(open(_main_checkout(os.path.join(REPO, "work", "named", "idmap.json")), encoding="utf-8"))
    ids = {e["row"]: e["id"] for e in idmap["skills"]}
    names = {e["id"]: e["name"] for e in idmap["skills"]}
    return caps, ids, names


def _setskill(skill, level):
    """`setskill`: the base level the game's own point function stopped at."""
    for _ in range(2):
        ok, lines = H("setskill %d %d" % (skill, level))
        m = re.search(r"base level (-?\d+)", " ".join(lines))
        if m:
            return int(m.group(1))
        time.sleep(1)
    return None


def check_skillcaps():
    """Bug #4: the skill-point caps. `setskill` raises a skill with the game's own point function until the skpoints formula
    answers 999, so the level it stops at is the cap."""
    nm = "Skill point caps (bug #4): per-skill cap, +N to a skill (skcp), Specialization"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    caps, ids, names = _caps_sources()
    stat_id = {e["row"]: e["id"] for e in json.load(open(_main_checkout(os.path.join(REPO, "work", "named", "idmap.json")), encoding="utf-8"))["itemstatcost"]}
    skcp, pst4 = stat_id[449], stat_id[397]  # Median's stat rows -> D2R ids (the harness takes D2R's)
    # sample: a plain (no skcn lock formula) skill of each cap value, spread over classes by taking every 7th
    t = _tsv(os.path.join(BUILT_EXCEL, "skills.txt"))
    reqlevel = {int(r[t[0].index("*Id")]): int(r[t[0].index("reqlevel")] or 0) for r in t[1:] if r and r[t[0].index("*Id")].isdigit()}
    by_cap = {}
    for row, (mlvl, skcn) in sorted(caps.items()):
        if mlvl > 0 and not skcn and row in ids and row > 6 and reqlevel.get(ids[row], 999) <= 80:
            by_cap.setdefault(mlvl, []).append(row)
    sample = []
    for mlvl, rows in sorted(by_cap.items()):
        sample += [(rows[i], mlvl) for i in range(0, len(rows), max(1, len(rows) // 2))][:2]
    sample.sort(key=lambda x: x[0] % 7)  # not all low caps first
    lvl0 = hstate()["player"].get("level")
    H("stat player 12 set 99")
    log, bad, done = [], [], []
    try:
        for row, mlvl in sample[:16]:
            sid = ids[row]
            got = _setskill(sid, 99)
            log.append("skill %s (Median row %d, id %d): cap by skill_caps.txt %d, game stopped at %s" % (names.get(sid), row, sid, mlvl, got))
            done.append(sid)
            if got != mlvl:
                bad.append("%s %s != %d" % (names.get(sid), got, mlvl))
            # +3 to the skill (skcp: stat 449 with the skill id as its parameter) raises the cap by 3
            H("stat player %d %d set 3" % (skcp, sid))
            got3 = _setskill(sid, 99)
            H("stat player %d %d set 0" % (skcp, sid))
            log.append("  with +3 to the skill: stopped at %s (want %d)" % (got3, mlvl + 3))
            if got3 != mlvl + 3:
                bad.append("%s +3: %s != %d" % (names.get(sid), got3, mlvl + 3))
            _setskill(sid, 0)
        # Specialization: earthquake_2 (Median row of the barbarian's tab) has its cap raised by the tab's Specialization
        # Specialization's first point needs skill('skills_1835'.pst4) > 0: pst4 is that skill's passivecalc4 = syn4 = the
        # player's stat 426 with the skill as its parameter (D2R id 1907 for the Median row 1835); the cap rises by the
        # Specialization's calc1 = blvl / 2
        by_name = _skill_ids_by_name()[0]
        spec, quake, gate = by_name.get("specialization_3"), by_name.get("earthquake_2"), by_name.get("skills_1835")
        spec_ok = False
        if spec and quake and gate:
            base = _setskill(quake, 99)
            _setskill(quake, 0)
            _setskill(gate, 1)
            H("stat player %d %d set 1" % (pst4, gate))
            lvl = _setskill(spec, 10)
            raised = _setskill(quake, 99)
            want = (base or 0) + (lvl or 0) // 2
            spec_ok = bool(lvl) and raised == want
            log.append("earthquake_2 (id %d): cap %s; Specialization (specialization_3, id %d) at level %s -> cap %s (want %s)" % (
                quake, base, spec, lvl, raised, want))
            if not spec_ok:
                bad.append("Specialization %s: earthquake_2 cap %s -> %s, want %s" % (lvl, base, raised, want))
            _setskill(quake, 0)
            _setskill(spec, 0)
            _setskill(gate, 0)
            H("stat player %d %d set 0" % (pst4, gate))
        else:
            bad.append("no specialization_3 / earthquake_2 / skills_1835 row (%s %s %s)" % (spec, quake, gate))
    finally:
        H("stat player 12 set %s" % (lvl0 if lvl0 else 1))
    add(nm, "PASS" if sample and not bad else "FAIL",
        "%d skills raised to their cap with the game's point function: cap = Median's mlvl (work/formulas/skill_caps.txt, "
        "skills with no lock formula); +3 to a skill (Median stat 449 = D2R 478) raised each cap by 3; Specialization (barbarian) raised "
        "earthquake_2's cap by half its level%s" % (len(done), ("; WRONG: " + "; ".join(bad[:8])) if bad else ""), [], log)


def _skill_ids_by_name():
    t = _tsv(os.path.join(BUILT_EXCEL, "skills.txt"))
    h = t[0]
    return {r[0]: int(r[h.index("*Id")]) for r in t[1:] if r and r[0] and r[h.index("*Id")].isdigit()}, t, h


def check_townportal():
    """Bug #6: the itemless Town Portal skill (D2R's srvdofunc 109): casts in the field, opens a portal to the town and one
    back, with no scroll or book."""
    nm = "Town Portal skill (bug #6): itemless cast opens a portal to town and back"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    by_name, t, h = _skill_ids_by_name()
    tp = by_name.get("town_portal")
    do109 = [r[0] for r in t[1:] if r and r[h.index("srvdofunc")] == "109"]
    if tp is None:
        return add(nm, "BLOCKED", "no town_portal row in skills.txt (rows with srvdofunc 109: %s)" % do109)
    fight_ready()  # Blood Moor, nothing alive nearby
    for code in ("tbk", "tsc", "ibk", "isc"):
        H("remove %s 99" % code)
    st0 = hstate()
    area = st0["player"].get("area")
    if not items(st0, "tbk") and not items(st0, "tsc"):
        pass
    _, sl = H("skills")
    if not any(l.startswith("skill id=%d " % tp) for l in sl):
        _setskill(tp, 1)
    H("stat player 8 set %d" % (rd("player", 9) or 0))  # full mana
    before = {o["id"] for o in hstate()["objs"]}
    ok, cl = H("cast %d self" % tp)
    new, t0 = [], time.time()
    while time.time() - t0 < 8 and not new:
        time.sleep(1)
        new = new_portals(before)
    sh("townportal_field")
    acts = _level_acts()
    want_town = TOWNS[acts.get(area, 0)]
    field = [o for o in new if o.get("class") == 59]
    log = cl + ["field portals after the cast: %s" % [(o["id"], o.get("class"), o.get("dest")) for o in new]]
    ok_field = bool(field) and field[0].get("dest") == want_town
    arrived = back = False
    if ok_field:
        for _ in range(3):
            ok1, l1 = H("enter nearest")
            log += l1
            time.sleep(4)
            if hstate()["player"].get("area") == want_town:
                arrived = True
                break
        if arrived:
            sh("townportal_town")
            town_objs = [o for o in hstate()["objs"] if o.get("class") == 59]
            log.append("portals in town: %s" % [(o["id"], o.get("dest")) for o in town_objs])
            for _ in range(3):
                H("enter nearest")
                time.sleep(4)
                if hstate()["player"].get("area") == area:
                    back = True
                    break
    add(nm, "PASS" if ok_field and arrived and back else "FAIL",
        "skill %d (town_portal; skills with srvdofunc 109: %s), no scroll or book in the pack, cast at the player in area %s: "
        "new blue portal %s (want dest = town %d); entering it -> %s; the town portal back -> %s" % (
            tp, do109, area, [(o.get("class"), o.get("dest")) for o in new], want_town,
            "arrived in town" if arrived else "not in town", "back in area %s" % area if back else "did not return"),
        [sh("townportal")], log)
    goto(2)


def check_potions():
    """Bug #10: Median's healing / mana / rejuvenation potions (item spell 22 -> D2R's timed potion). Two results: the life
    part (hp1, hp5, rvs, rvl: stat hpregen 74, life rises over a control wait) and the mana part (mp1, mp5: manarecoverybonus
    27 set by the potion, mana rises over a control wait)."""
    n1 = "Potions (bug #10): healing and rejuvenation potions restore life over time"
    n2 = "Potions (bug #10): mana potions raise the mana recovery (stat 27) and restore mana over time"
    if not need_game():
        add(n1, "BLOCKED", "not in a game")
        return add(n2, "BLOCKED", "not in a game")
    goto(1)
    close_menu()
    H("difficulty 0")
    # (code, heals, mana, stat 27 right after the use): Median's calc1 / calc2 are 100 / 200 per cent (rejuvenation 125 / 200); D2R's
    # timed potion stores calc / len, which ended mp1 / mp5 at 0 / 1 until txt_migrate multiplied the calc by len
    cases = [("hp1", True, False, None), ("hp5", True, False, None), ("rvs", True, False, 125), ("rvl", True, False, 200),
             ("mp1", False, True, 100), ("mp5", False, True, 200)]
    rows = {True: [], False: []}
    bad = {True: [], False: []}
    log = []
    maxhp, maxmp = rd("player", 7), rd("player", 9)

    def drain():
        # the previous potion's state (len 175 frames = 7 s) must be over: a second potion within it averages the stat
        for _ in range(40):
            if not rd("player", 27) and not rd("player", 74):
                break
            time.sleep(0.5)
        setstat("player", 6, maxhp // 5)
        setstat("player", 8, maxmp // 5)
        time.sleep(0.5)

    def gain(code, seconds=4):
        """Life and mana gain (raw 1/256) over `seconds` seconds after `use <code>` (code None: a control wait), with the
        regeneration stats read right after the use."""
        drain()
        l0, m0, h0, r0 = rd("player", 6), rd("player", 8), rd("player", 74), rd("player", 27)
        line = ""
        if code:
            ok, line, _ = hv("use %s" % code, "use")
            if not ok:
                return None
        if code:
            time.sleep(0.75)  # the potion's stat list can land a server frame after `use` returns (mp1 once read 0 right away)
        h1, r1 = rd("player", 74), rd("player", 27)
        time.sleep(seconds)
        return rd("player", 6) - l0, rd("player", 8) - m0, h0, h1, r0, r1, line

    control = gain(None)
    for code, heals, mana, want27 in cases:
        key = heals
        H("remove %s 9" % code)
        ok, line, _ = hv("item %s" % code, "item")
        r = gain(code) if ok else None
        left = len([i for i in hstate()["items"] if str(i.get("code")) == code])
        if r is None:
            rows[key].append("%s: WRONG (no item / use failed: %s)" % (code, line[:60]))
            bad[key].append(code)
            continue
        pl, pm, h0, h1, r0, r1, uline = r
        log += [line, uline, "%s: hpregen %s -> %s, manarecoverybonus %s -> %s; gain with potion life %d mana %d, control life %d mana %d" % (
            code, h0, h1, r0, r1, pl, pm, control[0], control[1])]
        good = left == 0
        if heals and (h1 is None or h1 <= (h0 or 0) or pl <= control[0] + 256 * 5):
            good = False
        if mana and (r1 is None or r1 <= (r0 or 0) or pm <= control[1] + 256 * 2):
            good = False
        if want27 is not None and (r1 or 0) - (r0 or 0) != want27:
            good = False
            rows[key].append("%s: manarecoverybonus rose by %s, wanted %d" % (code, (r1 or 0) - (r0 or 0), want27))
        rows[key].append("%s: %s (life +%d vs control +%d, mana +%d vs +%d, hpregen %s->%s, manarecoverybonus %s->%s)" % (
            code, "ok" if good else "WRONG", pl >> 8, control[0] >> 8, pm >> 8, control[1] >> 8, h0, h1, r0, r1))
        if not good:
            bad[key].append(code)
        H("remove %s 9" % code)
    setstat("player", 6, maxhp)
    setstat("player", 8, maxmp)
    shot = sh("potions")
    add(n1, "PASS" if not bad[True] else "FAIL",
        "`use` consumed each potion; in the 4 s after it hpregen (stat 74) rose and life rose above a control wait: " + "; ".join(rows[True]),
        [shot], log)
    add(n2, "PASS" if not bad[False] else "FAIL",
        "`use` consumed each potion; the potion sets manarecoverybonus (stat 27) to calc1 = 100 (mp1) / 200 (mp5) per cent and mana "
        "rises above a control wait: " + "; ".join(rows[False]), [shot], log)


def check_deathcrash():
    """Bug #12: the client formatted 'was slain by' with a missing %s argument and crashed. The test character is killed by a
    monster's hit; the game process must survive and the character must be able to respawn and travel."""
    nm = "Player death (bug #12): killed by a monster, the game survives, respawn works"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    fight_ready()
    pids = sorted(g.game_pids())

    def snap():
        st = hstate()
        eq = sorted((str(i.get("code")), i.get("body")) for i in st["items"] if i.get("body", 0) > 0)
        m = re.search(r"corpses (\d+) players=(\d+) self=(\d)", " ".join(st["lines"]))
        sk = re.search(r"no corpse (\w+): skipped corpse calls=(\d+) penalty calls=(\d+) respawns held=\d+ let=\d+ xp=(\d+)", " ".join(st["lines"]))
        rh = re.search(r"respawns held=(\d+) let=(\d+)", " ".join(st["lines"]))
        p = st["player"]
        return {"gold": p.get("gold"), "bank": p.get("bank"), "area": p.get("area"), "mode": p.get("mode"), "eq": eq,
                "corpses": int(m.group(1)) if m else None, "players": int(m.group(2)) if m else None,
                "self": int(m.group(3)) if m else None, "on": sk.group(1) == "on" if sk else None,
                "skc": int(sk.group(2)) if sk else None, "skp": int(sk.group(3)) if sk else None,
                "xp": int(sk.group(4)) if sk else None, "level": p.get("level"), "ground_gold": ground_count("gld"),
                "frame": p.get("frame"), "held": int(rh.group(1)) if rh else None, "let": int(rh.group(2)) if rh else None}

    level0, xp0 = rd("player", 12), rd("player", 13)
    H("unequip lring")
    H("unequip amulet")
    ok1, _l1 = H("equip rin lring")
    ok2, _l2 = H("equip amu amulet")
    H("unequip belt")
    ok3, _l3 = H("equip 629 belt")  # a belt (Median code 629): the belt's potions would go to the corpse in D2 (audit DC1); the harness cannot fill a belt
    H("gold set 23456 7890")
    if level0 is not None and level0 < 2:
        setstat("player", 12, 20)  # the experience penalty only exists above level 1
    setstat("player", 13, 1000000000)
    atk = spawn_attacker(1)
    if atk is None or not (ok1 and ok2 and ok3):
        return add(nm, "BLOCKED", "no attacker monster" if atk is None else "could not equip the three test items")
    before = snap()
    setstat("player", 7, BIG_LIFE)
    setstat("player", 6, 256 * 100)
    ok, line, lines = hv("hit player phys 2000000000 %s" % atk, "hit")
    for _ in range(12):  # the death animation (mode 0) runs a second or two; the corpse creator is called on the way into mode 0x11
        time.sleep(1)
        if hstate()["player"].get("mode") == 17:
            break
    dead = snap()
    shot_dead = sh("nocorpse_dead")
    g.press("esc")  # too early: the respawn request must be held back (audit DC4)
    time.sleep(1)
    early = snap()
    waited = 0
    while waited < 15 and (hstate()["player"].get("frame") or 0) - (dead["frame"] or 0) < 130:  # 125 frames = 5 s, plus a margin
        time.sleep(1)
        waited += 1
    survived = sorted(g.game_pids()) == pids and hstate()["ok"]
    revived = revive_if_dead()
    time.sleep(2)
    after = snap()
    shot_after = sh("nocorpse_respawned")
    alive = after["mode"] not in (0, 17)

    def same(a, b):
        return a["eq"] == b["eq"] and a["gold"] == b["gold"] and a["bank"] == b["bank"] and a["xp"] == b["xp"]

    problems = []
    if before["on"] is not True:
        problems.append("plugin part nocorpse is not on: %s" % before["on"])
    if before["self"] != 1:
        problems.append("the unit scan does not see the character itself (self=%s): the corpse count means nothing" % before["self"])
    if len(before["eq"]) < 2:
        problems.append("fewer than two equipped items before the death: %s" % before["eq"])
    if dead["mode"] not in (0, 17):
        problems.append("the character did not die (mode %s): %s" % (dead["mode"], line[:90]))
    if not survived:
        problems.append("the game process did not survive")
    if (dead["skc"] or 0) <= (before["skc"] or 0) or (dead["skp"] or 0) <= (before["skp"] or 0):
        problems.append("the death did not reach the corpse creator / penalty hooks (skipped calls %s/%s -> %s/%s)" %
                        (before["skc"], before["skp"], dead["skc"], dead["skp"]))
    for label, snapshot in (("dead", dead), ("after respawn", after)):
        if snapshot["corpses"] != before["corpses"]:  # a corpse from an earlier death in the save (made before the part) is not new
            problems.append("%s: %s corpse unit(s) in the game, %s before the death" % (label, snapshot["corpses"], before["corpses"]))
        if not same(before, snapshot):
            problems.append("%s: equipment / gold / bank / xp changed: %s -> %s" % (label, {k: before[k] for k in ("eq", "gold", "bank", "xp")},
                                                                                {k: snapshot[k] for k in ("eq", "gold", "bank", "xp")}))
        if snapshot["ground_gold"] != before["ground_gold"]:
            problems.append("%s: gold piles on the ground %s -> %s" % (label, before["ground_gold"], snapshot["ground_gold"]))
    if not revived or not alive:
        problems.append("respawn failed (revived %s, mode %s)" % (revived, after["mode"]))
    if early["mode"] not in (0, 17):
        problems.append("respawn at once was not held back (mode %s a second after the death)" % early["mode"])
    if (early["held"] or 0) <= (before["held"] or 0):
        problems.append("the plugin did not count a held respawn request (held %s -> %s): was Esc a respawn request?" % (before["held"], early["held"]))
    if (after["let"] or 0) <= (before["let"] or 0):
        problems.append("the respawn after the wait did not pass the hook (let through %s -> %s)" % (before["let"], after["let"]))
    if after["area"] is None or after["area"] not in (1, 40, 75, 103, 109):
        problems.append("not in a town after the respawn (area %s)" % after["area"])
    add(nm, "FAIL" if problems else "PASS",
        "%s | killed by a hit of 2e9 from a spawned monster (%s); before: items %s gold %s bank %s xp %s; dead (mode %s): corpses %s (before: %s), "
        "gold %s xp %s, hooks swallowed corpse %s->%s penalty %s->%s calls; after Esc: mode %s area %s, corpses %s, items %s gold %s bank %s xp %s, "
        "ground gold piles %s->%s; respawn held %s->%s, let %s->%s, frames dead->early Esc->respawn %s->%s->%s" %
        ("; ".join(problems) if problems else "all unchanged", line[:70], before["eq"], before["gold"], before["bank"], before["xp"],
         dead["mode"], dead["corpses"], before["corpses"], dead["gold"], dead["xp"], before["skc"], dead["skc"], before["skp"], dead["skp"],
         after["mode"], after["area"], after["corpses"], after["eq"], after["gold"], after["bank"], after["xp"],
         before["ground_gold"], after["ground_gold"], before["held"], after["held"], before["let"], after["let"],
         dead["frame"], early["frame"], after["frame"]), [shot_dead, shot_after], lines)
    # put the test character back
    H("unequip lring")
    H("unequip amulet")
    H("unequip belt")
    setstat("player", 13, xp0 if xp0 is not None else 0)
    if level0 is not None:
        setstat("player", 12, level0)
    setstat("player", 6, rd("player", 7) or BIG_LIFE)



    atk = spawn_attacker(1)
    if atk is None:
        return add(nm, "BLOCKED", "no attacker monster")
    setstat("player", 7, BIG_LIFE)
    setstat("player", 6, 256 * 100)
    ok, line, lines = hv("hit player phys 2000000000 %s" % atk, "hit")
    time.sleep(3)
    p = hstate()["player"]
    shot_dead = sh("death")
    dead = p.get("mode") in (0, 17) or str(p.get("life", "1")).split("/")[0] == "0"
    alive_pids = sorted(g.game_pids())
    time.sleep(4)
    survived = sorted(g.game_pids()) == pids and hstate()["ok"]
    revived = revive_if_dead()
    st = hstate()
    again = st["player"].get("area") is not None and not (st["player"].get("mode") in (0, 17))
    moved = goto(2)["player"].get("area") == 2
    add(nm, "PASS" if dead and survived and again and moved else "FAIL",
        "hit amount 2e9 from a spawned monster: %s; player dead (mode %s) %s; game process %s; respawned %s, alive again %s, travel to "
        "Blood Moor %s" % (line[:90], p.get("mode"), dead, "survived" if survived else "GONE", revived, again, moved),
        [shot_dead], lines)
    setstat("player", 6, rd("player", 7) or BIG_LIFE)


def check_itemskills():
    """Bug #16: skill / monster parameters of item properties. Table audit (every parameter of Median's property tables by skill
    name, tools/item_skill_audit.py) plus in-game: unique items with each kind of skill property carry the right skill."""
    n1 = "Item property skill parameters (bug #16): all tables, by skill name"
    n2 = "Item property skill parameters (bug #16): uniques in game carry the named skill"
    import item_skill_audit as audit
    checked, bad, samples = audit.audit()
    add(n1, "PASS" if not bad and sum(checked.values()) > 5000 else "FAIL",
        "%d skill / monster parameters in %d Median property tables compared with the skill's name (named/skills.txt, built "
        "skills.txt); mismatches %d%s" % (sum(checked.values()), len({t for t, _ in checked}), len(bad),
                                          (": " + "; ".join(str(b) for b in bad[:4])) if bad else ""))
    if not need_game():
        return add(n2, "BLOCKED", "not in a game")
    goto(1)
    ut = _tsv(os.path.join(BUILT_EXCEL, "uniqueitems.txt"))
    it = _tsv(os.path.join(BUILT_EXCEL, "itemstatcost.txt"))
    stat_id = {r[0]: int(r[it[0].index("*ID")]) for r in it[1:] if r and r[0] and r[it[0].index("*ID")].isdigit()}
    encode = {r[0]: r[it[0].index("Encode")] for r in it[1:] if r and r[0]}
    rows, bad2, log = [], [], []
    for code, picks in sorted(samples.items()):
        pick = next((p for p in picks if p[0] == "uniqueitems"), None)
        if pick is None:
            continue
        _table, index, uname, skill, skill_name, stat = pick
        base = ut[index + 1][ut[0].index("code")]
        if stat not in stat_id:
            continue
        H("cubeclear")
        ok, line, _ = hv("item %s uniq=%d" % (base, index), "item")
        m = re.search(r"item id=(\d+)", line)
        if not ok or not m:
            log.append("%s: item %s uniq=%d failed: %s" % (uname, base, index, line[:80]))
            rows.append("%s: no item" % uname)
            continue
        ok, lines = H("stat %s %d scan" % (m.group(1), stat_id[stat]), 60)
        layers = [(int(a), int(b)) for a, b in re.findall(r"scan \S+ \d+:(\d+) = (-?\d+)", " ".join(lines)) if int(b) != 0]
        # the layer is the skill (aura, oskill, ...) or skill << 6 | level (encode 2 / 3: skill on an event, charges)
        want_layer = lambda l: (l >> 6) == skill if encode.get(stat) in ("2", "3") else l == skill  # noqa: E731
        good = any(want_layer(l) for l, _v in layers)
        log.append("%s (uniq %d, prop %s, stat %s=%d, encode %s): skill %s = %d; layers %s" % (
            uname, index, code, stat, stat_id[stat], encode.get(stat), skill_name, skill, layers[:6]))
        rows.append("%s %s -> %s %s" % (uname, code, skill_name, "ok" if good else "WRONG %s" % layers[:3]))
        if not good:
            bad2.append(uname)
        H("remove %s 9" % base)
    add(n2, "PASS" if rows and not bad2 and len(rows) >= 8 else "FAIL",
        "%d unique items, one per skill property kind, created with `item <base> uniq=<row>`; `stat <item> <stat> scan` found the "
        "skill the name oracle expects: %s" % (len(rows), "; ".join(rows)), [], log)

def check_nslong():
    name = "Short aliases of the long Median keys (docs/strings-namespace.md): service text and displayed names"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    import json
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    aliases = json.load(open(os.path.join(root, "work", "mod", "strings_alias.json"), encoding="utf-8"))["aliases"]
    recs = {e["Key"]: e["enUS"] for e in json.load(open(os.path.join(
        root, "work", "mod", "data", "local", "lng", "strings", "medianxl", "median.json"), encoding="utf-8-sig"))}
    log, bad = [], []
    for k in ("Sunstone of the Twin Seas", "MRVasily's Falling Star", "To The Island of the Sunless Sea"):
        full = "medianxl:" + aliases[k]
        ok, lines = H("str key " + full)  # the harness splits a line at spaces: the long keys themselves cannot be asked
        # colour codes (ÿc + 1 character) are dropped on both sides, as check_strings does. The console shows the two
        # characters ÿc as two garbage characters and keeps the colour digit ("<2 junk>0Difficulty: <2 junk>2Very Easy"):
        # junk plus the character behind it is dropped from the answer.
        first = [w for w in recs[aliases[k]].split(chr(10)) if w][0]
        want = _COLOUR.sub("", first)
        want_console = first.replace(chr(255) + "c", "")
        got = " ".join(lines)
        got_plain = re.sub("[^\x00-\x7f]+.", "", _COLOUR.sub("", got))
        log.append("%s -> %s %s (record starts '%s')" % (full, ok, lines[0] if lines else "", want))
        if not (ok and (want in got_plain or want_console in got)):
            bad.append("alias %s does not give the record text '%s'" % (full, want))
    goto(1)
    close_menu()
    # ground labels (Alt held through the plugin's virtual modifier block) show the displayed names:
    # unique "Sunstone of the Twin Seas" (uniqueitems data row 9, code twi), set item "MRVasily's Falling Star" (setitems row 141)
    for cmd in ("item twi q=7 uniq=9 ground", "item 89@ q=5 uniq=141 ground", "item twi q=7 uniq=9 inv"):
        ok, lines = H(cmd)
        log.append("%s: %s" % (cmd, lines[0] if lines else ""))
    time.sleep(1.0)
    shots = []
    g._set_mods(("alt",))
    hwnd = g.need_window()
    g.user32.PostMessageW(hwnd, g.WM_SYSKEYDOWN, 0x12, g._lparam(0x12, 0, False))
    try:
        for i in range(3):
            time.sleep(1.5)
            shots.append(sh("nslong_labels%d" % i))
    finally:
        g.user32.PostMessageW(hwnd, g.WM_SYSKEYUP, 0x12, g._lparam(0x12, 0, True))
        g._set_mods(())
    add(name, "FAIL" if bad else "REVIEW", "service answers: %s; ground labels with Alt held (read the screenshots: the label is the displayed name)" % (
        "; ".join(bad) or "alias texts match the records"), shots, log)


# ------------------------------------------------------------------ strings: what the player reads (lane ns-verify)
# docs/strings-namespace.md moves Median-only strings into the `medianxl` loader namespace and keeps D2R's own keys native.
# The proof that the switch changes nothing the player sees: a fixed sample of strings is read back through the plugin
# harness `text` verb (plugin/src/harness_cmds.cpp) and compared with Median's own text from its .tbl files
# (tools/tbl2json.py parse), the same text before and after the switch.
#   text id <n>                  the client's string lookup (what an NPC menu line shows: the u16 id a menu record holds)
#   text row <table> <row> <off> the u16 string reference at a byte offset of the compiled table row the game loaded
#                                (the display name of an item, unique, set, monster, level, skill, stat line), resolved
#                                by the same lookup. Offsets are the name columns of the compiled rows; ROW_FIELDS below
#                                holds them, a table that is not listed is calibrated from the sample (`text scan`).
#   text key <full key>          D2RLoader's localization service: `d2r:Key` (D2R's own) or `medianxl:Key` (namespaced)
# Every sample prints one `SAMPLE` line (category, label, PASS / FAIL, expected, got) into the log and strings.tsv.

STR_NS = "medianxl"
STR_BANK = int(os.environ.get("MXL_STRINGS_BANK", "3"))  # data table bank: 1 classic, 2 lod, 3 rotw
# table -> byte offset of the name reference in the compiled row (from the calibration run: MXL_STRINGS_CALIBRATE=1)
ROW_FIELDS = {"items": 0xfc, "uniqueitems": 0x22, "setitems": 0x24, "sets": 0x2, "monstats": 0x36, "superuniques": 0x2,
              "skilldesc": 0x8, "itemstatcost": 0x34}
LEVEL_CELLS = {"LevelName": 0xfd, "LevelWarp": 0x125}  # inline in the compiled Levels row (from `text scan levels`)
# Median-only keys that stay native (`d2r:<key>`, tools/strings_native.json): too long for a prefixed fixed-size cell. With the
# namespace on they are the ones that can show "Missing string". Each is read where it sits: uniqueitems / setitems index, levels.
STR_NATIVE = ["Armor of the Old Religion", "Body of the Forbidden One", "Claw of the Spirit Wolf", "DurandaltheBlazingSword",
              "Gift of the Spiritbeast", "MRAchilios' Sledgehammer", "MRHunter's Trackless Step", "MRImperius' Winged Feet",
              "MRVasily's Falling Star", "MRVasily's Shepherd Moon", "MRWarmage's Flameshroud", "MRWitchhunter's Crucifix",
              "MRZann Esu's Binding Circle", "Spirit Teacher's Headdress", "Staff of Herding Design", "Sunstone of the Twin Seas",
              "To The Dimensional Labyrinth 10", "To The Island of the Sunless Sea", "UpgradedBossSummonerRing"]
STR_NATIVE_WHERE = [("uniqueitems", "index"), ("setitems", "index"), ("levels", "LevelWarp"), ("levels", "LevelName")]
_COLOUR = re.compile("ÿc.")

# (category, table, column of the name key, keys). `items` rows are found by item code, `levels` by id, the rest by row index.
STR_ROWS = [
    ("item names", "items", {"misc": "namestr", "armor": "namestr", "weapons": "namestr"},
     [("misc", "cotw"), ("misc", "yq2"), ("misc", "cyc3"), ("misc", "en09"), ("armor", "w23"), ("armor", "704"),
      ("weapons", "4@A"), ("weapons", "123")]),
    ("uniques", "uniqueitems", "index", ["CycleOfGold", "Wolfpack", "Bonechewer", "Vilehand", "FrozenHeart", "ShardRefraction"]),
    ("sets", "setitems", "index", ["MRCelestia's Charge", "MRSerpent's Coil", "MRAvalanche", "M_WisdomEye3"]),
    ("sets", "sets", "name", ["MSPantheon", "Set_Lazbro"]),
    ("monsters", "monstats", "NameStr", ["ReanimatedWarrior", "BloodBengal", "SapphireLord", "Sewer Rat", "DiscipleofHatred", "WycBoss3"]),
    ("superuniques", "superuniques", "Name", ["Snotspill", "NewFleshdoom", "Maghda", "JurisKhan"]),
    ("levels", "levels", "LevelName", ["Caldeum", "Butcher's Lair", "Kingdom of Shadow", "The Void", "Dimensional Labyrinth 5", "Heroic Duncraig"]),
    ("skills", "skilldesc", "str name", ["MasteryNameMeditation", "SkillnameSnakeStance", "SkillnameBlight", "SkillnameDeathCoil",
                                         "SkillnameIceBoltNova", "SkillnameFortress"]),
    ("stat lines", "itemstatcost", "descstrpos", ["ModTotPhysDmg", "ModitemBlessed", "ModitemHealAttack", "ModItemNoUnsocket", "ModitemCorrupted"]),
]
# Rare item affixes: no data table of the SDK exposes them, so the loader's key lookup is the only path.
STR_KEYS = ["TerrQ", "Leviath", "halo", "spines"]
# D2R keys Median overrides: (key, whether `text key` can take it: no spaces) - resolved by D2R's own numeric id and by key.
STR_D2R = ["CfgRun", "Zakarumite", "Maggot Lair", "gcy", "r27", "Baal Subject 3", "StrSklTabItem1", "skillld49", "Skillld253", "CharsiGossip7"]
# Bug #7: the first-talk line of the town NPCs is the NPC's own, not another string's (Median's "ArcaneApe" shared Gheed's id)
STR_INTRO = ["GheedIntroGossip1", "AkaraIntroGossip1", "KashyaIntroGossip1", "CharsiIntroGossip1", "WarrivAct1IntroGossip1",
             "WarrivAct1IntroPalGossip1"]
# NPC menus (npc class, [(top-level plugin line key) ...], the native lines D2R itself shows)
STR_MENUS = [(265, "Deckard Cain", ["strCainMenuCraftMaterials"], ["TalkMenu", "NPCIdentify1"]),
             (148, "Akara", ["AkaraMenu1b"], ["TalkMenu"]),
             (175, "Warriv", [], ["TalkMenu"])]
STR_CAIN_SUB = ["strCainMenuCMHoradricCube", "strCainMenuCMCatalyst1", "strCainMenuCMCatalyst2", "strCainMenuCMCatalyst3"]


class _StrSrc:
    """Median's strings from the .tbl files and D2R's own string files: key -> expected text, D2R key -> numeric id."""

    def __init__(self):
        import json
        import glob
        import tbl2json as T
        by_id, winner = T.load_median()
        self.texts = {}
        for sid, (key, text) in by_id.items():
            fk = T.final_key(sid, key)
            if fk.startswith("mxl_") or winner[key] == sid:
                self.texts[fk] = text
        self.d2r = {}
        for p in glob.glob(str(T.D2R_LNG / "strings" / "*.json")):
            for e in json.load(open(p, encoding="utf-8-sig")):
                self.d2r[e["Key"]] = e
        # keys tbl2json leaves with D2R's text (placeholders differ): not Median's text, not part of the sample
        self.kept = {k for k, e in self.d2r.items() if k in self.texts and T.placeholders(self.texts[k]) != T.placeholders(e.get("enUS", ""))}
        ns = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "mod", "strings_namespace.json")
        self.ns_keys = set(json.load(open(ns, encoding="utf-8"))["keys"]) if os.path.exists(ns) else None

    def expected(self, key):
        t = self.texts.get(key)
        return None if t is None else _str_norm(t)

    def full_key(self, key):
        """`medianxl:Key` for a Median-only key once the namespace exists, else D2R's own `d2r:Key` (native loading)."""
        g2 = key not in self.d2r
        if self.ns_keys is not None:
            g2 = key in self.ns_keys
        return (STR_NS if g2 and self.ns_keys is not None else "d2r") + ":" + key


def _str_norm(text):
    """The verb's form of a string: colour codes removed, newlines written as the two characters \\n."""
    return _COLOUR.sub("", text).replace("\r", "").replace("\n", "\\n")


def _txt_rows(name, bank):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "mod", "data", "global", "excel",
                        "" if bank == 3 else "base", name + ".txt")
    with open(path, encoding="utf-8-sig") as f:
        rows = [ln.rstrip("\n").split("\t") for ln in f]
    return rows[0], rows[1:]


def _cell_key(cell):
    """A localization cell is `Key`, `d2r:Key` or `medianxl:Key`: the plain key."""
    return cell.split(":", 1)[1] if cell.startswith((STR_NS + ":", "d2r:")) else cell


_HTXT = re.compile(r"(?:result=(\d+) )?len=(\d+) fnv=([0-9a-f]{8}) raw=([0-9a-f]*) = \[(.*)\]$")
_HSCAN = re.compile(r"\+0x([0-9a-f]+) id=(\d+) \[(.*)\]$")


def _fnv(text):
    h = 2166136261
    for b in text.encode("utf-8"):
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h


class _Got:
    """One `text` answer: the length and FNV-1a hash of the whole cleaned text (a speech does not fit one line) and its start."""

    def __init__(self, line):
        m = _HTXT.search(line)
        self.ok = m is not None
        self.result = int(m.group(1)) if m and m.group(1) else None
        self.n = int(m.group(2)) if m else -1
        self.fnv = int(m.group(3), 16) if m else -1
        self.raw = m.group(4) if m else ""
        self.text = m.group(5) if m else line

    def same(self, exp):
        return self.ok and exp is not None and self.n == len(exp.encode("utf-8")) and self.fnv == _fnv(exp)

    def sig(self):
        return (self.n, self.fnv)

    def show(self):
        return "%s%s" % (self.text, "..." if self.n > 200 else "")


def _text_id(sid):
    ok, lines = H("text id %d" % sid)
    return _Got(lines[0]) if ok and lines else None


def _text_key(full):
    """_Got of the loader's lookup (`result` set); a space in the key travels as ^ on the harness line."""
    ok, lines = H("text key " + full.replace(" ", "^"))
    return _Got(lines[0]) if lines else _Got("")


def _str_aliases():
    import json
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "mod", "strings_alias.json")
    return json.load(open(p, encoding="utf-8"))["aliases"] if os.path.exists(p) else {}


def _row_ref(table, key, col, sub=None):
    """(row reference for the verb, plain keys of the row) of the first row of the built table whose cell is `key`."""
    name = sub or table
    head, rows = _txt_rows(name, STR_BANK)
    ci = head.index(col if isinstance(col, str) else col[name])
    alias = _str_aliases().get(key, key)  # a long key sits in its cell as its short alias (work/mod/strings_alias.json)
    for i, r in enumerate(rows):
        if ci < len(r) and _cell_key(r[ci]) in (key, alias):
            if table == "items":
                return "code:" + r[head.index("code")].ljust(4)[:4].replace(" ", ""), i
            return str(i), i
    return None, None


def check_strings():
    name = "Strings the player reads: items, monsters, levels, skills, NPC menus, stat lines, D2R overrides (namespace contract)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    src = _StrSrc()
    rows_out = []  # (category, label, status, expected, got, note)

    def record(cat, label, ok, expected, got, note=""):
        rows_out.append((cat, label, "PASS" if ok else "FAIL", expected, got, note))
        g.log("SAMPLE %s | %s | %s | expected [%s] got [%s] %s" % (cat, label, "PASS" if ok else "FAIL", expected, got, note))

    # ---- table rows: the display name field of the compiled row, resolved by the client's string lookup
    calibrate = {}
    pending = []  # (cat, table, label, key, ref, expected)
    cellof = {}  # label -> column, for levels
    for cat, table, col, keys in STR_ROWS:
        for item in keys:
            sub, key = item if table == "items" else (None, item)
            ref, _i = _row_ref(table, key, col, sub)
            exp = src.expected(key)
            if ref is None or exp is None:
                record(cat, "%s:%s" % (table, key), False, exp, None, "no row in the built table" if ref is None else "no text in Median's .tbl")
                continue
            pending.append((cat, table, "%s:%s %s" % (table, key, ref), key, ref, exp))
            cellof[pending[-1][2]] = col
    for key in STR_NATIVE:
        exp = src.expected(key)
        for table, col in STR_NATIVE_WHERE:
            ref, _i = _row_ref(table, key, col)
            if ref is not None:
                break
        if ref is None or exp is None:
            record("long keys", key, False, exp, None, "no row in the built table" if ref is None else "no text in Median's .tbl")
            continue
        pending.append(("long keys", table, "%s.%s:%s %s" % (table, col, key, ref), key, ref, exp))
        cellof[pending[-1][2]] = col
    for cat, table, label, key, ref, exp in pending:
        if table == "levels":
            continue
        if ROW_FIELDS.get(table) is None or os.environ.get("MXL_STRINGS_CALIBRATE"):
            hits = {}
            ok, lines = H("text scan %s %s %d" % (table, ref, STR_BANK))
            if not ok:
                g.log("CALIBRATE %s %s: %s" % (table, ref, lines[-1] if lines else "no answer"))
            want = exp.encode("utf-8")[:90].decode("utf-8", "ignore")  # the scan prints at most 90 bytes
            for ln in lines:
                m = _HSCAN.search(ln)
                if m and m.group(3) == want:
                    hits[int(m.group(1), 16)] = m.group(3)
            if not hits:
                for ln in lines:
                    g.log("SCAN %s %s %s" % (table, ref, ln[:160]))
            calibrate.setdefault(table, []).append(set(hits))
    for table, sets in calibrate.items():
        votes = {}
        for s in sets:
            for o in s:
                votes[o] = votes.get(o, 0) + 1
        best = max(votes, key=lambda o: (votes[o], -o)) if votes else None
        g.log("CALIBRATE %s: offset %s matches %d of %d sample rows (votes %s)" % (
            table, hex(best) if best is not None else None, votes.get(best, 0) if best is not None else 0, len(sets),
            {hex(o): n for o, n in sorted(votes.items())}))
        if best is not None and votes[best] * 2 > len(sets) and (ROW_FIELDS.get(table) is None):
            ROW_FIELDS[table] = best
    for cat, table, label, key, ref, exp in pending:
        if table == "levels":
            # Levels keeps LevelName as characters in the compiled row (the game resolves it by key when it draws the name):
            # the cell must name the string, and the loader must resolve that name to the expected text.
            ok, lines = H("text cstr levels %s %x %d" % (ref.split(":")[-1], LEVEL_CELLS[cellof[label]], STR_BANK))
            m = re.search(r"= \[(.*)\]$", lines[0]) if ok and lines else None
            cell = m.group(1) if m else None
            full = None if cell is None else (cell if ":" in cell else "d2r:" + cell)
            kg = _text_key(full) if full else None
            good = kg is not None and kg.result == 0 and kg.same(exp)
            record(cat, label, good, exp, kg.show() if kg else None,
                   "row cell [%s] -> %s (expected key %s)" % (cell, full, src.full_key(key)) + ("" if ok else "; " + (lines[-1] if lines else "no answer")))
            continue
        off = ROW_FIELDS.get(table)
        if off is None:
            record(cat, label, False, exp, None, "no field of the compiled row resolves to the expected text")
            continue
        ok, lines = H("text row %s %s %x %d" % (table, ref, off, STR_BANK))
        got = _Got(lines[0]) if ok and lines else None
        mid = re.search(r"id=(\d+) len", lines[0]) if ok and lines else None
        full = src.full_key(key)
        kg = _text_key(full)
        note = "row offset 0x%x id=%s; key %s -> %s" % (off, mid.group(1) if mid else "?", full, "n/a (space)" if kg is None else
                                                      ("ok" if kg.result == 0 and kg.same(exp) else "result %s [%s]" % (kg.result, kg.show())))
        if not ok:
            note += "; " + (lines[-1] if lines else "no answer")
        record(cat, label, got is not None and got.same(exp), exp, got.show() if got else None, note)

    # ---- rare affixes: the loader's key lookup only
    for key in STR_KEYS:
        full = src.full_key(key)
        kg = _text_key(full)
        exp = src.expected(key)
        record("rare affixes", full, kg is not None and kg.result == 0 and kg.same(exp), exp, kg.show() if kg else None,
               "result %s" % (kg.result if kg else None))

    # ---- the plugin's own string ids (`h mstr`): every menu text the plugin hands to D2R resolves and gets an id
    ok, lines = H("mstr " + src.full_key("AkaraMenu1b"))
    if ok or (lines and "unknown" not in lines[-1].lower()):
        for key in [k for _c, _n, ks, _l in STR_MENUS for k in ks] + STR_CAIN_SUB:
            full = src.full_key(key)
            ok, lines = H("mstr " + full)
            m = re.search(r'service=(\w+) text="(.*)" id=(\d+) \(0x[0-9a-f]+\) via D2R lookup="(.*)" ids=', lines[0]) if lines else None
            exp = src.expected(key)
            via = re.sub("[-].", "", m.group(4)) if m else None
            record("plugin string ids", full, bool(m) and m.group(1) == "ok" and int(m.group(3)) != 0 and via == exp, exp, via,
                   "service %s id %s" % (m.group(1), m.group(3)) if m else (lines[-1] if lines else "no answer"))

    # ---- native exceptions by key: the loader must know the key, native (`d2r:`) or in the namespace (an unqualified record of a
    # namespaced mod belongs to it); the row samples above are the proof of what the player sees
    for key in STR_NATIVE:
        exp = src.expected(key)
        tries = [_text_key(f + ":" + key) for f in ("d2r", STR_NS)]
        kg = next((t for t in tries if t is not None and t.result == 0 and t.same(exp)), tries[0])
        record("long keys (key)", key, kg is not None and kg.result == 0 and kg.same(exp), exp, kg.show() if kg else None,
               "d2r: result %s, %s: result %s" % (tries[0].result, STR_NS, tries[1].result))

    # ---- D2R keys Median overrides: D2R's numeric id (native, fixed) and the key
    for key in STR_D2R:
        e = src.d2r.get(key)
        exp = src.expected(key)
        if e is None or exp is None or key in src.kept:
            record("D2R overrides", key, False, exp, None, "not a D2R key Median overrides")
            continue
        got_id = _text_id(e["id"])
        kg = _text_key("d2r:" + key)
        ok = got_id is not None and got_id.same(exp) and (kg is None or (kg.result == 0 and kg.same(exp)))
        alt = ""
        if src.ns_keys is not None:  # a namespaced mod keeps an unqualified record in its own namespace: is Median's text there?
            ag = _text_key(STR_NS + ":" + key)
            alt = "; %s:%s -> %s" % (STR_NS, key, ag.show() if ag and ag.result == 0 else "absent")
        record("D2R overrides", "%s (id %d)" % (key, e["id"]), ok, exp, got_id.show() if got_id else None,
               "key d2r:%s -> %s%s" % (key, kg.show() if kg else None, alt))

    # ---- bug #7: the first-talk line of each town NPC is its own string, by D2R's numeric id
    other = {}  # (length, hash) of every Median-only text -> key: a first-talk line that is one of them is the bug
    for k, t in src.texts.items():
        if k not in src.d2r:
            other[(len(_str_norm(t).encode("utf-8")), _fnv(_str_norm(t)))] = k
    for key in STR_INTRO:
        e = src.d2r.get(key)
        exp = src.expected(key)
        got = _text_id(e["id"]) if e else None
        stray = other.get(got.sig()) if got is not None and not got.same(exp) else None
        record("first talk (bug #7)", "%s (id %s)" % (key, e["id"] if e else "?"), got is not None and got.same(exp) and stray is None,
               exp[:60], got.show()[:60] if got else None, ("shows another Median string: %s" % stray) if stray else "")

    # ---- NPC menu lines: the strings the menu record points at, resolved by the client
    goto(1)
    close_menu()
    H("stat player 7 set 20000000")
    H("stat player 6 set 20000000")
    H("quest 1 0 set")   # Den of Evil done: Akara's respec line is open
    H("quest 41 1 set")
    H("quest 4 0 set")   # Search for Cain done: Cain's page has an open entry
    H("remove box 10")   # the cube entry shows only while the player lacks the cube
    for npc_class, who, plugin_keys, native_keys in STR_MENUS:
        menu = npc_menu(npc_class)
        if not menu:
            record("NPC menus", who, False, None, None, "the menu did not open")
            continue
        t0 = time.time()
        while time.time() - t0 < 120 and not any("handler=plugin" in x for x in menu):  # the plugin line is added when the speech ends
            time.sleep(1)
            ok0, again = g.hcmd("menu list", 5)
            menu = again if ok0 else menu
        lines = []
        for x in menu:
            m = re.search(r"line (\d+) string=0x([0-9a-f]+) handler=(\w+)", x)
            if m:
                lines.append((int(m.group(1)), int(m.group(2), 16), m.group(3) == "plugin"))
        plug = [ln for ln in lines if ln[2]]
        nat = [ln for ln in lines if not ln[2]]
        for (idx, sid, _p), pk in zip(plug, plugin_keys + [None] * len(plug)):
            exp = src.expected(pk) if pk else None
            got = _text_id(sid)
            record("NPC menus", "%s line %d (%s)" % (who, idx, pk), pk is not None and got is not None and got.same(exp), exp,
                   got.show() if got else None, "string id 0x%x" % sid)
        if len(plug) < len(plugin_keys):
            record("NPC menus", "%s plugin lines" % who, False, plugin_keys, [ln[1] for ln in plug], "fewer plugin lines than expected")
        for (idx, sid, _p), nk in zip(nat, native_keys):
            exp = src.expected(nk)
            if exp is None:
                exp = _str_norm(src.d2r[nk].get("enUS", ""))
            got = _text_id(sid)
            record("NPC menus", "%s line %d (%s)" % (who, idx, nk), got is not None and got.same(exp), exp,
                   got.show() if got else None, "string id 0x%x" % sid)
        if npc_class == 265 and plug:  # Cain's submenu: the click rebuilds the list with the open crafting materials
            before = menu
            ok, _l = H("menu %d" % plug[0][0])
            time.sleep(2.5)
            ok2, after = g.hcmd("menu list", 5)
            sub = []
            for x in (after or []):
                m = re.search(r"line (\d+) string=0x([0-9a-f]+)", x)
                if m:
                    sub.append((int(m.group(1)), int(m.group(2), 16)))
            allowed = {(len(src.expected(k).encode("utf-8")), _fnv(src.expected(k))): k for k in STR_CAIN_SUB}
            if not (ok and ok2 and sub and after != before):
                record("NPC menus", "Cain submenu", False, sorted(allowed.values()), None, "the click did not open a submenu: %s" % (after,))
            for idx, sid in sub:
                got = _text_id(sid)
                record("NPC menus", "Cain submenu line %d" % idx, got is not None and got.sig() in allowed, sorted(allowed.values()),
                       got.show() if got else None, "string id 0x%x" % sid)
        close_menu()

    # ---- report
    p = os.path.join(g.current_run(), "strings.tsv")
    with open(p, "w", encoding="utf-8") as f:
        f.write("category\tsample\tresult\texpected\tgot\tnote\n")
        for r in rows_out:
            f.write("\t".join(str(x).replace("\t", " ").replace("\n", "\\n") for x in r) + "\n")
    cats = []
    for r in rows_out:
        if r[0] not in cats:
            cats.append(r[0])
    bad = [r for r in rows_out if r[2] == "FAIL"]
    for c in cats:
        mine = [r for r in rows_out if r[0] == c]
        fails = [r for r in mine if r[2] == "FAIL"]
        add("Strings: " + c, "PASS" if not fails else "FAIL",
            "%d of %d samples read back as Median's text%s" % (len(mine) - len(fails), len(mine),
                                                              "; FAIL: " + "; ".join("%s got [%s]" % (r[1], str(r[4])[:50]) for r in fails[:6]) if fails else ""),
            log=["%s | %s | expected [%s] got [%s] %s" % (r[2], r[1], r[3], r[4], r[5]) for r in mine])
    add(name, "PASS" if rows_out and not bad else "FAIL",
        "%d samples, %d FAIL; namespace file %s; offsets %s; full list in %s" % (
            len(rows_out), len(bad), "present" if src.ns_keys is not None else "absent (native strings)",
            {k: hex(v) for k, v in ROW_FIELDS.items()}, os.path.basename(p)))



def check_goblinname():
    name = "Goblin notice names the variant (monstats NameStr, not the generic goblin)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    want = {2791: "Treasure Goblin", 2792: "Shiny Hoarder", 2793: "Voided Horror", 2799: "Relic Thief", 2802: "Rift Inspector"}
    got, bad = [], []
    for cls, text in want.items():
        ok, first, lines = hv("goblinsay %d" % cls)
        got.append("%d: %s" % (cls, first))
        if not ok or (text + " appears!") not in first:
            bad.append("class %d wanted '%s appears!', got %s" % (cls, text, lines[:2]))
    add(name, "PASS" if not bad else "FAIL",
        "`h goblinsay <class>` (the line a spawn sends): %s" % "; ".join(got) if not bad else "; ".join(bad), [], got)

def reward_stats():
    """`h rewardsnd` (client side): (hook on, sounds played, last message, last sound row)."""
    ok, first, _l = hv("rewardsnd")
    m = re.search(r"hook=(\d) played=(\d+) last message=0x([0-9a-f]+) sound=(\d+)", first)
    return (int(m.group(1)) == 1, int(m.group(2)), int(m.group(3), 16), int(m.group(4))) if m else None

def check_rewardsound():
    name = "Quest reward messages 0x5f-0x61: the client plays Median's sound (item_gem / shrine stand-in / item_lightarmor)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    s0 = reward_stats()
    if s0 is None or not s0[0]:
        return add(name, "FAIL", "client hook for packet 0x2c is off: %s" % (s0,))
    rows, bad = [], []
    played = s0[1]
    for msg, sound in ((0x5f, 217), (0x60, 2608), (0x61, 230)):
        ok, first, _l = hv("rewardmsg 0x%x" % msg)
        s1 = None
        for _ in range(12):  # the pending message goes out at the unit's next update
            time.sleep(0.5)
            s1 = reward_stats()
            if s1 and s1[1] > played:
                break
        rows.append("0x%x -> %s" % (msg, s1))
        if not ok or not s1 or s1[1] != played + 1 or s1[2] != msg or s1[3] != sound:
            bad.append("message 0x%x wanted sound %d with one more sound than %d, got %s (%s)" % (msg, sound, played, s1, first))
        elif s1:
            played = s1[1]
    add(name, "PASS" if not bad else "FAIL",
        "`h rewardmsg` through D2R's unit message, `h rewardsnd` on the client (hook, played, last message, sound row): %s"
        % ("; ".join(rows) if not bad else "; ".join(bad)), [], rows)

def _statorder():
    """`statorder` -> (ordered, rebuilds, rows) with rows a list of ("L", label) / ("S", D2R stat id)."""
    ok, lines = H("statorder")
    head = next((l for l in lines if l.startswith("statorder ordered=")), "")
    m = re.search(r"ordered=(\d) rebuilds=(\d+) rows=(\d+)", head)
    if not ok or not m:
        return None
    rows = []
    for l in lines:
        if l.startswith("statorder rows "):
            rows += [("L", a) if a else ("S", int(b)) for a, b in re.findall(r"\[([^\]]+)\]|(-?\d+)", l[len("statorder rows "):])]
    return int(m.group(1)) == 1, int(m.group(2)), rows

def check_statorder():
    name = "Stat pages: the Advanced Stats list in Median's order, under its section labels (stat_columns.cpp, mxl_statorder.h)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    import stat_screen
    goto(1)
    close_menu()
    ids = stat_screen.stat_ids()
    names = stat_screen.stat_names()
    expected = [("L", h) if h is not None else ("S", ids[s]) for h, s in stat_screen.section_order()]
    always = {ids[names.index(n)] for n in stat_screen.ALWAYS_LISTED}
    probe = 143  # item_absorbfire: under "Damage Absorb", listed while it is not 0
    bad, shots = [], []
    try:
        H("statorder close")
        time.sleep(1)
        rc = H("statorder open")
        time.sleep(2)
        rc = (rc[0], rc[1] + H("statorder adv")[1])
        time.sleep(3)
        first = _statorder()
        if first is None:
            return add(name, "FAIL", "`statorder` gave no answer")
        ordered, rebuilds, rows = first
        shots.append(sh("statorder_open"))
        if not ordered:
            bad.append("the rebuild hook is off (D2R's order)")
        if rebuilds < 1:
            bad.append("the panel built no list (open: %s)" % (rc[1][:3],))
        # the built list is a subsequence of Median's order: labels in sheet order, stats under their own label
        it = iter(expected)
        if not all(any(x == e for e in it) for x in rows):
            bad.append("rows are not in Median's order: %s" % (rows[:12],))
        labels = [r[1] for r in rows if r[0] == "L"]
        if not labels or labels[0] != "General":
            bad.append("the list does not start with the General label: %s" % (labels[:3],))
        missing = sorted(always - {r[1] for r in rows if r[0] == "S"})
        if missing:
            bad.append("always listed boxes missing: %s" % (missing,))
        if ("S", probe) in rows:
            bad.append("stat %d is listed at 0" % probe)
        # a stat that becomes non-zero makes the update rebuild the list: the stat appears, under Damage Absorb
        n0 = rebuilds
        ok_eq, l_eq = H("cstat %d set 25" % probe)  # the client's own copy: a stat set on the server's unit does not reach it
        time.sleep(3)
        cs = H("cstat %d" % probe)[1][:1]
        second = _statorder()
        if not ok_eq:
            bad.append("cstat %d set 25 failed: %s" % (probe, l_eq[:1]))
        elif second is None or ("S", probe) not in second[2]:
            bad.append("stat %d does not appear when the client copy has it (client reads %s)" % (probe, cs))
        else:
            if second[1] <= n0:
                bad.append("no rebuild after the stat changed (%d -> %d)" % (n0, second[1]))
            below = [r for r in second[2][:second[2].index(("S", probe))] if r[0] == "L"]
            if not below or below[-1][1] != "Damage Absorb":
                bad.append("stat %d is not under Damage Absorb (%s)" % (probe, below[-1:] if below else None))
            shots.append(sh("statorder_stat"))
        H("cstat %d set 0" % probe)
        time.sleep(3)
        third = _statorder()
        if third is None or ("S", probe) in third[2]:
            bad.append("stat %d stays listed after it went back to 0" % probe)
        add(name, "PASS" if not bad else "FAIL",
            "sheet opened with `statorder open`, Advanced Stats button pressed with `statorder adv`: %d rows, %d rebuilds, labels %s; "
            "%s. The grouping on the sheet is read from the screenshots (statorder_open.png, statorder_stat.png)" % (
                len(rows), rebuilds, labels, "; ".join(bad) if bad else "order = Median's, always boxes listed, a changed stat rebuilds the list"),
            shots, [str(r) for r in rows[:40]])
    finally:
        H("cstat %d set 0" % probe)
        H("statorder close")

def check_infopanel():
    name = "Infographic help panel: right click on the catalyst of disenchantment opens it (info_panels.cpp, plugin message 'I')"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(1)
    close_menu()
    H("remove 3o[ 5")
    ok, made = H("item 3o[ inv")
    if not ok:
        return add(name, "BLOCKED", "could not create the catalyst (3o[): %s" % made)
    H("infopanel close 0")
    time.sleep(0.5)
    _, before = H("infopanel")
    ok, used = H("use 3o[")
    st, last, shots = None, "", []
    for _ in range(10):  # the open request crosses the hidden chat packet and the UI thread queue
        time.sleep(1)
        _, lines = H("infopanel")
        last = lines[0] if lines else ""
        m = re.search(r"state0=(-?\d+) .*opened=(\d+) failed=(\d+)", last)
        if m and int(m.group(1)) == 2:
            st = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
            break
    time.sleep(1)
    shots.append(sh("infopanel_open"))
    _, closed = H("infopanel close 0")
    time.sleep(1)
    _, after = H("infopanel")
    state_after = re.search(r"state0=(-?\d+)", after[0] if after else "")
    _, held = H("state")
    kept = any("3o[" in ln for ln in held)
    H("remove 3o[ 5")
    good = ok and st is not None and st[1] >= 1 and st[2] == 0 and state_after and int(state_after.group(1)) == 1 and kept
    add(name, "PASS" if good else "FAIL",
        "`use 3o[`: %s; panel state after the right click %s (2 = open), `infopanel` before: %s, after close: %s; the catalyst is kept: %s. "
        "The text is read from the screenshot (infopanel_open.png)" % (used[0] if used else "-", last, before[0] if before else "-",
                                                                         after[0] if after else "-", kept), shots, made + used)



SPRINT_DIRS = [(0, 30), (30, 0), (0, -30), (-30, 0), (22, 22), (-22, -22), (22, -22), (-22, 22)]  # `move` offsets in `state` units; the Blood Moor start is cramped, so only short paths are accepted


STAMINA_POINT = 256  # stamina is stored <<8


def _charstats_row(cls):
    """Median's charstats row of the player class from the built tables (stamina, WalkVelocity, RunVelocity, RunDrain)."""
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "mod", "data", "global", "excel", "charstats.txt")
    with open(p, encoding="utf-8", errors="replace") as f:
        rows = [ln.rstrip("\n").split("\t") for ln in f]
    head = rows[0]
    row = rows[1 + cls]
    return {c: row[head.index(c)] for c in ("class", "stamina", "WalkVelocity", "RunVelocity", "RunDrain")}


def _sample():
    p = hstate()["player"]
    return (p.get("frame", 0), p.get("x", 0), p.get("y", 0), p.get("mode", -1), rd("player", 10) or 0)


def _sprint_leg(stam0, walk, max_s, stop_exhausted=False):
    """Stamina set to stam0, then `move` runs (or walks) along the first free direction of SPRINT_DIRS, re-issued when the
    path ends. Samples (frame, x, y, mode, stamina) until max_s seconds, or until a run has been forced back to walk."""
    setstat("player", 10, stam0)
    rows, di, ran, t0 = [], 0, False, time.time()
    while time.time() - t0 < max_s:
        row = _sample()
        rows.append(row)
        ran = ran or row[3] == 3
        if stop_exhausted and ran and row[3] == 2 and row[4] < STAMINA_POINT * 2:
            break
        if row[3] not in (2, 3):
            for k in range(len(SPRINT_DIRS)):
                dx, dy = SPRINT_DIRS[(di + k) % len(SPRINT_DIRS)]
                H("move %d %d%s" % (row[1] + dx, row[2] + dy, " walk" if walk else ""))
                if _sample()[3] in (2, 3):
                    di = (di + k) % len(SPRINT_DIRS)
                    break
            else:
                di += 1
    return rows


def _pairs(rows, mode):
    return [(a, b) for a, b in zip(rows, rows[1:]) if a[3] == mode and b[3] == mode and b[0] > a[0]]


def _speed(rows, mode):
    """Units per frame over the consecutive samples that are both in this mode; (speed, frames)."""
    pr = _pairs(rows, mode)
    frames = sum(b[0] - a[0] for a, b in pr)
    dist = sum(((b[1] - a[1]) ** 2 + (b[2] - a[2]) ** 2) ** 0.5 for a, b in pr)
    return (dist / frames if frames else 0.0), frames


def _drain(rows):
    """Median stamina lost per frame (raw, <<8) over consecutive run samples."""
    per = sorted((a[4] - b[4]) / (b[0] - a[0]) for a, b in _pairs(rows, 3))
    return per[len(per) // 2] if per else 0.0


def check_sprint():
    name = "Sprint (SK7): run lasts about 5 s on 50 stamina, faster than walking, no cooldown timer, stamina refills while walking"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    fight_ready()
    p0 = hstate()["player"]
    try:
        cs = _charstats_row(p0.get("class", 0))
    except Exception as e:
        return add(name, "BLOCKED", "work/mod charstats.txt not readable: %r" % (e,))
    walk_v, run_v, run_drain = int(cs["WalkVelocity"]), int(cs["RunVelocity"]), int(cs["RunDrain"])
    full = rd("player", 11) or 0
    bad, notes, log = [], [], []

    # 1. a run from full stamina: the drain per frame, the length it gives, the speed
    run = _sprint_leg(full, False, 14, stop_exhausted=True)
    log += ["run  " + " ".join(map(str, r)) for r in run]
    drain = _drain(run)
    secs = full / drain / 25 if drain else 0
    want_drain = run_drain * 2  # D2Game 0x77bb0: RunDrain * 2 per frame, scaled by the stamina drain stat 154
    run_speed, run_frames = _speed(run, 3)
    exhausted = any(r[3] == 2 and r[4] < STAMINA_POINT * 2 for a, r in zip(run, run[1:]) if a[3] == 3)
    notes.append("run: stamina %d.. drains %.0f raw/frame (RunDrain %d x2 = %d) = %.1f s from full, %d frames seen, speed %.3f units/frame" % (
        full >> 8, drain, run_drain, want_drain, secs, run_frames, run_speed))
    if not run_frames or abs(drain - want_drain) > 0.25 * want_drain:
        bad.append("run drain %.0f raw/frame, charstats RunDrain %d wants %d" % (drain, run_drain, want_drain))
    if not 3.0 <= secs <= 6.5:
        bad.append("a run from full lasts %.1f s (Median: about 5 s)" % secs)
    if not exhausted:
        bad.append("the run was never forced back to walking when the stamina ran out")

    # 2. the walk speed against the run speed
    walk = _sprint_leg(full, True, 5)
    log += ["walk " + " ".join(map(str, r)) for r in walk]
    walk_speed, walk_frames = _speed(walk, 2)
    ratio, want_ratio = (run_speed / walk_speed if walk_speed else 0), run_v / walk_v
    notes.append("walk speed %.3f units/frame over %d frames; run/walk %.2f (charstats %d/%d = %.2f)" % (
        walk_speed, walk_frames, ratio, run_v, walk_v, want_ratio))
    if not walk_frames or abs(ratio - want_ratio) > 0.15 * want_ratio:
        bad.append("run/walk speed %.2f, charstats wants %.2f" % (ratio, want_ratio))

    # 3. empty: walking refills the stamina without standing still
    empty = _sprint_leg(0, True, 3.5)
    log += ["empty " + " ".join(map(str, r)) for r in empty]
    gain = [b[4] - a[4] for a, b in _pairs(empty, 2) if a[4] < STAMINA_POINT]
    walked = max((r[4] for r in empty if r[3] == 2), default=0) - (empty[0][4] if empty else 0)
    notes.append("walking from empty: stamina +%d raw over %.1f s, %d steps that started under one point gained" % (
        walked, 3.5, sum(1 for d in gain if d > 0)))
    if walked < STAMINA_POINT or not any(d > 0 for d in gain):
        bad.append("no stamina came back while walking from empty (gain %d)" % walked)

    # 4. no cooldown: one point of stamina is enough for a run to start; refilling takes the stamina's own time
    setstat("player", 10, STAMINA_POINT * 2)
    here = _sample()
    H("move %d %d" % (here[1], here[2] + 30))
    modes = {_sample()[3] for _ in range(2)}
    notes.append("with 2 points of stamina a run starts: modes %s" % sorted(modes))
    if 3 not in modes:
        bad.append("a run did not start with 2 points of stamina (modes %s)" % sorted(modes))
    here = _sample()
    H("move %d %d walk" % (here[1], here[2]))
    time.sleep(1.5)
    setstat("player", 10, 0)
    refill = []
    t0, f0 = time.time(), _sample()[0]
    while time.time() - t0 < 16:
        r = _sample()
        refill.append(r)
        if r[4] >= full:
            break
    log += ["refill " + " ".join(map(str, r)) for r in refill]
    frames_full = (refill[-1][0] - f0) if refill else 0
    notes.append("standing refill 0 -> %d points: %.1f s (%d frames)" % (refill[-1][4] >> 8 if refill else -1, frames_full / 25, frames_full))
    if not refill or refill[-1][4] < full or not 6 <= frames_full / 25 <= 16:
        bad.append("standing refill took %.1f s / reached %s" % (frames_full / 25, refill[-1][4] if refill else None))

    # 5. the key: R (toggle sprint) makes a click-to-move run, R again walks. The toggle is restored.
    setstat("player", 10, full)
    keyrun = {}
    for label in ("on", "off"):
        g.press_bg("r", hold=0.1)
        time.sleep(0.3)
        for fx, fy in ((0.80, 0.50), (0.20, 0.50), (0.50, 0.85), (0.50, 0.15)):  # a click on a wall does not move
            g.click_bg(fx, fy)
            keyrun[label] = _sample()[3]
            if keyrun[label] in (2, 3):
                break
        time.sleep(2.5)
    notes.append("key R: click-to-move mode %s after the first press (3 = run), %s after the second (2 = walk)" % (keyrun["on"], keyrun["off"]))
    if keyrun["on"] != 3 or keyrun["off"] != 2:
        bad.append("toggle key R gave modes %s / %s (want 3 / 2)" % (keyrun["on"], keyrun["off"]))
    setstat("player", 10, full)
    add(name, "PASS" if not bad else "FAIL", "; ".join(notes) if not bad else "; ".join(bad) + " | " + "; ".join(notes),
        [sh("sprint")], log)




# ------------------------------------------------------------------ travel: map reveal, waypoint auto-activation, hotkeys, stash pages

def mapcells():
    """`h mapcells` (client): dict cells / layer / layers / debugflag, or None."""
    ok, lines = H("mapcells")
    for ln in lines:
        if ln.startswith("mapcells "):
            return {k: int(v) for k, v in kv(ln).items() if re.fullmatch(r"-?\d+", v)}
    return None


def check_mapreveal():
    name = "Map reveal: the automap of a fresh level has more cells after the reveal"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    # `cheat goto` (needs debug mode) arrives in a level whose automap is already drawn whole (the pictures show it), and the game's
    # town map is whole from the start, so the cell count and the picture cannot show a reveal on a level reached this way; what
    # the call does is measured in the layers: it adds the layers of the act's levels
    st = goto(3)  # Cold Plains
    if st["player"].get("area") != 3:
        return add(name, "BLOCKED", "could not go to area 3")
    time.sleep(2)
    g.press("tab")  # automap overlay on (D2R's default key)
    time.sleep(1.5)
    a0 = mapcells()
    p0 = g.shot("mapreveal_before")
    ok, lines = H("reveal")
    time.sleep(2.5)
    a1 = mapcells()
    p1 = g.shot("mapreveal")
    ok2, lines2 = H("reveal")  # a second reveal finds nothing new
    time.sleep(1)
    a2 = mapcells()
    g.press("tab")
    log = ["before %s" % a0, "after %s" % a1, "again %s" % a2] + lines + lines2
    if not a0 or not a1:
        return add(name, "BLOCKED", "no automap data: %s" % lines[:2], [], log)
    flag_kept = a1["debugflag"] == a0["debugflag"] == (a2 or a1)["debugflag"]
    cells_grew = a1["cells"] > a0["cells"]
    layers_grew = a1["layers"] > a0["layers"]
    again = (a2 or a1)["layers"] == a1["layers"] and (a2 or a1)["cells"] == a1["cells"]
    status = "PASS" if ok and ok2 and cells_grew and flag_kept and again else              "PARTIAL" if ok and ok2 and layers_grew and flag_kept and again else "FAIL"
    add(name, status,
        "harness `reveal` (the R hotkey's call of the debug menu's Reveal Automap) in Cold Plains: automap cells %d -> %d (%+d), layers %d -> %d, "
        "a second reveal %+d cells %+d layers; debug flag byte %d -> %d (kept: %s)%s" % (
            a0["cells"], a1["cells"], a1["cells"] - a0["cells"], a0["layers"], a1["layers"], (a2 or a1)["cells"] - a1["cells"],
            (a2 or a1)["layers"] - a1["layers"], a0["debugflag"], a1["debugflag"], flag_kept,
            "" if status == "PASS" else "; PARTIAL: the level was already drawn whole on arrival (mapreveal_before.png = mapreveal.png), only the layers grew"),
        [os.path.basename(p0), os.path.basename(p1)], log)


def wpbit(number):
    ok, lines = H("waypoint n %d get" % number)
    for ln in lines:
        m = re.match(r"waypoint number %d = (\d)" % number, ln)
        if m:
            return int(m.group(1))
    return None


def wpobjs():
    """`h wpobj`: (player dict, [object dicts])."""
    ok, lines = H("wpobj")
    player, objs = {}, []
    for ln in lines:
        if ln.startswith("wpobj player"):
            m = re.search(r"at (\d+),(\d+) level=(-?\d+) number=(-?\d+)", ln)
            if m:
                player = dict(x=int(m.group(1)), y=int(m.group(2)), level=int(m.group(3)), number=int(m.group(4)))
        elif ln.startswith("wpobj id="):
            m = re.search(r"id=(\d+) class=(\d+) at (\d+),(\d+) dist=(\d+) level=(-?\d+) inrange=(\d)", ln)
            if m:
                objs.append(dict(id=int(m.group(1)), cls=int(m.group(2)), x=int(m.group(3)), y=int(m.group(4)), dist=int(m.group(5)),
                                 level=int(m.group(6)), inrange=int(m.group(7))))
    return player, objs


def check_wpauto():
    name = "Waypoint auto-activation: walking up to a waypoint (distance < 36) activates it, standing away does not"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    st = goto(3)
    if st["player"].get("area") != 3:
        return add(name, "BLOCKED", "could not go to area 3")
    time.sleep(2)
    player, objs = wpobjs()
    here = [o for o in objs if o["level"] == player.get("level")]
    log = ["player %s" % player] + ["obj %s" % o for o in here]
    if not here or player.get("number", -1) < 0:
        return add(name, "BLOCKED", "no waypoint object of this level is loaded yet (%s)" % log[:2], [], log)
    num = player["number"]
    wp = min(here, key=lambda o: o["dist"])
    res = []
    # A. wherever the character stands: clear the bit and see whether the rule re-sets it, by distance.
    H("waypoint n %d clear" % num)
    time.sleep(1.5)
    bit_a = wpbit(num)
    res.append("A: distance %d, bit after 1.5 s: %s" % (wp["dist"], bit_a))
    near_ok = (bit_a == 1) if wp["dist"] < 36 else (bit_a == 0)
    # B. walk to the waypoint from outside the radius: clear first, expect the bit to come on within the walk.
    far = None
    if wp["dist"] >= 36 and bit_a == 0:
        far = wp["dist"]
    else:
        # step away: try points 55 sub-tiles from the object in eight directions until the character is out of range
        for ox, oy in ((55, 0), (-55, 0), (0, 55), (0, -55), (40, 40), (-40, -40), (40, -40), (-40, 40)):
            H("move %d %d" % (wp["x"] + ox, wp["y"] + oy))
            t0 = time.time()
            while time.time() - t0 < 9:
                time.sleep(1)
                p2, o2 = wpobjs()
                w2 = [o for o in o2 if o["id"] == wp["id"]]
                if w2 and w2[0]["dist"] >= 40:
                    far = w2[0]["dist"]
                    break
            if far is not None:
                break
        H("waypoint n %d clear" % num)
        time.sleep(1.5)
    bit_far = wpbit(num)
    res.append("B: left to distance %s, bit cleared and still %s after 1.5 s (rule must not fire)" % (far, bit_far))
    walked_ok = None
    if far is not None and bit_far == 0:
        H("move %d %d" % (wp["x"], wp["y"]))
        t0 = time.time()
        d_at = None
        while time.time() - t0 < 30:
            time.sleep(1)
            if wpbit(num) == 1:
                p3, o3 = wpobjs()
                w3 = [o for o in o3 if o["id"] == wp["id"]]
                d_at = w3[0]["dist"] if w3 else None
                break
        walked_ok = d_at is not None
        res.append("C: walking to the object set the bit at distance %s" % d_at)
    status = [l for l in H("travel")[1] if l.startswith("travel ")]
    log += res + status
    if far is None or bit_far != 0:
        return add(name, "PARTIAL" if near_ok else "FAIL", "; ".join(res) + " (could not get out of range for the negative control)", [], log)
    add(name, "PASS" if (near_ok and walked_ok) else "FAIL", "waypoint number %d of level %d: %s" % (num, player["level"], "; ".join(res)), [sh("wpauto")], log)


def travel_counts():
    """`h travel` -> dict: x r c presses, reveals, transmutes, blocked, registered bindings text, pickup state, cube state."""
    ok, lines = H("travel")
    line = next((l for l in lines if l.startswith("travel ")), "")
    d = {"line": line}
    m = re.search(r"keys X (\d+) R (\d+) C (\d+)", line)
    if m:
        d.update(x=int(m.group(1)), r=int(m.group(2)), c=int(m.group(3)))
    m = re.search(r"reveals (\d+)", line)
    d["reveals"] = int(m.group(1)) if m else None
    m = re.search(r"transmutes (\d+) \(blocked (\d+), cube (\w+)\)", line)
    if m:
        d.update(transmutes=int(m.group(1)), blocked=int(m.group(2)), cube=m.group(3))
    m = re.search(r"pickup (on|off)", line)
    d["pickup"] = m.group(1) if m else None
    d["bindings"] = line.split("||")[0]
    return d


def check_hotkeys():
    name = "Hotkeys X / R / C: window-message key presses reach the plugin's actions"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    goto(3)
    time.sleep(2)
    t0 = travel_counts()
    if "x" not in t0:
        return add(name, "BLOCKED", "no travel status line: %s" % t0["line"][:100], [], [t0["line"]])
    log = [t0["line"]]
    res, bad = [], []
    # X toggles auto pickup
    g.press("x")
    time.sleep(0.6)
    t1 = travel_counts()
    g.press("x")
    time.sleep(0.6)
    t2 = travel_counts()
    x_ok = t1.get("x") == t0["x"] + 1 and t1["pickup"] != t0["pickup"] and t2.get("x") == t0["x"] + 2 and t2["pickup"] == t0["pickup"]
    res.append("X: presses %s->%s->%s, pickup %s->%s->%s" % (t0.get("x"), t1.get("x"), t2.get("x"), t0["pickup"], t1["pickup"], t2["pickup"]))
    if not x_ok:
        bad.append("X")
    # R reveals the map
    a0 = mapcells()
    g.press("r")
    time.sleep(1.5)
    t3 = travel_counts()
    a1 = mapcells()
    r_ok = t3.get("r") == t2.get("r") + 1 and t3["reveals"] == t2["reveals"] + 1
    res.append("R: presses %s->%s, reveals %s->%s, cells %s->%s" % (t2.get("r"), t3.get("r"), t2["reveals"], t3["reveals"],
                                                                    a0 and a0["cells"], a1 and a1["cells"]))
    if not r_ok:
        bad.append("R")
    # the cube item lies in the inventory; shots of the inventory show where its cell is (for opening the panel)
    okb, lb = H("item box inv")
    log += lb
    mark = mxl_mark()
    g.press("i")
    time.sleep(1.2)
    inv_shot = sh("hotkeys_inventory")
    g.click(0.711, 0.453, "right")  # the cube item's cell in the 1600 x 900 inventory (hotkeys_inventory.png)
    time.sleep(1.5)
    cube_shot = sh("hotkeys_cube")
    msgs = [l for l in mxl_lines_since(mark) if "cube ui message" in l]
    log += msgs[:12]
    t_open = travel_counts()
    g.press("esc")  # one Esc closes the cube panel and the inventory with it (a second one would open the game menu)
    time.sleep(0.6)
    # C with no cube open: counted, not transmuted
    g.press("c")
    time.sleep(0.8)
    t4 = travel_counts()
    c_closed = t4.get("c") == t3.get("c") + 1 and t4["transmutes"] == t3["transmutes"]
    res.append("C (cube closed): presses %s->%s, transmutes %s->%s (blocked %s)" % (t3.get("c"), t4.get("c"), t3["transmutes"], t4["transmutes"], t4.get("blocked")))
    if not c_closed:
        bad.append("C closed")
    log += [t1["line"], t2["line"], t3["line"], t4["line"], t0["bindings"]]
    open_note = "C with the cube open: not driven (the plugin did not see the cube panel open; ui messages: %s)" % (msgs[:3] or "none")
    if t_open.get("cube") == "open":
        g.press("i")  # open it again for the key
        time.sleep(1.0)
        g.click(0.711, 0.453, "right")
        time.sleep(1.2)
        t5 = travel_counts()
        g.press("c")
        time.sleep(1.0)
        t6 = travel_counts()
        c_open = t6.get("transmutes") == t5.get("transmutes", 0) + 1 and t6.get("c") == t5.get("c") + 1
        log += [t_open["line"], t5["line"], t6["line"]]
        open_note = "C with the cube open: presses %s->%s, transmutes %s->%s (cube %s; ui message %s)" % (
            t5.get("c"), t6.get("c"), t5.get("transmutes"), t6.get("transmutes"), t5.get("cube"), msgs[:1])
        if not c_open:
            bad.append("C open")
        g.press("esc")
        time.sleep(0.5)
    add(name, "PASS" if not bad else "FAIL", "; ".join(res) + "; " + open_note + ("; failed: %s" % bad if bad else ""), [sh("hotkeys"), inv_shot, cube_shot], log)


def check_stashpages():
    name = "Stash pages: ten shared stash tabs exist (d2rloader.toml add_shared_tabs)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    res, log, wrong = [], [], []
    for tab in list(range(0, 11)) + [12]:
        ok, lines = H("stashprobe %d" % tab)
        if not ok:
            return add(name, "BLOCKED", "stashprobe refused: %s" % lines[:2], [], lines)
        out = ""
        for _ in range(20):
            time.sleep(0.4)
            ok2, l2 = H("stashprobe result")
            out = next((l for l in l2 if l.startswith("stashprobe result")), "")
            if " -1 " not in out:
                break
        log.append("tab %d: %s" % (tab, out))
        made = " 1 page=" in out
        should = tab < 10
        res.append("%d%s" % (tab, "+" if made else "-"))
        if made != should:
            wrong.append("tab %d %s" % (tab, "accepted an item" if made else "refused (%s)" % out[:60]))
    add(name, "PASS" if not wrong else "FAIL",
        "an item created in shared stash tab n through ItemService (+ accepted, - refused): %s; expected tabs 0-9 accepted and 10, 12 refused%s"
        % (" ".join(res), ("; wrong: " + "; ".join(wrong)) if wrong else ""), [], log)


CHECKS = [("mxl", check_mxl),("lockclock", check_lockout_clock), ("goblins", check_goblins), ("respec", check_respec),
          ("cain", check_cain), ("cainsub", check_cain_submenu), ("heart", check_heart), ("catalyst", check_catalyst), ("tower", check_tower), ("lockout", check_lockout), ("lockquest", check_lockquest),
          # reenter before cow: the cow check leaves a Cow Level portal in town that `enter nearest` would take
          ("reenter", check_dungeon_reenter), ("cow", check_cow), ("goldcraft", check_goldcraft), ("craftflags", check_craftflags),
          ("areacycle", check_areacycle), ("mapmem", check_mapmem), ("casts", check_casts), ("itemevents", check_item_events), ("medianai", check_median_ai), ("medianai_ctl", check_median_ai_controlled), ("tyrael", check_tyrael),
          # the verify rows of docs/unported.md
          ("resistcap", check_resistcap), ("absorb", check_absorb), ("damagered", check_damagered), ("avoidcaps", check_avoidcaps), ("classlock", check_classlock),
          ("hiregates", check_hiregates), ("itemrolls", check_itemrolls), ("dye", check_dye), ("goldclient", check_goldclient),
          ("bonusdrops", check_bonusdrops), ("summoner", check_summoner), ("tooltip", check_tooltip), ("gameend", check_gameend),
          ("trophy", check_trophy), ("statcols", check_statcols),
          # the old "fix to test" rows of docs/bugs.md
          ("skilltree", check_skilltree), ("keybinds", check_keybinds), ("npcstrings", check_npcstrings), ("automap", check_automap),
          ("scrollportals", check_scrollportals), ("vendors", check_vendors), ("monhover", check_monster_hover),
          ("levelcap", check_levelcap),
          ("skillcaps", check_skillcaps),
          ("townportal", check_townportal),
          ("potions", check_potions),
          ("itemskills", check_itemskills),
          ("cowportals", check_cowportals),
          ("deathcrash", check_deathcrash),
          ("nocorpse", check_nocorpse),
          ("nslong", check_nslong),
          ("strings", check_strings),
          ("goblinname", check_goblinname), ("rewardsound", check_rewardsound), ("statorder", check_statorder), ("infopanel", check_infopanel),
          ("sprint", check_sprint), ("steadyaim", check_steadyaim), ("heat", check_heat),
          ("trophytimer", check_trophytimer),
          # travel: map reveal, waypoint auto-activation, hotkeys, stash pages
          ("mapreveal", check_mapreveal), ("wpauto", check_wpauto), ("hotkeys", check_hotkeys), ("stashpages", check_stashpages)]



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
        for name in [g.CHAR_NAME] if os.path.exists(os.path.join(SAVES, g.CHAR_NAME + ".d2s")) else []:  # the test character only, never other saves
            patch_quests(name, {(0, 1): 0, (0, 4): 2, (0, 5): 0, (0, 6): 0, (0, 41): 0, (0, 47): 0,
                                (0, 13): 0, (1, 13): 0, (0, 25): 0, (1, 25): 0})
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

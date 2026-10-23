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
craftflags areacycle mapmem casts itemevents medianai medianai_ctl tyrael resistcap absorb damagered avoidcaps
classlock hiregates itemrolls allqualsockets dye goldclient bonusdrops summoner tooltip gameend trophy statcols
skilltree keybinds npcstrings automap scrollportals vendors monhover levelcap skillcaps townportal potions itemskills
cowportals cubeoutputs deathcrash nocorpse cooldowns collidetypes skillstarts activationfreq nslong orbtips strings goblinname rewardsound statorder infopanel sprint steadyaim restrictitems
heat trophytimer mapreveal wpauto hotkeys stashpages stashpage2 skillscaling nodurability nodurabilityui autoid townmana mercrevive questxp
questrewards gamble enginegaps monlevel normaltc leash bufficons.
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


def player_dead(p):
    """The server's player unit is dead: mode 0 / 17, or life 0."""
    life = str(p.get("life", "1")).split("/")[0]
    return p.get("mode") in (0, 17) or life == "0"


def revive_if_dead(timeout=20):
    """A dead test character (player mode 0 / 17) closes the game on `cheat goto` and ignores hits, menus and hiring. Returns True
    when it was dead.
    Two kinds of death. A real one (life 0, the client shows the death state): Esc is the respawn request (the plugin holds it
    back for 5 s), pressed every 3 s. A harness-made one: a check that sets the life stat (`stat player 6`) after the character
    died leaves the server unit in mode 17 with life above 0 while the client believes the character is alive (a screenshot shows
    it standing, Esc opens the pause menu instead and `state` stops answering for 15 s); no respawn request can come from the
    client then. Save and Exit + Play loads the character in the town, which is what a respawn does."""
    p = hstate()["player"]
    if not p or not player_dead(p):
        return False
    life = str(p.get("life", "0")).split("/")[0]
    g.log("the test character is dead (mode %s, life %s)" % (p.get("mode"), life))
    if life == "0":
        t0 = time.time()
        while time.time() - t0 < timeout:
            g.press("esc")
            time.sleep(3)
            st = hstate()
            if not st["player"]:  # `state` does not answer: Esc paused the game (the character was not dead for the client)
                break
            if not player_dead(st["player"]):
                return True
    p = hstate()["player"]
    if p and not player_dead(p):
        return True
    g.log("no respawn request is possible (the client does not know the death): Save and Exit + Play")
    leave_and_reenter(menu_open=not hstate()["player"])  # `state` silent: the pause menu is already open
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


def npc_menu(npc_class, spawn=True, attempts=4):
    """Stand next to an NPC (spawn it when it is not loaded), talk, wait for the menu. Returns the menu lines.
    The menu opens when the NPC's speech has played, and the click that starts the speech can be lost (the first talk of a long
    speech, an NPC that is still walking, an earlier menu still open): every attempt closes what is open, talks again and waits
    a shorter time; a dead character (it cannot talk) is revived first."""
    revive_if_dead()
    for attempt in range(attempts):
        close_menu()
        ok, lines = H("interact %d" % npc_class)  # the town's own NPC: its speech ends with the menu
        if not ok and spawn:
            H("spawn %d" % npc_class)
            time.sleep(1)
            ok, lines = H("interact %d" % npc_class)
        if not ok:
            time.sleep(2)
            continue
        menu = wait_menu(30 if attempt == 0 else 15)
        if menu is not None:
            return menu
    return None


def npc_within(npc_class, reach=6):
    """True when an NPC of that class stands within `reach` of the character (`h state` lists the units around it)."""
    near = [m for m in hstate()["mons"] if m.get("class") == npc_class]
    return bool(near) and near[0].get("dist", 99) <= reach


def npc_menu_near(npc_class, attempts=3):
    """npc_menu() for NPCs that stand far from the town's arrival point (Gheed, Charsi, Akara: 70+ units, the walk of an `interact`
    can take longer than the menu wait or end at a wall): walk next to the NPC first (`moveto`, repeated), a copy of the NPC next
    to the character when the walk does not get there, then talk; when the menu still does not open the whole round is repeated
    with a copy (the client has to learn a new unit before it can talk to it). Returns the menu lines or None."""
    revive_if_dead()
    if hstate()["player"].get("area") != 1:
        goto(1)
    close_menu()
    for attempt in range(attempts):
        for _ in range(5):
            ok, _l = H("moveto nearest %d" % npc_class)
            if not ok:
                break  # not loaded
            time.sleep(4)
            if npc_within(npc_class):
                break
        if not npc_within(npc_class) or attempt > 0:
            H("spawn %d" % npc_class)  # out of reach (behind a wall) or not loaded: a copy next to the character
            time.sleep(4)
        menu = npc_menu(npc_class, attempts=2)
        if menu is not None:
            return menu
    return None


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
    base_stats = [stat_of(H("stat player %d" % i)[1]) for i in range(4)]  # strength, energy, dexterity, vitality before the respec
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
    unused0 = stat_of(H("stat player 4")[1])
    ok, ml2 = H("menu %d" % idx)
    time.sleep(1.5)
    _, c2 = H("menu confirm")
    ok, yes_out = H("menu yes")
    time.sleep(1)
    _, after0 = H("stat player 0")
    _, after4 = H("stat player 4")
    after_stats = [stat_of(H("stat player %d" % i)[1]) for i in range(4)]
    lines = [ln for ln in mxl_lines_since(m) if "npc actions" in ln]
    s0 = stat_of(after0)
    s4 = stat_of(after4)
    _, q = H("quest 41 dump")
    done = any("done" in ln for ln in lines)
    # Every base stat goes back to the class start (Barbarian: strength 30, ...) and every point spent comes back as unused points:
    # the refund is the sum over all four stats, not strength alone (a character that has spent points on dexterity, vitality and
    # energy gets those back too). Item bonuses sit on both sides of a stat's difference, so before - after is the base refund.
    refunded = sum(b - a for b, a in zip(base_stats, after_stats)) if None not in base_stats + after_stats else None
    asked2 = any("open" in ln for ln in c2)
    add(name, "PASS" if (asked and declined and asked2 and ok and done and s0 is not None and s0 < spent and refunded is not None
                         and refunded >= 60 and s4 == unused0 + refunded) else "FAIL",
        "menu lines %s; the click opens the Yes / No page (%s) and No leaves strength at %s (%s); after Yes strength %s -> %s "
        "(Barbarian start 30), stats 0-3 %s -> %s, unused stat points %s -> %s (%s refunded over the four stats); log `%s`; quest 41 %s" % (
            [ln.strip() for ln in menu[1:]], asked, spent, declined, spent, s0, base_stats, after_stats, unused0, s4, refunded,
            lines[-1][-60:] if lines else "none", q[0]),
        [shot, confirm_shot], menu + ml + c1 + no_out + ml2 + c2 + yes_out + lines)
    close_menu()
    for i, v in enumerate(base_stats):  # the respec reset every stat and raised the unused points: the saved test character keeps its numbers
        if v is not None:
            H("stat player %d set %d" % (i, v))
    H("stat player 4 set %d" % base4)


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


STEADY_AIM_STATE = 558  # states.txt sk1296_state: Hunter's Steady Aim (Heartseeker needs it, restrict 5)


def aim_clean():
    """No Steady Aim left over: the counter (stat 220 layer 5) and its state. The earlier check's last cast of Heartseeker may not
    have spent the counter, and a leftover state makes Heartseeker usable when a check expects it restricted."""
    H("stat player 220 5 set 0")
    H("setstate player %d 0" % STEADY_AIM_STATE)


def tough_target(cls):
    """A spawned monster that survives a long bow fight: its life and maximum life set to the engine's top. Returns its unit id. The
    shots of a level 150 character kill an ordinary class 3 monster with the first arrow, and the later casts then find only the
    town NPC next to the arrival point (`cast ... nearest` takes the nearest monster of any kind)."""
    mid = spawn_id(cls)
    if mid is not None:
        setstat("mon:%d" % mid, 7, BIG_LIFE)
        setstat("mon:%d" % mid, 6, BIG_LIFE)
    return mid


def check_steadyaim():
    """Hunter's Steady Aim (docs/median-rules-audit.md SK10): Steady Shot (D2R 1368) builds stat 220 layer 5 on state
    sk1296_state one per shot up to the cap; Heartseeker (352, restrict 5: needs the state) is refused without it
    (plugin/src/skill_restrict.cpp), spends one charge per cast and drops the state at the last one."""
    nm = "Steady Aim: Steady Shot builds the counter, Heartseeker needs it and spends it (restrict 5)"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    goto(2)  # Blood Moor
    kill_all_near()
    H("packclear")  # an unequip into a full backpack fails and the bow never gets into the hand
    st0 = hstate()["player"]
    cls0 = st0.get("class")
    log = []
    for cmd in ("class 5", "stat player 0 set 400", "stat player 2 set 400", "unequip rhand", "equip 163 rhand",
                "unequip lhand", "equip aqv lhand", "setskill 1368 5", "setskill 352 5"):
        ok, lines = H(cmd)
        log += lines
    counter = lambda: stat_of(H("stat player 220 5")[1])
    aim_clean()
    before = skill_usable(352)
    first = counter()
    tough_target(3)
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
    aim_clean()
    if cls0 is not None:
        H("class %d" % cls0)
    good = (before == 4 and first == 0 and seen == [1, 2, 3, 4] and during == 0 and after_one is not None
            and 0 < after_one < seen[-1] + 1 and end == 0 and after in (1, 4))  # 1 = Heartseeker's cooldown answers before the restriction
    add(nm, "PASS" if good else "FAIL",
        "Heartseeker usability (4 = restricted) before the first shot %s, counter %s; after Steady Shot x4 the counter reads %s "
        "and Heartseeker %s; one Heartseeker leaves %s; spent to %s and Heartseeker %s (4 restricted, or 1 while its cooldown runs)" % (
            before, first, seen, during, after_one, end, after), (), log)


def check_restrictitems():
    """Restrict modes 3-7 on item-triggered skills (D2Sigma applies them on the player's cast path only: its restrict test
    0x6fd9f590 is called from D2Common 10787 alone, and the item event path 0x6fd114f0 -> 0x6fcc19a0 never reaches it; D2R's twin
    0x33f6a0 on the item path knows modes 0 to 2 only, so the port leaves modes 3 to 7 unrestricted there, as Median does).
    Heartseeker (352, restrict 5, needs Steady Aim's state) is refused for the player's own cast without the state (usable 4),
    yet an item with a "cast on kill" of it (stat 376, the item event registry's function 20 -> 0x5896e0 -> 0x589930) casts it
    both without and with the state: the handler is reached with the skill and its missile appears."""
    nm = "Restrict modes 3-7 on item-triggered skills: the cast path refuses Heartseeker, an item's cast on kill does not (as Median)"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    goto(2)  # Blood Moor
    kill_all_near()
    H("packclear")  # an unequip into a full backpack fails and the bow never gets into the hand
    st0 = hstate()["player"]
    cls0 = st0.get("class")
    log, notes = [], []
    for cmd in ("class 5", "stat player 0 set 400", "stat player 2 set 400", "unequip rhand", "equip 163 rhand",
                "unequip lhand", "equip aqv lhand", "setskill 1368 5", "setskill 352 5", "unequip lring"):
        log += H(cmd)[1]
    counter = lambda: stat_of(H("stat player 220 5")[1])

    def proc():
        """One cast on kill of Heartseeker level 5 from a ring: (handler reached, missiles seen)."""
        H("unequip lring")
        H("equip rin 376:%d=100 lring" % (352 * 64 + 5))
        m = spawn_id()
        time.sleep(2)  # a monster that was just spawned is not a valid target yet
        H("missiles reset")
        H("evtrace fire 9 player m%d" % m)
        _o, lm = H("missiles")
        _o2, lk = H("evtrace calls")
        log.extend(lm + lk)
        last = re.search(r"last cast on kill: skill (-?\d+) level (-?\d+)", " ".join(lk))
        seen = re.search(r"seen=(\d+)", " ".join(lm))
        H("kill nearest")
        H("unequip lring")
        return (last is not None and (int(last.group(1)), int(last.group(2))) == (352, 5)), int(seen.group(1)) if seen else None

    aim_clean()
    usable0 = skill_usable(352)
    counter0 = counter()
    reached0, seen0 = proc()
    notes.append("without Steady Aim (counter %s): player cast usable code %s (4 = restricted); item cast on kill reached %s, missiles seen %s" % (
        counter0, usable0, reached0, seen0))
    tough_target(3)
    for _ in range(4):
        H("cast 1368 nearest")
        time.sleep(2.5)
    counter1 = counter()
    usable1 = skill_usable(352)
    reached1, seen1 = proc()
    notes.append("with Steady Aim (counter %s): player cast usable code %s; item cast on kill reached %s, missiles seen %s" % (
        counter1, usable1, reached1, seen1))
    for _ in range(6):  # spend the counter as the steadyaim check does
        if not counter():
            break
        H("cast 352 nearest")
        time.sleep(2.5)
    kill_all_near()
    aim_clean()
    if cls0 is not None:
        H("class %d" % cls0)
    # the counter only has to exist for the restriction to lift (Steady Shot's own count 1..4 is the steadyaim check's subject: a weak
    # target can die after the first shot and the later shots find no enemy)
    good = (usable0 == 4 and not counter0 and reached0 and seen0 and counter1 and counter1 >= 1 and usable1 == 0 and reached1 and seen1)
    add(nm, "PASS" if good else "FAIL", " | ".join(notes), (), log)


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
    # a full bar: the drain of one tick is a share of the MAXIMUM mana (heat_logic.h: base = max mana / regeneration frames, times
    # the rate), so a fixed 25600 (100 points) on the level 150 character of slot 1 (6300 mana) was gone within one tick
    maxmana = rd("player", 9) or 0  # raw (x256), the unit of stat 8
    full = max(25600, maxmana)
    H("stat player 8 set %d" % full)
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
    H("stat player 8 set %d" % full)
    if cls0 is not None:
        H("class %d" % cls0)
    good = (rate is not None and rate > 0 and top is not None and top >= full // 2 and mid is not None and mid < top and low is not None and low < mid
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
    H("packclear")  # room for the cube and the cube items (a full backpack makes `item` fail with "placing failed")
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
    n1 = "Cube craft flag (player): the character class's setter row (6369 + 19 per class) sets it, row 6350 is offered afterwards"
    n2 = "Cube craft flag (game): dungeon key row 7102 sets game flag 50, a second try is refused"
    if not need_game():
        add(n1, "BLOCKED", "not in a game")
        return add(n2, "BLOCKED", "not in a game")
    goto(1)
    ensure_cube()
    H("cubeclear")
    # the setter recipes are class-split (rows 6369 assassin, 6388 paladin, 6407 druid, 6426 necromancer, 6445 sorceress, 6464
    # barbarian, 6483 amazon; flag 1 is the first row of each group): the character's own class picks its row. The check used to
    # switch the character to an assassin (`class 6`), but then the game refuses to place any item in the cube ("placing failed",
    # seen on the level 34 test character of slot 3), so no recipe matched
    cls0 = hstate()["player"]["class"]
    names = {6: ("assassin", 6369), 3: ("paladin", 6388), 5: ("druid", 6407), 2: ("necromancer", 6426),
             1: ("sorceress", 6445), 4: ("barbarian", 6464), 0: ("amazon", 6483)}
    cname, setter = names.get(cls0, ("class %d" % cls0, 6369))
    H("packclear")
    ok0, lp = H("item voXX cube uniq=55")
    try:
        H("stat player 378 set 214100")
        _, f0 = H("flag player 1 get")
        ok1, l1 = H("cubecraft")
        _, f1 = H("flag player 1 get")
        ok2, l2 = H("cubecraft")
    finally:
        H("stat player 378 set 0")
    H("cubeclear")
    r1 = re.search(r"recipe (\d+)", l1[0]) if l1 else None
    r2 = re.search(r"recipe (\d+)", l2[0]) if l2 else None
    add(n1, "PASS" if (ok0 and ok1 and r1 and r1.group(1) == str(setter) and f0[0].endswith("= 0") and f1[0].endswith("= 1") and ok2
                       and r2 and r2.group(1) == "6350") else "FAIL",
        "%s (row %d) with the quest item%s: before %s; first transmute %s; after %s; second transmute %s (6350 only offered "
        "with the flag)" % (cname, setter, "" if ok0 else " (NOT placed: %s)" % lp[-1][:50], f0[0], l1[0][:40], f1[0],
                           l2[0][:40] if l2 else "-"), [sh("craftflags")], l1 + l2)
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
    H("stat m%d 6 0 set 700000" % mid)  # a monster of the area's level has a few points of life, fewer than the 50 the event takes
    a = life_of(mid)
    H("evtrace fire 3 player m%d" % mid)
    b = life_of(mid)
    # the monster is next to the character and may attack in the same second: its own hit fires the event as well, so the loss
    # is one 50 or more (never a fraction of one)
    thorns = ok and a is not None and b is not None and a - b >= 50 * 256 and (a - b) % (50 * 256) == 0
    notes.append("thorns 50: monster life %s -> %s (%s)" % (a, b, "-%d points" % ((a - b) // 256) if thorns else "WRONG"))
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
    for _ in range(8):  # the heal may land a few frames after the event call returns: read again until it is whole (or 1.6 s have passed)
        if life is not None and base is not None and life - base >= 50 * 256:
            break
        time.sleep(0.2)
        life = stat_of(H("stat player 6")[1])
    heal = ok and ok2 and base is not None and life is not None and 50 * 256 <= life - base < 50 * 256 + 2000  # plus a little regeneration
    notes.append("life on kill 50: player life 25600 -> %s without the ring, %s with it (%s)" % (base, life, "+50 points" if heal else "WRONG"))
    # The measurement needed a character at 100 life; everything after it (cast on kill volleys, the wild monsters of the area) does
    # not, and a character left at 100 life dies: the server unit then stays dead (mode 17) for every later check. Life back up.
    H("stat player 6 0 set 700000")
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
    clean_area()
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
    shot = sh("medianai_controlled")
    clean_area()
    god(False)
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


def leftover_monsters():
    """Live monsters `h state` lists around the player that are not town NPCs (it lists at most 60 within 40 units)."""
    st = hstate()
    return [m for m in st["mons"] if not m.get("npc") and m.get("mode") not in (0, 12)] if st["ok"] else []


def clean_area():
    """Kill every monster that can be killed around the player. What survives (the dark nephalems Malthael's AI summons in `medianai`,
    class 1239, are unkillable) stays in the level for the rest of the game and kills a later check's character the moment it
    arrives (a level 150 character died within two seconds of `cheat goto 2`): only a new game removes it. Save and Exit + Play puts
    the character in town, and the area is entered again."""
    st = hstate()
    area = st["player"].get("area") if st["ok"] else None
    if area in (None, 1):
        return
    clear_mons()
    kill_all_near()
    left = leftover_monsters()
    if left:
        g.log("clean_area: %d monsters cannot be killed (%s): a new game clears the level" % (
            len(left), ", ".join(str(m.get("class")) for m in left[:6])))
        revive_if_dead()  # what survives may have killed the character meanwhile; a dead one has no pause menu (Esc respawns it in the town)
        if not leave_and_reenter():
            return
        goto(area)
        close_menu()
        kill_all_near()


def fight_ready():
    """Outside the town (damage and avoidance run there), no god mode, no leftovers of the earlier check."""
    god(False)
    st = hstate()
    if st["player"].get("area") != 2:
        goto(2)
    close_menu()
    clean_area()


def have_merc():
    _, lines = H("merc")
    return any(l.startswith("merc id=") for l in lines)


def ensure_merc():
    """A hired mercenary (Kashya, act 1 Rogue) standing next to the player in the field. Starts from any state of the character: a
    dead character is revived first (it cannot hire), the old mercenary may be dead or missing (the hire replaces it), gold is
    set, the hire quest is set, and when the server refuses one list entry the next free one is tried."""
    revive_if_dead()
    if not have_merc():
        for attempt in range(3):
            if hstate()["player"].get("area") != 1:
                goto(1)
            close_menu()
            H("gold set 20000 0")
            H("quest 2 0 set")  # Kashya hires once Blood Raven is dead (Median's gate is the quest alone)
            hired = False
            for n in range(6):  # `hire` counts the free entries of the list (hired / unavailable ones are skipped)
                ok, lines = H("hire 150 %d" % n)
                if ok:
                    hired = True
                    break
                g.log("ensure_merc: attempt %d, free entry %d: %s" % (attempt, n, " | ".join(lines)[-200:]))
                if any("no such NPC" in ln for ln in lines):
                    H("spawn 150")  # the hirer is not loaded yet
                    time.sleep(2)
                elif any("no free entry" in ln for ln in lines):
                    break
                else:
                    time.sleep(1)
            if hired or have_merc():
                break
            time.sleep(2)
    if not have_merc():
        return False
    if hstate()["player"].get("area") != 2:
        goto(2)
    for _ in range(30):  # the mercenary follows a few seconds after the player's area change
        if player_dead(hstate()["player"]):  # killed on arrival: revive (new game) and come back
            revive_if_dead()
            goto(2)
            kill_all_near()
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
        # Median's stat 184 (D2R 369) cuts the damage after the resistance, and an item of the character can carry it (the level 32
        # test character wields a weapon with 25%, so every hit did 75% of the cap's share): `stat set` writes the base value, so what
        # is left after zeroing it is what the gear gives, and the expectation is the cap's share cut by that.
        setstat(ref, 369, 0)
        cut = rd(ref, 369) or 0
        for elem, res, mx in elems:
            for add_max, want in cases:
                setstat(ref, res, 200)
                setstat(ref, mx, add_max)
                r = hit(ref, elem, amount, attacker)
                got = r[0] / amount if r else None
                want_cut = _reduce(round(want * amount), cut) / amount
                rows.append("%s %s max+%d: %s%%" % (ref, elem, add_max, "-" if got is None else "%.1f" % (got * 100)))
                if got is None or abs(got - want_cut) > 0.006:
                    bad.append("%s %s max+%d took %s (want %.1f%%: cap share %d%%, stat 184 from other sources %d%%)" % (
                        ref, elem, add_max, rows[-1].split(": ")[1], want_cut * 100, round(want * 100), cut))
        for s, v in saved.items():
            setstat(ref, s, v or 0)
        setstat(ref, 369, 0)
    H("kill nearest 1")
    add(name, "PASS" if not bad else "FAIL",
        "resist 200 against a 100000-unit hit (damage taken; the cap's share cut by the gear's stat 184, if any): %s" % ("; ".join(rows) if not bad else "; ".join(bad)),
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
        # the first hit after a stat write can read healed 0 (the game folds the stat change in on its next frame, as hit() waits for
        # a result): ask again, a short wait between, until the answer matches or the tries are gone; the last answer counts
        got = None
        for attempt in range(5):
            r = hit("player", elem, amount)
            got = r[1] if r else None
            if got is not None and abs(got - want) <= 2:
                break
            time.sleep(1)
        rows.append("%s: healed %s (want %s)" % (label, got, want) + ("" if attempt == 0 else " [after %d tries]" % (attempt + 1)))
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
    m = re.search(r"n=(\d+) failed=(\d+) ilvl=(\d+) ethereal=(\d+) (?:\w+=\S+ )*?sockets=\[([^\]]*)\]", line)  # identified= / qualities= / automagic= come with it
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


# Sockets and automagic for every quality (docs/sigma-patch-audit.md item 2; D2Sigma 0x1007e240, qualities.txt MaxSock /
# Automagics). Median: qualities 1-4 roll rand % 7 sockets, qualities 5-9 get the base's maximum without a die, and the
# automagic roll (item level 100+, base with an auto prefix group) runs for every quality, sets and uniques included.
AQ_BASES = ("60@", "6@C", "!90", "87@", "!14", "@43", "3@P")  # Median bases: 6 sockets, auto prefix group 406 with a row for the type, set and unique items
AQ_NAMES = {1: "low", 2: "normal", 3: "superior", 4: "magic", 5: "set", 6: "rare", 7: "unique", 8: "crafted", 9: "tempered"}
AQ_N = 3000


def _aq_roll(code, quality, n=AQ_N):
    """`itemroll` of one base and quality -> dict (n, failed, ilvl, hist {sockets: count}, quals {quality: count}, auto) or None."""
    ok, line, lines = hv("itemroll %s %d q=%d ilvl=110" % (code, n, quality), "itemroll", 240)
    m = re.search(r"n=(\d+) failed=(\d+) ilvl=(\d+) ethereal=(\d+) (?:\w+=\S+ )*?sockets=\[([^\]]*)\] qualities=\[([^\]]*)\] automagic=(\d+)", line)
    if not (ok and m):
        return None
    pairs = lambda s: {int(a): int(b) for a, b in (p.split(":") for p in s.split())}
    return {"n": int(m.group(1)), "failed": int(m.group(2)), "ilvl": int(m.group(3)), "hist": pairs(m.group(5)),
            "quals": pairs(m.group(6)), "auto": int(m.group(7)), "line": " | ".join(lines), "code": code, "q": quality}


def _aq_pick_base():
    """First base that holds sockets (normal items reach 4+) and has an auto prefix group (normal items show automagic)."""
    tried = []
    for code in AQ_BASES:
        r = _aq_roll(code, 2, 1500)
        if r is None:
            tried.append("%s -" % code)
            continue
        top = max(r["hist"]) if r["hist"] else 0
        tried.append("%s top=%d auto=%d" % (code, top, r["auto"]))
        if top >= 4 and r["auto"] > 0:
            return code, top, tried
    return None, 0, tried


def check_allqualsockets():
    name = "Sockets and automagic for every quality (Median MaxSock / Automagics)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    code, top, tried = _aq_pick_base()
    if code is None:
        return add(name, "BLOCKED", "no base with 4+ sockets and an auto prefix among %s" % ", ".join(tried))
    rolls, log = {}, []
    for q in range(1, 10):
        r = _aq_roll(code, q)
        for other in AQ_BASES:  # a base without a set / unique row of the right level gets a lower quality: try the next base
            if r is not None and r["quals"].get(q, 0) >= 0.9 * r["n"] or other == code:
                continue
            r2 = _aq_roll(other, q)
            if r2 is not None and r2["quals"].get(q, 0) >= 0.9 * r2["n"]:
                r = r2
                break
        rolls[q] = r
        log.append(r["line"] if r else "q=%d: no answer" % q)
    bad, rows = [], []
    for q in range(1, 10):
        r = rolls[q]
        if r is None:
            bad.append("%s: itemroll gave no answer" % AQ_NAMES[q])
            continue
        n = r["n"]
        made = r["quals"].get(q, 0)
        if n < 100 or made < n * 0.9:
            rows.append("%s: generator gave quality %s for %d of %d (not judged)" % (AQ_NAMES[q], r["quals"], n - made, n))
            continue
        hist = r["hist"]
        if q <= 4:  # the die: 1/7 for 0 .. top-1, (7 - top)/7 for top
            exp = {k: n / 7.0 for k in range(top)}
            exp[top] = n * (7 - top) / 7.0
            for k in range(top + 1):
                sd = (exp[k] * (1 - exp[k] / n)) ** 0.5
                if abs(hist.get(k, 0) - exp[k]) > 4 * sd:
                    bad.append("%s: %d sockets %d (want %.0f +- %.0f)" % (AQ_NAMES[q], k, hist.get(k, 0), exp[k], 4 * sd))
            rows.append("%s (%s): sockets %s" % (AQ_NAMES[q], r["code"], " ".join("%d:%d" % kv_ for kv_ in sorted(hist.items()))))
        else:  # the base's maximum for every item
            at_top = hist.get(top, 0)
            die_made = sum(c for k, c in r["quals"].items() if k <= 4)  # a fallback to a die quality may have fewer
            if n - at_top > die_made:
                bad.append("%s: %d of %d items have the maximum of %d sockets, histogram %s" % (AQ_NAMES[q], at_top, n, top, hist))
            rows.append("%s (%s): sockets %s" % (AQ_NAMES[q], r["code"], " ".join("%d:%d" % kv_ for kv_ in sorted(hist.items()))))
    # automagic: D2R's switch applied it to 1-4, 6, 8, 9; Median's Automagics column adds 5 and 7 at the same rate
    old = [q for q in (1, 2, 3, 4, 6, 8, 9) if rolls[q] and rolls[q]["quals"].get(q, 0) >= 0.9 * rolls[q]["n"]]
    old_n = sum(rolls[q]["n"] for q in old)
    old_a = sum(rolls[q]["auto"] for q in old)
    p = old_a / float(old_n) if old_n else 0.0
    rows.append("automagic: " + ", ".join("%s %d/%d" % (AQ_NAMES[q], rolls[q]["auto"], rolls[q]["n"]) for q in range(1, 10) if rolls[q]))
    for q in (5, 7):
        r = rolls[q]
        if r is None or r["quals"].get(q, 0) < 0.9 * r["n"]:
            continue
        if r["auto"] == 0:
            bad.append("%s: no automagic affix in %d items (other qualities %.1f%%)" % (AQ_NAMES[q], r["n"], 100 * p))
            continue
        sd = (p * (1 - p) / r["n"]) ** 0.5
        if p > 0 and abs(r["auto"] / float(r["n"]) - p) > 5 * sd:
            bad.append("%s: automagic rate %.1f%% against %.1f%% of the qualities D2R covers" % (AQ_NAMES[q], 100.0 * r["auto"] / r["n"], 100 * p))
    add(name, "PASS" if not bad else "FAIL",
        "base %s (max %d sockets; tried %s), %d items per quality at item level %d: %s%s"
        % (code, top, ", ".join(tried), AQ_N, rolls[2]["ilvl"] if rolls[2] else 0, " | ".join(rows), ("; FAIL: " + "; ".join(bad)) if bad else ""),
        [], log)


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


def leave_and_reenter(menu_open=False):
    """Esc menu -> Save and Exit -> character select -> Play (the same character). `menu_open`: the pause menu is already up."""
    if not menu_open:
        g.key("esc")
        time.sleep(1.5)
    g.click(*g.UI["save_exit"])
    time.sleep(9)
    STATE["in_game"] = False
    left = not g.in_game()
    if not left:  # the click did not leave the game (a dead character has no pause menu): the game goes on
        STATE["in_game"] = True
        return False
    time.sleep(2)
    return enter_game()


def clear_quest_bits(q, only=None):
    """Clear the set bits of quest record `q` (the game's current difficulty) through the harness; `only` limits it to those bits."""
    _, lines = H("quest %d dump" % q)
    m = re.search(r"bits0-15 ([01]{16})", " ".join(lines))
    if not m:
        return
    for bit, v in enumerate(m.group(1)):
        if v == "1" and (only is None or bit in only):
            H("quest %d %d clear" % (q, bit))


def check_bonusdrops():
    name = "Summoner / Izual bonus catalysts (Nightmare): dropped once on the ground, flag set"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    fight_ready()
    H("difficulty 1")
    # D2R's quest engine only runs a quest's kill handler while the quest is open, and the first kill completes it (the quest
    # words 13 Summoner / 25 Izual stay set in the saved character); the catalyst flags are bits 1 / 2 of quest 47. Whatever an
    # earlier run or check left there, the quests are reopened before every kill and the flag cleared before the first one:
    # the second kill then reaches the hook with the flag set, which is what must hand out nothing.
    clear_quest_bits(47, {1, 2})
    out = []
    ok = True
    for cls, code, bit, quest in ((250, "0o[", 1, 13), (256, "jcat", 2, 25)):
        clear_quest_bits(quest)
        clear_quest_bits(47, {bit})
        before = ground_count(code)
        H("spawn %d" % cls)
        H("kill nearest %d" % cls)
        time.sleep(1)
        n1 = ground_count(code) - before
        _, q = H("quest 47 %d get" % bit)
        clear_quest_bits(quest)  # the first kill completed the quest: reopen it so that the hook runs again with the flag set
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
    # fight_ready's clean_area removed what an earlier check left in Blood Moor: Malthael's dark nephalems of `medianai` (class 1239)
    # cannot be killed and kept the Summoner from casting ("modes [1]"); a new game clears them.
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
        menu = npc_menu_near(cls)  # walks next to the NPC (a copy when out of reach), talks again when the first click was lost
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

def _cd_counts():
    """`h state`'s cooldowns line: (on, starts, reduced, shared, dropped, monsters) or None (retried: the answer can be cut short)."""
    for _ in range(4):
        for ln in hstate()["lines"]:
            m = re.match(r"cooldowns (on|off): starts=(-?\d+) reduced=(-?\d+) shared=(-?\d+) dropped=(-?\d+) monsters=(-?\d+)", ln)
            if m:
                return (m.group(1) == "on",) + tuple(int(x) for x in m.groups()[1:])
        time.sleep(0.5)
    return None


def _cd_frame():
    return hstate()["player"].get("frame")


_CD_LAST = {}


def _cd_wait_ready(skill, timeout=40):
    """Until `h skills` calls the skill usable (0) again: its cooldown (stat 359 at layer = skill id) has run out. The last
    usability code is kept in _CD_LAST for the failure note (1 mana / cooldown, 2 requirements, 7 no level)."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        _ok, lines = H("skills")
        u = next((re.search(r"usable=(-?\d+)", ln) for ln in lines if re.match(r"skill id=%d " % skill, ln)), None)
        _CD_LAST[skill] = int(u.group(1)) if u else None
        if u and int(u.group(1)) == 0:
            return True
        time.sleep(0.5)
    return False


def _cd_cast(skill):
    """Cast the skill at a spawned monster and wait for the cooldown stat (359, layer = skill id) to appear: (end frame, frame
    when seen) or (None, None). The stat holds the end frame of D2R's per-skill cooldown timer."""
    H("spawn 18 1")
    H("stat player 8 set %d" % (3000 << 8))
    ok, _lines = H("cast %d nearest" % skill)
    t0 = time.time()
    while ok and time.time() - t0 < 6:
        time.sleep(0.15)
        end = rd("player", 359, skill)
        if end:
            return end, _cd_frame()
        if not ok:
            break
    return None, None


# A monster class with a skill that has a constant localdelay (built mod): Andariel (156) andariel_skill_2 (812, delay 125, anim SC = special cast, mode 7)
_CD_MON = (156, 812, 7)


def _cd_monster():
    """Median has no cooldown for non-players (D2Sigma 0x100b0a40+0x70: a unit that is not a player is always ready, and the start
    stores into the player's table only). A spawned monster uses a skill with a localdelay twice in a row (harness `mcast`, the
    AI's skill use 0x4a7bc0): both uses start, and the monster keeps no cooldown (stat 359 at the skill's layer and states 0xb9 /
    0x79 stay clear; D2R would store them), the plugin's `monsters` counter counts the two skipped starts. (problems, notes)."""
    cls, skill, mode = _CD_MON
    problems, notes = [], []
    c0 = _cd_counts()
    mid = spawn_id(cls)
    if mid is None:
        return ["no monster of class %d could be spawned" % cls], notes
    time.sleep(2)  # a monster that was just spawned is not a valid target yet
    seen = []
    for n in (1, 2):
        ok, lines = H("mcast %d %d unit %d" % (skill, mode, mid))
        text = " ".join(lines)
        m = re.search(r"returned (-?\d+), unit mode (\d+) -> (\d+), stat359\[\d+\]=(-?\d+) state 0xb9=(\d) state 0x79=(\d)", text)
        seen.append(m.groups() if m else None)
        if not ok or not m:
            problems.append("monster use %d of skill %d refused (%s)" % (n, skill, text[:160]))
            break
        deadline = time.time() + 6  # the attack animation runs; the skill finishes (and would start its cooldown) at its action frame
        while time.time() < deadline and _cd_counts()[5] - c0[5] < n and not rd("m%d" % mid, 359, skill):
            time.sleep(0.1)
        notes.append("use %d: %s; monsters counter +%d" % (n, m.group(0)[:120], _cd_counts()[5] - c0[5]))
    skipped = _cd_counts()[5] - c0[5]
    end = rd("m%d" % mid, 359, skill)
    notes.append("after both uses: stat 359 at layer %d = %s, monsters counter +%d (starts of non-players not passed on)" % (skill, end, skipped))
    if len(seen) == 2 and None not in seen:
        if end or any(x[4] != "0" or x[5] != "0" for x in seen):
            problems.append("the monster holds a cooldown (stat 359 at layer %d = %s, uses read %s): its start was passed on to D2R" % (skill, end, seen))
        if skipped:
            notes.append("the hook skipped %d start(s) D2R made for the monster" % skipped)
        else:
            notes.append("the monster's skill finish made no cooldown start at all (the hook has nothing to skip on this path)")
    H("kill %d" % mid)
    return problems, notes


# Barbarian skills of one skills2 cooldown group with a constant localdelay (built mod, mxl_cooldowns.h): group -> {skill: delay,
# reduction to test}. The first group whose skills the test character may use is tested.
_CD_GROUPS = (("99 (Deep Freeze, Thunderstorm, Purifying Flame, Mana Sweep)", {1310: 300, 1275: 300, 1309: 625, 1651: 480}, 200),
              ("4 (Blast Wave, skills_460, Cold Fear, Thunder Slam, Elvensong, Mind Control)", {554: 500, 532: 625, 1840: 350, 1954: 375, 1386: 500, 1897: 125}, 100),
              ("1 (Leap, Spirit Walk)", {1323: 75, 1290: None}, 50))
_CD_CONTROL = 1320  # Spirit Reave: localdelay 200, no group


def _cd_usable(skills):
    _ok, lines = H("skills")
    out = {}
    for ln in lines:
        m = re.match(r"skill id=(\d+) .*usable=(-?\d+)", ln)
        if m and int(m.group(1)) in skills:
            out[int(m.group(1))] = int(m.group(2))
    return out


def check_cooldowns():
    """Median's real cooldown reduction and shared groups (D2Sigma 0x100b0f40 / 0x100b1140 / 0x100b0ab0; audit section 4): the
    delay of a cooldown is the skill's localdelay minus stat 309 at layer = the skill id, and the skills of one skills2
    cooldown group share one timer. A skill of a group (the first usable one of _CD_GROUPS) is cast with stat 309 = 0 and with a
    reduction: the cooldown stat (359, layer = skill id) the game keeps must differ by the reduction (the plugin's `reduced`
    counter must rise); the group's other skills must then be on cooldown with the same end frame (`shared` counter, `h skills`
    usable code), and Spirit Reave (no group) must not be touched."""
    nm = "Cooldowns (Median): stat 309 shortens a skill's cooldown, skills of one group share one timer"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    c0 = _cd_counts()
    if c0 is None or not c0[0]:
        return add(nm, "FAIL", "plugin part cooldowns is not on (h state: %s; lines %s)" % (c0, [ln[:60] for ln in hstate()["lines"]][-8:]))
    goto(2)
    kill_all_near()
    god(True)
    problems, notes = [], []
    allskills = {sk for _g, d, _r in _CD_GROUPS for sk in d} | {_CD_CONTROL}
    for sk in sorted(allskills):
        H("setskill %d 1" % sk)
    base_level = rd("player", 12)
    setstat("player", 12, 80)  # the skills need character level 15-30
    codes = _cd_usable(allskills)
    notes.append("usable codes at level 80 (0 usable, 1 mana / cooldown, 2 requirements): %s" % codes)
    pick = None
    for label, delays, red in _CD_GROUPS:
        ok_skills = [sk for sk in delays if codes.get(sk) == 0]
        if len(ok_skills) >= 2:
            pick = (label, ok_skills, delays, red)
            break
    if pick is None:
        setstat("player", 12, base_level or 21)
        god(False)
        return add(nm, "FAIL", "no group with two usable skills for the test character | " + " | ".join(notes), [sh("cooldowns")])
    label, group, delays, red_amount = pick
    main, others = group[0], group[1:]
    base309 = rd("player", 309, main)
    durs = {}
    try:
        for red in (0, red_amount):
            for sk in group:
                setstat("player", 309, 0, sk)
            setstat("player", 309, red, main)
            if not _cd_wait_ready(main):
                problems.append("skill %d never became ready again before the cast with reduction %d (usable code %s)" % (main, red, _CD_LAST.get(main)))
                continue
            for sk in others:
                _cd_wait_ready(sk)
            before = _cd_counts()
            end, seen = _cd_cast(main)
            if end is None:
                problems.append("reduction %d: the cast of skill %d left no cooldown stat 359 at layer %d (cast refused, or the start hook never ran)" % (red, main, main))
                continue
            durs[red] = end - seen
            after = _cd_counts()
            notes.append("group %s, skill %d, reduction %d: end frame %d seen at %d -> %d frames left; starts %s->%s reduced %s->%s shared %s->%s dropped %s->%s" % (
                label, main, red, end, seen, end - seen, before[1], after[1], before[2], after[2], before[3], after[3], before[4], after[4]))
            if red and after[2] <= before[2]:
                problems.append("stat 309 = %d did not count as reduced (%s -> %s)" % (red, before[2], after[2]))
            if after[3] - before[3] < 1:
                problems.append("reduction %d: no shared start counted for the group's other skills" % red)
            usable = _cd_usable(allskills)
            ends = {sk: rd("player", 359, sk) for sk in sorted(allskills)}
            for sk in others:  # the same end frame: they are read one after the other, so a later start of a moving skill can lie between
                if usable.get(sk) == 0:
                    problems.append("reduction %d: group skill %d is not on cooldown after skill %d (usable 0)" % (red, sk, main))
                if ends[sk] != ends[main]:
                    problems.append("reduction %d: group skill %d ends at %s, skill %d at %s (one shared timer)" % (red, sk, ends[sk], main, ends[main]))
            if codes.get(_CD_CONTROL) == 0 and ends[_CD_CONTROL]:
                problems.append("reduction %d: Spirit Reave (no group) got a cooldown (end %s)" % (red, ends[_CD_CONTROL]))
            notes.append("  stat 359 ends %s usable %s" % (ends, usable))
        mp, mn = _cd_monster()
        problems += mp
        notes += mn
    finally:
        setstat("player", 309, base309 or 0, main)
        setstat("player", 12, base_level or 21)
        god(False)
        kill_all_near()
    if 0 in durs and red_amount in durs:
        gain = durs[0] - durs[red_amount]
        if abs(gain - red_amount) > 20:
            problems.append("stat 309 = %d shortened the cooldown by %d frames (%d -> %d)" % (red_amount, gain, durs[0], durs[red_amount]))
        want = delays.get(main)
        if want and not want - 35 <= durs[0] <= want + 5:
            problems.append("cooldown without reduction is %d frames, expected about %d (localdelay)" % (durs[0], want))
    add(nm, "FAIL" if problems else "PASS", "%s | %s" % ("; ".join(problems) if problems else "reduction and sharing as Median", " | ".join(notes)),
        [sh("cooldowns")])


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
        # The piles are counted in the area the character stands in: after the respawn that is the town, whose ground is not the
        # level the character died in (the count there says nothing about the death), so only the same area is compared.
        if snapshot["area"] == before["area"] and snapshot["ground_gold"] != before["ground_gold"]:
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
    # The save file keeps the unused stat points in itemstatcost CSvBits of stat 4 and the unused skill points in those of stat 5
    # (Median: 11 and 9 bits, so 2047 and 511; D2R's own: 10 and 8). A character that earned every level-up point keeps far
    # less (5 * 149 = 745 and 149 plus the quests), so the cap never bites in play; this check must test level 150, not the cap,
    # so the numbers it saves are the ones a real level 150 character carries (an earlier check's leftovers piled them up to
    # 2327 / 567 on slot 1, and the file stored the saturated 2047 / 511: 11 and 9 bits, not a bug of the level cap).
    ih, irows = _excel("itemstatcost")
    bits = {}
    for r in irows:
        if r and r[0] in ("statpts", "newskills"):
            bits[r[0]] = int(r[ih.index("CSvBits")] or 0)
    max_stat, max_skill = (1 << bits.get("statpts", 0)) - 1, (1 << bits.get("newskills", 0)) - 1
    earned_stat, earned_skill = per_stat * (maxlvl - 1), per_skill * (maxlvl - 1)
    log.append("save widths: stat points %s bits (max %d), skill points %s bits (max %d); a level %d character earns %d / %d" % (
        bits.get("statpts"), max_stat, bits.get("newskills"), max_skill, maxlvl, earned_stat, earned_skill))
    if max_stat < earned_stat or max_skill < earned_skill:
        bad.append("the save width cannot hold what a level %d %s earns: %s" % (maxlvl, col, log[-1]))
    setstat("player", 4, per_stat * 97)  # what the climbs below start from: level 98 with every point earned and none spent
    setstat("player", 5, per_skill * 97)

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
        v = v or 0
        if s_ == 4:  # the numbers it had, but never more than the save file can hold (it would store the saturated value)
            v = min(v, max_stat)
        elif s_ == 5:
            v = min(v, max_skill)
        setstat("player", s_, v)
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
    # The reply is one line per 50 recipes. The plugin used to drop a line now and then (about 7% of the replies: the
    # console file was opened with fopen_s while the harness polled it; fixed in HarnessEcho by lane cube-outputs). A lost
    # line shifted every later recipe by 50 and the check failed with differences that were no cube output at all (run
    # checks-20261022-212141-s1: rows 4950-4962, 5083-5100, 5133-...). Each chunk is read by its line labels and asked
    # again until complete.
    got, lost = "", []
    for first in range(0, len(want_mask), 2500):
        count = min(2500, len(want_mask) - first)
        wanted = list(range(first, first + count, 50))
        for attempt in range(4):
            ok, lines = H("cubeslots %d %d" % (first, count), 60)
            by_first = {int(m.group(1)): m.group(2) for m in (re.match(r"cubeslots (\d+) (\d+)$", l.strip()) for l in lines) if m}
            if all(w in by_first and len(by_first[w]) == min(50, first + count - w) for w in wanted):
                break
        else:
            lost.append("recipes %d..%d: lines missing at %s" % (first, first + count - 1, [w for w in wanted if w not in by_first]))
        got += "".join(by_first.get(w, "9" * min(50, first + count - w)) for w in wanted)   # 9: unread, shows as a difference
    diffs = [(i, want_mask[i], int(got[i])) for i in range(min(len(got), len(want_mask))) if want_mask[i] != int(got[i])]
    add(n2, "PASS" if quotes == 0 and len(got) == len(want_mask) and not diffs and not lost else "FAIL",
        "%d recipes: %d output cells with a quote in the built cubemain.txt; D2R's records (slot +0x5a/0x5b/0x5b) hold %d of %d "
        "slots the txt names and %d empty ones; differences (row, txt mask, D2R mask): %s" % (
            len(want_mask), quotes, sum(bin(m).count("1") for m in want_mask), len(want_mask) * 3,
            len(want_mask) * 3 - sum(bin(m).count("1") for m in want_mask), diffs[:12]), [],
        ["recipe %d: txt %d D2R %d" % d for d in diffs[:40]])


def _unique_of(key):
    """(base code, unique index) of a Median-only unique in the built uniqueitems.txt: the index is the data row (`item ... uniq=`)."""
    t = _tsv(os.path.join(BUILT_EXCEL, "uniqueitems.txt"))
    h = t[0]
    for row, r in enumerate(t[1:]):
        if r and r[0] == "medianxl:" + key:
            return r[h.index("code")], row
    raise KeyError(key)


def check_cubeoutputs():
    """Cube OUTPUT cells that name an item. Row 4950 (the Dulra catalyst unique + a perfect gem, op 18 on stat 520 = 1): both
    outputs (`useitem` and `gpw`, output b) come out. Row 26 (outputs a Median-only unique named `medianxl:SunstoneOfTheGods`): the
    item has that unique's base, quality unique and index. The qualified form is what D2R resolves in an output (the inputs
    take it too, docs/strings-namespace.md "Item references"); the empty-slot audit is `cowportals`."""
    nm = "Cube outputs craft (rows 4950, 26): both outputs of 4950 come out, row 26's medianxl: unique is created"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    goto(1)
    ensure_cube()
    cube = lambda: [i for i in hstate()["items"] if i.get("page") == 3]
    dul_code, dul_row = _unique_of("DulraCatalyst")
    sun_code, sun_row = _unique_of("SunstoneOfTheGods")
    H("cubeclear")
    H("item %s cube uniq=%d 520=1" % (dul_code, dul_row))   # row 4950: the catalyst's stat 520 is 1 (op 18, param 520, value 1)
    H("item gpw cube")
    ok1, l1 = H("cubecraft")
    out1 = sorted((i.get("code"), i.get("quality"), i.get("unique")) for i in cube())
    H("cubeclear")
    for code in ("x5x", "x6x", "x7x"):   # row 26's inputs
        H("item %s cube" % code)
    ok2, l2 = H("cubecraft")
    out2 = sorted((i.get("code"), i.get("quality"), i.get("unique")) for i in cube())
    H("cubeclear")
    want1 = sorted([(dul_code, 7, dul_row), ("gpw", 2, 0)])
    want2 = [(sun_code, 7, sun_row)]
    good = ok1 and ok2 and "recipe 4950 " in l1[0] and "recipe 26 " in l2[0] and out1 == want1 and out2 == want2
    add(nm, "PASS" if good else "FAIL",
        "row 4950: %s, cube holds (code, quality, unique index) %s, wanted %s; row 26: %s, cube holds %s, wanted %s" % (
            l1[0][:50], out1, want1, l2[0][:50], out2, want2), [], l1 + l2)


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


def _portal_ids():
    return {o["id"] for o in hstate()["objs"] if o.get("class") in (59, 60, 600)}


def check_funcslots():
    """Bug #31: Median rows on 1.13c's srvdofunc 109 (MonFrenzy, flash, feral_strike, bloodlord1_skill) ran D2R's itemless Town
    Portal (docs/function-slots.md); txt_migrate remaps them to D2R's own frenzy functions (srvdofunc 9, srvstfunc 67). The
    player casts feral_strike and flash at a monster, monsters cast MonFrenzy, flash and bloodlord1_skill (`mcast`, mode as
    monstats Sk<n>mode). PASS: no portal object ever appears (`h state`), the cast starts, and it does something (the target
    loses life, a missile is seen, or the monster changes mode); the game keeps running."""
    nm = "Function slots (bug #31): feral_strike, flash, MonFrenzy, bloodlord1_skill do not open a town portal"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    by_name, t, h = _skill_ids_by_name()
    row = {r[0]: r for r in t[1:] if r}
    sid = {n: by_name.get(n) for n in ("feral_strike", "flash", "MonFrenzy", "bloodlord1_skill", "town_portal")}
    if None in sid.values():
        return add(nm, "BLOCKED", "skills missing from the built skills.txt: %s" % [n for n, v in sid.items() if v is None])
    fn = {n: (row[n][h.index("srvstfunc")], row[n][h.index("srvdofunc")]) for n in sid}
    bad_data = [n for n in sid if n != "town_portal" and fn[n][1] == "109"]
    fight_ready()
    god(True)
    base_portals = _portal_ids()
    notes, log, problems, shots = [], [], [], []
    if bad_data:
        problems.append("built skills.txt still has srvdofunc 109 on %s" % bad_data)
    # feral_strike needs an axe or a sword in hand (skills.txt itypea1 axe, itypea2 swor): without one the game answers usable 2 and
    # the cast does nothing, whatever the character wields (the test character's hands hold what the earlier checks left there)
    H("packclear")
    for slot_ in ("rhand", "lhand"):
        H("unequip " + slot_)
    wield = ""
    for code in ("115", "bsd"):
        ok_w, lw = H("equip %s q=2 rhand" % code)
        if ok_w:
            wield = code
            break
        H("unequip rhand")
    notes.append("weapon in hand for feral_strike: %s" % (wield or "none could be equipped (%s)" % " | ".join(lw)[-80:]))
    # the player casts (setskill gives the skill; a monster skill has no class, the game does not check it)
    for name in ("feral_strike", "flash"):
        skill = sid[name]
        kill_all_near()
        tid = tough_target(18)  # the target; its life is what "dropped" reads (the town NPC next to the arrival point is a monster too)
        # frozen (state 1): feral_strike is melee and the harness cast does not walk, so a target that wanders off between the
        # cast and the hit frame is never touched (seen: dist 2 -> 8 within a second, life unchanged over 3 casts)
        if tid is not None:
            H("setstate mon:%d 1 1" % tid)
        H("moveto nearest")
        H("setskill %d 5" % skill)
        H("missiles reset")
        life0 = next((m["life"] for m in hstate()["mons"] if m.get("id") == tid and m.get("life")), None)
        ok, cl, seen, dropped, mode_seen = False, [], 0, False, False
        for _ in range(5):
            H("moveto nearest")
            time.sleep(0.3)
            H("stat player 8 set %d" % (rd("player", 9) or 0))
            ok, cl = H("cast %d nearest" % skill)
            log += cl[:2]
            t0 = time.time()
            while time.time() - t0 < 1.8:
                time.sleep(0.4)
                st = hstate()
                m = [re.search(r"seen=(\d+)", ln) for ln in H("missiles")[1] if ln.startswith("missiles ")]
                seen = max(seen, int(m[0].group(1)) if m and m[0] else 0)
                mode_seen = mode_seen or st["player"].get("mode") in (7, 8, 10)
                dropped = dropped or any(x.get("id") == tid and x.get("life") is not None and life0 is not None and x["life"] < life0
                                         for x in st["mons"])
            if ok and (seen or dropped):
                break
        portals = _portal_ids() - base_portals
        did = seen > 0 or dropped or mode_seen
        notes.append("%s (srvstfunc %s, srvdofunc %s): cast %s, missiles seen %d, target life dropped %s, attack mode %s, new portals %s" % (
            name, fn[name][0], fn[name][1], "OK" if ok else "FAIL", seen, dropped, mode_seen, sorted(portals)))
        shots.append(sh("funcslots_%s" % name))
        if portals:
            problems.append("%s opened a portal object %s" % (name, sorted(portals)))
        if not (ok and did):
            problems.append("%s: cast %s, no attack or missile seen" % (name, "OK" if ok else "refused"))
        kill_all_near()
    # monsters cast through the AI's skill use
    for cls, name, mode in ((127, "MonFrenzy", 7), (506, "bloodlord1_skill", 7), (1250, "flash", 10)):
        clear_mons()
        mid = spawn_id(cls)
        if mid is None:
            notes.append("%s: monster class %d could not be spawned (skipped)" % (name, cls))
            continue
        time.sleep(2)
        ok, lines = H("mcast %d %d unit %d" % (sid[name], mode, mid))
        text = " ".join(lines)
        log += lines[:1]
        m = re.search(r"unit mode (\d+) -> (\d+)", text)
        time.sleep(1.2)
        modes = [x.get("mode") for x in hstate()["mons"] if x.get("id") == mid]
        portals = _portal_ids() - base_portals
        notes.append("%s on class %d: mcast %s (%s), monster mode after %s, new portals %s" % (
            name, cls, "OK" if ok else "FAIL", m.group(0) if m else text[:80], modes, sorted(portals)))
        shots.append(sh("funcslots_mon_%s" % name))
        if portals:
            problems.append("monster %s opened a portal object %s" % (name, sorted(portals)))
        if not ok:
            problems.append("monster %s: mcast refused (%s)" % (name, text[:100]))
        H("kill %d" % mid)
    alive = hstate()["ok"]
    god(False)
    kill_all_near()
    if not alive:
        problems.append("the game is gone")
    add(nm, "PASS" if not problems else "FAIL", ("%s | " % "; ".join(problems) if problems else "") + " | ".join(notes), shots, log)


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
    # Outside the town and alive: a player in a town cannot be hurt (the hit is ignored, result 0) and a dead one cannot die again.
    # The check before this one (nocorpse) ends with a respawn, in the town.
    revive_if_dead()
    fight_ready()
    pids = sorted(g.game_pids())
    atk = spawn_attacker(1)
    if atk is None:
        return add(nm, "BLOCKED", "no attacker monster")
    setstat("player", 7, BIG_LIFE)
    setstat("player", 6, 256 * 100)
    for _ in range(5):  # the hit's own line (life, result) is read before the engine applies it; the player's mode tells whether it landed
        ok, line, lines = hv("hit player phys 2000000000 %s" % atk, "hit")
        time.sleep(1.5)
        if player_dead(hstate()["player"]):
            break
        setstat("player", 6, 256 * 100)  # ignored (the character had just arrived in the area): hit again
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
    # The pass criterion is the mismatches. The audit matches rows by their name cell with the `medianxl:` prefix stripped
    # (strings_ns.bare); a prefix it fails to strip would leave rows unmatched and shrink the count (3844 instead of ~6000)
    # without a mismatch, so the table coverage is the sanity check: every table whose Median rows carry a skill / monster
    # parameter must have contributed some (the count itself depends on the data and is not a criterion).
    tables = {t for t, _ in checked}
    present = sorted(audit.audit.wanted)
    silent = [t for t in present if t not in tables]
    add(n1, "PASS" if not bad and checked and not silent else "FAIL",
        "%d skill / monster parameters in %d of %d Median property tables compared with the skill's name (named/skills.txt, built "
        "skills.txt, rows matched by bare name cell); mismatches %d%s%s" % (sum(checked.values()), len(tables), len(present), len(bad),
                                                                          (": " + "; ".join(str(b) for b in bad[:4])) if bad else "",
                                                                          ("; tables with no parameter compared: %s" % ", ".join(silent)) if silent else ""))
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


def check_orbtips():
    """Mystic orb tooltips (audit T12): every orb's misc spelldescstr is its own string `mxlorb_<row>` (tools/orb_tips.py:
    limit per item, required level, properties, target type) instead of the shared "Right-Click to Apply"."""
    name = "Mystic orb tooltips: per-orb description (limit, level, properties, target type), hovered in the inventory"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    import csv
    import json
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    with open(os.path.join(root, "work", "mod", "data", "global", "excel", "base", "misc.txt"), encoding="utf-8-sig", newline="") as f:
        rows = list(csv.reader(f, delimiter="\t"))
    col = {h: i for i, h in enumerate(rows[0])}
    orbs = [(r[col["code"]], r[col["spelldescstr"]].split(":", 1)[-1]) for r in rows[1:]
            if r[col["spelldescstr"]].split(":", 1)[-1].startswith("mxlorb_")]
    recs = {e["Key"]: e["enUS"] for e in json.load(open(os.path.join(
        root, "work", "mod", "data", "global", "..", "local", "lng", "strings", "medianxl", "median.json"), encoding="utf-8-sig"))}
    log, bad = [], []
    texts = [recs.get(key, "") for _c, key in orbs]
    if len(orbs) < 100 or len(set(texts)) < 90 or any("Right-Click" == t.strip() for t in texts):
        bad.append("%d orbs, %d distinct texts" % (len(orbs), len(set(texts))))
    # a spread of orbs: the first line of each record is read back through the string service (the game's own lookup)
    pick = {}
    for (code, key), text in zip(orbs, texts):
        row = int(key.split("_")[1])
        for tag, test in (("weapon", "(Weapon Only)" in text), ("armor", "(Armor Only)" in text), ("any", "(Any Equippable Item)" in text),
                          ("chance", "Chance to cast" in text), ("resist", "Resist" in text), ("rare", "Rare items only" in text)):
            if test and tag not in pick:
                pick[tag] = (code, key, row, text)
    goto(1)
    close_menu()
    placed = []
    for tag, (code, key, row, text) in pick.items():
        first = [w for w in text.split("\n") if w][0]
        ok, lines = H("str key medianxl:" + key)
        got = " ".join(lines)
        want = _COLOUR.sub("", first)
        want_console = first.replace(chr(255) + "c", "")
        got_plain = re.sub("[^\x00-\x7f]+.", "", _COLOUR.sub("", got))
        log.append("%s %s (%s): string answer %s; record %r" % (tag, key, code, got[:100], text.replace("\n", " | ")[:160]))
        if not (ok and (want in got_plain or want_console in got)):
            bad.append("%s does not give the record text '%s'" % (key, want))
        H("remove %s 9" % code)
        ok, made = H("item %s inv" % code)
        m = re.search(r"item id=(\d+) .* x=(\d+) y=(\d+)", " ".join(made[:1])) if ok else None
        if m:
            placed.append((tag, key, int(m.group(2)), int(m.group(3))))
        else:
            bad.append("orb %s (%s) could not be created: %s" % (code, key, made[:1]))
    g.key("i")
    time.sleep(1.2)
    shots = [sh("orbtips_inventory")]
    for tag, key, x, y in placed:
        fx, fy = (1055 + (x + 0.5) * 27.6) / 1600.0, (381 + (y + 0.5) * 26.0) / 900.0  # the 15 x 10 backpack grid of the 1600 x 900 client
        g.click(fx, fy, move_only=True)
        time.sleep(1.0)
        g.click(fx + 0.002, fy + 0.002, move_only=True)  # the tooltip wants a second mouse move over the item and a dwell
        time.sleep(2.0)
        shots.append(sh("orbtips_%s_%s" % (tag, key)))
    g.key("esc")
    for tag, (code, _k, _r, _t) in pick.items():
        H("remove %s 9" % code)
    add(name, "FAIL" if bad else "REVIEW",
        "%d orbs with their own record (%d distinct texts); strings read back and orbs hovered in the inventory (%s): the screenshots "
        "show the tooltip (limit per item, required level, properties, target type); %s" % (
            len(orbs), len(set(texts)), ", ".join("%s=%s" % (t, k) for t, k, _x, _y in placed), "; ".join(bad) or "no mismatch"), shots, log)


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
    """(frame, x, y, mode, stamina raw) of one `state` reply, so the pairs of samples compare the stamina of known server frames."""
    p = hstate()["player"]
    return (p.get("frame", 0), p.get("x", 0), p.get("y", 0), p.get("mode", -1), p.get("stamina", 0))


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
    """Consecutive samples that are both in `mode` (a number, or a tuple of modes that count as one: 1 and 5 are both standing)."""
    modes = mode if isinstance(mode, tuple) else (mode,)
    return [(a, b) for a, b in zip(rows, rows[1:]) if a[3] in modes and b[3] in modes and b[0] > a[0]]


def _speed(rows, mode):
    """Units per frame in this mode; (speed, frames in the mode). The positions are whole units and the samples a few frames apart, so
    a single pair is off by a quarter: the speed is the distance between the ends of an unbroken stretch of samples in the mode (at
    least 12 frames), and the fastest stretch counts (one that turns a corner or is held up by an obstacle reads low)."""
    frames = sum(b[0] - a[0] for a, b in _pairs(rows, mode))
    stretches, cur = [], []
    for r in rows:
        if r[3] == mode and (not cur or r[0] > cur[-1][0]):
            cur.append(r)
            continue
        stretches.append(cur)
        cur = [r] if r[3] == mode else []
    stretches.append(cur)
    per = [((s[-1][1] - s[0][1]) ** 2 + (s[-1][2] - s[0][2]) ** 2) ** 0.5 / (s[-1][0] - s[0][0]) for s in stretches
           if len(s) > 1 and s[-1][0] - s[0][0] >= 12]
    return (max(per) if per else 0.0), frames


def _drain(rows):
    """Median stamina lost per frame (raw, <<8) over consecutive run samples."""
    per = sorted((a[4] - b[4]) / (b[0] - a[0]) for a, b in _pairs(rows, 3))
    return per[len(per) // 2] if per else 0.0


# The stamina stats the sprint check depends on: max (11), recovery bonus (28), drain percent (154), the skill percents (162 / 163) and
# the per-level / by-time gains (241 / 242 / 294 / 295). A character with gear, passives or a high level carries values in them (the level
# 150 test character ran for minutes on its stamina and never ran dry), so the check sets Median's own (charstats: 50 points,
# nothing per level or vitality, no bonuses) and puts the character's numbers back afterwards.
SPRINT_STAMINA_STATS = (28, 154, 162, 163, 241, 242, 294, 295)


def check_sprint():
    if not need_game():
        return add("Sprint (SK7)", "BLOCKED", "not in a game")
    revive_if_dead()
    fight_ready()
    kept = {s: rd("player", s) for s in (10, 11) + SPRINT_STAMINA_STATS}
    try:
        cs = _charstats_row(hstate()["player"].get("class", 0))
        own_max = int(cs["stamina"]) * STAMINA_POINT
    except Exception:
        own_max = 50 * STAMINA_POINT
    for s in SPRINT_STAMINA_STATS:
        setstat("player", s, 0)
    setstat("player", 11, own_max)
    setstat("player", 10, own_max)
    try:
        _check_sprint()
    finally:
        for s in SPRINT_STAMINA_STATS + (11, 10):
            if kept.get(s) is not None:
                setstat("player", s, kept[s])


def _check_sprint():
    """Median's stamina rules (audit SK7 and sigma-patch-audit item 15; plugin part `stamina`, D2Sigma 0x100a1f90), asserted per
    frame and exactly. With the character's stamina bonuses zeroed (the wrapper) and 50 points of maximum (12800 raw):
      standing (mode 1 / 5)      +max/256 = +50 raw per frame
      walking, attacking, ...    +75% of that = +37 raw per frame (D2R's own step gives 50% = 25)
      running (mode 3)           no regeneration, and the run's own drain: RunDrain x 2 = 120 raw per frame (D2Game 0x77bb0), so
                                 a run from full lasts ceil(12800 / 120) = 107 frames = 4.3 s and then falls back to walking
      state 136 (Stamina Shrine) fills the bar
      no cooldown: a run starts with two points of stamina and the bar is the only limit
    Every sample is one `state` reply: frame, mode and stamina of the same server frame. Two consecutive samples in the same mode
    must differ by exactly rate x frames; a pair whose mode changed in between and came back (a walk that stopped for a frame and was
    ordered again) lies strictly between its mode's rate and the neighbouring rates, and is counted as such, not as a pass."""
    name = "Sprint (SK7) / stamina rules (audit item 15): run 107 frames on 50 stamina, 120 drain, no regeneration while running, standing 50, walking 37 per frame, no cooldown"
    p0 = hstate()["player"]
    try:
        cs = _charstats_row(p0.get("class", 0))
    except Exception as e:
        return add(name, "BLOCKED", "work/mod charstats.txt not readable: %r" % (e,))
    walk_v, run_v, run_drain = int(cs["WalkVelocity"]), int(cs["RunVelocity"]), int(cs["RunDrain"])
    full = rd("player", 11) or 0
    stand_rate = full >> 8                      # max / 256 raw per frame (stat 28 is zero here)
    walk_rate = stand_rate * 75 // 100          # 75% in every other mode
    run_rate = -2 * run_drain                   # no regeneration, drain RunDrain x 2
    bad, notes, log = [], [], []

    def judge(label, rows, mode, rate, others, capped_at=None, need=8, allow_straddle=True):
        """Pairs of consecutive samples that are both in `mode` (and not at the bar's end, where the cap hides the step): exact when the
        stamina changed by rate x frames, a straddle when it lies strictly inside the band between `rate` and one of `others` (the
        mode changed and changed back between the samples), wrong otherwise."""
        exact = straddle = wrong = frames = 0
        for a, b in _pairs(rows, mode):
            d, n = b[4] - a[4], b[0] - a[0]
            if capped_at is not None and (a[4] + max(rate, 0) * n >= capped_at or b[4] >= capped_at):
                continue
            if rate < 0 and (a[4] + rate * n <= 0 or b[4] <= 0):
                continue
            frames += n
            if d == rate * n:
                exact += 1
            elif any(min(rate, o) * n < d < max(rate, o) * n for o in others):
                straddle += 1
            else:
                wrong += 1
        notes.append("%s: %d consecutive samples in mode %d change by exactly %d raw per frame (%d frames), %d straddle a mode change, %d wrong" % (
            label, exact, mode, rate, frames, straddle, wrong))
        if wrong or exact < need or (not allow_straddle and straddle):
            bad.append("%s: %d exact / %d straddling / %d wrong pairs (rule %d raw per frame in mode %d)" % (label, exact, straddle, wrong, rate, mode))
        return exact

    # 1. a run from full stamina: drain 120 per frame, no regeneration, 107 frames, forced back to walking at the end
    run = _sprint_leg(full, False, 14, stop_exhausted=True)
    log += ["run  " + " ".join(map(str, r)) for r in run]
    judge("running", run, 3, run_rate, (stand_rate, walk_rate), need=5)
    run_frames = sum(b[0] - a[0] for a, b in _pairs(run, 3))
    exhausted = any(r[3] == 2 and r[4] < STAMINA_POINT * 2 for r in run)
    want_frames = -(-full // -run_rate)
    notes.append("run from %d: %d frames seen running of the %d (= %.1f s) the bar allows" % (full >> 8, run_frames, want_frames, want_frames / 25))
    if not exhausted:
        bad.append("the run was never forced back to walking when the stamina ran out")
    run_speed, _ = _speed(run, 3)

    # 2. walking: +75% of the standing step, from empty (the bar is not full, so the cap hides nothing), no minimum stamina
    walk_from_empty = _sprint_leg(0, True, 3.5)
    log += ["empty " + " ".join(map(str, r)) for r in walk_from_empty]
    judge("walking from empty", walk_from_empty, 2, walk_rate, (stand_rate, 0), capped_at=full, need=8)
    gained = max((r[4] for r in walk_from_empty if r[3] == 2), default=0) - (walk_from_empty[0][4] if walk_from_empty else 0)
    if gained < STAMINA_POINT:
        bad.append("no stamina came back while walking from empty (gain %d)" % gained)
    # speed only: the bar is full. A leg that is held up (a corner, an NPC in the way: 0.52 against 0.62 units per frame was seen
    # on the level 150 character) reads low, so a leg that does not give the expected ratio is repeated twice and the fastest counts
    want_ratio = run_v / walk_v
    walk_speed, walk_frames = 0.0, 0
    for attempt in range(3):
        walk = _sprint_leg(full, True, 5)
        log += ["walk " + " ".join(map(str, r)) for r in walk]
        sp, fr = _speed(walk, 2)
        if sp > walk_speed:
            walk_speed, walk_frames = sp, fr
        if walk_speed and abs(run_speed / walk_speed - want_ratio) <= 0.15 * want_ratio:
            break
    ratio = run_speed / walk_speed if walk_speed else 0
    notes.append("walk speed %.3f units/frame over %d frames; run %.3f; run/walk %.2f (charstats %d/%d = %.2f)" % (
        walk_speed, walk_frames, run_speed, ratio, run_v, walk_v, want_ratio))
    if not walk_frames or abs(ratio - want_ratio) > 0.15 * want_ratio:
        bad.append("run/walk speed %.2f, charstats wants %.2f" % (ratio, want_ratio))

    # 3. standing: +50 per frame, no stamina threshold, refilled from empty in max / 50 = 256 frames. The character is idle, so
    # every pair is one mode and none may straddle.
    setstat("player", 10, STAMINA_POINT * 2)
    here = _sample()
    H("move %d %d walk" % (here[1], here[2]))
    time.sleep(1.5)
    setstat("player", 10, 0)
    refill = []
    t0 = time.time()
    while time.time() - t0 < 16:
        r = _sample()
        refill.append(r)
        if r[4] >= full:
            break
        time.sleep(0.2)
    log += ["refill " + " ".join(map(str, r)) for r in refill]
    judge("standing refill", refill, 1, stand_rate, (walk_rate, 0), capped_at=full, need=8, allow_straddle=False)
    first = refill[0] if refill else None
    frames_full = (refill[-1][0] - first[0]) if first else 0
    # From the first sample (stamina s0) the bar needs ceil((max - s0) / 50) frames; the last sample is the first at the maximum, so
    # it can come later than that by the gap to the sample before it, never earlier.
    need = -(-(full - first[4]) // stand_rate) if first else 0
    gap = (refill[-1][0] - refill[-2][0]) if len(refill) > 1 else 0
    notes.append("standing refill %d -> %d points: %d frames (= %.1f s; the rule needs %d from the first sample, up to %d with the sampling gap)" % (
        first[4] >> 8 if first else -1, refill[-1][4] >> 8 if refill else -1, frames_full, frames_full / 25, need, need + gap))
    if not refill or refill[-1][4] < full or not need <= frames_full <= need + gap:
        bad.append("standing refill took %d frames, the rule %d..%d, reached %s" % (frames_full, need, need + gap, refill[-1][4] if refill else None))

    # 4. no cooldown: two points of stamina are enough for a run to start (the first direction that is free; the points last only
    # a few frames, so sample at once)
    modes = set()
    for dx, dy in [(int(dx * k), int(dy * k)) for k in (1.0, 0.5, 0.25) for dx, dy in SPRINT_DIRS]:  # a cramped spot: shorter paths
        setstat("player", 10, STAMINA_POINT * 2)
        here = _sample()
        H("move %d %d" % (here[1] + dx, here[2] + dy))
        modes = {_sample()[3] for _ in range(4)}
        if 3 in modes:
            break
        H("move %d %d walk" % (here[1], here[2]))
        time.sleep(0.5)
    notes.append("with 2 points of stamina a run starts: modes %s" % sorted(modes))
    if 3 not in modes:
        bad.append("a run did not start with 2 points of stamina (modes %s)" % sorted(modes))
    here = _sample()
    H("move %d %d walk" % (here[1], here[2]))
    time.sleep(1.0)

    # 5. state 136 (the Stamina Shrine, "infinite stamina") fills the bar
    setstat("player", 10, 0)
    ok, ls = H("setstate player 136 1")
    time.sleep(1.0)
    filled = rd("player", 10) or 0
    H("setstate player 136 0")
    notes.append("state 136: stamina 0 -> %d of %d after 1 s" % (filled >> 8, full >> 8))
    if not ok or filled < full:
        bad.append("state 136 left the stamina at %d of %d" % (filled, full))

    # 6. the key: R (toggle sprint) makes a click-to-move run, R again walks. The toggle is restored. The click needs open ground
    # around the character, which the field the earlier checks left it in may not have (trees, a cliff): the town is open on every side.
    goto(1)
    setstat("player", 10, full)
    keyrun = {}
    # Screen points in the directions the legs walked (world offset to isometric screen, the character is in the middle of the
    # window), then a spread of fixed points: a character in a cramped spot has only a few free ways out.
    # A `state` reply takes about 0.6 s, so a click that walks 8 units (190 px) is over before the first sample: the far points come first
    # (30 units and more: two seconds of walking), then the near ones.
    click_points = [(0.12, 0.50), (0.88, 0.50), (0.50, 0.12), (0.50, 0.80), (0.20, 0.20), (0.80, 0.80), (0.20, 0.80), (0.80, 0.20)]
    for dx, dy in SPRINT_DIRS:
        u, v = dx - dy, (dx + dy) / 2.0
        n = (u * u + v * v) ** 0.5
        click_points.append((0.5 + u / n * 190 / 1600.0, 0.5 + v / n * 190 / 900.0))
    click_points += [(0.80, 0.50), (0.20, 0.50), (0.50, 0.85), (0.50, 0.15), (0.65, 0.65), (0.35, 0.35), (0.65, 0.30), (0.35, 0.70),
                     (0.58, 0.50), (0.42, 0.50)]
    for label in ("on", "off"):
        g.press_bg("r", hold=0.1)
        time.sleep(0.3)
        for fx, fy in click_points:  # a click on a wall does not move; the point depends on where the character stands
            g.click_bg(fx, fy)
            for _ in range(3):  # the order reaches the server a few frames after the click
                time.sleep(0.1)
                smp = _sample()
                keyrun[label] = smp[3]
                log.append("R %s click %.3f,%.3f -> frame %d x %d y %d mode %d" % ((label, fx, fy) + (smp[0], smp[1], smp[2], smp[3])))
                if keyrun[label] in (2, 3, 6):
                    break
            if keyrun[label] in (2, 3, 6):
                break
        time.sleep(2.5)
    # The run toggle is the character's own setting (it survives a restart), so the check does not know which way the first press goes:
    # the two presses must give one run and one walk, and the second press puts the character's toggle back.
    notes.append("key R: click-to-move mode %s after the first press, %s after the second (3 = run, 2 or 6 in the town = walk; one of each, the toggle ends where it began)" % (
        keyrun["on"], keyrun["off"]))
    if sorted(2 if m == 6 else m for m in (keyrun["on"], keyrun["off"])) != [2, 3]:  # 6 is walking in the town
        bad.append("toggle key R gave modes %s / %s (want one run and one walk)" % (keyrun["on"], keyrun["off"]))
    setstat("player", 10, full)
    shot = sh("sprint")
    goto(2)  # back to the field the next check starts from (standing there is mode 1, in the town it is mode 5)
    add(name, "PASS" if not bad else "FAIL", "; ".join(notes) if not bad else "; ".join(bad) + " | " + "; ".join(notes),
        [shot], log)


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
    """`h travel` -> dict: pickup / reveal / transmute key presses, reveals, transmutes, blocked, registered bindings text, pickup
    state, cube state."""
    ok, lines = H("travel")
    line = next((l for l in lines if l.startswith("travel ")), "")
    d = {"line": line}
    m = re.search(r"keys pickup (\d+) reveal (\d+) transmute (\d+)", line)
    if m:
        d.update(pickup_n=int(m.group(1)), reveal_n=int(m.group(2)), transmute_n=int(m.group(3)))
    m = re.search(r"reveals (\d+)", line)
    d["reveals"] = int(m.group(1)) if m else None
    m = re.search(r"transmutes (\d+) \(blocked (\d+), cube (\w+)\)", line)
    if m:
        d.update(transmutes=int(m.group(1)), blocked=int(m.group(2)), cube=m.group(3))
    m = re.search(r"pickup (on|off)", line)
    d["pickup"] = m.group(1) if m else None
    d["bindings"] = line.split("||")[0]
    return d


def plugin_key_bindings(bindings):
    """{logical id: virtual key} from the `travel` line's registered bindings ("mxl_pickup_toggle: key 0x58 modifier 0 | ...")."""
    return {m.group(1): int(m.group(2), 16) for m in re.finditer(r"(mxl_\w+): key 0x([0-9a-f]+) modifier 0", bindings)}


def d2r_key_bindings():
    """{virtual key: [D2R action ids]} from the test character's own key file (`<character>.key` next to the save: D2R's key
    bindings; a six-byte header, then records of five 16-bit words: virtual key (65535 = unbound), slot (0 primary, 1 secondary), 0,
    action id, 0). None when the file does not exist."""
    path = os.path.join(SAVES, g.CHAR_NAME + ".key")
    if not os.path.exists(path):
        return None
    d = open(path, "rb").read()
    w = struct.unpack("<%dH" % (len(d) // 2), d[:len(d) // 2 * 2])
    out = {}
    for i in range(3, len(w) - 4, 5):
        if w[i] != 65535:
            out.setdefault(w[i], []).append(w[i + 3])
    return out


HOTKEY_DEFAULTS = {"mxl_pickup_toggle": 0x58, "mxl_map_reveal": 0x4c, "mxl_transmute": 0x4b}  # X, L, K (plugin/src/travel_logic.h)
HOTKEY_COUNTERS = ("pickup_n", "reveal_n", "transmute_n", "reveals", "transmutes", "blocked")


def check_hotkeys():
    name = "Hotkeys (auto pickup X, map reveal L, transmute K): one press, one action; none on a D2R key (R run toggle, C Character screen)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    revive_if_dead()
    goto(3)
    time.sleep(2)
    t0 = travel_counts()
    if "pickup_n" not in t0:
        return add(name, "BLOCKED", "no travel status line: %s" % t0["line"][:100], [], [t0["line"]])
    log = [t0["line"]]
    res, bad = [], []
    bound = plugin_key_bindings(t0["bindings"])
    if set(bound) != set(HOTKEY_DEFAULTS):
        return add(name, "FAIL", "registered bindings %s, expected %s" % (t0["bindings"], sorted(HOTKEY_DEFAULTS)), [], log)
    keyname = {i: chr(k).lower() for i, k in bound.items()}

    def moved(before, after):
        """The counters that changed between two `travel` reads."""
        return [k for k in HOTKEY_COUNTERS if after.get(k) != before.get(k)]

    # 1. the registered keys: the plugin's defaults (unless the player rebound them in the Controls menu) and not one of them
    # bound by a D2R action. D2R's bindings come from the character's key file (the other keys D2R uses are in it too).
    for ident, want in HOTKEY_DEFAULTS.items():
        if bound[ident] != want:
            bad.append("%s is bound to %s, the default is %s" % (ident, chr(bound[ident]), chr(want)))
    d2r = d2r_key_bindings()
    if d2r is None:
        res.append("D2R key file not found (static conflict check skipped)")
    else:
        for ident, key in bound.items():
            if key in d2r:
                bad.append("%s key %s is also bound by D2R action(s) %s: one press does both" % (ident, chr(key), d2r[key]))
        res.append("plugin keys %s, D2R's own key file binds %d keys and none of these (D2R: R = action %s, the run toggle; C = action %s, the Character screen)" % (
            {i: chr(k) for i, k in bound.items()}, len(d2r), d2r.get(ord("R")), d2r.get(ord("C"))))
    for key in (ord("R"), ord("C")):
        if key in bound.values():
            bad.append("a plugin action is on %s, a D2R key" % chr(key))
    # 2. auto pickup key: toggles the flag, nothing else moves
    g.press(keyname["mxl_pickup_toggle"])
    time.sleep(0.6)
    t1 = travel_counts()
    g.press(keyname["mxl_pickup_toggle"])
    time.sleep(0.6)
    t2 = travel_counts()
    m1 = moved(t0, t1)
    x_ok = (t1["pickup_n"] == t0["pickup_n"] + 1 and t1["pickup"] != t0["pickup"] and t2["pickup_n"] == t0["pickup_n"] + 2 and
            t2["pickup"] == t0["pickup"] and m1 == ["pickup_n"])
    res.append("%s (auto pickup): presses %s->%s->%s, pickup %s->%s->%s, counters moved by the first press: %s" % (
        keyname["mxl_pickup_toggle"].upper(), t0["pickup_n"], t1["pickup_n"], t2["pickup_n"], t0["pickup"], t1["pickup"], t2["pickup"], m1))
    if not x_ok:
        bad.append("auto pickup key")
    # 3. map reveal key: one reveal, no transmute, no pickup toggle
    a0 = mapcells()
    g.press(keyname["mxl_map_reveal"])
    time.sleep(1.5)
    t3 = travel_counts()
    a1 = mapcells()
    m3 = moved(t2, t3)
    r_ok = t3["reveal_n"] == t2["reveal_n"] + 1 and t3["reveals"] == t2["reveals"] + 1 and set(m3) == {"reveal_n", "reveals"} and t3["pickup"] == t2["pickup"]
    res.append("%s (map reveal): presses %s->%s, reveals %s->%s, cells %s->%s, counters moved: %s" % (
        keyname["mxl_map_reveal"].upper(), t2["reveal_n"], t3["reveal_n"], t2["reveals"], t3["reveals"], a0 and a0["cells"], a1 and a1["cells"], m3))
    if not r_ok:
        bad.append("map reveal key")
    # 4. D2R's own keys: a press of R (run / walk toggle) or C (Character screen) must leave every plugin counter alone. C's panel
    # is in the screenshot; Esc closes it again.
    g.press("r")
    time.sleep(0.5)
    g.press("c")
    time.sleep(1.2)
    char_shot = sh("hotkeys_c_charscreen")
    t5 = travel_counts()
    g.press("esc")
    time.sleep(0.6)
    g.press("r")  # R is D2R's run toggle and the character keeps it: put it back
    time.sleep(0.4)
    m5 = moved(t3, t5)
    res.append("R and C pressed: plugin counters moved %s (must be none: the plugin has no action on D2R's run toggle and Character screen)" % (m5 or "none"))
    if m5 or t5["pickup"] != t3["pickup"]:
        bad.append("R / C moved a plugin counter: %s" % m5)
    # 5. the transmute key: closed cube -> counted and blocked, open cube -> transmutes once
    okb, lb = H("item box inv")
    log += lb
    mark = mxl_mark()
    g.press("i")
    time.sleep(1.2)
    inv_shot = sh("hotkeys_inventory")
    g.click(0.677, 0.452, "right")  # the cube item (inventory cell 0,0, 2 x 2 cells) in the 1600 x 900 inventory (hotkeys_inventory.png)
    time.sleep(1.5)
    cube_shot = sh("hotkeys_cube")
    msgs = [l for l in mxl_lines_since(mark) if "cube ui message" in l]
    log += msgs[:12]
    t_open = travel_counts()
    g.press("esc")  # one Esc closes the cube panel and the inventory with it (a second one would open the game menu)
    time.sleep(0.6)
    t6 = travel_counts()
    g.press(keyname["mxl_transmute"])
    time.sleep(0.8)
    t7 = travel_counts()
    m7 = moved(t6, t7)
    c_closed = t7["transmute_n"] == t6["transmute_n"] + 1 and t7["transmutes"] == t6["transmutes"] and "reveal_n" not in m7 and "pickup_n" not in m7
    res.append("%s (transmute), cube closed: presses %s->%s, transmutes %s->%s (blocked %s), counters moved: %s" % (
        keyname["mxl_transmute"].upper(), t6["transmute_n"], t7["transmute_n"], t6["transmutes"], t7["transmutes"], t7.get("blocked"), m7))
    if not c_closed:
        bad.append("transmute key, cube closed")
    log += [t1["line"], t2["line"], t3["line"], t5["line"], t7["line"], t0["bindings"]]
    open_note = "transmute with the cube open: not driven (the plugin did not see the cube panel open; ui messages: %s)" % (msgs[:3] or "none")
    if t_open.get("cube") == "open":
        g.press("i")  # open it again for the key
        time.sleep(1.0)
        g.click(0.677, 0.452, "right")
        time.sleep(1.2)
        t8 = travel_counts()
        g.press(keyname["mxl_transmute"])
        time.sleep(1.0)
        t9 = travel_counts()
        m9 = moved(t8, t9)
        c_open = t9.get("transmutes") == t8.get("transmutes", 0) + 1 and t9["transmute_n"] == t8["transmute_n"] + 1 and "reveal_n" not in m9 and "pickup_n" not in m9
        log += [t_open["line"], t8["line"], t9["line"]]
        open_note = "transmute with the cube open: presses %s->%s, transmutes %s->%s (cube %s; ui message %s)" % (
            t8["transmute_n"], t9["transmute_n"], t8.get("transmutes"), t9.get("transmutes"), t8.get("cube"), msgs[:1])
        if not c_open:
            bad.append("transmute key, cube open")
        g.press("esc")
        time.sleep(0.5)
    add(name, "PASS" if not bad else "FAIL", "; ".join(res) + "; " + open_note + ("; failed: %s" % bad if bad else ""),
        [sh("hotkeys"), inv_shot, cube_shot, char_shot], log)


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


def _stashpage_job(cmd, want):
    """A queued stashpage job (put / list / move / drop) and its answer: the `stashpage result` line once it is not pending."""
    ok, lines = H("stashpage " + cmd)
    if not ok:
        return False, "refused: %s" % (lines[:2],)
    out = ""
    for _ in range(20):
        time.sleep(0.4)
        ok2, l2 = H("stashpage result")
        out = next((l for l in l2 if l.startswith("stashpage result")), "")
        if " -1 pending" not in out and " -2 " not in out:
            break
    return want in out, out


def check_stashpage2():
    name = "Stash pages: personal page 2 (item page 6): panel, arrows, items on it, moves between the pages"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    txt = mxl_text()
    bad, res, log, shots = [], [], [], []
    if "stash pages on:" not in txt:
        return add(name, "FAIL", "`mxl` does not say `stash pages on`: %s" % next((l for l in txt.splitlines() if "stash pages" in l), "-"))
    # two test items on page 2 (codes the test character never carries)
    for cmd, label in (("put r05 1 1", "item 1"), ("put r06 5 5", "item 2")):
        ok, out = _stashpage_job(cmd, "container=9")  # 9 = the custom page
        log.append(out)
        if not ok:
            bad.append("%s not created on page 2 (%s)" % (label, out[:80]))
    ok, out = _stashpage_job("list", "p2:r05@1,1")
    log.append(out)
    res.append("page 2 holds both items: %s" % ("yes" if ok and "p2:r06@5,5" in out else "NO"))
    if not (ok and "p2:r06@5,5" in out):
        bad.append("page 2 list: %s" % out[:100])
    ids = {m.group(1): m.group(2) for m in re.finditer(r"p\d:(r0\d)@\d+,\d+#(\d+)", out)}
    # the stock stash with its strip, the arrow to page 2 and back (D2R's own messages; background clicks on the strip)
    H("stashpage uia PanelManager OpenPanel BankExpansionLayout")
    time.sleep(2)
    ok, lines = H("stashpage widget BankExpansionLayout MxlPersonalNavigation")
    log += lines
    strip = next((l for l in lines if "rect result 0" in l), "")
    res.append("stock stash has the 1 / 2 strip: %s" % ("yes" if strip else "NO"))
    if not strip:
        bad.append("MxlPersonalNavigation missing in the stock stash (%s)" % (lines[:1],))
    shots.append(sh("stashpage_p1"))
    g.click(0.2506, 0.6756)  # the right arrow of the stock strip
    time.sleep(1.8)
    st = next((l for l in H("stashpage state")[1] if l.startswith("stashpage state=")), "")
    opened = st.startswith("stashpage state=2")
    res.append("right arrow opens the page 2 panel: %s" % ("yes" if opened else "NO (%s)" % st[:40]))
    if not opened:
        bad.append("page 2 panel did not open")
    shots.append(sh("stashpage_p2"))
    g.click(0.1725, 0.6756)  # the left arrow of the page 2 strip: back to the stock stash
    time.sleep(1.8)
    st = next((l for l in H("stashpage state")[1] if l.startswith("stashpage state=")), "")
    closed = st.startswith("stashpage state=1")
    res.append("arrow on page 2 returns to the stock stash: %s" % ("yes" if closed else "NO (%s)" % st[:40]))
    if not closed:
        bad.append("the page 2 panel stayed open")
    # moves between the pages through ItemService (atomic), then back
    if "r05" in ids:
        ok1, o1 = _stashpage_job("move %s 1 2 2" % ids["r05"], "move rc=0 committed=1")
        ok2, l1 = _stashpage_job("list", "p1:r05@2,2")
        ok3, o3 = _stashpage_job("move %s 2" % ids["r05"], "move rc=0 committed=1")
        ok4, l2 = _stashpage_job("list", "p2:r05@")
        log += [o1, l1, o3, l2]
        res.append("item moved page 2 -> 1 -> 2: %s" % ("yes" if ok1 and ok2 and ok3 and ok4 else "NO"))
        if not (ok1 and ok2 and ok3 and ok4):
            bad.append("move between the pages failed (%s / %s / %s / %s)" % (o1[:50], l1[:60], o3[:50], l2[:60]))
    # clean-up: the test character keeps nothing on its pages
    H("stashpage uia PanelManager ClosePanel BankExpansionLayout")
    for code in ("r05", "r06"):
        ok, out = _stashpage_job("drop " + code, "1 destroyed")
        log.append(out)
        if not ok:
            bad.append("clean-up of %s failed (%s)" % (code, out[:60]))
    add(name, "PASS" if not bad else "FAIL", "; ".join(res) + ("; failed: %s" % "; ".join(bad) if bad else ""), shots, log)



def _sd_steps(st, lvl):
    """Per-level bonus: the five steps cover the levels 2-8, 9-16, 17-22, 23-28 and 29+."""
    if lvl <= 1:
        return 0
    total, prev = 0, 1
    for i, top in enumerate((8, 16, 22, 28, None)):
        top = lvl if top is None else min(top, lvl)
        if top <= prev:
            break
        total += (top - prev) * st[i]
        prev = top
    return total


def _sd_i32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def _sd_tdiv(a, b):
    """Integer division truncated toward zero (C)."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def _sd_pct(value, percent):
    """value * percent / 100 truncated toward zero (D2Sigma 0x10088e50); 0 when either factor is 0."""
    if value == 0 or percent == 0:
        return 0
    return _sd_i32(_sd_tdiv(value * percent, 100))


def _sd_base_synergy(clvl, energy, stat514):
    """((energy+20)*130/(energy+500) + 50 + min(stat/10, 100)) * (min(clvl, 120)+5) * 10 / 100 - 75 (0x100a6c70)."""
    extra = min(_sd_tdiv(stat514, 10), 100)
    spread = _sd_tdiv((energy + 20) * 130, energy + 500) if energy + 500 else 0
    return _sd_tdiv((spread + 50 + extra) * (min(clvl, 120) + 5) * 10, 100) - 75


def _sd_elemental(base, st, shift, lvl, percent, mastery):
    v = _sd_i32((_sd_steps(st, lvl) + base) << shift)
    if percent:
        v = _sd_i32(v + _sd_pct(v, percent))
    if mastery:
        v = _sd_i32(v + _sd_pct(v, mastery))
    return v


def _sd_physical(base, st, lvl, shift, percent, mastery):
    """Skills without a weapon share (SrcDam 0): the weapon part is 0."""
    own = _sd_steps(st, lvl) + base
    if percent:
        own = _sd_i32(own + _sd_pct(own, percent))
    if mastery:
        own = _sd_i32(own + _sd_pct(own, mastery))
    return _sd_i32(own << shift)


# damage type names of skills.txt EType that the mastery helper (0x337f40) maps to a mastery stat
_SD_MASTERY_TYPES = ("fire", "ltng", "mag", "cold", "pois")


_SD_MASTERY_STATS = (329, 330, 331, 332, 357)  # fire, lightning, cold, poison, magic mastery


def _sd_load():
    """D2R skill id -> the damage columns of the built skills.txt plus the skills2 flags (1 BaseSynergy, 2 StaticDamage)."""
    t = _tsv(os.path.join(BUILT_EXCEL, "skills.txt"))
    col = {c: i for i, c in enumerate(t[0])}

    def cell(r, c):
        return r[col[c]] if col[c] < len(r) else ""

    def num(r, c):
        try:
            return int(cell(r, c))
        except ValueError:
            return 0

    idmap = json.load(open(_main_checkout(os.path.join(REPO, "work", "named", "idmap.json")), encoding="utf-8"))
    sk2 = open(_main_checkout(os.path.join(REPO, "extract", "mxl", "data", "global", "excel", "skills2.bin")), "rb").read()
    n = struct.unpack_from("<I", sk2)[0]
    flags = {}
    for e in idmap["skills"]:
        if e["row"] < n:
            o = 4 + e["row"] * 66
            f = (1 if sk2[o + 0x2d] else 0) | (2 if sk2[o + 0x2e] else 0)
            if f:
                flags[e["id"]] = f
    skills = {}
    for r in t[1:]:
        if not r or not cell(r, "*Id").lstrip("-").isdigit():
            continue
        sid = int(cell(r, "*Id"))
        skills[sid] = {
            "name": r[0], "flags": flags.get(sid, 0), "shift": num(r, "HitShift"), "srcdam": num(r, "SrcDam"),
            "pmin": num(r, "MinDam"), "pmax": num(r, "MaxDam"), "pminst": [num(r, "MinLevDam%d" % i) for i in range(1, 6)],
            "pmaxst": [num(r, "MaxLevDam%d" % i) for i in range(1, 6)], "pcalc": cell(r, "DmgSymPerCalc"),
            "etype": cell(r, "EType"), "emin": num(r, "EMin"), "emax": num(r, "EMax"),
            "eminst": [num(r, "EMinLev%d" % i) for i in range(1, 6)], "emaxst": [num(r, "EMaxLev%d" % i) for i in range(1, 6)],
            "ecalc": cell(r, "EDmgSymPerCalc"), "passive": cell(r, "passive"),
        }
    return skills, {v["name"]: k for k, v in skills.items()}


def _sd_query(sid, lvl, mastery=True):
    """`skilldmg` parsed to a dict of ints; ele / phys / orig_ele / orig_phys are (min, max)."""
    for _ in range(3):  # the harness now and then misses its 15 s window while the game is busy
        ok, line, _l = hv("skilldmg %d %d%s" % (sid, lvl, "" if mastery else " nomastery"), "skilldmg")
        if ok and "skilldmg skill=" in line:
            break
        time.sleep(1)
    else:
        return None
    d = {k: int(v) for k, v in kv(line).items() if re.fullmatch(r"-?\d+", v)}
    for key in ("ele", "phys", "orig_ele", "orig_phys"):
        m = re.search(r"\b" + key + r"=(-?\d+)-(-?\d+)", line)
        d[key] = (int(m.group(1)), int(m.group(2))) if m else None
    return d


def check_skillscaling():
    nm = "Skill damage scaling (Median): BaseSynergy percent, StaticDamage skips mastery, physical magic mastery, per-level steps"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    try:
        skills, by_name = _sd_load()
    except Exception as e:
        return add(nm, "BLOCKED", "tables not readable: %r" % (e,))
    has_ele = lambda s: s["emax"] > 0 or s["emin"] > 0 or any(s["emaxst"])  # noqa: E731
    has_phys = lambda s: s["pmax"] > 0 or any(s["pmaxst"])  # noqa: E731
    active = lambda s: s["passive"] in ("", "0")  # noqa: E731  (a passive-flagged skill keeps D2R's physical function)
    pick = lambda pred, n=1: [i for i in sorted(skills) if pred(skills[i])][:n]  # noqa: E731
    elem_ok = lambda s: has_ele(s) and s["etype"] in _SD_MASTERY_TYPES  # noqa: E731
    phys_ok = lambda s: has_phys(s) and not has_ele(s) and s["srcdam"] == 0 and active(s)  # noqa: E731
    syn_ele = pick(lambda s: s["flags"] == 1 and elem_ok(s), 2)
    syn_phys = pick(lambda s: s["flags"] == 1 and phys_ok(s))
    static_ele = pick(lambda s: s["flags"] == 2 and elem_ok(s))
    static_phys = pick(lambda s: s["flags"] == 2 and phys_ok(s))
    plain_ele = pick(lambda s: s["flags"] == 0 and elem_ok(s) and not s["ecalc"], 2)
    plain_phys = pick(lambda s: s["flags"] == 0 and phys_ok(s) and not s["pcalc"], 2)
    plain_weapon = pick(lambda s: s["flags"] == 0 and s["srcdam"] != 0 and not has_ele(s) and active(s) and not s["pcalc"], 2)
    blvl = lambda s: [n for n in re.findall(r"skill\('([^']+)'\.blvl\)", s["ecalc"]) if n in by_name]  # noqa: E731
    calc_ele = pick(lambda s: elem_ok(s) and active(s) and blvl(s), 8)  # synergy formulas over other skills' levels
    groups = [("BaseSynergy elemental", syn_ele), ("BaseSynergy physical", syn_phys), ("StaticDamage elemental", static_ele),
              ("StaticDamage physical", static_phys), ("plain elemental", plain_ele), ("plain physical", plain_phys),
              ("synergy-formula elemental", calc_ele[:2])]
    keep = {s: rd("player", s) for s in (1, 12, 514) + _SD_MASTERY_STATS}
    log, bad, checked = [], [], 0

    def expect(sid, d, lvl, mastery):
        """Median's value for one skill: table columns plus the inputs `skilldmg` printed for the player."""
        s = skills[sid]
        syn = _sd_base_synergy(d["clvl"], d["energy"], d["stat514"]) if s["flags"] & 1 else 0
        m_ele = d["masteryElem"] if mastery and not s["flags"] & 2 else 0
        m_phys = d["masteryMagic"] if not s["flags"] & 2 else 0
        out = {"baseSyn": syn}
        if has_ele(s):
            out["ele"] = (_sd_elemental(s["emin"], s["eminst"], s["shift"], lvl, d["calcElem"] + syn, m_ele),
                          _sd_elemental(s["emax"], s["emaxst"], s["shift"], lvl, d["calcElem"] + syn, m_ele))
        if has_phys(s):
            out["phys"] = (_sd_physical(s["pmin"], s["pminst"], lvl, s["shift"], d["calcPhys"] + syn, m_phys),
                           _sd_physical(s["pmax"], s["pmaxst"], lvl, s["shift"], d["calcPhys"] + syn, m_phys))
        return out

    try:
        for gname, ids in groups:
            if not ids:
                bad.append("no %s skill in the tables" % gname)
        scenarios = ((1, 0, 0, 0), (40, 150, 0, 25), (100, 300, 600, 40), (150, 1000, 5000, 60))  # clvl, energy, stat 514, mastery
        for clvl, energy, e514, mast in scenarios:
            setstat("player", 12, clvl)
            setstat("player", 1, energy)
            setstat("player", 514, e514)
            for st in _SD_MASTERY_STATS:
                setstat("player", st, mast)
            for gname, ids in groups:
                for sid in ids:
                    s = skills[sid]
                    for lvl in (1, 2, 8, 9, 16, 17, 22, 23, 28, 29, 45):
                        for mastery in ((True, False) if lvl in (1, 29) else (True,)):
                            d = _sd_query(sid, lvl, mastery)
                            if d is None or (d["ele"] is None and d["phys"] is None):
                                bad.append("skilldmg %d %d failed" % (sid, lvl))
                                continue
                            want = expect(sid, d, lvl, mastery)
                            problems = []
                            if d["flags"] != s["flags"]:
                                problems.append("plugin flags %d, skills2 %d" % (d["flags"], s["flags"]))
                            if want["baseSyn"] != d["baseSyn"]:
                                problems.append("BaseSynergy %d, formula %d" % (d["baseSyn"], want["baseSyn"]))
                            if has_ele(s) and d["masteryElem"] != mast:
                                problems.append("mastery stat of %s read %d, set %d" % (s["etype"], d["masteryElem"], mast))
                            if has_phys(s) and d["masteryMagic"] != mast:
                                problems.append("stat 357 read %d, set %d" % (d["masteryMagic"], mast))
                            for key in ("ele", "phys"):
                                if key in want and tuple(d[key]) != want[key]:
                                    problems.append("%s %s, Median's formula %s" % (key, d[key], want[key]))
                            checked += 1
                            if problems or (lvl in (1, 9, 29) and mastery and clvl in (1, 100)):
                                log.append("%s %s(%d) L%d clvl %d energy %d stat514 %d mastery %d%s: ele %s phys %s (calc %d/%d, synergy %d)%s" % (
                                    gname, s["name"], sid, lvl, clvl, energy, e514, mast, "" if mastery else " (nomastery)", d["ele"],
                                    d["phys"], d["calcElem"], d["calcPhys"], d["baseSyn"], " PROBLEM: " + "; ".join(problems) if problems else ""))
                            if problems:
                                bad.append(log[-1])
        # where Median's rules change nothing (no synergy flag, no mastery) the plugin must give D2R's own numbers
        setstat("player", 12, 30)
        setstat("player", 1, 50)
        for st in _SD_MASTERY_STATS:
            setstat("player", st, 0)
        same = 0
        for sid in plain_ele + plain_phys + plain_weapon:
            for lvl in (1, 10, 25):
                d = _sd_query(sid, lvl, True)
                if d is None:
                    bad.append("skilldmg %d %d failed" % (sid, lvl))
                    continue
                same += 1
                s = skills[sid]
                if has_ele(s) and d["ele"] != d["orig_ele"]:
                    bad.append("plain elemental %s L%d: plugin %s, D2R's own %s" % (s["name"], lvl, d["ele"], d["orig_ele"]))
                if (has_phys(s) or sid in plain_weapon) and d["phys"] != d["orig_phys"]:
                    bad.append("plain physical %s L%d: plugin %s, D2R's own %s" % (s["name"], lvl, d["phys"], d["orig_phys"]))
        log.append("%d plain-skill samples compared with D2R's own functions (%s)" % (same, ", ".join(skills[i]["name"] for i in plain_ele + plain_phys + plain_weapon)))
        # synergy: raising a skill that a synergy formula names raises the percent column, and the damage follows
        setstat("player", 12, 60)
        syn_done = 0
        for sid in calc_ele:
            s = skills[sid]
            names = blvl(s)
            src = by_name[names[0]]
            low = _setskill(src, 1)
            a = _sd_query(sid, 10, True)
            high = _setskill(src, 12)
            b = _sd_query(sid, 10, True)
            if a is None or b is None or low is None or high is None:
                bad.append("synergy test of %s from %s: setskill / skilldmg failed (%s %s)" % (s["name"], names[0], low, high))
                continue
            wa, wb = expect(sid, a, 10, True), expect(sid, b, 10, True)
            log.append("synergy %s(%d) from %s level %s -> %s: percent column %d -> %d, elemental %s -> %s (formula %s -> %s)" % (
                s["name"], sid, names[0], low, high, a["calcElem"], b["calcElem"], a["ele"], b["ele"], wa["ele"], wb["ele"]))
            syn_done += 1
            if b["calcElem"] <= a["calcElem"] or tuple(a["ele"]) != wa["ele"] or tuple(b["ele"]) != wb["ele"] or b["ele"][1] <= a["ele"][1]:
                bad.append(log[-1])
            if syn_done >= 3:
                break
        if not syn_done:
            bad.append("no skill with a skill() synergy formula could be tested")
    finally:
        for st, v in keep.items():
            if v is not None:
                setstat("player", st, v)
    add(nm, "PASS" if not bad and checked else "FAIL",
        ("%d samples at 11 skill levels and 4 character setups (%s) equal Median's formula computed from the tables; %d synergy raises followed" % (
            checked, ", ".join("%s: %s" % (n, "/".join(skills[i]["name"] for i in ids)) for n, ids in groups), syn_done))
        if not bad and checked else "; ".join(bad[:8]), [], log)



def _item_id(lines):
    m = re.search(r"item id=(\d+)", " ".join(lines))
    return int(m.group(1)) if m else None


def _iflags(item_id, unid=False):
    """`iflags`: the item's flag word, mode, quantity, durability (dict of ints) or None."""
    ok, lines = H("iflags %d%s" % (item_id, " unid" if unid else ""))
    m = next((l for l in lines if l.startswith("iflags ")), None)
    if not ok or m is None:
        print("iflags %d: %s %s" % (item_id, ok, lines[-2:]))
        return None
    return {k: int(v, 16) if v.startswith("0x") else int(v) for k, v in kv(m).items()}


def check_nodurability():
    """Audit IT1 / IT2 (Median: no durability, no quantity use): worn weapon and armour keep their durability through
    many melee wear steps (harness `wear`, the function behind every hit) and a throwing stack keeps its quantity through
    real throws (cast of the Throw skill, id 2)."""
    nm = "No durability loss and no quantity use (audit IT1, IT2; plugin parts durability + quantity)"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    fight_ready()
    god(False)
    log, notes, good = [], [], True
    H("packclear")  # room in the backpack: an unequip into a full backpack fails and the hand stays occupied
    for slot in ("rhand", "lhand", "torso"):
        H("unequip " + slot)
    # hfh is a quest item that D2R's place function refuses once the character's quest state says its quest is done (see
    # check_statcols): fall back to ordinary weapons (115 axe, bsd broad sword)
    for code in ("hfh", "115", "bsd"):
        ok, l1 = H("equip %s q=2 rhand" % code)
        log += l1
        if ok and _item_id(l1):
            break
        H("unequip rhand")
    ok2, l2 = H("equip fld q=2 torso")
    log += l2
    weapon, armour = _item_id(l1), _item_id(l2)
    if not (ok and ok2 and weapon and armour):
        return add(nm, "BLOCKED", "could not equip a test weapon (hfh, 115, bsd) / armour fld: %s" % " | ".join((l1 + l2)[-3:]), [], log)
    w0, a0 = _iflags(weapon), _iflags(armour)
    log.append("before: weapon %s armour %s" % (w0, a0))
    if not w0 or not a0 or w0["maxdurability"] <= 0 or a0["maxdurability"] <= 0:
        return add(nm, "BLOCKED", "the test items have no durability to lose (weapon %s, armour %s)" % (w0, a0), [], log)
    mon = spawn_attacker(1)
    if mon is None:
        return add(nm, "BLOCKED", "no monster to hit", [], log)
    boosted = {}
    for st, val in ((0, 600), (2, 600), (12, 60)):  # strength, dexterity, level: the Throw skill checks the weapon's requirements
        boosted[st] = rd("player", st)
        setstat("player", st, val)
    # 2 x 300 wear steps: weapon (player attacks) and armour (monster attacks); the wear proc is 4 - 10 percent, so a
    # build that wears items loses dozens of points while this one must not lose one
    _, l3 = H("wear player %s 300" % mon, 60)
    _, l4 = H("wear %s player 300" % mon, 60)
    log += l3 + l4
    w1, a1 = _iflags(weapon), _iflags(armour)
    log.append("after: weapon %s armour %s" % (w1, a1))
    dur_ok = bool(w1 and a1) and w1["durability"] == w0["durability"] and a1["durability"] == a0["durability"]
    good = good and dur_ok
    notes.append("durability weapon %s -> %s, armour %s -> %s after 600 wear steps" % (
        w0["durability"], w1 and w1["durability"], a0["durability"], a1 and a1["durability"]))
    # get-hit: real monsters (SUnitDmg 0x44b2b0 -> 0x4526c0 -> the dispatcher 0x441b10) hit the armed character for a while.
    # The life lost shows that hits landed; the worn armour must keep its durability. This leg alone does not tell the part from
    # D2R: with the part off (control run) the armour did not wear down in 25 s of real hits either (613 life lost), the `wear`
    # step above (the same call SUnitDmg makes, 0x4526c0) is what shows the part (off: armour 48 -> 20).
    # Normal-difficulty level-1 skeletons almost never hit a level-150 character (measured: 0 - 1 life points in 20 s), so the
    # attackers are spawned under the Hell override (`h difficulty 2`; the same monsters took 13000 life in 20 s)
    setstat("player", 7, BIG_LIFE)
    setstat("player", 6, BIG_LIFE)
    H("difficulty 2")
    for _ in range(3):
        spawn_attacker(1)
    time.sleep(2)
    life0 = rd("player", 6) or 0
    a2, t_end, lost = _iflags(armour), time.time() + 25, 0
    while time.time() < t_end:
        time.sleep(2.5)
        now = rd("player", 6) or 0
        lost += max(0, life0 - now)
        life0 = now
        if now < BIG_LIFE // 2:
            setstat("player", 6, BIG_LIFE)
            life0 = BIG_LIFE
    a3 = _iflags(armour)
    kill_all_near()
    H("difficulty 0")
    log.append("get-hit: armour %s -> %s, life lost %d raw in 25 s" % (a2, a3, lost))
    hit_ok = bool(a2 and a3) and a3["durability"] == a2["durability"] and lost >= 256 * 20
    good = good and hit_ok
    notes.append("real monster hits: armour %s -> %s, %d life lost%s" % (
        a2 and a2["durability"], a3 and a3["durability"], lost >> 8, "" if lost >= 256 * 20 else " (too few hits landed to judge)"))
    # quantity: real throws
    H("unequip rhand")
    ok, l5 = H("equip tax q=2 rhand")
    log += l5
    knives = _item_id(l5)
    q0 = _iflags(knives) if knives else None
    if not ok or not q0 or q0["quantity"] <= 1:
        notes.append("throwing knives not usable here (%s)" % " | ".join(l5[-2:]))
        good = False
    else:
        boosted = {}
        for st, val in ((0, 600), (2, 600), (12, 60)):  # strength, dexterity, level: Throw checks the weapon's requirements
            boosted[st] = rd("player", st)
            setstat("player", st, val)
        H("setskill 2 1")
        throws = 0  # casts the game accepted with the skill usable (code 0) and the unit in a cast mode
        for _ in range(14):
            H("moveto nearest")
            time.sleep(0.4)
            okc, lc = H("cast 2 nearest")
            log += lc
            if okc and "usable=0" in " ".join(lc):
                throws += 1
            time.sleep(1.2)
            if throws >= 4:
                break
            if not okc or not hstate()["mons"]:
                spawn_attacker(1)
        q1 = _iflags(knives)
        qty_ok = bool(q1) and q1["quantity"] == q0["quantity"]
        good = good and qty_ok and throws > 0
        notes.append("quantity %s -> %s after %d accepted throws (Throw usable)" % (q0["quantity"], q1 and q1["quantity"], throws))
        for st, val in boosted.items():
            if val is not None:
                setstat("player", st, val)
    kill_all_near()
    for slot in ("rhand", "torso"):
        H("unequip " + slot)
    add(nm, "PASS" if good else "FAIL", "; ".join(notes), [], log)



REPAIR_STRIP = (110, 642, 572, 706)  # vendor panel (1600 x 900 client): between the item grid and the panel's lower edge, where the repair buttons sit


def _panel_diff(a, b, box=REPAIR_STRIP):
    """Mean absolute pixel difference (0-255) of two client grabs inside box."""
    from PIL import ImageChops, ImageStat
    d = ImageChops.difference(a.convert("RGB").crop(box), b.convert("RGB").crop(box))
    return sum(ImageStat.Stat(d).mean) / 3.0


def _desc_lines(item_id, eth=False):
    """`itemdesc`: the lines of D2R's own item description of the item (`eth`: as an ethereal item), colour codes stripped (None
    when the verb fails)."""
    ok, lines = H("itemdesc %d%s" % (item_id, " eth" if eth else ""))
    if not ok:
        return None
    return [re.sub(r"[\ue000-\uf8ff].", "", re.sub(r"^itemdesc id=\d+ line \d+: ", "", l)) for l in lines if re.match(r"itemdesc id=\d+ line \d+: ", l)]


def check_nodurabilityui():
    """Audit IT1 (what Median shows of durability: nothing). Items keep the durability stats Median has (stat 0x48 / 0x49, ethereal
    75%), but D2R's item description has no `Durability: x of y` line (plugin part durabilitytip), an ethereal item reads
    `Ethereal` (Median's text, not `Ethereal (Cannot be Repaired)`), Charsi's menu line is `trade` (0xd44) not `trade/repair`
    (0xd06) and her panel is the plain trade panel, not the repair panel (plugin part repairui)."""
    nm = "No durability in the UI: no tooltip line, ethereal text, no repair line or repair panel (audit IT1; plugin parts durabilitytip + repairui)"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    revive_if_dead()
    goto(1)
    close_menu()
    log, notes, bad, shots = [], [], [], []
    H("packclear")
    # 1. descriptions of an armour, a weapon, an ethereal armour, a magic armour
    made, cells = {}, {}
    for key, cmd in (("armour", "item fld q=2 inv"), ("weapon", "item bsd q=2 inv"), ("magic armour", "item fld q=4 inv"), ("ethereal armour", "item fld q=2 inv")):
        ok, l = H(cmd)
        log += l
        iid = _item_id(l)
        if not ok or iid is None:
            bad.append("%s: could not be made (%s)" % (key, " | ".join(l[-2:])))
            continue
        made[key] = iid
        m = re.search(r"item id=\d+ .* x=(\d+) y=(\d+)", " ".join(l[:1]))
        if m:
            cells[key] = (int(m.group(1)), int(m.group(2)))
    descs = {}
    for key, iid in made.items():
        fl = _iflags(iid)
        d = _desc_lines(iid, eth=(key == "ethereal armour"))
        log.append("%s id %d: stats %s; description %s" % (key, iid, fl and {k: fl[k] for k in ("durability", "maxdurability")}, d))
        if d is None or not d:
            bad.append("%s: no description text (itemdesc failed)" % key)
            continue
        descs[key] = d
        if not fl or fl["maxdurability"] <= 0 or fl["durability"] <= 0:
            bad.append("%s: Median keeps the durability stats, this item has %s" % (key, fl and (fl["durability"], fl["maxdurability"])))
        hit = [x for x in d if re.search(r"durability|repair|indestructible", x, re.I)]
        if hit:
            bad.append("%s: the description still mentions durability/repair: %s" % (key, hit))
    eth = descs.get("ethereal armour")
    if eth is not None:
        if not any(x.strip().lower().startswith("ethereal") for x in eth):
            bad.append("ethereal armour: no `Ethereal` line in %s" % eth)
        if any("cannot be repaired" in x.lower() for x in eth):
            bad.append("ethereal armour still says Cannot be Repaired: %s" % eth)
    notes.append("descriptions (%s): %s" % (", ".join(sorted(descs)), "; ".join("%s: %d lines" % (k, len(v)) for k, v in sorted(descs.items()))))
    # the real tooltip on screen (for the human): hover the armour and the weapon in the inventory with the plugin's virtual cursor
    g.key("i")
    time.sleep(1.5)
    for key in ("armour", "weapon"):
        if key not in cells:
            continue
        fx, fy = (1055 + (cells[key][0] + 0.5) * 27.6) / 1600.0, (381 + (cells[key][1] + 1.0) * 26.0) / 900.0  # the 15 x 10 backpack grid of the 1600 x 900 client
        g.click(fx, fy, move_only=True)
        time.sleep(1.0)
        g.click(fx + 0.002, fy + 0.002, move_only=True)  # the tooltip wants a second mouse move over the item and a dwell
        time.sleep(2.0)
        shots.append(sh("nodur_tooltip_%s" % key))
    g.key("esc")
    time.sleep(0.5)
    # 2. Charsi: the menu line and the panel
    panels = {}
    for cls, nm_ in ((154, "charsi"), (147, "gheed")):
        H("packclear")
        menu = npc_menu_near(cls)
        if menu is None:
            bad.append("%s: no menu" % nm_)
            continue
        log += menu
        lines = [l for l in menu if re.match(r"\s*line \d+ ", l)]
        ids = [int(m.group(1), 16) for l in lines for m in [re.search(r"string=0x([0-9a-f]+)", l)] if m]
        notes.append("%s menu string ids %s" % (nm_, ", ".join("0x%x" % i for i in ids)))
        if 0xd06 in ids:
            bad.append("%s: the menu still has the trade/repair line (0xd06): %s" % (nm_, ids))
        if cls == 154 and 0xd44 not in ids:
            bad.append("charsi: the menu has no plain trade line (0xd44): %s" % ids)
        shots.append(sh("nodur_%s_menu" % nm_))
        trade = ids.index(0xd44) if 0xd44 in ids else (ids.index(0xd06) if 0xd06 in ids else 1)
        H("menu %d" % trade)
        for _ in range(6):
            time.sleep(2)
            if trade_panel_open():
                break
        else:
            bad.append("%s: the trade panel did not open" % nm_)
        time.sleep(1.5)
        panels[nm_] = g.grab_client(g.need_window())
        shots.append(sh("nodur_%s_panel" % nm_))
        g.click(*TRADE_CLOSE)
        time.sleep(1.0)
        close_menu()
    if "charsi" in panels and "gheed" in panels:
        diff = _panel_diff(panels["charsi"], panels["gheed"])
        notes.append("panel strip below the grid, Charsi vs Gheed: mean pixel difference %.2f (repair panel mode: far above 0, plain trade panel: ~0)" % diff)
        log.append("repair strip diff charsi/gheed: %.3f" % diff)
        if diff > 2.5:
            bad.append("Charsi's panel differs from the plain trade panel below the grid (diff %.2f): the repair mode is still on" % diff)
    add(nm, "PASS" if not bad else "FAIL", "; ".join(notes) + ("; PROBLEMS: " + "; ".join(bad) if bad else ""), shots, log)


def _quest_bit(q, bit):
    ok, lines = H("quest %d %d get" % (q, bit))
    return ok and any(re.search(r"= 1\b", l) for l in lines if l.startswith("quest"))


def check_potionamounts():
    """Audit item 11 (Median patches the Potion of Life to +50 life, was +20, and Lam Esen's Tome to 10 stat points, was 5): the
    game's own grant steps (harness `rewardstep`) run on the player; the plugin part `potionamounts` patches the two immediates."""
    nm = "Potion of Life +50 life, Lam Esen's Tome 10 stat points (audit item 11; plugin part potionamounts)"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    log, notes, good = [], [], True
    life7, pts4 = rd("player", 7), rd("player", 4)
    ok, l1 = H("rewardstep potion")
    log += l1
    m = re.search(r"gained=(-?\d+) points=(-?\d+)", " ".join(l1))
    potion = int(m.group(2)) if m else None
    notes.append("Potion of Life: +%s max life (Median 50, D2R 20)" % potion)
    good = good and ok and potion == 50
    if life7 is not None:
        setstat("player", 7, life7)
    had = _quest_bit(17, 1)
    H("quest 17 1 set")
    ok, l2 = H("rewardstep tome")
    log += l2
    m = re.search(r"gained=(-?\d+)", " ".join(l2))
    tome = int(m.group(1)) if m else None
    notes.append("Lam Esen's Tome: +%s stat points (Median 10, D2R 5)" % tome)
    good = good and ok and tome == 10
    if pts4 is not None:
        setstat("player", 4, pts4)
    if not had:
        H("quest 17 1 clear")
    add(nm, "PASS" if good else "FAIL", "; ".join(notes), [], log)


def check_manarules():
    """Audit item 15 (Median's stamina and mana recovery, D2Sigma 0x100a1f90 / 0x100a1b50; plugin parts stamina + manaregen):
    stamina per frame standing 100%, walking 75% (D2R 50%); state 136 (Stamina Shrine, "infinite stamina") fills the bar; a run
    in the Rogue Encampment costs no stamina (vanilla's town rule); a mana pool smaller than the frame count regenerates
    nothing (D2R raises the base to 1)."""
    nm = "Stamina and mana recovery rules (audit item 15; plugin parts stamina + manaregen)"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    fight_ready()
    log, notes, bad = [], [], []
    full = rd("player", 11) or 0
    bonus = rd("player", 28) or 0
    point = full >> 8
    step = point + point * bonus // 100

    def per_frame(rows, mode):
        pr = [(a, b) for a, b in _pairs(rows, mode) if a[4] + step * 4 < full]
        frames = sum(b[0] - a[0] for a, b in pr)
        gain = sum(b[4] - a[4] for a, b in pr)
        return (gain / frames if frames else None), frames

    # 1. standing in the field: max/256 raw per frame (x (100 + stat 28)%); walking 75% of it
    setstat("player", 10, 0)
    stand = []
    t0 = time.time()
    while time.time() - t0 < 4:
        stand.append(_sample())
        time.sleep(0.2)
    log += ["stand " + " ".join(map(str, r)) for r in stand]
    rate, frames = per_frame(stand, (1, 5))  # 5 (town neutral) is what a character has in the field until it moves after arriving
    notes.append("standing: %s raw/frame over %d frames (Median %d = max %d / 256 x (100+%d)%%)" % (
        ("%.1f" % rate) if rate is not None else "?", frames, step, point, bonus))
    if rate is None or abs(rate - step) > 0.05 * step:
        bad.append("standing recovery %s raw/frame, Median %d" % (rate, step))
    walk = _sprint_leg(0, True, 4)
    log += ["walk " + " ".join(map(str, r)) for r in walk]
    rate, frames = per_frame(walk, 2)
    want = step * 75 // 100
    notes.append("walking: %s raw/frame over %d frames (Median 75%% = %d, D2R 50%% = %d)" % (
        ("%.1f" % rate) if rate is not None else "?", frames, want, step // 2))
    if rate is None or frames < 10 or abs(rate - want) > 0.15 * want:  # a sample pair can straddle a standing frame (100%)
        bad.append("walking recovery %s raw/frame over %d frames, Median %d" % (rate, frames, want))

    # 2. state 136 fills the bar at once, whatever the mode
    H("move %d %d" % (walk[-1][1], walk[-1][2]))
    setstat("player", 10, 0)
    ok, ls = H("setstate player 136 1")
    log += ls
    time.sleep(1.0)
    filled = rd("player", 10) or 0
    H("setstate player 136 0")
    notes.append("state 136: stamina 0 -> %d of %d after 1 s" % (filled >> 8, point))
    if not ok or filled < full:
        bad.append("state 136 left the stamina at %d of %d" % (filled, full))

    # 3. a run in a town costs no stamina (infinite stamina in the Rogue Encampment)
    goto(1)
    close_menu()
    run = _sprint_leg(full, False, 5)
    log += ["town run " + " ".join(map(str, r)) for r in run]
    ran = [r for r in run if r[3] == 3]
    lowest = min((r[4] for r in run), default=0)
    notes.append("run in town: %d samples running, stamina lowest %d of %d" % (len(ran), lowest >> 8, point))
    if len(ran) < 3:
        notes.append("(too few running samples to judge the town drain)")
        bad.append("no run in the town to measure")
    elif lowest < full - 4 * step:
        bad.append("a run in town drained the stamina to %d of %d" % (lowest >> 8, point))

    # 4. a mana pool below the frame count regenerates nothing (Median's base has no floor of 1)
    fight_ready()
    saved = {s_: rd("player", s_) for s_ in (8, 9, 26, 27)}
    try:
        setstat("player", 9, 40)
        setstat("player", 8, 0)
        setstat("player", 26, 0)
        setstat("player", 27, 0)
        time.sleep(2.5)
        mana = rd("player", 8) or 0
        notes.append("mana pool 40 raw, bonuses off: %d raw after 2.5 s (Median 0, D2R +1 per frame)" % mana)
        if mana > 2:
            bad.append("a pool of 40 raw regenerated to %d (D2R's floor of 1 per frame)" % mana)
    finally:
        for s_, v in saved.items():
            if v is not None:
                setstat("player", s_, v)
    add(nm, "PASS" if not bad else "FAIL", "; ".join(notes) + ("; BAD: " + " | ".join(bad) if bad else ""), [], log)


def check_autoid():
    """Audit IT3 (Median: the magic, rare, set and unique generators push 1 into SetItemFlag(0x10 identified), D2Game 0x104c1,
    0x15443, 0x13daa and the unique picker): monster drops of those qualities lie on the ground identified. Real drops: the
    harness `item` / `itemroll` go through MakeItem (0x43d530), which identifies its item itself, so only monster kills show
    what the generator does. Andariel (class 156) drops magic and rare items; `dump ground` prints quality and the flag."""
    nm = "Items drop identified (audit IT3; plugin part autoid in item_gen.cpp)"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    fight_ready()
    log, seen = [], set()

    def ground():
        out = []
        for l in H("dump ground")[1]:
            if l.startswith("ground ") and re.search(r"mode=[35] ", l):
                d = {k: v for k, v in re.findall(r"(\w+)=(-?\d+)", l)}
                if int(d["id"]) not in seen:
                    out.append(d)
        return out

    seen.update(int(d["id"]) for d in ground())
    drops = []
    for _ in range(14):
        H("spawn 156")
        ok, l = H("kill nearest 156")
        log += l
        time.sleep(1.5)
        for d in ground():
            seen.add(int(d["id"]))
            drops.append(d)
        if len([d for d in drops if 4 <= int(d["quality"]) <= 7]) >= 6:
            break
    kill_all_near()
    fancy = [d for d in drops if 4 <= int(d["quality"]) <= 7]
    ident = [d for d in fancy if d["identified"] == "1"]
    plain = [d for d in drops if int(d["quality"]) in (1, 2, 3)]
    pident = [d for d in plain if d["identified"] == "1"]
    note = "%d drops: %d magic/set/rare/unique, %d of them identified; %d low/normal/superior, %d identified" % (
        len(drops), len(fancy), len(ident), len(plain), len(pident))
    log.append(note)
    if len(fancy) < 3:
        return add(nm, "BLOCKED", "too few magic or better drops to judge (%s)" % note, [], log)
    add(nm, "PASS" if len(ident) == len(fancy) else "FAIL", note, [], log)


def _intown_skills(limit=60):
    """Skills of the built skills.txt that may be cast in town (InTown), can be selected (left / right skill) and cost mana
    (mana column), without a restrict value, by required level: [(id, name, mana, lvlmana, manashift)]."""
    import csv
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "mod", "data", "global", "excel", "skills.txt")
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        rows = list(csv.reader(f, delimiter="	"))
    h = rows[0]
    col = {c: h.index(c) for c in ("skill", "*Id", "InTown", "mana", "leftskill", "rightskill", "restrict", "reqlevel", "lvlmana", "manashift")}
    out = []
    for r in rows[1:]:
        if len(r) < len(h) or r[col["InTown"]] != "1" or r[col["restrict"]] or r[col["mana"]] in ("", "0"):
            continue
        if r[col["leftskill"]] != "1" and r[col["rightskill"]] != "1":
            continue
        out.append((int(r[col["reqlevel"]] or 0), int(r[col["*Id"]]), r[col["skill"]], r[col["mana"]], r[col["lvlmana"]], r[col["manashift"]]))
    out.sort()
    return [(i, n, m, lm, sh) for _lv, i, n, m, lm, sh in out[:limit]]


def drop_cursor_item(log):
    """Skills cannot be used while an item is held on the cursor (0x4249f0 "busy"): drop it (client packet 0x17)."""
    ok, lines = H("cursor")
    log.extend(lines)
    m = re.search(r"cursor id=(\d+)", " ".join(lines))
    if m:
        H("pkt 17 " + " ".join("%02x" % b for b in struct.pack("<I", int(m.group(1)))))
        time.sleep(1.0)
        log.extend(H("cursor")[1])


def check_townmana():
    """Audit SK3 (P2.1.0): skills cost no mana in a town. An InTown skill that costs mana (the first of skills.txt the game
    accepts as a cast) cast in the Rogue Encampment leaves the mana alone, cast in the Blood Moor it costs its normal price."""
    nm = "Free mana in town (audit SK3; plugin part townmana)"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    log, notes = [], []
    saved = {}

    def prep(skill):
        for s_ in (9, 8, 12):
            if s_ not in saved:
                saved[s_] = rd("player", s_)
        setstat("player", 12, 60)  # the skills' level requirements
        setstat("player", 9, 5000 * 256)
        setstat("player", 8, 5000 * 256)
        H("setskill %d 5" % skill)

    def cast_mana(skill):
        # the mana of a cast is charged when its action starts and regenerated within a few frames: the plugin samples the
        # player's mana on every server frame (`manamin`), the lowest value since the reset is the cost
        H("manamin reset")
        ok, lines = H("cast %d self" % skill)
        log.extend(lines)
        time.sleep(2.0)
        _, ml = H("manamin")
        log.extend(ml)
        log.extend(H("manacharge")[1])
        m = re.search(r"manamin min=(-?\d+) max=(-?\d+)", " ".join(ml))
        # the pool was filled to its maximum before the cast, so the highest value seen is the start
        return (ok and "usable=0" in " ".join(lines)), (int(m.group(2)) if m else None), (int(m.group(1)) if m else None)

    goto(1)
    close_menu()
    drop_cursor_item(log)
    skill, t_ok, t0, t1 = None, False, None, None
    tried = []
    for cand, cname, *_cost in _intown_skills():
        prep(cand)
        t_ok, t0, t1 = cast_mana(cand)
        tried.append("%d %s" % (cand, cname))
        if t_ok:
            skill = cand
            break
    if skill is None:
        return add(nm, "BLOCKED", "no InTown skill could be cast in the Rogue Encampment (tried %s)" % ", ".join(tried), [], log)
    town_ok = t0 is not None and t1 is not None and t1 >= t0
    notes.append("town: skill %d cast OK (after %d tries), mana %s -> lowest %s" % (skill, len(tried), t0, t1))
    goto(2)
    close_menu()
    kill_all_near()
    prep(skill)
    f_ok, f0, f1 = cast_mana(skill)
    field_ok = f_ok and f0 is not None and f1 is not None and f0 - f1 >= 5
    notes.append("field: skill %d cast %s, mana %s -> lowest %s (cost %s)" % (skill, "OK" if f_ok else "refused", f0, f1, (f0 - f1) if f0 is not None and f1 is not None else "?"))
    kill_all_near()
    for s_, v in saved.items():
        if v is not None:
            setstat("player", s_, v)
    add(nm, "PASS" if town_ok and field_ok else "FAIL", "; ".join(notes), [], log)



def merc_dead():
    """True when the mercenary is dead (`h merc`: mode 0 / 8, or no life), None when there is none."""
    _, lines = H("merc")
    for l in lines:
        m = re.match(r"merc id=\d+ .*mode=(\d+) life=(-?\d+)/", l)
        if m:
            return int(m.group(1)) in (0, 8) or int(m.group(2)) <= 0
    return None


def revive_price_expected(level):
    """Median's mercenary revive price (D2Hirelings 0x10067450): 100 gold a level up to 105, then 250 * (level - 90)^1.5."""
    return level * 100 if level <= 105 else int(((level - 90) ** 1.5) * 250)


def check_gamble():
    name = "Gamble window (audit item 16, IT10): Gheed offers 32 items, ring and amulet among them, rare / set / unique chances from the data, a new draw on every request"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    revive_if_dead()
    goto(1)
    close_menu()
    txt = mxl_text()
    gamble_line = [ln for ln in txt.splitlines() if "gamble" in ln.lower()]
    bad, rows, log = [], [], list(gamble_line)
    if not any("gamble on" in ln for ln in gamble_line):
        bad.append("the `mxl` line does not say `gamble on`: %s" % (gamble_line or "no gamble line"))
    # open Gheed's gamble window the player's way: talk, then the Gamble line of the menu (its handler is 0x112c80)
    menu = npc_menu_near(147)
    if menu is None:
        return add(name, "BLOCKED", "Gheed's menu did not open (walked next to him, then a copy, three rounds)", log=log)
    line = None
    for ln in menu:
        m = re.search(r"line (\d+) .*handler=image\+0x112c80", ln)
        if m:
            line = int(m.group(1))
    log += menu
    if line is None:
        close_menu()
        return add(name, "FAIL", "Gheed's menu has no Gamble line (handler 0x112c80): %s" % menu[:6], log=log)
    H("menu %d" % line)
    time.sleep(2.5)
    ok, shop = H("shop 147")
    npc = next((kv(l) for l in shop if l.startswith("shop npc")), {})
    npc_id = int(npc.get("id", "0"))
    rounds, counts, firsts, same = [], {}, [], 0
    prev = None
    for i in range(13):
        if i > 0:  # the same request as the Gamble line sends: packet 0x38, kind 2, the NPC's id (little endian)
            b = [(npc_id >> (8 * k)) & 0xff for k in range(4)]
            H("pkt 38 02 00 00 00 %02x %02x %02x %02x" % tuple(b))
            time.sleep(0.6)
        ok, lines = H("gamble")
        items = [kv(l) for l in lines if l.startswith("item ")]
        n = len(items)
        codes = [it.get("code", "") for it in items]
        ids = sorted(int(it.get("id", "0")) for it in items)
        rounds.append(n)
        if i == 0:
            log += lines[:40]
        if n != 32:
            bad.append("round %d: %d offers instead of 32" % (i, n))
        if not any(c.startswith("rin") for c in codes) or not any(c.startswith("amu") for c in codes):
            bad.append("round %d: no ring or no amulet among %s" % (i, codes))
        for it in items:
            q = int(it.get("quality", "0"))
            counts[q] = counts.get(q, 0) + 1
        if len({(it.get("page"), it.get("x"), it.get("y")) for it in items}) != n:
            bad.append("round %d: two offers share a grid cell" % i)
        if prev is not None and ids == prev:
            same += 1
        prev = ids
    total = sum(counts.values())
    rare = counts.get(6, 0) * 1000 // max(total, 1)
    rows.append("offers per window %s" % sorted(set(rounds)))
    rows.append("%d offers sampled: magic %d, rare %d (%d per mille; Median's chance 250), set %d, unique %d, other %s" % (
        total, counts.get(4, 0), counts.get(6, 0), rare, counts.get(5, 0), counts.get(7, 0),
        {k: v for k, v in counts.items() if k not in (4, 5, 6, 7)}))
    if not 150 <= rare <= 350:
        bad.append("rare share %d per mille is far from Median's 250" % rare)
    if same == len(rounds) - 1:
        bad.append("the offers never changed between requests")
    rows.append("windows identical to the previous one: %d of %d" % (same, len(rounds) - 1))
    close_menu()
    add(name, "PASS" if not bad else "FAIL", "; ".join(bad + rows)[:900], [], log)


def check_engine_gaps():
    name = "Small engine rules (audit item 21): hit 20 floor 0, recast guard of srvdofunc 47 removed, player slow floor -12 / -25 / -50 by difficulty"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    revive_if_dead()
    txt = mxl_text()
    line = [ln for ln in txt.splitlines() if "engine gaps" in ln]
    bad, rows = [], []
    if not any("hit 20 floor 0x45edce on" in ln and "recast guard 0x56f859 on" in ln and "0x449f40 on" in ln for ln in line):
        bad.append("the `mxl` engine gaps line is not all `on`: %s" % (line or "missing"))
    first = None
    for diff, want in ((0, -12), (1, -25), (2, -50)):
        H("difficulty %d" % diff)
        ok, lines = H("gaps")
        d = kv(lines[0]) if lines else {}
        if first is None:
            first = d
        got = d.get("slowfloor")
        rows.append("difficulty %d: slow floor %s (want %d)" % (diff, got, want))
        if got != str(want):
            bad.append("difficulty %d: slow floor %s, expected %d" % (diff, got, want))
    H("difficulty 0")
    if first:
        rows.append("hit20 bytes %s patched=%s, recast bytes %s patched=%s" % (first.get("hit20"), first.get("hit20_patched"), first.get("recast"), first.get("recast_patched")))
        if first.get("hit20_patched") != "1" or first.get("recast_patched") != "1":
            bad.append("a byte patch is not in memory: %s" % first)
    else:
        bad.append("`h gaps` gave no answer")
    add(name, "PASS" if not bad else "FAIL", "; ".join(bad + rows), [], line + [rows[-1]] if rows else line)


def check_mercrevive():
    name = "Mercenary revive price (Median: level x 100 to 105, 250 x (level - 90)^1.5 above)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    fight_ready()  # first: it kills everything near, the mercenary too
    if not ensure_merc():
        return add(name, "BLOCKED", "no mercenary could be hired")
    saved = rd("merc", 12)
    rows, bad, log = [], [], []
    txt = mxl_text()
    if "merc revive price on" not in txt:
        bad.append("`mxl` does not say `merc revive price on`: %s" % next((l for l in txt.splitlines() if "merc revive" in l), "-"))
    for level in (1, 80, 105, 106, 126, 150):
        ok, line, _l = hv("mercprice %d" % level, "mercprice")
        want = revive_price_expected(level)
        m = re.search(r"price=(-?\d+) median=(-?\d+)", line)
        got = int(m.group(1)) if m else None
        rows.append("level %d: D2R's price function says %s (want %d)" % (level, got, want))
        log.append(line)
        if not ok or got != want:
            bad.append(rows[-1])
    setstat("merc", 12, saved or 1)
    # the price the death computes (0x4ffb90 asks the price function at the moment the mercenary dies)
    priced0 = None
    mm = re.search(r"merc revive price .*\((\d+) priced", mxl_text())
    priced0 = int(mm.group(1)) if mm else None
    for level in (126,):  # one death: the dead mercenary cannot be killed twice; 126 differs from the sweep's last level 150
        if not ensure_merc():
            bad.append("no mercenary for the death test at level %d" % level)
            continue
        setstat("merc", 12, level)
        setstat("merc", 6, 256)  # one life point left, then one ordinary hit (a huge one is ignored by the engine)
        r = None
        for _ in range(4):
            ok, line, _l = hv("hit merc phys 100000", "hit")
            r = line[:100]
            if merc_dead() is not False:
                break
            time.sleep(1)
        if merc_dead() is False:
            H("kill nearest")  # the harness' own kill of the nearest monster, which is the mercenary
        time.sleep(1)
        txt = mxl_text()
        m = re.search(r"merc revive price .*\((\d+) priced, last (-?\d+) gold for level (\d+)\)", txt)
        last = (int(m.group(2)), int(m.group(3))) if m else None
        priced1 = int(m.group(1)) if m else None
        if priced0 is not None and priced1 == priced0:
            bad.append("death at level %d: the price function was not called (priced count stays %s)" % (level, priced1))
        priced0 = priced1
        dead = merc_dead()
        rows.append("merc of level %d killed (hit %s; merc afterwards: %s): last price %s" % (level, r, {True: "dead", False: "alive", None: "gone"}[dead], last))
        log.append(rows[-1])
        if last is None or last != (revive_price_expected(level), level):
            bad.append("death at level %d: last price %s, want %s" % (level, last, (revive_price_expected(level), level)))
    # at the NPC: the menu of Kashya with the mercenary dead (the price the dialog shows and charges is the same function)
    menu = None
    try:
        goto(1)
        close_menu()
        menu = npc_menu(150)
    except Exception as e:  # the dialog is the part most likely to differ; report it, do not fail on it
        log.append("npc menu: %s" % e)
    if menu:
        log += menu
        rows.append("Kashya's menu opened with the mercenary dead (%d lines listed; the revive line is the client's own, not clicked)" % len(menu))
    close_menu()
    add(name, "PASS" if not bad else "FAIL", "; ".join(rows) if not bad else "; ".join(bad[:6]), [sh("mercrevive")], log)


def check_questxp():
    name = "Act 5 quest experience (Rite of Passage): a flat 50,000 in every difficulty, none above level 129"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    st = goto(109)
    if st["player"].get("area") != 109:
        return add(name, "BLOCKED", "could not reach Harrogath (area %s)" % st["player"].get("area"))
    close_menu()
    god(False)
    lvl0, xp0 = rd("player", 12), rd("player", 13)
    d0 = st["player"].get("difficulty", 0)
    rows, bad, log = [], [], []
    txt = mxl_text()
    if "quest experience on" not in txt:
        bad.append("`mxl` does not say `quest experience on`: %s" % next((l for l in txt.splitlines() if "quest experience" in l), "-"))
    cases = ((0, 25, 50000), (1, 45, 50000), (2, 65, 50000), (2, 130, 0), (2, 150, 0))  # difficulty, level, wanted gain
    for diff, level, want in cases:
        H("difficulty %d" % diff)
        setstat("player", 12, level)
        setstat("player", 13, 1000)
        H("quest 39 0 clear")
        H("quest 39 13 clear")
        ok, line, lines = hv("a5done", "a5done")
        log += lines
        m = re.search(r"level (\d+)->(\d+) xp (-?\d+)->(-?\d+) gained=(-?\d+)", line)
        gained = int(m.group(5)) if m else None
        rows.append("difficulty %d level %d: gained %s (want %d)" % (diff, level, gained, want))
        if not ok or gained != want:
            bad.append(rows[-1] + " [%s]" % line)
    txt = mxl_text()
    log += [l for l in txt.splitlines() if "quest experience" in l]
    H("difficulty %d" % d0)
    setstat("player", 12, lvl0 or 1)
    setstat("player", 13, xp0 or 0)
    H("quest 39 0 clear")
    add(name, "PASS" if not bad else "FAIL", "`h a5done` (D2R's own Rite of Passage completion): %s" % ("; ".join(rows) if not bad else "; ".join(bad[:6])),
        [sh("questxp")], log)


def check_questrewards():
    name = "Quest rewards D2Sigma removes: Qual-Kehk runes, Akara ring, Andariel chipped gems, Hellforge and Countess classes"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    rows, bad, log = [], [], []
    m0 = mxl_mark()
    txt = mxl_text()
    qr = next((l for l in txt.splitlines() if "quest rewards" in l), "")
    log.append(qr)
    if "quest rewards on" not in qr:
        bad.append("`mxl`: %s" % (qr or "no quest rewards line"))
    runes = ("r07", "r08", "r09", "r10")

    def count(code):
        return len(items(hstate(), code)) + ground_count(code)

    def ground_total():
        for l in H("dump ground")[1]:
            m = re.match(r"ground items (\d+)", l)
            if m:
                return int(m.group(1))
        return 0

    # Qual-Kehk (Normal): Median's r08 r09 r10 only; D2R would add r07 r08 r09 (r07 absent, r08 / r09 once)
    st = goto(109)
    if st["player"].get("area") == 109:
        close_menu()
        H("difficulty 0")
        H("packclear")
        for r in runes:
            H("remove %s 6" % r)
        H("quest 36 0 clear")
        H("quest 36 1 set")
        H("quest 36 6 clear")
        H("quest 36 7 clear")
        before = {r: count(r) for r in runes}
        if not H("interact 515")[0]:  # the message packet is handled for the NPC the player is talking to
            H("spawn 515")
            H("interact 515")
        time.sleep(3)
        ok, ml = H("msg 515 0x4e8e")
        time.sleep(3)
        got = {r: count(r) - before[r] for r in runes}
        rows.append("Qual-Kehk gave %s" % " ".join("%s x%d" % (r, got[r]) for r in runes))
        log += ml
        if not ok:
            bad.append("Qual-Kehk could not be reached (msg 515)")
        elif got != {"r07": 0, "r08": 1, "r09": 1, "r10": 1}:
            bad.append("Qual-Kehk runes %s (want r08 r09 r10 once each, no r07)" % got)
    else:
        bad.append("could not reach Harrogath")

    # Akara: no ring (the Heart is the reward)
    goto(1)
    close_menu()
    H("quest 4 1 set")
    H("packclear")
    H("remove 600 6")
    H("remove rin 6")
    before = count("rin")
    if not H("interact 148")[0]:
        H("spawn 148")
    ok, ml = H("msg 148 0x76")
    time.sleep(3)
    ring, heart = count("rin") - before, count("600")
    rows.append("Akara gave %d rings and %d hearts" % (ring, heart))
    log += ml
    if ring != 0:
        bad.append("Akara still gave a ring")
    close_menu()

    # Andariel: the two chipped gems of the vanilla quest drop are gone (three kills would leave 6 or more of them)
    gems = ("gcv", "gcy", "gcb", "gcg", "gcr", "skc")
    H("difficulty 0")
    H("quest 47 0 clear")
    n0 = sum(count(c) for c in gems)
    for _ in range(3):
        H("spawn 156")
        H("kill nearest 156")
        time.sleep(1)
    n1 = sum(count(c) for c in gems) - n0
    rows.append("three Andariel kills dropped %d chipped gems (vanilla: at least 6, two a kill; the standard roll can add a few)" % n1)
    if n1 >= 6:
        bad.append("Andariel dropped %d chipped gems in three kills" % n1)

    # Hellforge (Hephasto 376) and the Countess (45): the plugin rolls Median's classes (counters in `mxl`)
    def counters():
        t = mxl_text()
        m = re.search(r"quest rewards .*\(hellforge (\d+), countess (\d+), prison of ice (\d+)\)", t)
        return tuple(int(x) for x in m.groups()) if m else None

    c0 = counters()
    g0 = ground_total()
    # the forge is an object (class 376) that the harness cannot spawn: the reward function runs on a monster standing in for it
    H("spawn 1")
    okh, lh = H("hellforgereward 1")
    time.sleep(1)
    c1 = counters()
    g1 = ground_total()
    rows.append("Hellforge reward function run (%s): hellforge counter %s -> %s, ground items %d -> %d" % ("ok" if okh else "failed", c0, c1, g0, g1))
    log += lh
    if not (c0 and c1 and c1[0] == c0[0] + 1 and g1 > g0):
        bad.append("Hellforge class not rolled: counters %s -> %s, ground items %d -> %d" % (c0, c1, g0, g1))
    # Anya's Prison of Ice reward task: D2R builds a ring request, the hook makes it an amulet
    H("packclear")
    a0 = count("amu")
    okp, lp = H("prisonreward")
    time.sleep(1)
    a1 = count("amu")
    c3 = counters()
    rows.append("Prison of Ice reward task run (%s): amulets %d -> %d, rewritten counter %s" % ("code rewritten" if okp else "code NOT rewritten", a0, a1, c3[2] if c3 else None))
    log += lp
    if not okp or a1 != a0 + 1:
        bad.append("Prison of Ice reward: amulets %d -> %d (%s)" % (a0, a1, lp[:1]))
    # a Countess spawned by the harness is not on quest 5's unit list (see check_tower): the kill never reaches the quest
    # handler, so the reward function runs on its own with the kill context set by hand
    c1, g1 = counters(), ground_total()
    H("spawn 45")
    okc, lc = H("countessreward")
    time.sleep(1)
    c2 = counters()
    g2 = ground_total()
    rows.append("Countess reward: counters %s -> %s, ground items %d -> %d" % (c1, c2, g1, g2))
    log += lc
    if not (c1 and c2 and c2[1] == c1[1] + 1 and g2 > g1):
        bad.append("Countess class not rolled: counters %s -> %s, ground items %d -> %d" % (c1, c2, g1, g2))
    log += [l for l in mxl_lines_since(m0) if re.search(r"hellforge|countess|qual-kehk|quest exp", l)]
    add(name, "PASS" if not bad else "FAIL", "; ".join(rows) if not bad else "; ".join(bad[:6]) + " | " + "; ".join(rows), [sh("questrewards")], log)


def _collide_table():
    """`collidetable` parsed: {"server": {type: (fn, mask)}, "client": {...}} (hex), or None when the tables are not moved."""
    ok, lines = H("collidetable")
    out = {}
    for ln in lines:
        m = re.match(r"collidetable (server|client):\s+tables moved(.*)", ln)
        if m:
            out[m.group(1)] = {int(t): (int(fn, 16), int(mask, 16)) for t, fn, mask in re.findall(r"(\d+):([0-9a-f]+)/([0-9a-f]+)", m.group(2))}
    return out if "server" in out and "client" in out else None


# D2R's own nine rows (type: fn RVA, mask) of the server and client tables (collide_types_logic.h)
_COLLIDE_D2R = {
    "server": {0: (0, 0), 1: (0x457c70, 0x84), 2: (0x457be0, 0x104), 3: (0x457d10, 0x184), 4: (0, 0), 5: (0x457be0, 0x104), 6: (0, 4),
               7: (0x457da0, 0x40), 8: (0x457d10, 0x185)},
    "client": {0: (0, 0), 1: (0x1b0080, 0x84), 2: (0x1b0020, 0x104), 3: (0x1b00f0, 0x184), 4: (0, 0), 5: (0x1b0020, 0x104), 6: (0, 4),
               7: (0x1b0150, 0x40), 8: (0x1b00f0, 0x185)},
}


def _collide_fire(row, dx, dy, ctype, wait=14):
    """`collide`: one missile of `row` towards (+dx, +dy) of the player with collision type `ctype`; waits until it is gone.
    Returns the `collide result` dict (frames, dist, ...) or None, and the log lines."""
    ok, lines = H("collide %d %d %d %d" % (row, dx, dy, ctype))
    if not ok:
        return None, lines
    res, t0 = None, time.time()
    while time.time() - t0 < wait:
        time.sleep(0.5)
        _ok, rl = H("collide result")
        lines += rl
        for ln in rl:
            if ln.startswith("collide result:"):
                res = {k: int(float(v)) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", ln)}
        if res and res.get("flying") == 0 and res.get("frames", 0) > 0:
            return res, lines
    return res, lines


def _collide9_rows():
    """missiles.txt rows (0-based data rows = *ID) of the built mod with CollideType 9."""
    t = _tsv(os.path.join(BUILT_EXCEL, "missiles.txt"))
    h = t[0]
    i = h.index("CollideType")
    return [n for n, r in enumerate(t[1:]) if len(r) > i and r[i] == "9"]


def check_collidetypes():
    """Median's missile collide types 9 and 10 (D2Sigma D2Missile collide table, 11 types; audit item 14): D2R's tables hold nine
    rows, so the plugin moves them to 256-row copies (collide_types.cpp) with Median's rows added: type 9 = mask 0x180 and the
    check function of type 3 (no wall bit 4: the missile flies through walls and stops at monsters), type 10 = mask 0x2080 and the
    function of type 1. Checks: (1) both tables moved, rows 0-8 the engine's own, 9 and 10 as above, 255 empty; (2) the 15
    missiles.txt rows with CollideType 9 still have 9 in D2R's loaded records; (3) a plain arrow (row 0, type 3) is fired with its
    type set to 3, 8 and 9 at a monster: all stop at it (type 6, which the tick skips, flies on); (4) the same towards a wall (the
    tick's own wall test, bit 4 of the mask): D2R's movement ends every type at the wall (a plain arrow cannot show the missing
    wall bit of 9 / 10), so 1, 8, 9 and 10 must end where type 3 does."""
    nm = "Missile collide types 9 / 10 (Median): table rows, stop at monsters, pass walls"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    problems, notes, log = [], [], []
    tab = _collide_table()
    if tab is None:
        return add(nm, "FAIL", "plugin part collidetypes is not on (`collidetable` says the tables are not moved; mxl: %s)" % [
            ln[:80] for ln in H("mxl")[1] if "collide" in ln])
    for side in ("server", "client"):
        t = tab[side]
        for ty, want in _COLLIDE_D2R[side].items():
            if t.get(ty) != want:
                problems.append("%s table row %d is %s, the engine has %s" % (side, ty, t.get(ty), want))
        if t.get(9, (None, None))[1] != 0x180 or t.get(9, (None,))[0] != t.get(3, (0,))[0]:
            problems.append("%s row 9 is %s, want mask 0x180 and the function of row 3 %s" % (side, t.get(9), t.get(3)))
        if t.get(10, (None, None))[1] != 0x2080 or t.get(10, (None,))[0] != t.get(1, (0,))[0]:
            problems.append("%s row 10 is %s, want mask 0x2080 and the function of row 1 %s" % (side, t.get(10), t.get(1)))
        if t.get(255) != (0, 0):
            problems.append("%s row 255 is %s, want an empty row" % (side, t.get(255)))
    notes.append("tables: server %s client %s" % ({k: (hex(v[0]), hex(v[1])) for k, v in tab["server"].items() if k in (3, 9, 1, 10)},
                                                   {k: (hex(v[0]), hex(v[1])) for k, v in tab["client"].items() if k in (3, 9)}))
    _ok, rl = H("collidetable rows")
    notes.append("loaded records by CollideType: %s" % " ".join(rl)[:200])
    rows9 = _collide9_rows()
    got = {}
    for r in rows9:
        _ok, ql = H("collidetable row %d" % r)
        m = re.search(r"CollideType (-?\d+)", " ".join(ql))
        got[r] = int(m.group(1)) if m else None
    bad = {r: v for r, v in got.items() if v != 9}
    notes.append("built missiles.txt has %d rows with CollideType 9; loaded records say %s" % (len(rows9), sorted(set(got.values()), key=str)))
    if not rows9:
        problems.append("no built missiles.txt row has CollideType 9")
    elif bad:
        problems.append("%d of %d rows with CollideType 9 in missiles.txt load as another type: %s" % (len(bad), len(rows9), dict(list(bad.items())[:8])))
    goto(2)
    kill_all_near()
    god(True)
    try:
        # --- monsters: the monster stands between the player and the target
        want_stop = {3: True, 8: True, 9: True, 6: False}
        mon_notes = []
        for ty, stops in want_stop.items():
            verdict = None
            for attempt in range(6):
                kill_all_near()
                if spawn_id(18) is None:
                    continue
                time.sleep(0.5)
                st = hstate()
                p = st["player"]
                mons = [m for m in st["mons"] if "mx" in m and "my" in m]
                if not mons:
                    continue
                mo = min(mons, key=lambda m: (m["mx"] - p["x"]) ** 2 + (m["my"] - p["y"]) ** 2)
                vx, vy = mo["mx"] - p["x"], mo["my"] - p["y"]
                d0 = (vx * vx + vy * vy) ** 0.5
                if d0 < 7 or d0 > 18:  # the missile starts a few sub-tiles ahead of the player
                    continue
                k = 40.0 / d0
                res, ml = _collide_fire(0, int(vx * k), int(vy * k), ty)
                log += ml
                if not res:
                    continue
                dist = res.get("dist", 0)
                mon_notes.append("type %d: monster at %.0f, missile ended after %d frames at %d" % (ty, d0, res.get("frames", 0), dist))
                if stops and dist <= d0 + 8:
                    verdict = True
                    break
                if not stops and dist >= d0 + 12:
                    verdict = True
                    break
                verdict = False
            if not verdict:
                problems.append("type %d %s the monster (%s)" % (ty, "did not stop at" if stops else "did not pass", mon_notes[-1] if mon_notes else "no shot"))
        notes.append("monsters: " + "; ".join(mon_notes))
        kill_all_near()
        # --- walls: first area with a wall 6-20 sub-tiles away in one of the eight directions
        wall = None
        for area in (2, 3, 8, 9, 4):
            if hstate()["player"].get("area") != area:
                goto(area)
                kill_all_near()
                god(True)
            ok, wl = H("wallscan")
            log += wl
            mb = [re.match(r"wallscan best (-?\d+) (-?\d+) wall=(\d+)", ln) for ln in wl]
            mb = [m for m in mb if m]
            if mb:
                wall = (int(mb[0].group(1)), int(mb[0].group(2)), int(mb[0].group(3)), area)
                break
        if wall is None:
            problems.append("no wall 6-20 sub-tiles from the player found in areas 2 3 8 9 4 (wallscan)")
        else:
            wx, wy, wk, area = wall
            wall_dist = wk * (1.41 if wx and wy else 1)
            notes.append("wall: area %d direction %d,%d at %d sub-tiles (%.0f away), room cells beyond it" % (area, wx, wy, wk, wall_dist))
            ends = {}
            for ty in (3, 1, 8, 9, 10, 6, 4):
                res, wl2 = _collide_fire(0, wx * 40, wy * 40, ty)
                log += wl2
                ends[ty] = res.get("dist", -1) if res else -1
                notes.append("type %d: ended at %d after %s frames" % (ty, ends[ty], res.get("frames") if res else "?"))
                if res is None:
                    problems.append("type %d towards the wall: no result" % ty)
            # D2R's missile movement ends every type at a wall (types 4 and 6 have no wall test in the tick and end there too),
            # so Median's missing wall bit in type 9 / 10 shows as no difference for a plain arrow: they must end where type 3
            # does, neither earlier nor beyond the wall
            for ty in (1, 8, 9, 10):
                if ends[ty] >= 0 and ends[3] >= 0 and abs(ends[ty] - ends[3]) > 3:
                    problems.append("type %d ended at %d towards the wall, type 3 at %d" % (ty, ends[ty], ends[3]))
            if ends[3] > wall_dist + 4:
                problems.append("type 3 flew through the wall (wall %.0f, ended %d)" % (wall_dist, ends[3]))
    finally:
        god(False)
        kill_all_near()
    add(nm, "FAIL" if problems else "PASS", "%s | %s" % ("; ".join(problems) if problems else "rows, monster stops and wall passes as Median", " | ".join(notes)),
        [sh("collidetypes")], log)


def _state_id(name):
    """Row of a state in the built states.txt (skills.txt names its aurastate)."""
    if name.isdigit():
        return int(name)
    t = _tsv(os.path.join(BUILT_EXCEL, "states.txt"))
    for i, r in enumerate(t[1:]):
        if r and r[0] == name:
            return i
    return None


def _has_state(state):
    """`hasstate`: (on, list present, expiry frame, current frame)."""
    _ok, lines = H("hasstate %d" % state)
    m = re.search(r"hasstate \d+: (\d) list=(yes|no) expiry=(-?\d+) frame=(\d+)", " ".join(lines))
    return (int(m.group(1)), m.group(2) == "yes", int(m.group(3)), int(m.group(4))) if m else None


def _startfn(slot, skill, level):
    ok, lines = H("startfn %d %d %d" % (slot, skill, level))
    m = re.search(r"returned (-?\d+) frame=(\d+)", " ".join(lines))
    return (int(m.group(1)), int(m.group(2))) if m else None


def _start_count(slot):
    """Starts the plugin counted for a slot (`startstats`)."""
    _ok, lines = H("startstats")
    m = re.search(r"slot %d (\d+) starts" % slot, " ".join(lines))
    return int(m.group(1)) if m else None


def check_skillstarts():
    """D2Sigma's own skill start functions D2R lacks (srvstfunc 30 and 66; audit item 20): txt_migrate moves Median's skills with 30
    (Flash, Move Only) to the free slot 71 and with 66 (sk851) to 72; the plugin fills those slots (skill_starts.cpp). Slot 71:
    the caster gets the skill's aurastate until frame + auralencalc (a skill without a state does not start); slot 72 the same
    but refuses while the state is on. Checks the built data, the mxl line, then calls both slots the way the cast path does
    (`startfn`) and reads the state back (`hasstate`): Flash's state is on for its length and clears; Move Only is refused; sk851
    (25*lvl frames) is refused while on and starts again after it ended; a real cast of sk851 runs the slot."""
    nm = "Skill start functions (Median srvstfunc 30 / 66 -> slots 71 / 72): timed state on the caster"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    problems, notes, log = [], [], []
    by_name, t, h = _skill_ids_by_name()
    col = {n: h.index(n) for n in ("srvstfunc", "aurastate", "auralencalc")}
    want = {"flash": ("71", "19"), "skills_851": ("72", "25*lvl"), "move_only": ("71", "")}
    ids = {}
    for name, (slot, _len) in want.items():
        row = next((r for r in t[1:] if r and r[0].lower() == name), None)
        if row is None:
            problems.append("no %s row in the built skills.txt" % name)
            continue
        ids[name] = int(row[h.index("*Id")])
        if row[col["srvstfunc"]] != slot:
            problems.append("%s has srvstfunc %s in the built skills.txt, want %s" % (name, row[col["srvstfunc"]], slot))
    oath = next((r for r in t[1:] if r and r[0].lower() == "blood oath"), None)
    if oath is not None and oath[col["srvstfunc"]] != "66":
        problems.append("Blood Oath (D2R's own 66) has srvstfunc %s, want it to keep 66" % oath[col["srvstfunc"]])
    xl = [ln for ln in mxl_text().splitlines() if "skill starts" in ln]
    notes.append("mxl: %s" % (xl[0][:110] if xl else "no 'skill starts' line"))
    if not xl or "(2/2)" not in xl[0]:
        problems.append("mxl line 'skill starts ... (2/2)' missing: %s" % xl)
    if len(ids) < 3:
        return add(nm, "FAIL", "; ".join(problems))
    fl = next(r for r in t[1:] if r and r[0].lower() == "flash")
    sk = next(r for r in t[1:] if r and r[0].lower() == "skills_851")
    st_flash, st_851 = _state_id(fl[col["aurastate"]]), _state_id(sk[col["aurastate"]])
    notes.append("Flash state %s (%s), sk851 state %s (%s)" % (st_flash, fl[col["aurastate"]], st_851, sk[col["aurastate"]]))
    if st_flash is None or st_851 is None:
        return add(nm, "FAIL", "aurastate not found in states.txt: " + "; ".join(problems + notes))
    goto(2)
    kill_all_near()
    god(True)
    try:
        for s in (st_flash, st_851):  # nothing on from an earlier run (the states end by themselves within seconds)
            for _ in range(10):
                if not (_has_state(s) or (0,))[0]:
                    break
                time.sleep(1)
        # slot 71 / Flash: level 1, length 19 frames
        r = _startfn(71, ids["flash"], 1)
        s1 = _has_state(st_flash)
        notes.append("slot 71 flash: %s, state %s" % (r, s1))
        if r is None or r[0] != 1 or not s1 or not s1[0] or not s1[1]:
            problems.append("slot 71 Flash: start %s, state %s (want 1, on, with a stat list)" % (r, s1))
        elif not 15 <= s1[2] - r[1] <= 22:
            problems.append("Flash's state ends %d frames after the call, want 19 (auralencalc)" % (s1[2] - r[1]))
        r2 = _startfn(71, ids["flash"], 1)
        s2 = _has_state(st_flash)
        if r2 is None or r2[0] != 1 or not s2 or s2[2] < s1[2]:
            problems.append("slot 71 again: %s, expiry %s -> %s (a second start prolongs the state)" % (r2, s1 and s1[2], s2 and s2[2]))
        time.sleep(2.5)  # 19 frames = 0.8 s
        s3 = _has_state(st_flash)
        notes.append("2.5 s later: %s" % (s3,))
        if not s3 or s3[0]:
            problems.append("Flash's state still on 2.5 s after a 19-frame start: %s" % (s3,))
        # slot 71 / Move Only: no aurastate, the cast does not start
        r = _startfn(71, ids["move_only"], 1)
        notes.append("slot 71 move_only: %s" % (r,))
        if r is None or r[0] != 0:
            problems.append("Move Only start returned %s, want 0 (no state)" % (r,))
        # slot 72 / sk851 level 3: 75 frames, refused while on
        r = _startfn(72, ids["skills_851"], 3)
        s1 = _has_state(st_851)
        notes.append("slot 72 sk851 level 3: %s, state %s" % (r, s1))
        if r is None or r[0] != 1 or not s1 or not s1[0]:
            problems.append("slot 72 sk851: start %s, state %s (want 1, on)" % (r, s1))
        elif not 70 <= s1[2] - r[1] <= 77:
            problems.append("sk851's state ends %d frames after the call, want 75 (25*lvl)" % (s1[2] - r[1]))
        r2 = _startfn(72, ids["skills_851"], 3)
        s2 = _has_state(st_851)
        notes.append("second call while on: %s, expiry %s -> %s" % (r2, s1 and s1[2], s2 and s2[2]))
        if r2 is None or r2[0] != 0 or (s1 and s2 and s2[2] != s1[2]):
            problems.append("sk851 while the state is on: %s, expiry %s -> %s (want refused, expiry unchanged)" % (r2, s1 and s1[2], s2 and s2[2]))
        time.sleep(4.5)  # 75 frames = 3 s
        s3 = _has_state(st_851)
        r3 = _startfn(72, ids["skills_851"], 1)
        s4 = _has_state(st_851)
        notes.append("4.5 s later: %s; start again: %s state %s" % (s3, r3, s4))
        if not s3 or s3[0]:
            problems.append("sk851's state still on 4.5 s after a 75-frame start: %s" % (s3,))
        if r3 is None or r3[0] != 1 or not s4 or not s4[0]:
            problems.append("sk851 after the state ended: %s, state %s (want a new start)" % (r3, s4))
        # a real cast of Flash (leftskill / rightskill): the cast path runs slot 71 (D2R's own start sequence)
        n0 = _start_count(71)
        H("setskill %d 5" % ids["flash"])
        H("stat player 8 set %d" % (rd("player", 9) or 0))
        ok, cl = H("cast %d self" % ids["flash"])
        time.sleep(1)
        n1 = _start_count(71)
        log += cl
        notes.append("real cast of Flash: ok=%s slot 71 starts %s -> %s; %s" % (ok, n0, n1, cl[-1][:100] if cl else ""))
        if n0 is None or n1 is None or n1 <= n0:
            problems.append("a real cast of Flash did not reach slot 71 (starts %s -> %s; cast lines %s)" % (n0, n1, [ln[:110] for ln in cl[-3:]]))
    finally:
        god(False)
    add(nm, "FAIL" if problems else "PASS", "%s | %s" % ("; ".join(problems) if problems else "start functions as D2Sigma", " | ".join(notes)),
        [sh("skillstarts")], log)


def _storm_count(skill, seconds):
    """Missile units the server frames saw in `seconds` after one cast of `skill` at the player."""
    H("missiles reset")
    ok, lines = H("cast %d self" % skill)
    time.sleep(seconds)
    _ok, ml = H("missiles")
    m = [re.search(r"seen=(\d+)", ln) for ln in ml if ln.startswith("missiles ")]
    return (int(m[0].group(1)) if m and m[0] else 0), ok, lines


def check_activationfreq():
    """Activation frequency (stat 427) on periodic skills (audit item 13): D2Sigma's formula (perdelay shortened by stat 427) sits in
    a patched path the game never reaches and no code reads stat 427 on the periodic events, so Median's real rate does NOT depend
    on the stat; the tooltip line is its only effect. Parity check: a periodic storm skill (Sentence, dofunc 29, perdelay 5) cast
    at the player with stat 427 = 0 and = 200: the missile counts over the same window must stay within 40 % of each other (a
    working +200 % rate would double them)."""
    nm = "Activation frequency (stat 427): periodic skill rate unchanged, as Median"
    if not need_game():
        return add(nm, "BLOCKED", "not in a game")
    by_name, t, h = _skill_ids_by_name()
    sk = by_name.get("sentence")
    if sk is None:
        return add(nm, "BLOCKED", "no sentence row in skills.txt")
    goto(2)
    kill_all_near()
    god(True)
    notes, counts, log = [], {}, []
    try:
        H("setskill %d 10" % sk)
        base427 = rd("player", 427)
        for val in (0, 200, 0, 200):
            setstat("player", 427, val)
            H("stat player 8 set %d" % (rd("player", 9) or 0))
            time.sleep(6)  # the earlier storm ends
            for _m in range(3):
                spawn_id(18)
            n, ok, cl = _storm_count(sk, 6)
            kill_all_near()
            log += cl
            counts.setdefault(val, []).append(n)
        setstat("player", 427, base427 or 0)
    finally:
        god(False)
    a, b = sum(counts[0]), sum(counts[200])
    notes.append("missiles in 6 s after a cast of Sentence (%d): stat 427 = 0: %s, = 200: %s" % (sk, counts[0], counts[200]))
    if a < 4:
        return add(nm, "FAIL", "the control produced only %d missiles: no measurement | %s" % (a, notes[0]), [sh("activationfreq")], log)
    ratio = b / float(a)
    ok = 0.6 <= ratio <= 1.4
    add(nm, "PASS" if ok else "FAIL", "stat 427 = 200 gave %.2f x the missiles of stat 427 = 0 (want about 1: no code reads the stat; Median's tooltip line only) | %s" % (ratio, notes[0]),
        [sh("activationfreq")], log)


def _spawn_ids(cls, count):
    """Spawns `count` monsters of a class; returns their unit ids (the `spawned class N id=M` lines)."""
    ok, lines = H("spawn %d %d" % (cls, count))
    return [int(m) for m in re.findall(r"spawned class \d+ id=(\d+)", " ".join(lines))]


def _monlevel_tables():
    """(area id -> [MonLvlEx per difficulty], monstats class -> {"level": [Level, Level(N), Level(H)], "lundead": bool, "npc": bool})
    from the built mod (the expansion's area levels: D2R's area-level function reads the Ex columns)."""
    lv = _tsv(_main_checkout(os.path.join(BUILT_EXCEL, "levels.txt")))
    h = lv[0]
    ci = {n: i for i, n in enumerate(h)}
    areas = {}
    for r in lv[1:]:
        if len(r) > ci["MonLvlEx(H)"] and r[ci["Id"]].isdigit():
            areas[int(r[ci["Id"]])] = [int(r[ci[c]] or 0) for c in ("MonLvlEx", "MonLvlEx(N)", "MonLvlEx(H)")]
    ms = _tsv(_main_checkout(os.path.join(BUILT_EXCEL, "monstats.txt")))
    mh = ms[0]
    mi = {n: i for i, n in enumerate(mh)}
    mons = {}
    for cls, r in enumerate(ms[1:]):
        if len(r) > mi["Level(H)"]:
            mons[cls] = {"level": [int(r[mi[c]] or 0) for c in ("Level", "Level(N)", "Level(H)")],
                         "lundead": (r[mi["lUndead"]] or "0") == "1", "npc": (r[mi["npc"]] or "0") == "1"}
    return areas, mons


def check_monlevel():
    name = "Monster level and players count (Median: the area level in every difficulty, life +(n-1)*50 %, no experience bonus)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    txt = mxl_text()
    bad, rows, log = [], [], []
    for word in ("monster level on", "players scaling on"):
        if word not in txt:
            bad.append("`mxl` does not say `%s`: %s" % (word, next((l for l in txt.splitlines() if word.split()[0] in l and word.split()[1] in l), "-")))
    areas, mons = _monlevel_tables()
    classes = (18, 0)  # a living class and a low-undead one (monstats flag 0x40: opted out of the area level before)
    god(True)
    try:
        H("players 1")
        for area in (2, 76, 111):
            goto(area)
            for diff in (0, 1, 2):
                H("difficulty %d" % diff)
                for cls in classes:
                    ids = _spawn_ids(cls, 1)
                    if not ids:
                        bad.append("area %d diff %d class %d: nothing spawned" % (area, diff, cls))
                        continue
                    got = rd("m%d" % ids[0], 12)
                    want = areas[area][diff]
                    if mons[cls]["npc"]:
                        want = mons[cls]["level"][diff]
                    rows.append("area %d %s class %d: level %s (area level %d, own row %d)" % (area, ("Normal", "Nightmare", "Hell")[diff], cls, got, areas[area][diff], mons[cls]["level"][diff]))
                    log.append(rows[-1])
                    if got != want:
                        bad.append(rows[-1] + ", want %d" % want)
                    H("kill %d" % ids[0])
        # players-count scaling: eight monsters at each setting, in Hell (the levels stay the same)
        goto(2)
        H("difficulty 2")
        stats = {}
        for players in (1, 8):
            ok, line, _l = hv("players %d" % players, "players")
            log.append(line)
            ids = _spawn_ids(18, 8)
            life = [rd("m%d" % i, 7) for i in ids]
            life = [v >> 8 for v in life if v]
            exp = {rd("m%d" % i, 13) for i in ids}
            lvl = {rd("m%d" % i, 12) for i in ids}
            n = {rd("m%d" % i, 100) for i in ids}
            stats[players] = (sum(life) / len(life) if life else 0, exp, lvl, n)
            log.append("players %d: %d monsters, mean max life %.0f, experience %s, level %s, players stat %s" % (players, len(ids), stats[players][0], sorted(exp), sorted(lvl), sorted(n)))
            kill_all_near()
        (l1, e1, v1, n1), (l8, e8, v8, n8) = stats[1], stats[8]
        ratio = l8 / l1 if l1 else 0
        rows.append("players 1 -> 8: life x%.2f (Median 4.50), experience %s -> %s, level %s -> %s, players stat %s -> %s" % (ratio, sorted(e1), sorted(e8), sorted(v1), sorted(v8), sorted(n1), sorted(n8)))
        if not (4.0 <= ratio <= 5.0):
            bad.append("life ratio 8 players / 1 player is %.2f, Median's is 4.50" % ratio)
        if e1 != e8 or len(e1) != 1:
            bad.append("experience differs with the players count: %s -> %s" % (sorted(e1), sorted(e8)))
        if v1 != v8:
            bad.append("level differs with the players count: %s -> %s" % (sorted(v1), sorted(v8)))
        if n1 != {1} or n8 != {8}:
            bad.append("the players stat is %s / %s, want 1 / 8 (the players setting did not reach the monsters)" % (sorted(n1), sorted(n8)))
        txt = mxl_text()
        log += [l for l in txt.splitlines() if "players scaling" in l or "monster level" in l]
    finally:
        H("players 1")
        H("difficulty 0")
        kill_all_near()
        god(False)
    add(name, "PASS" if not bad else "FAIL", "; ".join(rows[-4:]) if not bad else "; ".join(bad[:6]), [sh("monlevel")], log + rows)


def check_normaltc():
    name = "Normal-difficulty drops use the level-upgraded treasure class (Median 0x12d6a)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    txt = mxl_text()
    bad, rows, log = [], [], []
    if "normal-difficulty treasure class on" not in txt:
        bad.append("`mxl` does not say `normal-difficulty treasure class on`: %s" % next((l for l in txt.splitlines() if "treasure class" in l), "-"))
    god(True)
    try:
        goto(4)  # Wilderness 3: Normal monsters of level 6 (Median's area table)
        for diff in (0, 1):  # Normal, then Nightmare (the executor's own behaviour: the control)
            H("difficulty %d" % diff)
            H("tctrace reset")
            ids = _spawn_ids(18, 1)
            if not ids:
                bad.append("difficulty %d: nothing spawned" % diff)
                continue
            level = rd("m%d" % ids[0], 12)
            H("kill %d" % ids[0])
            time.sleep(1)
            ok, line, _l = hv("tctrace", "tctrace")
            m = re.search(r"calls=(\d+) leveled=(\d+) lastclass=(-?\d+) lastlevel=(-?\d+)", line)
            log.append("%s: killed a level %s monster; %s" % (("Normal", "Nightmare")[diff], level, line))
            if not m:
                bad.append("difficulty %d: no tctrace line (%s)" % (diff, line[:80]))
                continue
            calls, leveled, tc, lvl = (int(x) for x in m.groups())
            rows.append("%s kill of a level %s monster: %d class lookups, %d with a level, last class %d level %d" % (("Normal", "Nightmare")[diff], level, calls, leveled, tc, lvl))
            if calls < 1 or leveled < 1 or lvl != level:
                bad.append(rows[-1] + " (want the monster's level handed to the class getter)")
    finally:
        H("difficulty 0")
        kill_all_near()
        god(False)
    add(name, "PASS" if not bad else "FAIL", "; ".join(rows) if not bad else "; ".join(bad[:6]), [sh("normaltc")], log)


def _leash_probe(dist, seconds=2.5):
    """Moves the player `dist` sub-tiles from the mercenary, then samples the Euclidean distance every harness round trip.
    Returns (placed distance, samples [(t, euclid)], largest single drop between two samples, the log lines)."""
    ok, line, _l = hv("leash %d" % dist, "leash")
    m = re.search(r"euclid=([\d.]+)", line)
    if not ok or not m:
        return None, [], 0, [line]
    start = float(m.group(1))
    t0 = time.time()
    samples, log = [(0.0, start)], [line]
    while time.time() - t0 < seconds:
        ok, line, _l = hv("leash", "leash")
        m = re.search(r"euclid=([\d.]+)", line)
        if m:
            samples.append((round(time.time() - t0, 2), float(m.group(1))))
            log.append(line)
    drop = max([a[1] - b[1] for a, b in zip(samples, samples[1:])] or [0])
    return start, samples, drop, log


def check_leash():
    name = "Summon / hireling leash (Median: warp to the owner past 40 sub-tiles)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    txt = mxl_text()
    bad, rows, log = [], [], []
    if "summon leash on" not in txt:
        bad.append("`mxl` does not say `summon leash on`: %s" % next((l for l in txt.splitlines() if "leash" in l), "-"))
    fight_ready()
    if not ensure_merc():
        return add(name, "BLOCKED", "no mercenary could be hired")
    god(True)
    try:
        # one probe per distance: the mercenary is placed that far from the player (moving the player would pull the mercenary
        # along at once); a warp is a jump of the distance, a walk a few sub-tiles per sample
        for dist, warps in ((20, False), (30, False), (36, False), (44, True), (47, True), (60, True)):
            start, samples, drop, lines = _leash_probe(dist)
            log += lines
            if start is None:
                bad.append("distance %d: the player could not be placed (%s)" % (dist, lines[0][:100]))
                continue
            warped = drop >= 15
            rows.append("mercenary %d sub-tiles from the player: %s (largest drop between two samples %.0f, %.1f after %.1f s)" % (
                dist, "warped to the owner" if warped else "walked back", drop, samples[-1][1], samples[-1][0]))
            if warped != warps:
                bad.append(rows[-1] + ", want %s" % ("a warp" if warps else "a walk"))
    finally:
        kill_all_near()
        god(False)
    add(name, "PASS" if not bad else "FAIL", "; ".join(rows) if not bad else "; ".join(bad[:6]), [sh("leash")], log)


BUFF_TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "plugin", "src", "mxl_state_icons.h")


def _state_icon_rows():
    """The generated table (tools/state_icons.py): [(state, side, tint, frame, tooltip key)] in drawing order."""
    with open(BUFF_TABLE, encoding="utf-8") as f:
        return [(int(a), int(b), int(c), int(d), e) for a, b, c, d, e in
                re.findall(r'\{(\d+), (\d), (\d+), (\d+), "([^"]*)"\}', f.read())]


def _buff_icons():
    """`h bufficons` parsed: (counters dict, [icon dicts])."""
    ok, lines = H("bufficons")
    head = dict(kv(lines[0])) if lines and lines[0].startswith("bufficons ") else {}
    icons = [{k: int(v) for k, v in kv(ln).items()} for ln in lines if ln.startswith("bufficons icon ")]
    return head, icons


def check_bufficons():
    name = "Buff / debuff icon bar: Median's two icon groups, timers and hover tooltip (buff_icons.cpp, OverlayService rectangles; counters, not pixels)"
    if not need_game():
        return add(name, "BLOCKED", "not in a game")
    rows = _state_icon_rows()
    side_of = {r[0]: r[1] for r in rows}
    tip_of = {r[0]: r[4] for r in rows}
    order = {}
    for i, r in enumerate(rows):
        order.setdefault(r[0], i)  # the first row of a state decides its place in the drawing order
    left = [s for s in (32, 101, 26) if side_of.get(s) == 0]  # battle orders, holy shield, shout: no effect without stats
    right = [s for s in (89, 19) if side_of.get(s) == 1]  # battle cry, weaken
    if len(left) < 2 or not right:
        return add(name, "BLOCKED", "the table has no rows for the test states (left %s, right %s)" % (left, right))
    goto(1)
    close_menu()
    chosen = left + right
    for s in chosen:  # a clean start: no test state on the player
        H("setstate player %d 0" % s)
        g.hcmd("bufficons cstate %d 0" % s, 5)
    time.sleep(1.5)
    base_head, base_icons = _buff_icons()
    if base_head.get("on") != "1":
        return add(name, "FAIL", "the part is off: %s" % base_head)
    # Put the states on the server's player (a state list that ends in 10 minutes, the way D2R's self buffs do); then on the client's
    # copy if D2R's own packets did not carry them (the bar reads the client's unit).
    log = []
    for s in chosen:
        ok, out = H("setstate player %d 1" % s)
        log += out
    time.sleep(2.5)
    head, icons = _buff_icons()
    shown = {i["state"]: i for i in icons}
    carried = [s for s in chosen if s in shown]  # states that reached the client through D2R's own packets
    if len(carried) < len(chosen):
        for s in chosen:
            if s not in shown:
                ok, out = g.hcmd("bufficons cstate %d 1" % s, 5)
                log += out
        time.sleep(2.5)
        head, icons = _buff_icons()
        shown = {i["state"]: i for i in icons}
    problems = []
    for s in chosen:
        i = shown.get(s)
        if i is None:
            problems.append("state %d is not drawn" % s)
        elif i["side"] != side_of[s]:
            problems.append("state %d in group %d, expected %d" % (s, i["side"], side_of[s]))
    # Order within a group: table order, slots 0..n-1 without gaps
    for side in (0, 1):
        grp = sorted((i for i in icons if i["side"] == side), key=lambda i: order.get(i["state"], 9999))
        if [i["slot"] for i in grp] != list(range(len(grp))):
            problems.append("group %d slots %s are not 0..%d in table order" % (side, [i["slot"] for i in grp], len(grp) - 1))
    if int(head.get("failed", "1")) != 0:
        problems.append("the overlay refused %s draw requests (first result %s)" % (head.get("failed"), head.get("firstFail")))
    # Timers: set for 10 minutes by `setstate`, they must read about 600 s and count down
    secs0 = {s: shown[s]["secs"] for s in chosen if s in shown}
    time.sleep(5)
    head2, icons2 = _buff_icons()
    shown2 = {i["state"]: i for i in icons2}
    for s in chosen:
        if s in shown2 and s in secs0:
            a, b = secs0[s], shown2[s]["secs"]
            if not (a > 0 and b > 0 and 3 <= a - b <= 8):
                problems.append("state %d timer %d -> %d after 5 s" % (s, a, b))
            if a > 600 or a < 580:
                problems.append("state %d timer %d, expected about 600" % (s, a))
    # The screenshot for the human. It is not an assertion: PrintWindow of the background window does not capture the loader's
    # overlay (the same finding as the dungeon lockout timer), so the on-screen proof is the counters above, not the pixels.
    shots = [sh("bufficons_bar")]
    now_shot = grab()
    h, w, _ = now_shot.shape
    # Does the PrintWindow capture show the overlay at all? A magenta test rectangle
    H("bufficons probe on")
    time.sleep(1.5)
    probe_shot = grab()
    shots.append(sh("bufficons_probe"))
    H("bufficons probe off")
    region = (slice(int(h * 0.3) + 4, int(h * 0.45) - 4), slice(int(w * 0.3) + 4, int(w * 0.7) - 4))
    probe_seen = bool((abs(probe_shot[region] - now_shot[region]).sum(axis=2) > 200).mean() > 0.5)
    # Hover: the tooltip of the first left state that has one
    tip_state = next((s for s in left if tip_of.get(s)), None)
    hover_note = "no test state has a tooltip"
    if tip_state is not None:
        H("bufficons hover state %d" % tip_state)
        time.sleep(1.2)
        head3, icons3 = _buff_icons()
        hov = [i["state"] for i in icons3 if i["hover"]]
        if hov != [tip_state]:
            problems.append("hover on state %d: icons hovered %s" % (tip_state, hov))
        if int(head3.get("tooltip", "-1")) != tip_state or int(head3.get("lines", "0")) < 1:
            problems.append("hover on state %d drew no tooltip (%s lines %s)" % (tip_state, head3.get("tooltip"), head3.get("lines")))
        hover_note = "hover %d (%s): tooltip %s, %s line(s)" % (tip_state, tip_of[tip_state], head3.get("tooltip"), head3.get("lines"))
        shots.append(sh("bufficons_hover"))
        H("bufficons hover off")
    # Cleanup: the states go, the icons go
    for s in chosen:
        H("setstate player %d 0" % s)
        g.hcmd("bufficons cstate %d 0" % s, 5)
    time.sleep(2)
    head4, icons4 = _buff_icons()
    left_over = [i["state"] for i in icons4 if i["state"] in chosen]
    if left_over:
        problems.append("icons still drawn after clearing: %s" % left_over)
    note = ("states %s (left) %s (right): %s; %s left / %s right icons; timers %s -> %s; %s; overlay refused %s requests; rectangles per frame %s. "
            "The states reached the client %s. Probe rectangle seen in the PrintWindow capture: %s (informational; the loader's overlay is not part of that capture). Screenshots: bufficons_bar.png, bufficons_probe.png, bufficons_hover.png" % (
                left, right, "drawn in the right group, in table order, with their timers" if not problems else "PROBLEMS: " + "; ".join(problems),
                head2.get("left", "0"), head2.get("right", "0"), secs0, {s: shown2[s]["secs"] for s in shown2 if s in chosen},
                hover_note, head.get("failed"), head2.get("rectsLast"),
                "by D2R's own packets for %s, by `bufficons cstate` (the client's copy) for %s" % (carried, [s for s in chosen if s not in carried]), probe_seen))
    add(name, "PASS" if not problems else "FAIL", note, shots, log)


CHECKS = [("mxl", check_mxl), ("lockclock", check_lockout_clock), ("goblins", check_goblins), ("respec", check_respec),
          ("cain", check_cain), ("cainsub", check_cain_submenu), ("heart", check_heart), ("catalyst", check_catalyst), ("tower", check_tower), ("lockout", check_lockout), ("lockquest", check_lockquest),
          # reenter before cow: the cow check leaves a Cow Level portal in town that `enter nearest` would take
          ("reenter", check_dungeon_reenter), ("cow", check_cow), ("goldcraft", check_goldcraft), ("craftflags", check_craftflags),
          ("areacycle", check_areacycle), ("mapmem", check_mapmem), ("casts", check_casts), ("itemevents", check_item_events), ("medianai", check_median_ai), ("medianai_ctl", check_median_ai_controlled), ("tyrael", check_tyrael),
          # the verify rows of docs/unported.md
          ("resistcap", check_resistcap), ("absorb", check_absorb), ("damagered", check_damagered), ("avoidcaps", check_avoidcaps), ("classlock", check_classlock),
          ("hiregates", check_hiregates), ("itemrolls", check_itemrolls), ("allqualsockets", check_allqualsockets), ("dye", check_dye), ("goldclient", check_goldclient),
          ("bonusdrops", check_bonusdrops), ("summoner", check_summoner), ("tooltip", check_tooltip), ("gameend", check_gameend),
          ("trophy", check_trophy), ("statcols", check_statcols),
          # the old "fix to test" rows of docs/bugs.md
          ("skilltree", check_skilltree), ("keybinds", check_keybinds), ("npcstrings", check_npcstrings), ("automap", check_automap),
          ("scrollportals", check_scrollportals), ("vendors", check_vendors), ("monhover", check_monster_hover),
          ("levelcap", check_levelcap),
          ("skillcaps", check_skillcaps),
          ("townportal", check_townportal),
          ("funcslots", check_funcslots),
          ("potions", check_potions),
          ("itemskills", check_itemskills),
          ("cowportals", check_cowportals),
          ("cubeoutputs", check_cubeoutputs),
          ("deathcrash", check_deathcrash),
          ("nocorpse", check_nocorpse),
          ("cooldowns", check_cooldowns),
          ("collidetypes", check_collidetypes), ("skillstarts", check_skillstarts), ("activationfreq", check_activationfreq),
          ("nslong", check_nslong), ("orbtips", check_orbtips),
          ("strings", check_strings),
          ("goblinname", check_goblinname), ("rewardsound", check_rewardsound), ("statorder", check_statorder), ("infopanel", check_infopanel),
          ("sprint", check_sprint), ("steadyaim", check_steadyaim), ("restrictitems", check_restrictitems), ("heat", check_heat),
          ("trophytimer", check_trophytimer),
          ("mapreveal", check_mapreveal), ("wpauto", check_wpauto), ("hotkeys", check_hotkeys), ("stashpages", check_stashpages), ("stashpage2", check_stashpage2),
          ("skillscaling", check_skillscaling),
          ("nodurability", check_nodurability), ("nodurabilityui", check_nodurabilityui), ("autoid", check_autoid), ("townmana", check_townmana),
          ("potionamounts", check_potionamounts), ("manarules", check_manarules),
          ("mercrevive", check_mercrevive), ("questxp", check_questxp), ("questrewards", check_questrewards),
          ("monlevel", check_monlevel), ("normaltc", check_normaltc), ("leash", check_leash),
          ("gamble", check_gamble), ("enginegaps", check_engine_gaps), ("bufficons", check_bufficons)]


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

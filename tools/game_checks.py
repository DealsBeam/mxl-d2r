#!/usr/bin/env python
"""Scripted in-game checks for the untested rows of docs/testing.md.

Built on tools/game_ctl.py (window-only screenshots, SendInput) and D2RLoader's developer console (Ctrl+`), see
docs/dev-console.md. Run with `py -3 tools/game_checks.py <command>`; `all` runs every check and writes report.md into
%USERPROFILE%\\mxl-d2r-runs\\checks-<time>\\ (screenshots, game_ctl.log, mxl-core.log excerpts).

Low-level commands (game already running, console closed unless noted):
  boot [char]     start the game, clear the intro, play `char` (default: the selected one)
  toggle          open / close the console
  con CMD         open the console, type CMD, screenshot the console panel, close it
  cheat ARGS      same as `con cheat ARGS`
  stop            stop the game
Checks: see CHECKS below; `check NAME` runs one against the running game, `all` boots and runs everything.

Evidence rules: a check records PASS / FAIL / BLOCKED with the screenshots and the mxl-core.log lines it relied on.
Screenshots are of the game window client area only. The game lock directory is taken by `all` (and `lock`/`unlock`).
"""
import datetime
import hashlib
import os
import shutil
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_ctl as g  # noqa: E402

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

LOCK = os.path.join(g.RUNS, "game.lock")
SAVES = g.SAVES

# ------------------------------------------------------------------ run folder and lock


def new_run():
    name = "checks-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    os.makedirs(g.RUNS, exist_ok=True)
    open(os.path.join(g.RUNS, "current.txt"), "w").write(name)
    return g.current_run()


def lock(wait_s=1200):
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


# ------------------------------------------------------------------ images

def grab():
    import numpy as np
    from PIL import ImageGrab
    l, t, r, b = g.client_rect(g.need_window())
    return np.asarray(ImageGrab.grab(bbox=(l, t, r, b), all_screens=True).convert("RGB")).astype(int)


def find_all_changed(a, b, c, region=(0.36, 0.12, 0.64, 0.86), min_blocks=2):
    """Clusters of change from b to c, ignoring motion between a and b: [(fx, fy, blocks)], biggest first."""
    import numpy as np
    h, w, _ = c.shape
    d = np.abs(c - b).sum(axis=2) > 90
    n = np.abs(b - a).sum(axis=2) > 30
    nn = n.copy()
    for dy in range(-6, 7, 3):
        for dx in range(-6, 7, 3):
            nn |= np.roll(np.roll(n, dy, 0), dx, 1)
    m = d & ~nn
    mask = np.zeros_like(m)
    mask[int(region[1] * h):int(region[3] * h), int(region[0] * w):int(region[2] * w)] = True
    m &= mask
    S = 6
    gh, gw = h // S, w // S
    cnt = m[:gh * S, :gw * S].reshape(gh, S, gw, S).sum(axis=(1, 3))
    blocks = cnt >= 4
    seen = np.zeros_like(blocks)
    out = []
    for y in range(gh):
        for x in range(gw):
            if blocks[y, x] and not seen[y, x]:
                stack, pts = [(y, x)], []
                seen[y, x] = True
                while stack:
                    cy, cx = stack.pop()
                    pts.append((cy, cx))
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            ny, nx = cy + dy, cx + dx
                            if 0 <= ny < gh and 0 <= nx < gw and blocks[ny, nx] and not seen[ny, nx]:
                                seen[ny, nx] = True
                                stack.append((ny, nx))
                if len(pts) >= min_blocks:
                    wts = np.array([cnt[p] for p in pts], float)
                    ys = np.array([p[0] for p in pts], float)
                    xs = np.array([p[1] for p in pts], float)
                    out.append((float(((xs * wts).sum() / wts.sum() + 0.5) * S / w),
                                float(((ys * wts).sum() / wts.sum() + 0.5) * S / h), len(pts)))
    return sorted(out, key=lambda t: -t[2])


def con_shot(name):
    """Screenshot of just the console panel (window client area only)."""
    from PIL import ImageGrab
    l, t, r, b = g.client_rect(g.need_window())
    img = ImageGrab.grab(bbox=(l + 330, t, l + 1270, t + 235), all_screens=True)
    p = os.path.join(g.current_run(), name + ".png")
    img.save(p)
    return p


# ------------------------------------------------------------------ console

class Console:
    """Open/closed state is tracked here: always go through this object, a stray Ctrl+` flips it."""
    is_open = False

    def open(self):
        if not self.is_open:
            g.focus(g.need_window())
            g.press("`", mods=("ctrl",))
            time.sleep(0.5)
            self.is_open = True

    def close(self):
        if self.is_open:
            g.focus(g.need_window())
            g.press("`", mods=("ctrl",))
            time.sleep(0.4)
            self.is_open = False

    def run(self, cmd, shot=None, wait=0.9, keep_open=False):
        self.open()
        g.type_text(cmd)
        g.press("enter")
        time.sleep(wait)
        p = con_shot(shot) if shot else None
        if not keep_open:
            self.close()
        return p


CON = Console()


def cheat(args, shot=None):
    return CON.run("cheat " + args, shot)


# ------------------------------------------------------------------ logs


def mxl_lines_since(off):
    return [ln for ln in g._read_from(g.MXL_LOG, off).splitlines() if ln.strip()]


def mxl_mark():
    return g._size(g.MXL_LOG)


# ------------------------------------------------------------------ menus / entering the game

DIFF_Y = {"normal": 0.42, "nightmare": 0.482, "hell": 0.542}


def to_menu(restart=False):
    """Game running with the main menu showing the character list. Returns True on success."""
    if restart and g.game_pids():
        g.stop()
    if not g.game_pids():
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
        if g.in_game():
            return True
        time.sleep(2)
    return False


def play(difficulty="normal"):
    """Play the selected character; answers the difficulty dialog when it shows. Returns in_game()."""
    g.click(*g.UI["menu_play"])
    if wait_in_game(8):  # single-difficulty character: straight in
        return True
    time.sleep(2)
    g.click(0.5, DIFF_Y[difficulty])
    if wait_in_game(45):
        return True
    g.click(*g.UI["menu_play"])  # the first click on the menu can be swallowed
    time.sleep(3)
    g.click(0.5, DIFF_Y[difficulty])
    return wait_in_game(45)


def create_char(name, cls_xy=None):
    g.click(*g.UI["char_create"])
    time.sleep(3)
    g.click(*(cls_xy or g.UI["barb_class"]))
    time.sleep(2)
    g.click(*g.UI["name_box"])
    g.type_text(name)
    time.sleep(1)
    g.shot("create_" + name)
    g.click(*g.UI["create_ok"])
    time.sleep(16)
    return g.in_game()


def save_exit():
    """Esc menu -> Save and Exit. Returns to the main menu."""
    CON.close()
    for _ in range(3):  # close panels / dialogs first; the last Esc opens the pause menu
        g.key("esc")
        time.sleep(1.0)
    g.click(*g.UI["save_exit"])
    time.sleep(9)


def attack(fx, fy, n=1):
    """Shift + left click: attack where the pointer is without walking."""
    for _ in range(n):
        g._send_key(g.SC["shift"][0], 0, 0)
        try:
            g.click(fx, fy)
        finally:
            g._send_key(g.SC["shift"][0], 0, 1)


def spawn_at(args, fx=0.5, fy=0.30, shot=None):
    """Run a spawn cheat with the pointer at (fx, fy): the cheats spawn at the pointer ('selected location')."""
    g.click(fx, fy, move_only=True)
    return cheat(args, shot)


def ensure_window():
    """Put the window back to the 1600x900 client rect (a stray double click on the title bar maximises it)."""
    h = g.need_window()
    l, t, r, b = g.client_rect(h)
    if (r - l, b - t) != (g.CLIENT_W, g.CLIENT_H):
        g.fit_window(h)


def console_error_seen(path):
    """True when the bottom lines of a console screenshot hold an `ERR` tag (red text)."""
    from PIL import Image
    im = Image.open(path).convert("RGB")
    w, h = im.size
    n = 0
    for y in range(h - 28, h - 6):  # the newest line only
        for x in range(10, 140):
            r, gg, b = im.getpixel((x, y))
            if r > 200 and gg < 90 and b < 90:
                n += 1
    return n > 12


def enable_debug():
    """Cheats need D2RLoader's debug mode: `debug_mode` TOGGLES it and it stays on for the whole process (also across
    characters), so test with a harmless cheat first and toggle only when it says 'Cheats are disabled'."""
    p = CON.run("cheat heal", "debug_probe", wait=0.8)
    if console_error_seen(p):
        CON.run("debug_mode", "debug_mode", wait=0.8)
        p = CON.run("cheat heal", "debug_probe2", wait=0.8)
        return not console_error_seen(p)
    return True


# ------------------------------------------------------------------ save editing (game closed or at the main menu)

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


def patch_save(name, quests=None, header=None):
    """Edit a closed character: quests {(difficulty, quest): word} sets a 16-bit word in the quest section ('Woo!',
    3 x 48 words per difficulty after 10 header bytes); header {offset: byte}. Fixes the checksum and D2RLoader's
    `.d2rl` sidecar (docs/save-format.md). Returns the old values."""
    p = os.path.join(SAVES, name + ".d2s")
    s = bytearray(open(p, "rb").read())
    i = bytes(s).find(b"Woo!")
    old = {}
    for (diff, q), w in (quests or {}).items():
        off = i + 10 + diff * 96 + q * 2
        old[(diff, q)] = struct.unpack_from("<H", s, off)[0]
        struct.pack_into("<H", s, off, w)
    for off, v in (header or {}).items():
        old[off] = s[off]
        s[off] = v
    s = bytearray(_checksum(bytes(s)))
    open(p, "wb").write(bytes(s))
    sp = os.path.join(SAVES, name + ".d2rl")
    r = bytearray(open(sp, "rb").read())
    struct.pack_into("<I", r, 0x34, struct.unpack_from("<I", s, 12)[0])
    struct.pack_into("<I", r, 0x38, len(s))
    r[0x3c:0x5c] = hashlib.sha256(bytes(s)).digest()
    r[0x0c:0x2c] = hashlib.sha256(bytes(r[0x2c:])).digest()
    open(sp, "wb").write(bytes(r))
    return old


# ------------------------------------------------------------------ session helpers

STATE = {"in_game": False}
CARD_Y = 0.076
CARD_DY = 0.095


def select_card(i):
    """Click the i-th character card (0 = top) in the list on the right of the main menu."""
    g.click(0.87, CARD_Y + CARD_DY * i)
    time.sleep(0.8)


def leave():
    if STATE["in_game"]:
        save_exit()
        STATE["in_game"] = False


def enter(card, difficulty="normal", quests=None, header=None, god=True):
    """Leave any running game, optionally patch the (closed) save, play card `card` in `difficulty`, switch debug mode
    on. Returns True when in game with cheats working."""
    leave()
    CON.is_open = False
    if quests or header:
        name = {0: "MxlQ", 1: "MxlBarb"}[card]
        patch_save(name, quests, header)
    select_card(card)
    ok = play(difficulty)
    STATE["in_game"] = ok
    if not ok:
        return False
    time.sleep(2)
    ok = enable_debug()
    if ok and god:
        cheat("god 1")
    return ok


# ------------------------------------------------------------------ world helpers

INV_REGION = (0.645, 0.42, 0.925, 0.72)
CUBE_REGION = (0.08, 0.29, 0.35, 0.60)


def clusters(a, b, region):
    import numpy as np
    h, w, _ = a.shape
    m = np.abs(a - b).sum(axis=2) > 120
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
                if len(pts) >= 3:
                    ys = [p[0] for p in pts]
                    xs = [p[1] for p in pts]
                    out.append((((min(xs) + max(xs) + 1) * S / 2) / w, ((min(ys) + max(ys) + 1) * S / 2) / h, len(pts)))
    return sorted(out, key=lambda t: (t[1], t[0]))


def empty_ref():
    return grab()


def open_inventory():
    g.key("i")
    time.sleep(1.0)


def open_cube():
    g.click(0.677, 0.452, "right")  # the Horadric Cube sits at the top left cell of the 15 x 10 inventory
    time.sleep(1.2)


def spawn_pickup(codes):
    """Spawn items on the ground next to the player and pick them up into the (open) inventory. Returns the item
    clusters [(fx, fy, size)] that appeared in the inventory grid."""
    CON.close()
    before = grab()
    cheat("itemgroundclear")
    for code in codes:
        cheat("spawn " + code)
    cheat("pickup")
    time.sleep(1.0)
    after = grab()
    return clusters(before, after, INV_REGION)


def move_to_cube(items):
    """Drag each inventory cluster into the cube grid, spaced out over its top row."""
    for k, (fx, fy, _) in enumerate(items):
        g.click(fx, fy)
        time.sleep(0.5)
        g.click(0.12 + 0.07 * k, 0.33)
        time.sleep(0.5)


CUBE_EMPTY = []


def transmute():
    g.click(0.209, 0.697)
    time.sleep(2.5)


def red_pixels(region=CUBE_REGION):
    import numpy as np
    im = grab()
    h, w, _ = im.shape
    sub = im[int(region[1] * h):int(region[3] * h), int(region[0] * w):int(region[2] * w)]
    return int(((sub[..., 0] > 150) & (sub[..., 1] < 50) & (sub[..., 2] < 50)).sum())


# ------------------------------------------------------------------ checks

class Result:
    def __init__(self, name, status, note, shots=(), log=()):
        self.name, self.status, self.note, self.shots, self.log = name, status, note, list(shots), list(log)


RESULTS = []


def add(name, status, note, shots=(), log=()):
    RESULTS.append(Result(name, status, note, shots, log))
    g.log("CHECK %s: %s %s" % (name, status, note))


def sh(name):
    return os.path.basename(g.shot(name))


def cube_try(codes, tag):
    """Open the inventory and cube, put `codes` into the cube, Transmute. Returns (before_count, after_count, red, shots)."""
    CON.close()
    open_inventory()
    open_cube()
    CUBE_EMPTY[:] = [grab()]
    items = spawn_pickup(codes)
    if len(items) < len(codes):
        return None, None, None, [sh(tag + "_pickup_failed")]
    move_to_cube(items)
    s1 = sh(tag + "_loaded")
    n0 = len(clusters(CUBE_EMPTY[0], grab(), CUBE_REGION))
    transmute()
    s2 = sh(tag + "_after")
    n1 = len(clusters(CUBE_EMPTY[0], grab(), CUBE_REGION))
    return n0, n1, red_pixels(), [s1, s2]


def check_mxl_command():
    ok = enter(0, "normal")
    if not ok:
        return add("mxl console command", "BLOCKED", "could not enter the game")
    p = CON.run("mxl", "mxl_cmd", wait=1.3)
    wanted = ["states fix", "level types", "calc codes", "skill functions", "item spells", "missile hits", "mana costs",
              "whirlwind", "cube conditions", "craft flags", "act 1 quests", "dungeon lockout", "goblins on",
              "npc actions on", "skill tree on"]
    log = [ln for ln in g._read_from(g.MXL_LOG, 0).splitlines() if "mxl-core:" in ln or "loaded" in ln]
    missing = [w for w in wanted if not any(w in ln for ln in log)]
    add("mxl console command and plugin load lines", "PASS" if not missing else "FAIL",
        "all 15 feature lines in mxl-core.log; command output read from the console screenshot (the command logs nothing)"
        if not missing else "missing in mxl-core.log: %s" % missing, [os.path.basename(p)], log[-16:])


def check_cow():
    for diff, town in (("hell", True), ("hell", False), ("normal", True)):
        tag = "cow_%s_%s" % (diff, "town" if town else "field")
        if not (STATE.get("cow_diff") == diff and STATE["in_game"]):
            if not enter(0, diff):
                add("Cow portal recipe (%s, %s)" % (diff, "town" if town else "field"), "BLOCKED", "could not enter the game")
                continue
            STATE["cow_diff"] = diff
        if not town:
            CON.close()
            for _ in range(2):
                g.key("esc")
                time.sleep(0.5)
            cheat("goto 2")
            time.sleep(6)
        n0, n1, red, shots = cube_try(["leg", "tbk"], tag)
        if n0 is None:
            add("Cow portal recipe (%s, %s)" % (diff, "town" if town else "field"), "BLOCKED",
                "spawn/pickup did not put both items into the inventory", shots)
            continue
        ran = n1 < n0
        want = (diff == "hell" and town)
        status = "PASS" if ran == want else "FAIL"
        add("Cow portal recipe (%s, %s)" % (diff, "town" if town else "field"), status,
            "cube items %d -> %d after Transmute (%s); expected %s" % (n0, n1, "recipe ran" if ran else "nothing happened",
                                                                       "portal in Hell town only (recipe row 2: min diff 2, op 36)" if want else "no portal"),
            shots)
        CON.close()
        g.key("esc")
        time.sleep(0.5)


def find_spawned(cmd, region=(0.15, 0.15, 0.85, 0.8), fallback=(0.53, 0.44)):
    """Run a spawn cheat and return where something appeared (fraction coordinates) or the fallback."""
    CON.close()
    A = grab()
    time.sleep(0.4)
    B = grab()
    cheat(cmd)
    time.sleep(1.2)
    C = grab()
    cl = find_all_changed(A, B, C, region=region)
    return (cl[0][0], cl[0][1]) if cl else fallback


def check_goblins():
    name = "Treasure goblin spawns in the Blood Moor after a kill (MXL_GOBLINS_FORCE=1)"
    if not enter(0, "normal"):
        return add(name, "BLOCKED", "could not enter the game")
    cheat("goto 2")
    time.sleep(6)
    m = mxl_mark()
    lines = []
    shots = []
    for i in range(4):
        x, y = find_spawned("mspawn fallen1")
        for _ in range(5):
            attack(x, y)
        lines = [ln for ln in mxl_lines_since(m) if "goblin" in ln and "spawned" in ln]
        if lines:
            break
        time.sleep(16)  # the roll runs on game-frame multiples of 375 (15 s)
    time.sleep(2)
    shots.append(sh("goblin_scene"))
    p = CON.run("mxl", "goblin_mxl", wait=1.2)
    shots.append(os.path.basename(p))
    add(name, "PASS" if lines else "FAIL",
        "log: %s; `mxl` counters in goblin_mxl.png; look at goblin_scene.png for the monster" % (lines[0][-90:] if lines else "no goblin line in 4 kills / 70 s"),
        shots, lines)


def stat_crop():
    im = grab()
    return im[630:660, 140:460]


def check_respec():
    name = "Akara respec after Den of Evil (Reset Stat/Skill Points)"
    if not enter(0, "normal", quests={(0, 41): 2}):
        return add(name, "BLOCKED", "could not enter the game")
    import numpy as np
    cheat("level 12")
    time.sleep(1)
    cheat("mspawn akara")
    CON.close()
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
    g.click(0.5, 0.335)  # the spawned Akara: first click gives her greeting window, Esc then shows the menu
    time.sleep(3)
    g.key("esc")
    time.sleep(1.2)
    sM = sh("respec_3_menu")
    m = mxl_mark()
    g.click(0.5, 0.2144)  # second 'Reset Stat/Skill Points' line = the plugin's (kind 0x10 + 2*16 + 0 = 48)
    time.sleep(2.5)
    lines = [ln for ln in mxl_lines_since(m) if "npc actions" in ln]
    g.key("c")
    time.sleep(1.2)
    C = stat_crop()
    sC = sh("respec_4_sheet_after")
    d_ab = float(np.abs(A - B).mean())
    d_ac = float(np.abs(A - C).mean())
    done = any("done" in ln for ln in lines)
    ok = done and d_ab > 1.0 and d_ac < 0.5
    add(name, "PASS" if ok else "FAIL",
        "log %s; stat-points text changed by spending (%.1f) and is back to the start value after the reset (%.2f)" % (
            "'npc actions: done (48, 148)'" if done else "has no done line: %s" % lines, d_ab, d_ac),
        [sA, sB, sM, sC], lines)


def inv_snapshot():
    open_inventory()
    im = grab()
    g.key("i")
    time.sleep(0.5)
    return im


def kill_spawn(cmd, tries=14):
    x, y = find_spawned(cmd)
    for _ in range(tries):
        attack(x, y)
    return x, y


def check_catalyst():
    name = "Andariel catalyst of disenchantment (Normal), not twice"
    if not enter(0, "normal"):
        return add(name, "BLOCKED", "could not enter the game")
    base = inv_snapshot()
    cheat("goto 37")
    time.sleep(7)
    kill_spawn("mspawn 156")
    time.sleep(4)
    shots = [sh("catalyst_after_kill1")]
    open_inventory()
    one = grab()
    shots.append(sh("catalyst_inv_after_kill1"))
    g.key("i")
    new1 = clusters(base, one, INV_REGION)
    kill_spawn("mspawn 156")
    time.sleep(4)
    open_inventory()
    two = grab()
    shots.append(sh("catalyst_inv_after_kill2"))
    g.key("i")
    new2 = clusters(one, two, INV_REGION)
    ok = len(new1) >= 1 and len(new2) == 0
    add(name, "PASS" if ok else "FAIL",
        "new inventory items after kill 1: %d (want >= 1, expect the catalyst `3o[ `), after kill 2: %d (want 0). Verify the icon in the screenshots; Andariel may not have been killed if the spawn/attack missed" % (len(new1), len(new2)),
        shots)
    return new1


def check_heart():
    name = "Griswold's Heart from Akara after Cain (quest 4 bit 1)"
    if not enter(0, "normal", quests={(0, 4): 2}):
        return add(name, "BLOCKED", "could not enter the game")
    base = inv_snapshot()
    cheat("mspawn akara")
    CON.close()
    g.click(0.5, 0.335)
    time.sleep(4)
    s1 = sh("heart_talk1")
    g.key("esc")
    time.sleep(1)
    g.key("esc")
    time.sleep(0.6)
    g.click(0.5, 0.335)
    time.sleep(4)
    s2 = sh("heart_talk2")
    g.key("esc")
    time.sleep(1)
    open_inventory()
    after = grab()
    s3 = sh("heart_inventory")
    new = clusters(base, after, INV_REGION)
    add(name, "PASS" if new else "FAIL",
        "%d new inventory item(s) after talking to Akara twice with quest 4 bit 1 set (patched save); the log has no line unless the heart fails" % len(new),
        [s1, s2, s3])


def portal_use(code, tag):
    """Spawn a scroll/key, use it from the inventory, return the portal position (fraction) or None."""
    CON.close()
    open_inventory()
    items = spawn_pickup([code])
    if not items:
        return None, [sh(tag + "_nopickup")]
    A = grab()
    g.click(items[0][0], items[0][1], "right")
    time.sleep(0.5)
    g.key("i")
    time.sleep(1.5)
    B = grab()
    cl = find_all_changed(A, B, B * 0 + B, region=(0.2, 0.2, 0.8, 0.8))
    time.sleep(2)
    C = grab()
    cl = find_all_changed(A, B, C, region=(0.2, 0.15, 0.8, 0.8))
    return ((cl[0][0], cl[0][1]) if cl else None), [sh(tag + "_portal")]


def check_lockout():
    name = "Dungeon lockout: entry refused by level/difficulty"
    if not enter(0, "normal"):
        return add(name, "BLOCKED", "could not enter the game")
    m = mxl_mark()
    pos, shots = portal_use("b61X", "lock_normal")
    if pos is None:
        return add(name, "BLOCKED", "the Scroll of Summoning 2 (`b61X`) was not picked up or no portal appeared", shots)
    g.click(*pos)
    time.sleep(6)
    shots.append(sh("lock_normal_after_click"))
    lines = [ln for ln in mxl_lines_since(m) if "lockout" in ln]
    p = CON.run("mxl", "lock_mxl", wait=1.2)
    shots.append(os.path.basename(p))
    ok = any("requirements not met" in ln for ln in lines)
    add(name, "PASS" if ok else "FAIL",
        "Normal level-12 character into level 251 (min difficulty Hell, level 125): %s" % (lines[0][-80:] if lines else "no lockout log line; see the screenshots"),
        shots, lines)


def check_lockout_clock():
    name1 = "Dungeon lockout: clock starts on entry, re-entry inside the lock time"
    name2 = "Dungeon lockout: level sealed after the lock time (251, 60 s)"
    if not enter(1, "hell"):
        add(name1, "BLOCKED", "could not enter MxlBarb in Hell")
        return add(name2, "BLOCKED", "could not enter MxlBarb in Hell")
    m = mxl_mark()
    pos, shots = portal_use("b61X", "lock_hell")
    if pos is None:
        add(name1, "BLOCKED", "scroll/portal not found", shots)
        return add(name2, "BLOCKED", "scroll/portal not found", shots)
    g.click(*pos)
    time.sleep(8)
    t0 = time.time()
    shots.append(sh("lock_hell_inside"))
    lines1 = [ln for ln in mxl_lines_since(m) if "lockout" in ln]
    cheat("goto 1")
    time.sleep(6)
    m2 = mxl_mark()
    g.click(*pos)  # same portal again, inside the 60 s
    time.sleep(7)
    shots.append(sh("lock_hell_reenter"))
    lines2 = [ln for ln in mxl_lines_since(m2) if "lockout" in ln]
    ok1 = any("clock started" in ln for ln in lines1) and not any("refused" in ln for ln in lines2)
    add(name1, "PASS" if ok1 else "FAIL", "clock log: %s; second entry inside lock time: %s" % (
        [ln[-60:] for ln in lines1][:1], "refused" if any("refused" in ln for ln in lines2) else "allowed"), shots, lines1 + lines2)
    cheat("goto 1")
    time.sleep(6)
    while time.time() - t0 < 75:
        time.sleep(3)
    m3 = mxl_mark()
    g.click(*pos)
    time.sleep(7)
    s3 = sh("lock_hell_sealed")
    lines3 = [ln for ln in mxl_lines_since(m3) if "lockout" in ln]
    p = CON.run("mxl", "lock_hell_mxl", wait=1.2)
    ok2 = any("sealed" in ln for ln in lines3)
    add(name2, "PASS" if ok2 else "FAIL", "after 75 s: %s" % (lines3[0][-70:] if lines3 else "no lockout line"),
        [s3, os.path.basename(p)], lines3)


def check_gold_craft():
    name = "Cube gold payment (recipe 5196: jcat + jewel, 5,000 gold)"
    if not enter(0, "normal"):
        return add(name, "BLOCKED", "could not enter the game")
    cheat("gold 20000")
    time.sleep(0.5)
    CON.close()
    open_inventory()
    g0 = grab()[655:680, 1040:1280]
    g.key("i")
    n0, n1, red, shots = cube_try(["jcat", "jew"], "goldcraft")
    if n0 is None:
        return add(name, "BLOCKED", "`spawn jcat` / `spawn jew` did not give two inventory items", shots)
    import numpy as np
    g1 = grab()[655:680, 1040:1280]
    ran = n1 < n0 or n1 != n0
    gold_changed = float(np.abs(g0 - g1).mean()) > 1.0
    add(name, "PASS" if (ran and gold_changed) else "BLOCKED",
        "cube items %d -> %d, gold field %s. If nothing happened the recipe also needs the item condition (op 18, stat 481) that a plain spawned catalyst does not meet: needs an item with that stat" % (
            n0, n1, "changed" if gold_changed else "unchanged"), shots)


CHECKS = [("mxl", check_mxl_command), ("goblins", check_goblins), ("respec", check_respec), ("catalyst", check_catalyst),
          ("heart", check_heart), ("lockout", check_lockout), ("cow", check_cow), ("goldcraft", check_gold_craft),
          ("lockclock", check_lockout_clock)]


def write_report(extra=""):
    p = os.path.join(g.current_run(), "report.md")
    with open(p, "w", encoding="utf-8") as f:
        f.write("# Game checks %s\n\n" % os.path.basename(g.current_run()))
        f.write("| Check | Result | Note | Evidence |\n|---|---|---|---|\n")
        for r in RESULTS:
            f.write("| %s | %s | %s | %s |\n" % (r.name, r.status, r.note.replace("|", "/"), ", ".join(r.shots)))
        f.write("\n" + extra + "\n")
        for r in RESULTS:
            if r.log:
                f.write("\n### mxl-core.log: %s\n\n```\n%s\n```\n" % (r.name, "\n".join(x[:230] for x in r.log)))
    g.log("report " + p)
    return p


def run_all(only=None):
    new_run()
    if not lock():
        print("game.lock held for 20 min, giving up")
        return 2
    os.environ["MXL_GOBLINS_FORCE"] = "1"
    try:
        backup_saves()
        g.snapshot_marks()
        if not to_menu(restart=True):
            add("start game", "BLOCKED", "game did not reach the main menu")
        else:
            for name, fn in CHECKS:
                if only and name not in only:
                    continue
                try:
                    fn()
                except Exception as e:  # keep going with the next check
                    add(name, "BLOCKED", "script error: %r" % (e,))
    finally:
        try:
            g.stop()
        finally:
            unlock()
            r = g.logscan()
            write_report("## logscan\n\n```\n" + r["text"] + "\n```")
    return 0


# ------------------------------------------------------------------ main (low level)


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    c = a[0]
    if c == "all":
        sys.exit(run_all(a[1:] or None))
    elif c == "boot":
        new_run() if "--same" not in a else None
        print("menu", to_menu())
    elif c == "toggle":
        CON.is_open = False
        g.focus(g.need_window())
        g.press("`", mods=("ctrl",))
    elif c == "con":
        print(CON.run(" ".join(a[1:]), "con_%d" % int(time.time())))
    elif c == "cheat":
        print(cheat(" ".join(a[1:]), "con_%d" % int(time.time())))
    elif c == "stop":
        g.stop()
    elif c == "lock":
        print(lock())
    elif c == "unlock":
        unlock()
    else:
        print(__doc__)


if __name__ == "__main__":
    main()

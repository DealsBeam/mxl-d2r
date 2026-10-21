#!/usr/bin/env python
"""Drive D2R + D2RLoader (Median XL) for unattended smoke checks.

Run with `py -3 tools/game_ctl.py <command>`. Commands:
  start                 launch D2RLoader, wait for startup log line and the game window
  shot NAME             screenshot of the game window's client area only
  key KEY [KEY..]       send keys (names: esc enter tab space up down left right f1.. or single chars)
  click FX FY [right]   click at a fraction (0..1) of the client rect
  console CMD           open the developer console (Ctrl+`), type CMD, Enter, close
  stop                  close the game (CloseMainWindow, force after timeout)
  logscan               new ERROR/ASSERT lines, mxl-core.log summary, new crash files
  smoke                 full scenario, writes report.md in the run folder
Run folders: %USERPROFILE%\\mxl-d2r-runs\\<run>; the current run name is kept in runs\\current.txt.
Privacy: only the game window client rect is ever captured, never the whole desktop.
"""
import ctypes
import ctypes.wintypes as wt
import datetime
import json
import os
import subprocess
import sys
import time

GAME_DIR = r"C:\Program Files (x86)\Diablo II Resurrected"
LOADER = os.path.join(GAME_DIR, "D2RLoader.exe")
LOADER_LOG = os.path.join(GAME_DIR, r"d2rloader\logs\d2rloader.log")
MXL_LOG = os.path.join(GAME_DIR, r"mods\MedianXL\d2rloader\logs\mxl-core.log")
CRASH_DIR = os.path.join(GAME_DIR, r"d2rloader\crashes")
RUNS = r"%USERPROFILE%\mxl-d2r-runs"
SAVES = os.path.join(os.environ.get("USERPROFILE", ""), r"Saved Games\Diablo II Resurrected\mods\MedianXL")
TITLE = "Diablo II: Resurrected"
CLIENT_W, CLIENT_H = 1600, 900
CHAR_NAME = "MxlBarb"

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    user32.SetProcessDPIAware()

# ---------------------------------------------------------------- run state


def current_run(create=False):
    os.makedirs(RUNS, exist_ok=True)
    cur = os.path.join(RUNS, "current.txt")
    if create or not os.path.exists(cur):
        name = datetime.datetime.now().strftime("run-%Y%m%d-%H%M%S")
        open(cur, "w").write(name)
    name = open(cur).read().strip()
    d = os.path.join(RUNS, name)
    os.makedirs(d, exist_ok=True)
    return d


def state_path():
    return os.path.join(current_run(), "state.json")


def load_state():
    try:
        return json.load(open(state_path()))
    except Exception:
        return {}


def save_state(s):
    json.dump(s, open(state_path(), "w"))


def log(msg):
    line = time.strftime("%H:%M:%S ") + msg
    print(line, flush=True)
    with open(os.path.join(current_run(), "game_ctl.log"), "a", encoding="utf-8") as f:
        f.write(line + "\n")


# ---------------------------------------------------------------- processes / windows


def game_pids():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq D2RLoader.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    pids = []
    for ln in out.splitlines():
        p = [x.strip('"') for x in ln.split('","')]
        if len(p) > 1 and p[0].strip('"').lower() == "d2rloader.exe":
            pids.append(int(p[1]))
    return pids


def find_window():
    """HWND of the game window (title match, owned by a D2R/D2RLoader process)."""
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, _):
        if not user32.IsWindowVisible(h):
            return True
        n = user32.GetWindowTextLengthW(h)
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(h, buf, n + 1)
        if buf.value == TITLE:
            pid = wt.DWORD()
            user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
            found.append((h, pid.value))
        return True

    user32.EnumWindows(cb, 0)
    return found[0][0] if found else None


def client_rect(h):
    """Client rect in screen coords: (left, top, right, bottom)."""
    r = wt.RECT()
    user32.GetClientRect(h, ctypes.byref(r))
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(h, ctypes.byref(pt))
    return pt.x, pt.y, pt.x + r.right, pt.y + r.bottom


def focus(h):
    if user32.IsIconic(h):
        user32.ShowWindow(h, 9)
    # Alt-key trick so SetForegroundWindow is allowed
    user32.keybd_event(0x12, 0, 0, 0)
    user32.keybd_event(0x12, 0, 2, 0)
    user32.SetForegroundWindow(h)
    user32.BringWindowToTop(h)
    time.sleep(0.3)


def fit_window(h, x=100, y=60):
    """Resize so the client area is CLIENT_W x CLIENT_H, placed at (x, y) on the primary monitor."""
    user32.ShowWindow(h, 1)
    user32.SetWindowPos(h, 0, x, y, CLIENT_W + 40, CLIENT_H + 80, 0x0004)
    time.sleep(0.5)
    l, t, r, b = client_rect(h)
    wr = wt.RECT()
    user32.GetWindowRect(h, ctypes.byref(wr))
    dw = (wr.right - wr.left) - (r - l)
    dh = (wr.bottom - wr.top) - (b - t)
    user32.SetWindowPos(h, 0, x, y, CLIENT_W + dw, CLIENT_H + dh, 0x0004)
    time.sleep(0.8)
    return client_rect(h)


def need_window():
    h = find_window()
    if not h:
        raise SystemExit("game window not found")
    return h


# ---------------------------------------------------------------- screenshots


def shot(name):
    from PIL import ImageGrab
    h = need_window()
    focus(h)
    l, t, r, b = client_rect(h)
    img = ImageGrab.grab(bbox=(l, t, r, b), all_screens=True)
    path = os.path.join(current_run(), name if name.lower().endswith(".png") else name + ".png")
    img.save(path)
    log(f"shot {os.path.basename(path)} {img.size}")
    return path


# ---------------------------------------------------------------- input (SendInput)

ULONG_PTR = ctypes.c_size_t


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ULONG_PTR)]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD),
                ("dwExtraInfo", ULONG_PTR)]


class _U(ctypes.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wt.DWORD), ("u", _U)]


KEYEVENTF_EXTENDEDKEY, KEYEVENTF_KEYUP, KEYEVENTF_SCANCODE = 1, 2, 8

# name -> (scancode, extended)
SC = {"esc": (0x01, 0), "enter": (0x1C, 0), "tab": (0x0F, 0), "space": (0x39, 0), "backspace": (0x0E, 0),
      "ctrl": (0x1D, 0), "shift": (0x2A, 0), "alt": (0x38, 0), "`": (0x29, 0), "-": (0x0C, 0), "=": (0x0D, 0),
      "up": (0x48, 1), "down": (0x50, 1), "left": (0x4B, 1), "right": (0x4D, 1),
      ".": (0x34, 0), ",": (0x33, 0), "/": (0x35, 0), ";": (0x27, 0), "'": (0x28, 0)}
for i, c in enumerate("qwertyuiop"):
    SC[c] = (0x10 + i, 0)
for i, c in enumerate("asdfghjkl"):
    SC[c] = (0x1E + i, 0)
for i, c in enumerate("zxcvbnm"):
    SC[c] = (0x2C + i, 0)
for i, c in enumerate("1234567890"):
    SC[c] = (0x02 + i, 0)
for i in range(1, 11):
    SC[f"f{i}"] = (0x3A + i, 0)

SHIFT_CHARS = {"_": "-", "+": "="}


def _send_key(sc, ext, up):
    fl = KEYEVENTF_SCANCODE | (KEYEVENTF_EXTENDEDKEY if ext else 0) | (KEYEVENTF_KEYUP if up else 0)
    i = INPUT(1, _U(ki=KEYBDINPUT(0, sc, fl, 0, 0)))
    user32.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT))


def press(key, hold=0.06, mods=()):
    key = key.lower() if len(key) > 1 or key.isalpha() else key
    sc, ext = SC[key]
    for m in mods:
        _send_key(SC[m][0], SC[m][1], 0)
    time.sleep(0.03)
    _send_key(sc, ext, 0)
    time.sleep(hold)
    _send_key(sc, ext, 1)
    for m in reversed(mods):
        _send_key(SC[m][0], SC[m][1], 1)
    time.sleep(0.15)


def key(*names, focus_first=True):
    if focus_first:
        focus(need_window())
    for n in names:
        if "+" in n and len(n) > 1:
            *mods, k = n.split("+")
            press(k, mods=[m.lower() for m in mods])
        else:
            press(n)


def type_text(s):
    for ch in s:
        if ch == " ":
            press("space")
        elif ch.isupper():
            press(ch.lower(), mods=("shift",))
        elif ch in SHIFT_CHARS:
            press(SHIFT_CHARS[ch], mods=("shift",))
        else:
            press(ch)


def click(fx, fy, button="left", move_only=False):
    h = need_window()
    focus(h)
    l, t, r, b = client_rect(h)
    x = int(l + fx * (r - l))
    y = int(t + fy * (b - t))
    user32.SetCursorPos(x, y)
    time.sleep(0.25)
    if move_only:
        return
    down, up = (0x0002, 0x0004) if button == "left" else (0x0008, 0x0010)
    user32.mouse_event(down, 0, 0, 0, 0)
    time.sleep(0.08)
    user32.mouse_event(up, 0, 0, 0, 0)
    time.sleep(0.4)
    log(f"click {fx:.3f},{fy:.3f} -> screen {x},{y} {button}")


# ---------------------------------------------------------------- log handling


def _size(p):
    try:
        return os.path.getsize(p)
    except OSError:
        return 0


def _read_from(p, off):
    try:
        if _size(p) < off:
            off = 0  # log was rotated/truncated at launch
        with open(p, "rb") as f:
            f.seek(off)
            return f.read().decode("utf-8", "replace")
    except OSError:
        return ""


def wait_log(needle, timeout=90, off=0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if needle in _read_from(LOADER_LOG, off):
            return True
        time.sleep(1)
    return False


def snapshot_marks():
    s = load_state()
    s["marks"] = {"loader": _size(LOADER_LOG), "mxl": _size(MXL_LOG),
                  "crashes": sorted(os.listdir(CRASH_DIR)) if os.path.isdir(CRASH_DIR) else []}
    save_state(s)


def logscan(update=True):
    s = load_state()
    m = s.get("marks", {"loader": 0, "mxl": 0, "crashes": []})
    out = []
    new = _read_from(LOADER_LOG, m["loader"] if _size(LOADER_LOG) >= m["loader"] else 0)
    errs = [ln for ln in new.splitlines() if "ERROR" in ln or "ASSERT" in ln.upper() or "FATAL" in ln.upper()]
    out.append(f"d2rloader.log: {len(new.splitlines())} new lines, {len(errs)} ERROR/ASSERT lines")
    out += ["  " + e for e in errs[:40]]
    mx = _read_from(MXL_LOG, m["mxl"] if _size(MXL_LOG) >= m["mxl"] else 0)
    mlines = mx.splitlines()
    bad = [ln for ln in mlines if any(k in ln.lower() for k in ("error", "fail", "assert", "exception"))]
    out.append(f"mxl-core.log: {len(mlines)} new lines, {len(bad)} with error/fail/assert")
    out += ["  " + e[:240] for e in bad[:40]]
    for ln in mlines:
        if any(k in ln.lower() for k in ("loaded", "version", "installed", "on:", "functions")):
            out.append("  [summary] " + ln[:200])
            if len(out) > 120:
                break
    crashes = sorted(os.listdir(CRASH_DIR)) if os.path.isdir(CRASH_DIR) else []
    newc = [c for c in crashes if c not in m.get("crashes", [])]
    out.append(f"crash files: {len(newc)} new {newc}")
    text = "\n".join(out)
    print(text)
    open(os.path.join(current_run(), "logscan.txt"), "w", encoding="utf-8").write(text)
    return {"errors": errs, "bad": bad, "crashes": newc, "text": text}


# ---------------------------------------------------------------- start / stop / console


def start(timeout=120):
    if game_pids():
        log("game already running")
    else:
        snapshot_marks()
        off = _size(LOADER_LOG)
        subprocess.Popen([LOADER], cwd=GAME_DIR)
        log("launched D2RLoader")
        if not wait_log("D2R startup complete.", timeout, off):
            log("TIMEOUT waiting for startup line")
            return False
    t0 = time.time()
    h = None
    while time.time() - t0 < 60 and not h:
        h = find_window()
        time.sleep(1)
    if not h:
        log("no game window")
        return False
    r = fit_window(h)
    focus(h)
    log(f"window ready client rect {r}")
    time.sleep(3)
    return True


def stop(timeout=20):
    pids = game_pids()
    if not pids:
        log("not running")
        return
    h = find_window()
    if h:
        user32.PostMessageW(h, 0x0010, 0, 0)  # WM_CLOSE == CloseMainWindow
    t0 = time.time()
    while time.time() - t0 < timeout and game_pids():
        time.sleep(1)
    if game_pids():
        log("forcing kill")
        for p in game_pids():
            subprocess.run(["taskkill", "/F", "/PID", str(p)], capture_output=True)
        time.sleep(2)
    # D2R may leave a child process named D2R.exe; only ours (spawned by loader) - check
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq D2R.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    if "D2R.exe" in out:
        log("D2R.exe still alive, killing")
        subprocess.run(["taskkill", "/F", "/IM", "D2R.exe"], capture_output=True)
    log("stopped")


def console(cmd):
    focus(need_window())
    press("`", mods=("ctrl",))
    time.sleep(0.5)
    type_text(cmd)
    press("enter")
    time.sleep(0.6)
    press("`", mods=("ctrl",))


# ---------------------------------------------------------------- smoke scenario

# Fractional click targets (fx, fy) of the 1600x900 client rect. "seen" ones were measured from screenshots;
# "guess" ones are untested (the 2026-10-21 run was blocked by D2R's 30-day online license check before the menu).
UI = {
    "calib_continue": (0.500, 0.838),   # seen: Gamma / Screen Space / Color Blind calibration screens
    "license_ok": (0.498, 0.537),       # seen: "not been online in the last 30 days" dialog (OK quits the game)
    "menu_play": (0.5, 0.45),           # guess
    "char_create": (0.85, 0.9),         # guess
    "barb_class": (0.5, 0.5),           # guess
    "name_box": (0.5, 0.88),            # guess
    "create_ok": (0.5, 0.95),           # guess
    "play_btn": (0.5, 0.92),            # guess
}


class Report:
    def __init__(self):
        self.rows = []

    def add(self, step, ok, shots, note=""):
        self.rows.append((step, ok, shots, note))
        log(f"STEP {step}: {'PASS' if ok else 'FAIL'} {note}")

    def write(self, extra=""):
        p = os.path.join(current_run(), "report.md")
        with open(p, "w", encoding="utf-8") as f:
            f.write(f"# Smoke run {os.path.basename(current_run())}\n\n")
            f.write("| Step | Result | Screenshots | Note |\n|---|---|---|---|\n")
            for step, ok, shots, note in self.rows:
                f.write(f"| {step} | {'PASS' if ok else 'FAIL'} | {', '.join(shots)} | {note} |\n")
            f.write("\n" + extra + "\n")
        log(f"report {p}")


def clear_intro():
    """Calibration screens (every launch), skippable intro cinematics (hold a key), title screen.
    Returns False if the game quit (license check dialog: OK closes the game)."""
    from PIL import ImageGrab, ImageStat
    def button_visible():
        l, t, r, b = client_rect(need_window())
        w, h = r - l, b - t
        box = (l + int(.45 * w), t + int(.82 * h), l + int(.55 * w), t + int(.86 * h))
        return ImageStat.Stat(ImageGrab.grab(bbox=box, all_screens=True).convert("L")).mean[0] > 50

    def wait_button(sec):
        t0 = time.time()
        while time.time() - t0 < sec:
            if button_visible():
                return True
            time.sleep(1)
        return False

    # three calibration screens; the Continue button is bright while one is up
    while wait_button(40):
        click(*UI["calib_continue"])
        time.sleep(3)
        if not wait_button(6):
            break
    time.sleep(6)
    press("esc", hold=3)  # "Hold any button to skip"
    time.sleep(5)
    shot("intro_end")
    if not game_pids():
        return False
    # Blizzard license dialog: only OK, which quits. Detect by clicking it and seeing whether the game exits.
    click(*UI["license_ok"])
    time.sleep(6)
    return bool(game_pids())


def step_shot(rep, step, name, key_name=None, wait=1.5):
    if key_name:
        key(key_name)
    time.sleep(wait)
    p = shot(name)
    rep.add(step, True, [os.path.basename(p)], "screenshot taken, review by eye")


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    c = a[0]
    if c == "start":
        current_run(create=True)
        sys.exit(0 if start() else 1)
    elif c == "shot":
        shot(a[1])
    elif c == "key":
        key(*a[1:])
    elif c == "click":
        click(float(a[1]), float(a[2]), a[3] if len(a) > 3 else "left")
    elif c == "console":
        console(" ".join(a[1:]))
    elif c == "stop":
        stop()
    elif c == "logscan":
        logscan()
    elif c == "smoke":
        run_smoke()
    else:
        print(__doc__)


def run_smoke():
    current_run(create=True)
    rep = Report()
    try:
        ok = start()
        rep.add("1a start (log line + window)", ok, [])
        if not ok:
            return
        if not clear_intro():
            rep.add("1b main menu reached", False, ["intro_end.png"],
                    "BLOCKED: D2R shows 'You have not been online in the last 30 days. Please start the game while "
                    "online to verify the product license.' and OK quits the game. Needs one online launch "
                    "(Battle.net) by the user; not attempted by this script.")
            return
        rep.add("1b main menu reached", True, [os.path.basename(shot("menu"))])
        # ---- everything below is untested guesswork; tune UI fractions from the screenshots ----
        click(*UI["menu_play"]); time.sleep(3)
        shot("charselect")
        click(*UI["char_create"]); time.sleep(2)
        click(*UI["barb_class"]); time.sleep(1)
        click(*UI["name_box"]); type_text(CHAR_NAME); time.sleep(1)
        shot("create")
        click(*UI["create_ok"]); time.sleep(3)
        click(*UI["play_btn"])
        time.sleep(15)
        step_shot(rep, "3a in town", "town")
        step_shot(rep, "3b character sheet (C)", "charsheet", "c")
        key("c")
        step_shot(rep, "3c skill tree (T)", "skilltree", "t")
        for i in range(1, 9):  # tab positions are a guess
            click(0.62, 0.12 + 0.08 * i)
            step_shot(rep, f"3d skill tab {i}", f"skilltab{i}", wait=1.0)
        key("t")
        step_shot(rep, "3e inventory (I)", "inventory", "i")
        key("i")
        key("esc"); time.sleep(1)
        shot("escmenu")
        click(0.5, 0.45); time.sleep(8)  # Save and Exit (guess)
        shot("charselect2")
        click(*UI["play_btn"]); time.sleep(15)
        step_shot(rep, "4 reload", "town_reload")
        key("esc"); time.sleep(1)
        click(0.5, 0.45); time.sleep(8)
    finally:
        try:
            stop()
        finally:
            r = logscan()
            rep.write("## logscan\n\n```\n" + r["text"] + "\n```")


if __name__ == "__main__":
    main()

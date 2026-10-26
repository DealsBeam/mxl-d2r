#!/usr/bin/env python
"""Game slots: up to three D2R test instances side by side (docs/testing.md, "Parallel slots").

A slot is a full D2R 3.3 build 93847 folder with D2RLoader and the Median XL mod, its own save folder and its own
run lock:

  slot 1  C:\\Program Files (x86)\\Diablo II Resurrected          saves  Saved Games\\Diablo II Resurrected\\mods\\MedianXL
  slot 2  D:\\D2R-93847                                            saves  ...\\mods\\MedianXL-s2
  slot 3  D:\\D2R-93847-s3                                         saves  ...\\mods\\MedianXL-s3

Folders can be moved with env MXL_SLOT1/2/3_DIR. Locks are directories under %USERPROFILE%\\mxl-d2r-runs:
game.lock.<n> (slot 1 also honours the old single game.lock, and takes both). A lock holds an `owner.txt`
(`<owner> pid=<pid> <time>`); a lock without that file (made by hand with mkdir) is respected as "someone holds it".

Commands (py -3 tools/game_slots.py ...):
  status                    per slot: folder, running processes, lock, save folder
  setup [N ...]             make slots ready: modinfo savepath, d2rloader.toml copied from slot 1 (never overwritten)
  lock N [OWNER]            take the lock of slot N (exit 1 when held)
  unlock N [OWNER]          release it (only the owner, or with OWNER omitted a stale one)
  install [N ...]           run tools/install_mod.py for those slots (default: all slots that exist)
"""
import ctypes
import ctypes.wintypes as wt
import datetime
import json
import os
import shutil
import subprocess
import sys
import time

RUNS = os.environ.get("MXL_RUNS") or os.path.join(os.path.expanduser("~"), "mxl-d2r-runs")
DEFAULTS = {1: r"C:\Program Files (x86)\Diablo II Resurrected", 2: r"D:\D2R-93847", 3: r"D:\D2R-93847-s3"}
SAVE_ROOT = os.path.join(os.environ.get("USERPROFILE", ""), r"Saved Games\Diablo II Resurrected\mods")
MOD = "MedianXL"


def slot_dir(n):
    return os.environ.get(f"MXL_SLOT{n}_DIR", DEFAULTS[n])


def save_name(n):
    """The mod's save folder name inside Saved Games\\Diablo II Resurrected\\mods (slot 1 keeps MedianXL)."""
    return MOD if n == 1 else f"{MOD}-s{n}"


def save_dir(n):
    return os.path.join(SAVE_ROOT, save_name(n))


def mod_dir(n):
    return os.path.join(slot_dir(n), "mods", MOD)


def slot_of_game_dir(game):
    g = os.path.normcase(os.path.abspath(game))
    for n in DEFAULTS:
        if os.path.normcase(os.path.abspath(slot_dir(n))) == g:
            return n
    return None


# ---------------------------------------------------------------- processes

_k32 = ctypes.windll.kernel32
_k32.OpenProcess.restype = ctypes.c_void_p
_k32.CreateToolhelp32Snapshot.restype = ctypes.c_void_p
_k32.CloseHandle.argtypes = [ctypes.c_void_p]


class _PE(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD), ("th32DefaultHeapID", ctypes.c_size_t),
                ("th32ModuleID", wt.DWORD), ("cntThreads", wt.DWORD), ("th32ParentProcessID", wt.DWORD),
                ("pcPriClassBase", wt.LONG), ("dwFlags", wt.DWORD), ("szExeFile", ctypes.c_wchar * 260)]


def image_path(pid):
    h = _k32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return ""
    try:
        buf = ctypes.create_unicode_buffer(1024)
        n = wt.DWORD(1024)
        _k32.QueryFullProcessImageNameW(ctypes.c_void_p(h), 0, buf, ctypes.byref(n))
        return buf.value
    finally:
        _k32.CloseHandle(h)


ALL_IMAGES = ("d2rloader.exe", "d2r.exe", "d2rloader-s2.exe", "d2rloader-s3.exe")


def loader_name(game):
    """Slots 2 and 3 run a renamed copy of the loader (D2RLoader-sN.exe), made by `setup`. Older tools that look for
    every D2RLoader.exe (to wait for it, close it or kill it) then never see the test instances of other lanes, and
    the instances never see theirs."""
    n = slot_of_game_dir(game)
    alt = f"D2RLoader-s{n}.exe"
    return alt if n and n > 1 and os.path.exists(os.path.join(game, alt)) else "D2RLoader.exe"


def processes(names=ALL_IMAGES):
    """[(pid, exe name, full image path)] of processes with these image names."""
    snap = _k32.CreateToolhelp32Snapshot(2, 0)
    out = []
    pe = _PE()
    pe.dwSize = ctypes.sizeof(_PE)
    ok = _k32.Process32FirstW(ctypes.c_void_p(snap), ctypes.byref(pe))
    while ok:
        if pe.szExeFile.lower() in names:
            out.append((pe.th32ProcessID, pe.szExeFile, image_path(pe.th32ProcessID)))
        ok = _k32.Process32NextW(ctypes.c_void_p(snap), ctypes.byref(pe))
    _k32.CloseHandle(ctypes.c_void_p(snap))
    return out


def under(path, folder):
    p, f = os.path.normcase(os.path.abspath(path)), os.path.normcase(os.path.abspath(folder))
    return p.startswith(f.rstrip("\\") + "\\")


def slot_processes(game, names=ALL_IMAGES):
    """Processes whose image lives in this game folder."""
    return [(pid, nm) for pid, nm, path in processes(names) if path and under(path, game)]


def pid_alive(pid):
    h = _k32.OpenProcess(0x1000, False, pid)
    if not h:
        return False
    code = wt.DWORD()
    _k32.GetExitCodeProcess(ctypes.c_void_p(h), ctypes.byref(code))
    _k32.CloseHandle(ctypes.c_void_p(h))
    return code.value == 259  # STILL_ACTIVE


# ---------------------------------------------------------------- locks


def lock_paths(n):
    paths = [os.path.join(RUNS, f"game.lock.{n}")]
    if n == 1:
        paths.append(os.path.join(RUNS, "game.lock"))
    return paths


def lock_info(n):
    """None when free, else (path, owner text or None for a lock without owner.txt)."""
    for p in lock_paths(n):
        if os.path.isdir(p):
            try:
                txt = open(os.path.join(p, "owner.txt"), encoding="utf-8").read().strip()
            except OSError:
                txt = None
            return p, txt
    return None


def _owner_pid(txt):
    for part in (txt or "").split():
        if part.startswith("pid="):
            try:
                return int(part[4:])
            except ValueError:
                pass
    return None


def acquire(n, owner, pid=None):
    """Take the lock of slot N. Returns (ok, message). Stale locks (owner pid gone) are replaced. pid=0 marks a manual
    lock (command line `lock N`): it is never treated as stale and lasts until `unlock N`."""
    os.makedirs(RUNS, exist_ok=True)
    pid = os.getpid() if pid is None else pid
    info = lock_info(n)
    if info:
        path, txt = info
        op = _owner_pid(txt)
        if txt and txt.split()[0] == owner:
            return True, f"lock {path} already ours"
        if op is not None and op != 0 and not pid_alive(op):  # pid 0: manual lock, never stale
            for p in lock_paths(n):
                shutil.rmtree(p, ignore_errors=True)
        else:
            return False, f"lock {path} held by {txt or 'someone (no owner.txt)'}"
    made = []
    for p in lock_paths(n):
        try:
            os.mkdir(p)
        except FileExistsError:
            for q in made:
                shutil.rmtree(q, ignore_errors=True)
            return False, f"lock {p} taken just now"
        made.append(p)
        open(os.path.join(p, "owner.txt"), "w", encoding="utf-8").write(
            f"{owner} pid={pid} {datetime.datetime.now():%Y-%m-%d %H:%M:%S}")
    return True, f"lock {made[0]} taken by {owner}"


def release(n, owner=None):
    """Remove the lock if it is ours (owner given: only a lock whose owner.txt names that owner) or, with owner None
    (command line `unlock N`), whatever is there. A lock somebody made by hand (no owner.txt) is never removed by a
    run that merely used it."""
    info = lock_info(n)
    if not info:
        return True
    path, txt = info
    if owner is not None and (not txt or txt.split()[0] != owner):
        return False
    for p in lock_paths(n):
        shutil.rmtree(p, ignore_errors=True)
    return True


# ---------------------------------------------------------------- setup


def set_savepath(n):
    """modinfo.json (mod root and inside the folder MPQ) points the mod at this slot's own save folder."""
    mod = mod_dir(n)
    text = json.dumps({"name": MOD, "savepath": save_name(n) + "/"}, indent=2)
    for target in (os.path.join(mod, f"{MOD}.mpq", "modinfo.json"), os.path.join(mod, "modinfo.json")):
        if os.path.isdir(os.path.dirname(target)):
            open(target, "w", encoding="utf-8").write(text)


def setup(n):
    game = slot_dir(n)
    if not os.path.exists(os.path.join(game, "D2RLoader.exe")):
        return f"slot {n}: {game} has no D2RLoader.exe (copy slot 1's folder first, see docs/setup.md)"
    src = os.path.join(slot_dir(1), "d2rloader", "config")
    dst = os.path.join(game, "d2rloader", "config")
    os.makedirs(dst, exist_ok=True)
    for f in ("d2rloader.toml", "debugsettings.json"):
        if n != 1 and os.path.exists(os.path.join(src, f)) and not os.path.exists(os.path.join(dst, f)):
            shutil.copy2(os.path.join(src, f), os.path.join(dst, f))
    set_savepath(n)
    os.makedirs(save_dir(n), exist_ok=True)
    if n > 1:
        src_exe, dst_exe = os.path.join(game, "D2RLoader.exe"), os.path.join(game, f"D2RLoader-s{n}.exe")
        if not os.path.exists(dst_exe) or os.path.getsize(dst_exe) != os.path.getsize(src_exe):
            shutil.copy2(src_exe, dst_exe)
    return f"slot {n}: {game}, saves {save_dir(n)}, loader {loader_name(game)}"


# ---------------------------------------------------------------- cli


def status():
    for n in DEFAULTS:
        game = slot_dir(n)
        have = os.path.exists(os.path.join(game, "D2RLoader.exe"))
        procs = slot_processes(game) if have else []
        lk = lock_info(n)
        try:
            sp = json.load(open(os.path.join(mod_dir(n), "modinfo.json")))["savepath"]
        except Exception:
            sp = "?"
        print(f"slot {n}: {game} {'ok' if have else 'MISSING'}; running {procs or '-'}; "
              f"lock {lk[1] or 'held (no owner.txt)' if lk else 'free'}; savepath {sp}")


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 0
    c = a[0]
    if c == "status":
        status()
    elif c == "setup":
        for n in [int(x) for x in a[1:]] or list(DEFAULTS):
            print(setup(n))
    elif c == "lock":
        # pid 0: this command exits right away, so its own pid would make the lock look stale at once
        ok, msg = acquire(int(a[1]), a[2] if len(a) > 2 else "manual", pid=0)
        print(msg)
        return 0 if ok else 1
    elif c == "unlock":
        return 0 if release(int(a[1]), a[2] if len(a) > 2 else None) else 1
    elif c == "install":
        here = os.path.dirname(os.path.abspath(__file__))
        rc = 0
        for n in [int(x) for x in a[1:]] or [k for k in DEFAULTS if os.path.exists(os.path.join(slot_dir(k), "D2RLoader.exe"))]:
            rc |= subprocess.call([sys.executable, os.path.join(here, "install_mod.py"), "--slot", str(n)])
        return rc
    else:
        print(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main())

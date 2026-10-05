"""ctypes loaders for the StormLib and CascLib builds in build/."""
import ctypes
from ctypes import wintypes
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def _load(name: str) -> ctypes.WinDLL:
    path = ROOT / "build" / name / "Release" / f"{name}.dll"
    if not path.exists():
        raise FileNotFoundError(f"{path} missing; run tools/build_native.ps1")
    return ctypes.WinDLL(str(path))


storm = _load("StormLib")
storm.SFileOpenArchive.argtypes = [ctypes.c_char_p, wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE)]
storm.SFileOpenArchive.restype = wintypes.BOOL
storm.SFileCloseArchive.argtypes = [wintypes.HANDLE]
storm.SFileCloseArchive.restype = wintypes.BOOL

STREAM_FLAG_READ_ONLY = 0x100


def open_mpq(path: str | Path) -> wintypes.HANDLE:
    handle = wintypes.HANDLE()
    if not storm.SFileOpenArchive(str(path).encode("mbcs"), 0, STREAM_FLAG_READ_ONLY, ctypes.byref(handle)):
        raise OSError(ctypes.GetLastError(), f"SFileOpenArchive failed: {path}")
    return handle

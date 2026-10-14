"""Median's own inventory pictures as D2R HD sprites (Phase 5), used by tools/item_hd.py.

D2R's HD inventory sprites (data/hd/global/ui/items/<weapon|armor|misc>/...) are one RGBA frame, 98 pixels per
inventory cell (classic: 28), plus a .lowend.sprite at 49 pixels per cell; header as tools/skill_icons.py's
write_sprite but with the frame width field equal to the width. A Median-only picture (data/global/items/<inv>.dc6,
frame 0, units palette) is scaled by 3.5 (bilinear, premultiplied alpha) and centred on its item's invwidth x
invheight cells, then written to <folder>/mxl/<inv>.sprite; the asset name is "mxl/<inv>". Results are cached: a
sprite newer than its DC6 is kept.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from skill_icons import decode_frame, palette, read_dc6  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MXL_ITEMS = ROOT / "extract" / "mxl" / "data" / "global" / "items"
D2R_FILELIST = ROOT / "extract" / "d2r" / "93847" / "filelist.txt"
OUT = ROOT / "work" / "mod" / "data" / "hd" / "global" / "ui" / "items"
CELL_CLASSIC, CELL_HD, CELL_LOW = 28, 98, 49

_pal = None
_d2r = None


def median_only(inv: str) -> bool:
    """True when <inv>.dc6 is Median's own picture (D2R has no classic file of that name)."""
    global _d2r
    if _d2r is None:
        _d2r = {l.strip().lower() for l in D2R_FILELIST.read_text("utf-8").splitlines()}
    return bool(inv) and (MXL_ITEMS / f"{inv}.dc6").exists() and f"data:data/global/items/{inv.lower()}.dc6" not in _d2r


def scale(rows: list[list[tuple]], factor: float, out_w: int, out_h: int) -> bytes:
    """Bilinear scale by factor, centred on an out_w x out_h transparent canvas; RGBA bytes."""
    h, w = len(rows), len(rows[0]) if rows else 0
    sw, sh = round(w * factor), round(h * factor)
    ox, oy = (out_w - sw) // 2, (out_h - sh) // 2
    out = bytearray(out_w * out_h * 4)
    for y in range(max(sh, 0)):
        ty = y + oy
        if not 0 <= ty < out_h:
            continue
        sy = min(max((y + 0.5) / factor - 0.5, 0), h - 1)
        y0 = int(sy)
        fy = sy - y0
        y1 = min(y0 + 1, h - 1)
        r0, r1 = rows[y0], rows[y1]
        for x in range(sw):
            tx = x + ox
            if not 0 <= tx < out_w:
                continue
            sx = min(max((x + 0.5) / factor - 0.5, 0), w - 1)
            x0 = int(sx)
            fx = sx - x0
            x1 = min(x0 + 1, w - 1)
            r = g = b = a = 0.0
            for p, wt in ((r0[x0], (1 - fx) * (1 - fy)), (r0[x1], fx * (1 - fy)), (r1[x0], (1 - fx) * fy),
                          (r1[x1], fx * fy)):
                if p[3]:
                    k = wt * p[3] / 255
                    r += p[0] * k
                    g += p[1] * k
                    b += p[2] * k
                    a += k
            if a > 0:
                i = (ty * out_w + tx) * 4
                out[i:i + 4] = bytes((round(r / a), round(g / a), round(b / a), min(255, round(a * 255))))
    return bytes(out)


def write_sprite(path: Path, width: int, height: int, rgba: bytes) -> None:
    head = b"SpA1" + struct.pack("<HHIIIIIIII", 31, width, width, height, 0, 1, 0, 0, len(rgba), 4)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(head + rgba)


def median_asset(inv: str, folder: str, cells_w: int, cells_h: int) -> str | None:
    """Writes the HD sprites for Median's picture <inv> (if needed) and returns its asset name, else None."""
    global _pal
    if not median_only(inv):
        return None
    src = MXL_ITEMS / f"{inv}.dc6"
    hd = OUT / folder / "mxl" / f"{inv.lower()}.sprite"
    low = hd.with_suffix(".lowend.sprite")
    if not (hd.exists() and low.exists() and hd.stat().st_mtime >= src.stat().st_mtime):
        if _pal is None:
            _pal = palette()
        frames = read_dc6(src)
        if not frames:
            return None
        w, h, _ox, _oy, block = frames[0]
        rows = decode_frame(w, h, block, _pal)
        # the picture's own size decides when the table's cells are missing or too small
        cw = max(cells_w or 1, -(-w // CELL_CLASSIC))
        ch = max(cells_h or 1, -(-h // CELL_CLASSIC))
        for path, cell in ((hd, CELL_HD), (low, CELL_LOW)):
            write_sprite(path, cw * cell, ch * cell, scale(rows, cell / CELL_CLASSIC, cw * cell, ch * cell))
    return f"mxl/{inv.lower()}"

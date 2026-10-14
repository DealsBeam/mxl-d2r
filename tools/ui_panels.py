"""Median's panel art as D2R HD panel backgrounds (Phase 5: inventory and cube sizes).

D2R's HD panels draw a 1162 x 1507 background sprite (e.g. PANEL\\Inventory\\Classic_Background =
data/hd/global/ui/panel/inventory/classic_background.sprite, plus a .lowend at half size). Median's panels are
512 x 704 classic pictures in six 256-pixel tiles (themes/classic_sigma/game/panels/*.dc6, 2 x 3 tiles per page).
Each page used here is assembled, scaled to 1162 x 1507 (x 2.2695 across, x 2.1406 down; bilinear) and written to
data/hd/global/ui/panel/mxl/<name>.sprite, referenced by the layouts as PANEL\\mxl\\<name> (tools/ui_layouts.py).
Sprites newer than their DC6 (and this script) are kept.

Stash: Median's gold box sits above the grid, where D2R's Personal / Shared tabs go, while D2R's gold button is below
the shared-stash arrows. The box (MOVES) is moved down to the empty bottom of the panel and its old place is filled
with the panel's background, taken from just right of the box.

Usage: python tools/ui_panels.py
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from skill_icons import decode_frame, palette, read_dc6  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "extract" / "mxl" / "data" / "global" / "themes" / "classic_sigma" / "game" / "panels"
OUT = ROOT / "work" / "mod" / "data" / "hd" / "global" / "ui" / "panel" / "mxl"
CLASSIC_W, CLASSIC_H = 512, 704
HD_W, HD_H = 1162, 1507
# sprite name -> (dc6, page)
PANELS = {"inventory": ("character-inventory", 0), "cube": ("crafting", 0), "stash": ("stash", 0)}
# sprite name -> [(left, top, right, bottom, new top)] in classic pixels
MOVES = {"stash": [(112, 86, 403, 134, 615)]}
FILL_FROM = 404  # background strip for the holes: opaque pixels of the same row from this x on


def move_boxes(rows: list[list[tuple]], moves: list[tuple]) -> None:
    for x0, y0, x1, y1, new_y in moves:
        box = [r[x0:x1] for r in rows[y0:y1]]
        for y in range(y0, y1):
            strip = [p for p in rows[y][FILL_FROM:] if p[3]][:16]
            for x in range(x0, x1):
                rows[y][x] = strip[(x - x0) % len(strip)]
        for i, line in enumerate(box):
            for j, p in enumerate(line):
                if p[3]:
                    rows[new_y + i][x0 + j] = p


def page_rows(dc6: Path, page: int, pal) -> list[list[tuple]]:
    frames = read_dc6(dc6)
    rows = [[(0, 0, 0, 0)] * CLASSIC_W for _ in range(CLASSIC_H)]
    for t in range(6):
        w, h, _ox, _oy, block = frames[page * 6 + t]
        tile = decode_frame(w, h, block, pal)
        tx, ty = (t % 2) * 256, (t // 2) * 256
        for y in range(h):
            if ty + y < CLASSIC_H:
                rows[ty + y][tx:tx + w] = tile[y]
    return rows


def scale(rows: list[list[tuple]], out_w: int, out_h: int) -> bytes:
    h, w = len(rows), len(rows[0])
    fx_, fy_ = w / out_w, h / out_h
    out = bytearray(out_w * out_h * 4)
    xs = []
    for x in range(out_w):
        sx = min(max((x + 0.5) * fx_ - 0.5, 0), w - 1)
        x0 = int(sx)
        xs.append((x0, min(x0 + 1, w - 1), sx - x0))
    i = 0
    for y in range(out_h):
        sy = min(max((y + 0.5) * fy_ - 0.5, 0), h - 1)
        y0 = int(sy)
        fy = sy - y0
        r0, r1 = rows[y0], rows[min(y0 + 1, h - 1)]
        for x0, x1, fx in xs:
            r = g = b = a = 0.0
            for p, wt in ((r0[x0], (1 - fx) * (1 - fy)), (r0[x1], fx * (1 - fy)), (r1[x0], (1 - fx) * fy), (r1[x1], fx * fy)):
                if p[3]:
                    k = wt * p[3] / 255
                    r += p[0] * k
                    g += p[1] * k
                    b += p[2] * k
                    a += k
            if a > 0:
                out[i:i + 4] = bytes((round(r / a), round(g / a), round(b / a), min(255, round(a * 255))))
            i += 4
    return bytes(out)


def write_sprite(path: Path, width: int, height: int, rgba: bytes) -> None:
    head = b"SpA1" + struct.pack("<HHIIIIIIII", 31, width, width, height, 0, 1, 0, 0, len(rgba), 4)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(head + rgba)


def main() -> int:
    pal = None
    for name, (dc6, page) in PANELS.items():
        src = SRC / f"{dc6}.dc6"
        hd, low = OUT / f"{name}.sprite", OUT / f"{name}.lowend.sprite"
        newest = max(src.stat().st_mtime, Path(__file__).stat().st_mtime)
        if hd.exists() and low.exists() and hd.stat().st_mtime >= newest:
            continue
        pal = pal or palette()
        rows = page_rows(src, page, pal)
        move_boxes(rows, MOVES.get(name, []))
        write_sprite(hd, HD_W, HD_H, scale(rows, HD_W, HD_H))
        write_sprite(low, HD_W // 2, HD_H // 2, scale(rows, HD_W // 2, HD_H // 2))
        print(f"   panel {name}: {dc6}.dc6 page {page} -> {HD_W}x{HD_H}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

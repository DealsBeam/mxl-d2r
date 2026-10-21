"""Median's panel art as D2R HD panel backgrounds (Phase 5: inventory and cube sizes).

D2R's HD panels draw a 1162 x 1507 background sprite (e.g. PANEL\\Inventory\\Classic_Background =
data/hd/global/ui/panel/inventory/classic_background.sprite, plus a .lowend at half size). Median's panels are
512 x 704 classic pictures in six 256-pixel tiles (themes/classic_sigma/game/panels/*.dc6, 2 x 3 tiles per page).
Each page used here is assembled, scaled to 1162 x 1507 (x 2.2695 across, x 2.1406 down; bilinear) and written to
data/hd/global/ui/panel/mxl/<name>.sprite (consumables.sprite: see below), referenced by the layouts as PANEL\\mxl\\<name> (tools/ui_layouts.py).
Sprites newer than their DC6 (and this script) are kept.

Stash: Median's gold box sits above the grid, where D2R's Personal / Shared tabs go, while D2R's gold button is below
the shared-stash arrows. The box (MOVES) is moved down to the empty bottom of the panel and its old place is filled
with the panel's background, taken from just right of the box.

Trade window (Median's trade.dc6): two 7 x 15 grids side by side. Store (store.dc6, page 0): a 15 x 15 grid under four
tabs; the page has its last tab drawn pressed, so the unpressed tab from page 3 (same place) is pasted over it
(PATCHES), because D2R's tab bar draws the active tab itself.

Character sheet (stat-screen.dc6, page 0): the main page of Median's statsheet (the other two pages are its
extra stat pages, see docs/PLAN.md), empty boxes that tools/ui_layouts.py fills with D2R's stat text widgets.

Stash, materials tab: D2R's panel (PANEL/Stash/AdditionalStash/PANEL_AdditionalStash_Consumables) shows the
Horadric Cube as a small 3 x 4 recess. Median's cube grid is 15 x 10, so the cube picture of crafting.dc6 is pasted
over the recess at the size of the cube view in the layout (CUBE_VIEW; tools/ui_layouts.py reads the same numbers).
The sprite is taken from the extracted D2R files (extract/d2r/93847).

Classic mode (G): the same pages scaled to D2R's classic panel (320 x 432; the virtual screen is 640 x 480) as DC6
files in data/global/ui/panel/mxl/<name>.dc6 (4 tiles of 256 / 64 x 256 / 176; inventory 8 = two sets), quantized to
D2R's palette. Median's 512 x 704 page becomes 314 x 432 (x 0.6136 both ways), at the outer screen edge of the
320-pixel panel (6 transparent pixels beside the middle of the screen for the left panels, at the right of the
inventory). tools/ui_layouts.py places the grids and widgets with the same factor.

Usage: python tools/ui_panels.py
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from PIL import Image  # noqa: E402
from skill_icons import decode_frame, palette, read_dc6, write_dc6  # noqa: E402
from skill_tree_art import encode, quantizer  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "extract" / "mxl" / "data" / "global" / "themes" / "classic_sigma" / "game" / "panels"
D2R_STASH = ROOT / "extract" / "d2r" / "93847" / "data" / "hd" / "global" / "ui" / "panel" / "stash" / "additionalstash"
CUBE_ART = ("crafting", 0, (39, 208, 474, 498))  # Median's cube grid picture: dc6, page, classic rectangle
# where the cube view sits in the materials tab, in HD pixels (x, y, width, height); 15 x 10 cells of 54 x 50.6
CUBE_VIEW = (177, 720, 810, 506)
OUT = ROOT / "work" / "mod" / "data" / "hd" / "global" / "ui" / "panel" / "mxl"
CLASSIC_OUT = ROOT / "work" / "mod" / "data" / "global" / "ui" / "panel" / "mxl"
CLASSIC_W, CLASSIC_H = 512, 704
PANEL_W, PANEL_H = 320, 432                   # D2R's classic panel
CLASSIC_SCALE = PANEL_H / CLASSIC_H           # Median's page -> classic panel, both ways (314 x 432)
RIGHT_PANELS = {"inventory"}                  # the others are left panels
HD_W, HD_H = 1162, 1507
# sprite name -> (dc6, page)
PANELS = {"inventory": ("character-inventory", 0), "cube": ("crafting", 0), "stash": ("stash", 0),
          "trade": ("trade", 0), "store": ("store", 0), "charsheet": ("stat-screen", 0)}
# sprite name -> [(left, top, right, bottom, new top)] in classic pixels
MOVES = {"stash": [(112, 86, 403, 134, 615)]}
# sprite name -> [(page, left, top, right, bottom)]: pixels of another page of the same DC6 pasted in place
PATCHES = {"store": [(3, 362, 128, 478, 178)]}
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


def patch_rows(rows: list[list[tuple]], dc6: Path, patches: list[tuple], pal) -> None:
    for page, x0, y0, x1, y1 in patches:
        other = page_rows(dc6, page, pal)
        for y in range(y0, y1):
            rows[y][x0:x1] = other[y][x0:x1]


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
        patch_rows(rows, src, PATCHES.get(name, []), pal)
        write_sprite(hd, HD_W, HD_H, scale(rows, HD_W, HD_H))
        write_sprite(low, HD_W // 2, HD_H // 2, scale(rows, HD_W // 2, HD_H // 2))
        print(f"   panel {name}: {dc6}.dc6 page {page} -> {HD_W}x{HD_H}")
    cube_in_stash()
    classic_panels(pal)
    return 0


def classic_panels(pal=None) -> None:
    """Median's pages as classic panel DC6 files (see the module text)."""
    q = None
    for name, (dc6, page) in PANELS.items():
        src = SRC / f"{dc6}.dc6"
        out = CLASSIC_OUT / f"{name}.dc6"
        newest = max(src.stat().st_mtime, Path(__file__).stat().st_mtime)
        if out.exists() and out.stat().st_mtime >= newest:
            continue
        pal = pal or palette()
        # indices 225-254 of the units palette are dark shades, but the UI palette has hot pink there: not used
        ui_pal = pal[:225] + [(1, 255, 1)] * 31
        q = q or quantizer(ui_pal)
        rows = page_rows(src, page, pal)
        move_boxes(rows, MOVES.get(name, []))
        patch_rows(rows, src, PATCHES.get(name, []), pal)
        im = Image.new("RGBA", (CLASSIC_W, CLASSIC_H))
        im.putdata([p for r in rows for p in r])
        w = round(CLASSIC_W * CLASSIC_SCALE)
        small = im.convert("RGBa").resize((w, PANEL_H), Image.LANCZOS).convert("RGBA")
        panel = Image.new("RGBA", (PANEL_W, PANEL_H))
        panel.paste(small, (0 if name in RIGHT_PANELS else PANEL_W - w, 0))
        frames = []
        for _set in range(2 if name == "inventory" else 1):
            for box in ((0, 0, 256, 256), (256, 0, 320, 256), (0, 256, 256, 432), (256, 256, 320, 432)):
                tile = panel.crop(box)
                frames.append((tile.width, tile.height, 0, 0, encode(tile, q, ui_pal)))
        write_dc6(out, frames)
        print(f"   classic panel {name}: {dc6}.dc6 page {page} -> {w}x{PANEL_H}")


def cube_in_stash() -> None:
    """D2R's stash materials page with Median's cube grid picture over its 3 x 4 recess (HD and lowend)."""
    name = "panel_additionalstash_consumables"
    src = SRC / f"{CUBE_ART[0]}.dc6"
    if not (D2R_STASH / f"{name}.sprite").exists():
        print(f"   {name}: D2R sprite not extracted, cube view keeps D2R's art")
        return
    hd, low = OUT / "consumables.sprite", OUT / "consumables.lowend.sprite"
    newest = max(src.stat().st_mtime, Path(__file__).stat().st_mtime, (D2R_STASH / f"{name}.sprite").stat().st_mtime)
    if hd.exists() and low.exists() and hd.stat().st_mtime >= newest:
        return
    pal = palette()
    page = page_rows(src, CUBE_ART[1], pal)
    left, top, right, bottom = CUBE_ART[2]
    crop = [r[left:right] for r in page[top:bottom]]
    for path, k in ((hd, 1), (low, 2)):
        base = D2R_STASH / f"{name}{'.lowend' if k == 2 else ''}.sprite"
        data = base.read_bytes()
        head, (_ver, _w, width, height, *_rest) = data[:40], struct.unpack("<HHIIIIIIII", data[4:40])
        pixels = bytearray(data[40:40 + width * height * 4])
        x, y, w, h = (v // k for v in CUBE_VIEW)
        art = scale(crop, w, h)
        for j in range(h):
            pixels[((y + j) * width + x) * 4:((y + j) * width + x + w) * 4] = art[j * w * 4:(j + 1) * w * 4]
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(head + bytes(pixels))
    print(f"   panel stash materials: Median's cube grid at {CUBE_VIEW}")


if __name__ == "__main__":
    sys.exit(main())

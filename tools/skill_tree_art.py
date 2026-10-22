"""Median's skill tree look for D2R's skill tree panel (bug #3): tab titles, tree background, arrows, tab pictures (8, the Sorceress 9).

What Median has (D2Sigma SkillTree.cpp, themes/classic_sigma/game): one stone panel (panels/skill-tree.dc6, the same in
every class), one arrow layout per class and tab (skills/layout-<class>.dc6: 512 x 704 pages, arrows only; the icon
grid is 3 columns x 6 rows, 100-pixel pitch, 48-pixel icons) and tab titles that are TEXT (string ids from
charstatsexp: StrSkillTabs<Class> + tab, e.g. 11676 Divine, Bow, Javelin, ... Reward), not art. D2R fills text for
three tabs only (textTab0-2, one array per class), so the titles are drawn here into the tab pictures.

What D2R has: the layout names a per-class sprite (skillBackgroundFile) and every TabWidget has a "Background" image
with a frame number; the sprite is a strip of frames 895 x 1169 (stride 897; lowend half size) drawn at the panel
position 132, 160 for the active tab. D2R's frame holds the tab strip across the top (its 3 tabs), the skill
sockets and the arrows of that tab; the stone under it is PANEL_SkillTreeBG (the same for every class).
So the background can differ per class AND per tab, with as many frames as the layout has tabs (tools/ui_layouts.py:
TABS = 9, frame n for tab n).

HD (data/hd/global/ui/spells/skill_trees/):
  <xx>skilltree.sprite / .lowend.sprite   9 frames per class: the tab strip with Median's 8 (Sorceress 9) titles (the active tab
                                           lit), D2R's own socket art on the cells of that tab's skills, arrows as
                                           Median's layout draws them (found in layout-<class>.dc6, redrawn between
                                           D2R's sockets: D2R's grid is 262 x 180 pixels, Median's 100 x 100)
  panel_skilltreebg.sprite / .lowend      D2R's frame with Median's stone inside (the holes in the DC6 filled)
Classic mode (G) reads data/global/ui/spells/skltree_<x>_back.dc6: 4 tiles per picture (320 x 432 as 256+64 x 256+176),
vanilla picture sets 0..3; here set 0 = panel with the tab column, sets 1..9 = tab n-1 active with its sockets, arrows
and the tab titles (units palette; ui_layouts.py gives the classic panel 9 tab cells in the same column).

Usage: python tools/skill_tree_art.py [--preview DIR]   (--preview writes PNGs: HD frames and classic sets)
"""
import argparse
import math
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
from skill_icons import decode_frame, palette, read_dc6, write_dc6  # noqa: E402
from tbl2json import load_median  # noqa: E402
from txt_migrate import read_tsv  # noqa: E402
from ui_layouts import CLASSIC_TABS, TABS  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MXL = ROOT / "extract" / "mxl" / "data" / "global"
GAME_ART = MXL / "themes" / "classic_sigma" / "game"
D2R_TREES = ROOT / "extract" / "d2r" / "93847" / "data" / "hd" / "global" / "ui" / "spells" / "skill_trees"
MOD = ROOT / "work" / "mod" / "data"
HD_OUT = MOD / "hd" / "global" / "ui" / "spells" / "skill_trees"
CLASSIC_OUT = MOD / "global" / "ui" / "spells"
FONTS = Path(r"C:\Windows\Fonts")

# Median class (charstatsexp row order = D2R class order) -> D2R sprite prefix, classic file letter
CLASSES = [("ama", "am", "a"), ("sor", "so", "s"), ("nec", "ne", "n"), ("pal", "pa", "p"), ("bar", "ba", "b"),
           ("dru", "dr", "d"), ("ass", "as", "i")]

# ---- HD geometry (pixels of D2R's 1162-wide panel; frame origin = panel 132, 160) ----------------------------------
FW, FH, STRIDE = 895, 1169, 897
LOW_FW, LOW_FH, LOW_STRIDE = 447, 584, 449
SKILL_COL = (253, 515, 775)                       # layout skillColumn / skillRow: left / top of each 130-pixel icon
SKILL_ROW = (296, 478, 654, 834, 1014, 1190)
ORIGIN = (132, 160)
SOCKET_CROP = (106, 124, 328, 272)                # D2R's socket + level box in its frame 1 (cell col 0, row 0)
SOCKET_AT = (-15, -12)                            # where that crop starts, relative to (icon left, icon top) - origin
SOCKET_HALF = (74, 73)                            # half size of the socket square
STRIP_H = 96
# stone inside D2R's PANEL_SkillTreeBG (1023 x 1298): left, top, right, bottom
STONE_BOX = (76, 118, 947, 1209)

# ---- Median classic geometry (512 x 704 page) ----------------------------------------------------------------------
MX = (97, 197, 297)                               # icon centres of the arrow layouts: arrows run from the centre
MY = (80, 180, 280, 380, 480, 580)                # of one icon to (just short of) the centre of the next

# ---- D2R classic panel (320 x 432 pictures) ------------------------------------------------------------------------
CLASSIC_W, CLASSIC_H = 320, 432
CL_COL = (15, 84, 153)                            # icon left / top (layout skillColumn / skillRow + 240), 56 pixels
CL_ROW = (14, 82, 150, 218, 287, 355)
CL_ICON = 56
CL_TAB_X, CL_TAB_Y = CLASSIC_TABS["x"], CLASSIC_TABS["y"] + 240

ARROW_FILL, ARROW_EDGE = (52, 40, 30), (150, 118, 82)
GOLD, GOLD_DIM = (236, 214, 160), (170, 156, 134)


def font(size: int, bold: bool = True) -> ImageFont.FreeTypeFont:
    for name in ("georgiab.ttf", "timesbd.ttf", "arialbd.ttf") if bold else ("georgia.ttf", "times.ttf", "arial.ttf"):
        if (FONTS / name).exists():
            return ImageFont.truetype(str(FONTS / name), size)
    return ImageFont.load_default(size)


def to_image(rows: list[list[tuple]]) -> Image.Image:
    im = Image.new("RGBA", (len(rows[0]), len(rows)))
    im.putdata([p for r in rows for p in r])
    return im


def page(dc6: Path, number: int, pal) -> Image.Image:
    """One 512 x 704 page (six tiles, 2 x 3) of a Median DC6 as RGBA."""
    frames = read_dc6(dc6)
    im = Image.new("RGBA", (512, 704))
    for t in range(6):
        w, h, _ox, _oy, block = frames[number * 6 + t]
        im.paste(to_image(decode_frame(w, h, block, pal)), ((t % 2) * 256, (t // 2) * 256))
    return im


def fill_holes(im: Image.Image) -> Image.Image:
    """The stone DC6 has a few transparent pixels (index 0): fill them from the surrounding stone."""
    alpha = im.getchannel("A")
    solid = im.convert("RGB")
    mean = ImageFilter.BoxBlur(3)
    near = Image.composite(solid, Image.new("RGB", im.size, (30, 12, 8)), alpha.point(lambda a: 255 if a > 128 else 0))
    soft = near.filter(mean)
    out = Image.composite(solid, soft, alpha.point(lambda a: 255 if a > 128 else 0))
    out.putalpha(255)
    return out


def sprite(path: Path, frames: list[Image.Image], stride: int, frame_w: int | None = None) -> None:
    """D2R .sprite as skill_icons.write_sprite: frames side by side, each in a stride-wide cell (frame width field
    stride - 2, or the width itself for a single picture)."""
    h = frames[0].height
    strip = Image.new("RGBA", (stride * len(frames), h))
    for i, f in enumerate(frames):
        strip.paste(f, (i * stride, 0))
    data = strip.tobytes()
    head = b"SpA1" + struct.pack("<HHIIIIIIII", 31, frame_w or stride - 2, strip.width, h, 0, len(frames), 0, 0, len(data), 4)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(head + data)


def read_sprite(path: Path) -> list[Image.Image]:
    d = path.read_bytes()
    _v, fw, total, h, _z, n = struct.unpack_from("<HHIIII", d, 4)
    strip = Image.frombytes("RGBA", (total, h), d[40:40 + total * h * 4])
    stride = total // n
    return [strip.crop((i * stride, 0, i * stride + fw, h)) for i in range(n)]


# ---- data ---------------------------------------------------------------------------------------------------------
def titles() -> dict[str, list[str]]:
    """Median class -> tab titles (charstatsexp: first string id + tab)."""
    by_id, winner = load_median()
    rows = read_tsv(ROOT / "work" / "sigma" / "charstatsexp.txt")[1]
    out = {}
    for (cls, _p, _l), r in zip(CLASSES, rows):
        first = winner[r[2]]
        out[cls] = [by_id[first + i][1] for i in range(int(r[3]))]
    return out


def skill_cells() -> dict[tuple[str, int], set[tuple[int, int]]]:
    """(class, tab index) -> {(row, column)} (0-based) of the skills on that tab."""
    excel = MOD / "global" / "excel" / "base"
    dh, drows = read_tsv(excel / "skilldesc.txt")
    sh, srows = read_tsv(excel / "skills.txt")
    pg, ro, co = dh.index("SkillPage"), dh.index("SkillRow"), dh.index("SkillColumn")
    cc, sd = sh.index("charclass"), sh.index("skilldesc")
    desc = {r[0]: r for r in drows}
    cells: dict[tuple[str, int], set[tuple[int, int]]] = {}
    for r in srows:
        d = desc.get(r[sd])
        if d and r[cc] and d[pg] not in ("", "0"):
            cells.setdefault((r[cc], int(d[pg]) - 1), set()).add((int(d[ro]) - 1, int(d[co]) - 1))
    return cells


# ---- arrows -------------------------------------------------------------------------------------------------------
def opaque(a: Image.Image, x: float, y: float, r: int = 1) -> bool:
    xi, yi = round(x), round(y)
    return any(0 <= xi + dx < a.width and 0 <= yi + dy < a.height and a.getpixel((xi + dx, yi + dy)) > 0
               for dx in range(-r, r + 1) for dy in range(-r, r + 1))


def find_arrows(layout: Image.Image, cells: set[tuple[int, int]]) -> list[tuple[tuple, tuple]]:
    """Arrows (from cell, to cell; the head is at 'to') between skill cells in one of Median's layout pages."""
    a = layout.getchannel("A")
    found = []
    ordered = sorted(cells)
    for i, ca in enumerate(ordered):
        for cb in ordered[i + 1:]:
            ax, ay = MX[ca[1]], MY[ca[0]]
            bx, by = MX[cb[1]], MY[cb[0]]
            dx, dy = bx - ax, by - ay
            length = math.hypot(dx, dy)
            ux, uy = dx / length, dy / length
            if abs(cb[1] - ca[1]) > 2 or any(
                    c not in (ca, cb) and 0.05 < ((MX[c[1]] - ax) * ux + (MY[c[0]] - ay) * uy) / length < 0.95
                    and abs((MX[c[1]] - ax) * uy - (MY[c[0]] - ay) * ux) < 30 for c in cells):
                continue
            edge = 26 / max(abs(ux), abs(uy))
            ts = [edge + 4 + 2 * k for k in range(int((length - 2 * edge - 8) / 2) + 1)]
            if len(ts) < 6 or sum(opaque(a, ax + ux * t, ay + uy * t) for t in ts) < 0.92 * len(ts):
                continue
            ends = []
            for sign, (px, py) in ((1, (bx, by)), (-1, (ax, ay))):
                hx, hy = px - sign * ux * 38, py - sign * uy * 38  # a head is ~26 wide here, the shaft ~9
                ends.append(sum(opaque(a, hx - uy * k, hy + ux * k, 0) for k in range(-18, 19)) >= 16)
            tail, head = ends[1], ends[0]
            if head:
                found.append((ca, cb))
            if tail:
                found.append((cb, ca))
            if not head and not tail:
                found.append((ca, cb))
    return found


def draw_arrows(arrows: list[tuple[tuple, tuple]], centre, half, shaft: float, head_len: float, head_w: float,
                size: tuple[int, int], scale: int = 2) -> Image.Image:
    """Arrows between cell boxes (centre(cell) -> (x, y), half = box half sizes), anti-aliased by drawing at scale."""
    layer = Image.new("RGBA", (size[0] * scale, size[1] * scale))
    d = ImageDraw.Draw(layer)
    for ca, cb in arrows:
        (ax, ay), (bx, by) = centre(ca), centre(cb)
        dx, dy = bx - ax, by - ay
        length = math.hypot(dx, dy)
        ux, uy = dx / length, dy / length
        ta = min(half[0] / abs(ux) if ux else 1e9, half[1] / abs(uy) if uy else 1e9)
        sx, sy = ax + ux * ta, ay + uy * ta
        ex, ey = bx - ux * ta, by - uy * ta
        vx, vy = -uy, ux
        bx0, by0 = ex - ux * head_len, ey - uy * head_len
        for pts, col in (([(sx + vx * shaft / 2, sy + vy * shaft / 2), (bx0 + vx * shaft / 2, by0 + vy * shaft / 2),
                           (bx0 - vx * shaft / 2, by0 - vy * shaft / 2), (sx - vx * shaft / 2, sy - vy * shaft / 2)], None),
                         ([(ex, ey), (bx0 + vx * head_w / 2, by0 + vy * head_w / 2),
                           (bx0 - vx * head_w / 2, by0 - vy * head_w / 2)], None)):
            poly = [(x * scale, y * scale) for x, y in pts]
            d.polygon(poly, fill=ARROW_FILL + (255,), outline=ARROW_EDGE + (255,), width=max(1, scale))
    return layer.resize(size, Image.LANCZOS)


# ---- HD -----------------------------------------------------------------------------------------------------------
def text_fit(im: Image.Image, box: tuple[int, int, int, int], text: str, colour, max_size: int) -> None:
    d = ImageDraw.Draw(im)
    size = max_size
    while size > 8:
        f = font(size)
        l, t, r, b = d.textbbox((0, 0), text, font=f)
        if r - l <= box[2] - box[0]:
            break
        size -= 1
    x = box[0] + (box[2] - box[0] - (r - l)) // 2 - l
    y = box[1] + (box[3] - box[1] - (b - t)) // 2 - t
    d.text((x + 2, y + 2), text, font=f, fill=(0, 0, 0, 255))
    d.text((x, y), text, font=f, fill=colour + (255,))


def tab_strip(d2r_frame: Image.Image, tab: int, names: list[str]) -> Image.Image:
    """D2R's tab cell art (active and inactive) cut to Median's TABS cells across the frame."""
    cw = FW // TABS
    active, idle = d2r_frame.crop((0, 0, 297, STRIP_H)), d2r_frame.crop((296, 0, 596, STRIP_H))
    strip = Image.new("RGBA", (FW, STRIP_H))
    for n in range(TABS):
        x = n * cw
        w = cw if n < TABS - 1 else FW - x
        src, mid = (active, 107) if n == tab else (idle, 108)
        if n >= len(names):
            continue  # a class without this page: blank (the plugin ignores a click on it)
        cell = Image.new("RGBA", (w, STRIP_H))
        cell.paste(src.crop((0, 0, 12, STRIP_H)), (0, 0))
        cell.paste(src.crop((mid, 0, mid + w - 24, STRIP_H)), (12, 0))
        cell.paste(src.crop((src.width - 12, 0, src.width, STRIP_H)), (w - 12, 0))
        text_fit(cell, (6, 10, w - 6, 78), names[n], GOLD if n == tab else GOLD_DIM, 30)
        strip.paste(cell, (x, 0))
    return strip


def hd_stone(pal) -> tuple[Image.Image, Image.Image]:
    """D2R's PANEL_SkillTreeBG with Median's stone inside (full size, lowend)."""
    base = read_sprite(D2R_TREES / "panel_skilltreebg.sprite")[0]
    stone = fill_holes(page(GAME_ART / "panels" / "skill-tree.dc6", 0, pal).crop((28, 16, 376, 688)))
    left, top, right, bottom = STONE_BOX
    w, h = right - left, bottom - top
    scale = w / stone.width
    stone = stone.resize((w, round(stone.height * scale)), Image.LANCZOS)
    y0 = (stone.height - h) // 2
    base.paste(stone.crop((0, y0, w, y0 + h)), (left, top))
    return base, base.resize((base.width // 2, base.height // 2), Image.LANCZOS)


def hd_class(cls: str, prefix: str, names: list[str], cells_of, pal, preview: Path | None) -> str:
    d2r = read_sprite(D2R_TREES / f"{prefix}skilltree.sprite")
    sockets = d2r[1].crop(SOCKET_CROP)
    layouts = GAME_ART / "skills" / f"layout-{cls}.dc6"
    pages = len(read_dc6(layouts)) // 6

    def centre(c):
        return (SKILL_COL[c[1]] - ORIGIN[0] - 9 + 74, SKILL_ROW[c[0]] - ORIGIN[1] - 7 + 73)

    frames, arrows_total = [], 0
    for tab in range(TABS):
        cells = cells_of.get((cls, tab), set())
        im = Image.new("RGBA", (FW, FH))
        arrows = find_arrows(page(layouts, tab, pal), cells) if cells and tab < pages else []
        arrows_total += len(arrows)
        im.alpha_composite(draw_arrows(arrows, centre, SOCKET_HALF, 14, 26, 36, (FW, FH)))
        for r, c in cells:
            im.alpha_composite(sockets, (SKILL_COL[c] - ORIGIN[0] + SOCKET_AT[0], SKILL_ROW[r] - ORIGIN[1] + SOCKET_AT[1]))
        im.alpha_composite(tab_strip(d2r[0], tab, names), (0, 0))
        frames.append(im)
    sprite(HD_OUT / f"{prefix}skilltree.sprite", frames, STRIDE)
    sprite(HD_OUT / f"{prefix}skilltree.lowend.sprite",
           [f.resize((LOW_FW, LOW_FH), Image.LANCZOS) for f in frames], LOW_STRIDE)
    if preview:
        sheet = Image.new("RGBA", (FW * 4 // 2, FH * 2 // 2), (40, 24, 16, 255))
        for i, f in enumerate(frames):
            sheet.alpha_composite(f.resize((FW // 2, FH // 2), Image.LANCZOS), (i % 4 * FW // 2, i // 4 * FH // 2))
        sheet.save(preview / f"hd_{prefix}.png")
    return f"{prefix}skilltree: {TABS} frames, {arrows_total} arrows"


# ---- classic ------------------------------------------------------------------------------------------------------
def quantizer(pal) -> Image.Image:
    q = Image.new("P", (1, 1))
    q.putpalette([c for rgb in pal for c in rgb])
    return q


def encode(im: Image.Image, q: Image.Image, pal) -> bytes:
    """DC6 frame data from an RGBA image: palette indices, alpha < 128 transparent (index 0 is transparent in DC6)."""
    idx = im.convert("RGB").quantize(palette=q, dither=Image.Dither.NONE)
    dark = min(range(1, 256), key=lambda i: sum(v * v for v in pal[i]))
    data, alpha = idx.tobytes(), im.getchannel("A").tobytes()
    out = bytearray()
    w = im.width
    for y in range(im.height - 1, -1, -1):
        row = [(data[y * w + x] or dark) if alpha[y * w + x] >= 128 else None for x in range(w)]
        x = 0
        while x < w:
            if row[x] is None:
                n = 0
                while x < w and row[x] is None:
                    n, x = n + 1, x + 1
                if x < w:
                    while n > 127:
                        out += bytes((0xFF,))
                        n -= 127
                    out += bytes((0x80 | n,))
            else:
                run = []
                while x < w and row[x] is not None and len(run) < 127:
                    run.append(row[x])
                    x += 1
                out += bytes((len(run),)) + bytes(run)
        out += b"\x80"
    return bytes(out)


def classic_class(cls: str, letter: str, names: list[str], cells_of, stone: Image.Image, pal, preview: Path | None) -> str:
    q = quantizer(pal)
    layouts = GAME_ART / "skills" / f"layout-{cls}.dc6"
    pages = len(read_dc6(layouts)) // 6
    ch = CLASSIC_TABS["height"] * 3 / TABS  # D2R's 324-pixel tab column shared by TABS tabs
    cw = CLASSIC_TABS["width"]

    def centre(c):
        return (CL_COL[c[1]] + CL_ICON / 2, CL_ROW[c[0]] + CL_ICON / 2)

    def tabs(active: int | None) -> Image.Image:
        im = Image.new("RGBA", (CLASSIC_W, CLASSIC_H))
        d = ImageDraw.Draw(im)
        for n in range(TABS):
            if n >= len(names):
                continue  # a class without this page: blank
            y0 = round(CL_TAB_Y + n * ch)
            y1 = round(CL_TAB_Y + (n + 1) * ch) - 1
            on = n == active
            d.rectangle((CL_TAB_X, y0, CL_TAB_X + cw - 2, y1), fill=(58, 44, 28, 255) if on else (14, 8, 6, 255),
                        outline=(176, 142, 84, 255) if on else (84, 62, 40, 255))
            text_fit(im, (CL_TAB_X + 3, y0 + 2, CL_TAB_X + cw - 4, y1 - 1), names[n], GOLD if on else GOLD_DIM, 15)
        return im

    sets = []
    for tab in [None] + list(range(TABS)):
        im = stone.copy().convert("RGBA")
        if tab is not None:
            cells = cells_of.get((cls, tab), set())
            arrows = find_arrows(page(layouts, tab, pal), cells) if cells and tab < pages else []
            d = ImageDraw.Draw(im)
            for r, c in cells:
                d.rectangle((CL_COL[c] - 3, CL_ROW[r] - 3, CL_COL[c] + CL_ICON + 2, CL_ROW[r] + CL_ICON + 2),
                            fill=(8, 4, 2, 255), outline=(110, 90, 64, 255))
            im.alpha_composite(draw_arrows(arrows, centre, (CL_ICON / 2 + 3, CL_ICON / 2 + 3), 3, 9, 12,
                                           (CLASSIC_W, CLASSIC_H), 4))
        im.alpha_composite(tabs(tab))
        sets.append(im)
        if preview and tab in (None, 0, 3):
            sets[-1].save(preview / f"classic_{letter}_{'idle' if tab is None else tab}.png")
    frames = []
    for im in sets:
        for box in ((0, 0, 256, 256), (256, 0, 320, 256), (0, 256, 256, 432), (256, 256, 320, 432)):
            tile = im.crop(box)
            frames.append((tile.width, tile.height, 0, 0, encode(tile, q, pal)))
    write_dc6(CLASSIC_OUT / f"skltree_{letter}_back.dc6", frames)
    return f"skltree_{letter}_back.dc6: {len(frames)} frames"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", type=Path, help="also write PNGs of the HD frames and classic sets here")
    args = ap.parse_args()
    if args.preview:
        args.preview.mkdir(parents=True, exist_ok=True)
    pal = palette()
    names, cells_of = titles(), skill_cells()
    full, low = hd_stone(pal)
    sprite(HD_OUT / "panel_skilltreebg.sprite", [full], full.width, full.width)
    sprite(HD_OUT / "panel_skilltreebg.lowend.sprite", [low], low.width, low.width)
    stone = fill_holes(page(GAME_ART / "panels" / "skill-tree.dc6", 0, pal)).resize((CLASSIC_W, CLASSIC_H), Image.LANCZOS)
    report = []
    for cls, prefix, letter in CLASSES:
        report.append(hd_class(cls, prefix, names[cls], cells_of, pal, args.preview))
        report.append(classic_class(cls, letter, names[cls], cells_of, stone, pal, args.preview))
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())

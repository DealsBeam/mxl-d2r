"""Skill icons: Median's icon sheets as D2R skill icon files, and skilldesc IconCel to match.

Median leaves skilldesc IconCel at 0 and D2Sigma draws icons itself: the frame is the u16 at offset 0x16 of the
skilldesc.bin record (D2SkillIcons.cpp), in themes/classic_sigma/game/skills/icons-<class>.dc6 for class skills
and icons-shared.dc6 for the rest, two frames per icon (normal, pressed). D2R reads IconCel as a frame of
Spells/<Class>/<Xx>Skillicon (class skills) or Spells/SubMenu/Skillicon (others), also normal + pressed.

D2R's HD sprites are one horizontal strip under the 16384-pixel texture limit and are drawn at their own pixel
size (no scaling to the slot), so icons stay at D2R's 130 pixels (132 stride) and a sheet holds 62 icons.
Median's sheets hold up to 158 icons, so each sheet keeps the icons its skills use: skill tree skills first,
then skills items or the character start grant, then monster-only skills, which share icon 0 once the sheet is
full. IconCel is renumbered to that order.

One skilldesc row has one IconCel, but Median's skills of different classes (and the class-less item skills) share
skilldesc rows (the seven Mastery pages of every class use the same few rows), each reading its own class's sheet. So a
row used from several sheets is split: the sheet of the skill-tree skills keeps the row, the others get a copy named
"<row>~<class>" that their skills point to (skills.txt), each with its own IconCel. The copy of the class-less users
("<row>~shared": item skills, monster skills such as the Fortress effects that Median gave the Fortress row too) loses
the tree place (SkillPage / SkillRow / SkillColumn), so only class skills can sit on a tree slot: Barbarian
Warmonger 6,1 is Median's own skill 336 (named RogueMissile in 1.13c, a self buff, string SkillnameFortress), alone.

Writes into work/mod/data (after txt_migrate.py):
  global/excel/{base,}/skilldesc.txt          IconCel (+ the split rows), skills.txt skilldesc of the split users
  global/ui/spells/<xx>skillicon.dc6          classic graphics (Median's frames, repacked)
  hd/global/ui/spells/<class>/<xx>skillicon.sprite (+ .lowend.sprite)   HD graphics (scaled up)
Usage: python tools/skill_icons.py [--preview DIR]   (--preview also writes PNG strips to look at)
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from txt_migrate import read_tsv, write_tsv, is_record  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "work" / "mod" / "data"
MXL = ROOT / "assets" / "median" / "data" / "global"
SHEETS = MXL / "themes" / "classic_sigma" / "game" / "skills"
PALETTE = ROOT / "extract" / "d2r" / "93847" / "data" / "global" / "palette" / "units" / "pal.dat"
GAME = r"C:\Program Files (x86)\Diablo II Resurrected"
# charclass -> (Median sheet, D2R classic file, D2R HD folder/file)
TARGETS = {
    "ama": ("icons-ama", "amskillicon", "amazon/amskillicon"),
    "sor": ("icons-sor", "soskillicon", "sorceress/soskillicon"),
    "nec": ("icons-nec", "neskillicon", "necromancer/neskillicon"),
    "pal": ("icons-pal", "paskillicon", "paladin/paskillicon"),
    "bar": ("icons-bar", "baskillicon", "barbarian/baskillicon"),
    "dru": ("icons-dru", "drskillicon", "druid/drskillicon"),
    "ass": ("icons-ass", "asskillicon", "assassin/asskillicon"),
    "": ("icons-shared", "skillicon", "submenu/skillicon"),
}
MAX_STRIP, D2R_STRIDE = 16384, 132
MAX_ICONS = MAX_STRIP // D2R_STRIDE // 2  # 62 icons (normal + pressed) per full-size sheet


# ---- DC6 ----------------------------------------------------------------------------------------------
def read_dc6(path: Path) -> list[tuple[int, int, int, int, bytes]]:
    """Frames as (width, height, offx, offy, encoded block) for a 1-direction DC6."""
    d = path.read_bytes()
    _ver, _flags, _enc, _term, dirs, fpd = struct.unpack_from("<6I", d, 0)
    ptrs = struct.unpack_from(f"<{dirs * fpd}I", d, 24)
    frames = []
    for p in ptrs:
        _flip, w, h, ox, oy, _unk, _next, length = struct.unpack_from("<8i", d, p)
        frames.append((w, h, ox, oy, d[p + 32:p + 32 + length]))
    return frames


def write_dc6(path: Path, frames: list[tuple[int, int, int, int, bytes]]) -> None:
    head = struct.pack("<6I", 6, 1, 0, 0xEEEEEEEE, 1, len(frames))
    offset = 24 + 4 * len(frames)
    ptrs, blobs = [], []
    for w, h, ox, oy, block in frames:
        ptrs.append(offset)
        blob = struct.pack("<8i", 0, w, h, ox, oy, 0, 0, len(block)) + block + b"\xee\xee\xee"
        blobs.append(blob)
        offset += len(blob)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(head + struct.pack(f"<{len(ptrs)}I", *ptrs) + b"".join(blobs))


def decode_frame(w: int, h: int, block: bytes, pal: list[tuple[int, int, int]]) -> list[list[tuple]]:
    """RGBA rows top to bottom. DC6 frames are stored bottom-up: 0x80 ends a line, high bit skips pixels."""
    rows = [[(0, 0, 0, 0)] * w for _ in range(h)]
    x, y, i = 0, h - 1, 0
    while i < len(block) and y >= 0:
        b = block[i]
        i += 1
        if b == 0x80:
            x, y = 0, y - 1
        elif b & 0x80:
            x += b & 0x7F
        else:
            for c in block[i:i + b]:
                if 0 <= x < w:
                    r, g, bl = pal[c]
                    rows[y][x] = (r, g, bl, 255)
                x += 1
            i += b
    return rows


def resize(rows: list[list[tuple]], size: int) -> list[list[tuple]]:
    """Bilinear resize of a square-ish RGBA image to size x size (premultiplied so edges do not go dark)."""
    h, w = len(rows), len(rows[0])
    out = []
    for y in range(size):
        sy = min(max((y + 0.5) * h / size - 0.5, 0), h - 1)
        y0, fy = int(sy), sy - int(sy)
        y1 = min(y0 + 1, h - 1)
        line = []
        for x in range(size):
            sx = min(max((x + 0.5) * w / size - 0.5, 0), w - 1)
            x0, fx = int(sx), sx - int(sx)
            x1 = min(x0 + 1, w - 1)
            acc = [0.0, 0.0, 0.0, 0.0]
            for px, wt in ((rows[y0][x0], (1 - fx) * (1 - fy)), (rows[y0][x1], fx * (1 - fy)),
                           (rows[y1][x0], (1 - fx) * fy), (rows[y1][x1], fx * fy)):
                a = px[3] / 255 * wt
                acc[0] += px[0] * a
                acc[1] += px[1] * a
                acc[2] += px[2] * a
                acc[3] += a
            if acc[3] > 0:
                line.append((round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]), round(acc[3] * 255)))
            else:
                line.append((0, 0, 0, 0))
        out.append(line)
    return out


# ---- D2R sprite / PNG ---------------------------------------------------------------------------------
def strip(images: list[list[list[tuple]]], stride: int, height: int) -> bytes:
    """Frames side by side, each in a stride-wide cell, RGBA bytes row by row."""
    out = bytearray()
    for y in range(height):
        for img in images:
            line = img[y] if y < len(img) else []
            for x in range(stride):
                out += bytes(line[x]) if x < len(line) else b"\0\0\0\0"
    return bytes(out)


def write_sprite(path: Path, images: list, stride: int, height: int) -> None:
    """D2R .sprite, version 31 (as Spells/*Skillicon): 'SpA1', u16 31, u16 frame width (stride - 2, like D2R's
    130/132), u32 strip width, u32 height, u32 0, u32 frames, u32 0, u32 0, u32 data size, u32 4, RGBA."""
    data = strip(images, stride, height)
    head = b"SpA1" + struct.pack("<HHIIIIIIII", 31, stride - 2, stride * len(images), height, 0, len(images), 0, 0,
                                 len(data), 4)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(head + data)


def write_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    raw = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))

    def chunk(t: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + t + body + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def palette() -> list[tuple[int, int, int]]:
    if not PALETTE.exists():
        import native
        PALETTE.parent.mkdir(parents=True, exist_ok=True)
        PALETTE.write_bytes(native.casc_read(native.open_casc(GAME), "data:data/global/palette/units/pal.dat"))
    d = PALETTE.read_bytes()
    return [(d[i * 3 + 2], d[i * 3 + 1], d[i * 3]) for i in range(256)]  # stored BGR


# ---- main ---------------------------------------------------------------------------------------------
def median_frames() -> dict[str, int]:
    """skilldesc key -> Median icon frame (u16 at 0x16 of each 288-byte skilldesc.bin record)."""
    d = (MXL / "excel" / "skilldesc.bin").read_bytes()
    n = struct.unpack_from("<I", d)[0]
    size = (len(d) - 4) // n
    _h, named = read_tsv(ROOT / "work" / "named" / "skilldesc.txt")
    return {named[i][0]: struct.unpack_from("<H", d, 4 + i * size + 0x16)[0] for i in range(n)}


SPLIT = "~"


def base_desc(name: str) -> str:
    return name.split(SPLIT)[0]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", type=Path, help="also write PNG strips of the HD sheets here")
    args = ap.parse_args()
    frames_of = median_frames()
    pal = palette()
    # Which icon (Median frame) each sheet needs, from the skills that use each skilldesc, in first-use order
    excel = OUT / "global" / "excel" / "base"
    sh, srows = read_tsv(excel / "skills.txt")
    cc, sd = sh.index("charclass"), sh.index("skilldesc")
    # A full-size HD sheet holds MAX_ICONS icons. Players see tree skills first, then skills items or the
    # character start grant (and the basic rows Attack..Left Hand Swing); monster-only skills come last and
    # share icon 0 when a sheet is full.
    dh, drows = read_tsv(excel / "skilldesc.txt")
    on_tree = {r[0] for r in drows if r[dh.index("SkillPage")] not in ("", "0")}
    granted = set()
    for t in ("uniqueitems", "setitems", "sets", "runes", "magicprefix", "magicsuffix", "automagic", "cubemain",
              "gems", "weapons", "armor", "misc", "charstats", "qualityitems"):
        if (excel / f"{t}.txt").exists():
            granted |= {c for r in read_tsv(excel / f"{t}.txt")[1] for c in r}
    def sheet(r: list[str]) -> str:
        return r[cc] if r[cc] in TARGETS else ""

    # Each skilldesc row is read from one sheet: that of its skill-tree skills, else of its first user. The other
    # sheets' users get a copy of the row ("row~class", skills.txt skilldesc repointed).
    primary: dict[str, str] = {}
    for r in srows:
        b = base_desc(r[sd])
        if is_record(r) and b in frames_of:
            primary.setdefault(b, sheet(r))
    tree_sheet: dict[str, str] = {}
    for r in srows:
        b = base_desc(r[sd])
        if is_record(r) and b in frames_of and b in on_tree and sheet(r):
            tree_sheet.setdefault(b, sheet(r))
    primary = {b: tree_sheet.get(b, c) for b, c in primary.items()}

    def desc_key(name: str, cls: str) -> str:
        b = base_desc(name)
        return b if b not in primary or primary[b] == cls else f"{b}{SPLIT}{cls or 'shared'}"

    sheet_of: dict[str, str] = {}
    rank: dict[str, int] = {}
    for i, r in enumerate(srows):
        if is_record(r) and base_desc(r[sd]) in frames_of:
            key = desc_key(r[sd], sheet(r))
            sheet_of.setdefault(key, sheet(r))
            prio = 0 if base_desc(r[sd]) in on_tree and sheet(r) else 1 if (r[0] in granted or i < 7) else 2
            rank[key] = min(rank.get(key, 2), prio)
    order: dict[str, list[int]] = {k: [] for k in TARGETS}
    seen: dict[str, list[int]] = {k: [] for k in TARGETS}  # icons players can see (tree, items, start)
    for key in sorted(sheet_of, key=lambda k: rank[k]):  # stable: first use within each priority
        f = frames_of[base_desc(key)] // 2 * 2
        if f not in order[sheet_of[key]]:
            order[sheet_of[key]].append(f)
            if rank[key] < 2:
                seen[sheet_of[key]].append(f)
    # Monster-only icons go once a sheet is full; icons players see are all kept, and a sheet with more of
    # those than fit at full size (the Amazon tree has 72) gets smaller frames instead.
    keep = {cls: max(MAX_ICONS, len(seen[cls])) for cls in TARGETS}
    dropped = {cls: max(len(o) - keep[cls], 0) for cls, o in order.items()}
    order = {cls: o[:keep[cls]] for cls, o in order.items()}
    icon_cel = {}
    for key, cls in sheet_of.items():
        f = frames_of[base_desc(key)] // 2 * 2
        icon_cel[key] = order[cls].index(f) * 2 if f in order[cls] else 0
    for bank in ("base", ""):
        p = OUT / "global" / "excel" / bank / "skilldesc.txt"
        q = OUT / "global" / "excel" / bank / "skills.txt"
        if p.exists():
            h, rows = read_tsv(p)
            ic = h.index("IconCel")
            used = set()
            if q.exists():  # the users of a split row point at its copy
                qh, qrows = read_tsv(q)
                qc, qd = qh.index("charclass"), qh.index("skilldesc")
                for r in qrows:
                    if is_record(r) and base_desc(r[qd]) in frames_of:
                        r[qd] = desc_key(r[qd], r[qc] if r[qc] in TARGETS else "")
                        used.add(r[qd])
                write_tsv(q, qh, qrows)
            have = {r[0]: r for r in rows}
            for key in sorted(used | set(icon_cel)):  # copies of the rows read from more than one sheet
                if key not in have and base_desc(key) in have:
                    rows.append([key] + have[base_desc(key)][1:])
                    have[key] = rows[-1]
            place = [h.index(c) for c in ("SkillPage", "SkillRow", "SkillColumn")]
            for r in rows:
                if r[0] in icon_cel:
                    r[ic] = str(icon_cel[r[0]])
                elif SPLIT in r[0]:
                    r[ic] = "0"  # a copy for skills outside the base bank's sheets: icon 0
                if r[0].endswith(SPLIT + "shared"):
                    for c in place:  # class-less skills have no slot in a tree
                        r[c] = ""
            write_tsv(p, h, rows)
    report = []
    for cls, (sheet, classic, hd) in TARGETS.items():
        src = read_dc6(SHEETS / f"{sheet}.dc6")
        picked = [src[i] for f in order[cls] for i in (f, f + 1) if i < len(src)]
        write_dc6(OUT / "global" / "ui" / "spells" / f"{classic}.dc6", picked)
        stride = min(D2R_STRIDE, MAX_STRIP // max(len(picked), 1))
        size = stride - 2
        images = [resize(decode_frame(w, h, blk, pal), size) for w, h, _ox, _oy, blk in picked]
        for suffix in (".sprite", ".lowend.sprite"):
            write_sprite(OUT / "hd" / "global" / "ui" / "spells" / f"{hd}{suffix}", images, stride, size)
        if args.preview:
            write_png(args.preview / f"{classic}.png", stride * len(images), size, strip(images, stride, size))
        report.append(f"{sheet}: {len(order[cls])} icons ({len(picked)} frames), HD {size}px"
                      + (f", {dropped[cls]} monster-only icons share icon 0" if dropped[cls] else ""))
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Compare every skill's icon in the build with the icon Median XL draws for it (bug #52).

Median (D2Sigma D2SkillIcons): the frame is the u16 at +0x16 of the skill's skilldesc.bin record, in
themes/classic_sigma/game/skills/icons-<class>.dc6 for class skills and icons-shared.dc6 for the rest. The frame is
drawn as is: the class sheets hold pairs (normal, pressed), icons-shared holds one frame per icon.

The build (D2R): IconCel of the skill's skilldesc row in work/mod, a frame of global/ui/spells/<xx>skillicon.dc6 (classic)
and hd/global/ui/spells/<class>/<xx>skillicon.sprite (HD), the sheet picked by the skill's charclass.

For each skill with a Median icon: classic must be the same pixels; HD (scaled up)
must match Median's frame scaled up the same way (mean RGB difference under HD_LIMIT). Mismatches are grouped by cause and written as an HTML page
with the images side by side.

Usage: python tools/skill_icon_check.py [--out DIR]   (default work/skill_icon_check; exit 1 on a mismatch)
"""
import argparse
import base64
import html
import io
import struct
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from skill_icons import OUT, ROOT, SHEETS, TARGETS, decode_frame, median_frames, palette, priorities, BAR, ITEM, MONSTER, read_dc6, resize  # noqa: E402
from txt_migrate import is_record, read_tsv  # noqa: E402

HD_LIMIT = 2.0
HD_LIMIT_ITEMS = ("HD sheet limit: item skill (charges / oskill) past the 62 full-size icons of icons-shared, "
                  "shows Median's \"?\" icon")
DELIBERATE = ("deliberate: monster-only skill or item proc on a full sheet, shares the sheet's icon 0 "
              "(never shown on a skill bar or tree)")


def median_icons() -> dict[int, tuple[str, int, str]]:
    """Median skill id -> (sheet key, frame, skill name) for every skill with a skilldesc (work/named: Median's
    tables with names, rows in Median's order; skilldesc.bin frame as skill_icons.median_frames)."""
    frame = median_frames()
    h, rows = read_tsv(ROOT / "work" / "named" / "skills.txt")
    ident, cc, sd = h.index("Id"), h.index("charclass"), h.index("skilldesc")
    return {int(r[ident]): (r[cc] if r[cc] in TARGETS else "", frame[r[sd]], r[0])
            for r in rows if is_record(r) and r[sd] in frame and r[ident].isdigit()}


def read_sprite(path: Path) -> list[Image.Image]:
    """Frames of a version-31 RGBA .sprite as written by skill_icons.write_sprite."""
    d = path.read_bytes()
    _v, fw, sw, h, _z, frames = struct.unpack_from("<HHIIII", d, 4)
    img = Image.frombytes("RGBA", (sw, h), d[40:40 + sw * h * 4])
    stride = sw // frames
    return [img.crop((i * stride, 0, i * stride + fw, h)) for i in range(frames)]


def to_image(rows: list[list[tuple]]) -> Image.Image:
    img = Image.new("RGBA", (len(rows[0]), len(rows)))
    img.putdata([p for line in rows for p in line])
    return img


def diff(a: Image.Image, b: Image.Image) -> float:
    """Mean RGB difference of two images (b scaled to a's size), transparent pixels as black."""
    if b.size != a.size:
        b = b.resize(a.size, Image.BILINEAR)
    pa, pb = a.convert("RGBA").get_flattened_data(), b.convert("RGBA").get_flattened_data()
    tot = 0
    for x, y in zip(pa, pb):
        tot += sum(abs(x[k] * x[3] - y[k] * y[3]) // 255 for k in range(3))
    return tot / (3 * len(pa))


def png64(img: Image.Image, scale: int = 1) -> str:
    if scale != 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return base64.b64encode(buf.getvalue()).decode()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=ROOT / "work" / "skill_icon_check")
    args = ap.parse_args()
    pal = palette()
    median = median_icons()
    msheet = {cls: read_dc6(SHEETS / f"{sheet}.dc6") for cls, (sheet, _c, _h) in TARGETS.items()}
    mimg_cache: dict[tuple[str, int], Image.Image] = {}

    def mimg(cls: str, f: int) -> Image.Image:
        if (cls, f) not in mimg_cache:
            w, h, _x, _y, blk = msheet[cls][f]
            mimg_cache[cls, f] = to_image(decode_frame(w, h, blk, pal))
        return mimg_cache[cls, f]

    def hd_want(cls: str, f: int, size: int) -> Image.Image:
        """Median's frame scaled up as skill_icons does it for an HD sheet of that frame size."""
        w, h, _x, _y, blk = msheet[cls][f]
        return to_image(resize(decode_frame(w, h, blk, pal), size))

    ours_dc6 = {cls: read_dc6(OUT / "global" / "ui" / "spells" / f"{c}.dc6") for cls, (_s, c, _h) in TARGETS.items()}
    ours_hd = {cls: read_sprite(OUT / "hd" / "global" / "ui" / "spells" / f"{h}.sprite") for cls, (_s, _c, h) in TARGETS.items()}
    excel = OUT / "global" / "excel" / "base"
    sh, srows = read_tsv(excel / "skills.txt")
    dh, drows = read_tsv(excel / "skilldesc.txt")
    prio = priorities(excel)
    cel = {r[0]: int(r[dh.index("IconCel")] or 0) for r in drows}
    ident = sh.index("*Id") if "*Id" in sh else None
    cc, sd = sh.index("charclass"), sh.index("skilldesc")
    results, causes = [], {}
    checked = 0
    for i, r in enumerate(srows):
        if not is_record(r):
            continue
        sid = int(r[ident]) if ident is not None and r[ident].isdigit() else i
        if sid not in median:
            continue
        mcls, mf, name = median[sid]
        cls = r[cc] if r[cc] in TARGETS else ""
        want = mimg(mcls, mf)
        if r[sd] not in cel:
            causes.setdefault("no skilldesc row in the build", []).append(sid)
            results.append((sid, f"{name} ({r[0]}, {r[sd]!r})", "no skilldesc row in the build", want, None, None))
            continue
        checked += 1
        c = cel[r[sd]]
        problems = []
        got_c = to_image(decode_frame(*ours_dc6[cls][c][:2], ours_dc6[cls][c][4], pal)) if c < len(ours_dc6[cls]) else None
        got_h = ours_hd[cls][c] if c < len(ours_hd[cls]) else None
        if cls != mcls:
            problems.append(f"sheet: build reads {TARGETS[cls][1]}, Median {TARGETS[mcls][0]}")
        if got_c is None or list(got_c.get_flattened_data()) != list(want.get_flattened_data()):
            problems.append("classic")
        if got_h is None or diff(hd_want(mcls, mf, got_h.width), got_h) > HD_LIMIT:
            problems.append("HD")
        if problems:
            # Cause: find which Median frame the build shows instead
            shown = None
            if got_c is not None:
                for f in range(len(msheet[cls])):
                    if list(mimg(cls, f).get_flattened_data()) == list(got_c.get_flattened_data()):
                        shown = f
                        break
            if cls != mcls:
                cause = "sheet differs (charclass)"
            elif c == 0 and prio[r[0]][0] in (ITEM, MONSTER):
                cause = DELIBERATE
            elif c == 0 and prio[r[0]][0] == BAR:
                cause = HD_LIMIT_ITEMS
            elif shown is not None and shown == mf - 1 and (mcls == "" or mf % 2):
                cause = ("shared sheet: one frame per icon, the build took the frame before (pairs assumed)" if mcls == ""
                         else "class sheet: odd frame, the build took the pair's first frame")
            elif c == 0:
                cause = "icon dropped (sheet full), shows the sheet's icon 0"
            elif "classic" not in problems:
                cause = "HD only"
            else:
                cause = f"other (build shows Median frame {shown})"
            causes.setdefault(cause, []).append(sid)
            results.append((sid, f"{name} ({r[0]}, {r[sd]}, Median {TARGETS[mcls][0]} frame {mf}, build cel {c}"
                            f"{', = Median ' + str(shown) if shown is not None else ''})", cause, want, got_c, got_h))
    args.out.mkdir(parents=True, exist_ok=True)
    page = ["<html><head><meta charset='utf-8'><title>Skill icons vs Median</title><style>body{font:13px sans-serif;"
            "background:#222;color:#ddd} td{padding:2px 8px;vertical-align:middle} img{image-rendering:pixelated}"
            "</style></head><body>",
            f"<h1>Skill icons: build vs Median 2.14.4</h1><p>{checked} skills with a Median icon checked, "
            f"{sum(len(v) for v in causes.values())} mismatches. Columns: Median (classic sheet), build classic DC6, "
            f"build HD sprite.</p>"]
    for cause, ids in sorted(causes.items(), key=lambda kv: -len(kv[1])):
        page.append(f"<h2>{html.escape(cause)}: {len(ids)}</h2><table>")
        for sid, label, c_, want, got_c, got_h in results:
            if sid in ids and c_ == cause:
                cells = [f"<td><img src='data:image/png;base64,{png64(want, 2)}'></td>"]
                for g, s in ((got_c, 2), (got_h, 1)):
                    cells.append(f"<td><img src='data:image/png;base64,{png64(g.resize((96, 96)) if s == 1 else g, s)}'></td>"
                                 if g is not None else "<td>-</td>")
                page.append(f"<tr>{''.join(cells)}<td>{sid} {html.escape(label)}</td></tr>")
        page.append("</table>")
    page.append("</body></html>")
    (args.out / "skill_icons.html").write_text("\n".join(page), "utf-8")
    print(f"skill icons: {checked} skills checked, {sum(len(v) for v in causes.values())} mismatches -> "
          f"{args.out / 'skill_icons.html'}")
    for cause, ids in sorted(causes.items(), key=lambda kv: -len(kv[1])):
        print(f"  {len(ids):4}  {cause}: {', '.join(map(str, ids[:12]))}{' ...' if len(ids) > 12 else ''}")
    return 1 if set(causes) - {DELIBERATE, HD_LIMIT_ITEMS} else 0


if __name__ == "__main__":
    sys.exit(main())

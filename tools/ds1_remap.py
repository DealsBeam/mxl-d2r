"""Renumber the monster and object ids inside Median's .ds1 map files for D2R's tables.

A .ds1 preset map lists "objects": (type, id, x, y, flags) and D2R resolves the id per act (the act is the .ds1
header's, verified in D2R at 0x3dfae0):
  type 1 (monster/NPC): id below the act's MonPreset row count = that row; at or above it = the MonStats row itself
    (the engine keeps the raw id, 0x3e0367). Median's rows from the 1.13c count upward moved when D2R's own added rows
    were kept (monstats 734 -> 751, docs/id-mapping.md), so every Median map that places a Median-only monster (bosses,
    trap monsters, the Duncraig and Void areas) named the wrong class. A negative id reads outside the table in D2R;
    Median's flat table (row = 70 * act + id) lets maps reach another act's rows, and every such row is a plain monster.
  type 2 (object): (act, id) -> the act's ObjPreset table -> Objects row. D2R's table is per act with its own indexes;
    Median's is one flat table (150 slots per act). tools/objpreset.py adds Median's table to ObjPreset.txt as Index
    1000 + slot in every act; this rewrites each id to that index (empty or out-of-range slots: dropped).

  py -3 tools/ds1_remap.py --check [TILES_DIR]         count what would change, write nothing
  py -3 tools/ds1_remap.py TILES_DIR                   rewrite the .ds1 files under TILES_DIR in place
tools/install_mod.py calls remap_tree() on the copy it makes under the mod folder (the extracted originals are never
modified). Run once per fresh copy. Needs work/named/idmap.json and the built monpreset.txt / monstats.txt.
"""
import argparse
import csv
import json
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import objpreset  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
IDMAP = ROOT / "work" / "named" / "idmap.json"
EXCEL = ROOT / "work" / "mod" / "data" / "global" / "excel"
MXL_TILES = ROOT / "extract" / "mxl" / "data" / "global" / "tiles"
MXL_OBJPRESET = ROOT / "extract" / "mxl" / "data" / "global" / "excel" / "objpreset.bin"


class Ds1:
    """Header + object list of a .ds1 (version 2..18); offsets so the object block can be rewritten."""

    def __init__(self, data: bytes):
        self.data = data
        o = 0

        def i32():
            nonlocal o
            v = struct.unpack_from("<i", data, o)[0]
            o += 4
            return v

        self.version = i32()
        self.width = i32() + 1
        self.height = i32() + 1
        self.act = i32() if self.version >= 8 else 0
        self.tag_type = i32() if self.version >= 10 else 0
        self.files = []
        if self.version >= 3:
            for _ in range(i32()):
                e = data.index(b"\0", o)
                self.files.append(data[o:e].decode("latin1"))
                o = e + 1
        if 9 <= self.version <= 13:
            o += 8
        walls = i32() if self.version >= 4 else 1
        floors = i32() if self.version >= 16 else 1
        self.walls, self.floors = walls, floors
        self.layers_at = o  # wall1, orient1, wall2, orient2, ..., floor1.., shadow, [tag]: width*height dwords each
        o += (walls * 2 + floors + 1 + (1 if self.tag_type in (1, 2) else 0)) * self.width * self.height * 4
        self.objects = []
        self.count_at = o
        if self.version >= 2:
            n = i32()
            for _ in range(n):
                t, i, x, y = struct.unpack_from("<4i", data, o)
                o += 16
                f = 0
                if self.version >= 5:
                    f = i32()
                self.objects.append([t, i, x, y, f])
        self.end_at = o
        if o > len(data):
            raise ValueError("object block runs past the end of the file")

    def cells(self, index: int):
        """Layer `index` of the layer stream as a tuple of width*height dwords."""
        n = self.width * self.height
        return struct.unpack_from(f"<{n}I", self.data, self.layers_at + index * n * 4)

    def pack(self) -> bytes:
        out = bytearray(self.data[: self.count_at])
        out += struct.pack("<i", len(self.objects))
        for t, i, x, y, f in self.objects:
            out += struct.pack("<4i", t, i, x, y)
            if self.version >= 5:
                out += struct.pack("<i", f)
        return bytes(out) + self.data[self.end_at:]


class Maps:
    def __init__(self, idmap_path: Path = IDMAP, excel: Path = EXCEL, objpreset_bin: Path = MXL_OBJPRESET):
        idmap = json.loads(Path(idmap_path).read_text("utf-8"))
        self.monster = {e["row"]: e["id"] for e in idmap["monstats"]}
        self.monster_rows = len(self.monster)
        self.monster_by_name = {e["name"].lower(): e["id"] for e in idmap["monstats"]}
        rows = list(csv.reader(open(Path(excel) / "monpreset.txt", encoding="utf-8"), delimiter="	"))[1:]
        rows = [r for r in rows if r and r[0].isdigit()]
        self.preset_rows = rows                                     # Median's flat MonPreset (70 per act)
        self.presets = Counter(int(r[0]) for r in rows)             # act (1-based) -> rows
        self.flat = objpreset.load_flat(objpreset_bin)

    def preset_base(self, act0: int) -> int:
        return sum(self.presets.get(a, 0) for a in range(1, act0 + 1))

    def monster_new(self, act0: int, i: int):
        """New id for a type-1 id, or None to drop."""
        count = self.presets.get(act0 + 1, 0)
        if 0 <= i < count:
            return i                                                # a MonPreset row of the act: same table in the mod
        if i < 0:                                                   # another act's row in Median's flat table
            row = self.preset_base(act0) + i
            if 0 <= row < len(self.preset_rows):
                new = self.monster_by_name.get(self.preset_rows[row][1].strip().lower())
                if new is not None and new >= self.presets.get(act0 + 1, 0):
                    return new                                      # a plain monster: place it by class
            return None
        if i >= self.monster_rows:
            return None
        return self.monster[i]

    def object_new(self, act0: int, i: int):
        g = objpreset.flat_slot(act0, i, self.flat)
        return None if g is None else objpreset.INDEX_BASE + g


def remap_bytes(data: bytes, maps: Maps, stats: Counter):
    """-> new bytes (same object if nothing changed)."""
    ds = Ds1(data)
    changed = False
    kept = []
    for obj in ds.objects:
        t, i = obj[0], obj[1]
        new = i
        if t == 1:
            new = maps.monster_new(ds.act, i)
        elif t == 2:
            new = maps.object_new(ds.act, i)
        if new is None:
            stats[f"type{t}_dropped"] += 1
            changed = True
            continue
        if new != i:
            stats[f"type{t}_renumbered"] += 1
            obj[1] = new
            changed = True
        kept.append(obj)
    if not changed:
        return data
    ds.objects = kept
    return ds.pack()


def remap_tree(tiles: Path, maps: Maps | None = None, write: bool = True):
    maps = maps or Maps()
    stats = Counter()
    files = changed = 0
    for p in sorted(Path(tiles).rglob("*.ds1")):
        files += 1
        data = p.read_bytes()
        new = remap_bytes(data, maps, stats)
        if new is not data and new != data:
            changed += 1
            if write:
                p.write_bytes(new)
    stats["files"] = files
    stats["files_changed"] = changed
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tiles", nargs="?", default=str(MXL_TILES))
    ap.add_argument("--check", action="store_true", help="count only, write nothing")
    a = ap.parse_args()
    if not IDMAP.exists() or not (EXCEL / "monpreset.txt").exists():
        sys.exit(f"need {IDMAP} and {EXCEL / 'monpreset.txt'} (run tools/build.py first)")
    s = remap_tree(Path(a.tiles), write=not a.check)
    print(("would change " if a.check else "changed ") + ", ".join(f"{k}={v}" for k, v in sorted(s.items())))


if __name__ == "__main__":
    main()

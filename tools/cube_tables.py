"""Cube recipe conditions beyond D2R's: plugin/src/mxl_cube.h (+ ext item conditions folded into cubemain.txt).

Median's recipes carry up to two conditions and a gold cost (docs/plugin-spec.md, "Cube conditions"):
  - cubemain.txt's op/param/value (D2R knows ops 1-28; Median adds 29-42), and
  - cubemainext (work/sigma/cubemainext.txt, one row per recipe, 24 bytes): +0x00 gold cost / 100, +0x04 op, +0x08 param,
    +0x0c value, +0x10 layer of the second condition's stat, +0x14 layer of the first condition's stat.
Item conditions (ops 15-27: a stat of each input item) are checked by D2R's input matcher from the record's own
op/param/value, so a second (ext) item condition is moved there when the record has none. Everything else (player
conditions 3-14 and 29-42, the cost) is evaluated by the plugin (cube_conditions.cpp) from the table written here.

Ids are Median row numbers in the sources and migrated ids (idmap.json) in the output: skills (op 30/31), states
(32/33), areas (34/35) and stats (3-14, stat compares use the stat's ValShift: Median compares value >> shift).

Usage: python tools/cube_tables.py   (after txt_migrate.py; build.py runs it)
"""
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from txt_migrate import NAMED, OUT, ROOT, read_tsv, write_tsv  # noqa: E402

ITEM_OPS = range(15, 28)       # D2R's matcher checks these on each input item
PLUGIN_OPS = set(range(3, 15)) | set(range(29, 43))  # player conditions the plugin evaluates (1/2: D2R's dates)
STAT_OPS = range(3, 15)
LEVEL_ACT_COLUMN = "Act"


def ids(idmap: dict, entity: str) -> dict[int, int]:
    return {e["row"]: e["id"] for e in idmap[entity]}


def main() -> int:
    done = OUT / "cube_tables.done"
    if done.exists():
        print("cubemain.txt was already converted (its stat ids would be mapped twice); run txt_migrate.py first")
        return 1
    idmap = json.loads((NAMED / "idmap.json").read_text("utf-8"))
    skills, states, levels, stats = (ids(idmap, e) for e in ("skills", "states", "levels", "itemstatcost"))
    # ValShift per Median stat row (named itemstatcost is in Median row order)
    ih, irows = read_tsv(NAMED / "itemstatcost.txt")
    shift_col = ih.index("ValShift")
    shifts = {i: int(r[shift_col] or 0) for i, r in enumerate(irows) if r}

    _, ext_rows = read_tsv(ROOT / "work" / "sigma" / "cubemainext.txt")
    ext_rows = [[int(c) for c in r[2:8]] for r in ext_rows if r]  # cost, op, param, value, layer2, layer1
    base_dir = OUT / "data" / "global" / "excel"
    h, rows = read_tsv(base_dir / "base" / "cubemain.txt")
    c_op, c_par, c_val = h.index("op"), h.index("param"), h.index("value")
    rows = [r for r in rows if r and r[0] != "Expansion"]
    if len(rows) != len(ext_rows):
        print(f"cubemain has {len(rows)} recipes, cubemainext {len(ext_rows)}: not the same table")
        return 1

    def num(cell: str) -> int:
        return int(cell) if cell.lstrip("-").isdigit() else 0

    def migrate(op: int, param: int, value: int) -> tuple[int, int]:
        """(param, value) in D2R numbering; the stat compares carry the stat's ValShift."""
        if op in (30, 31):
            return skills.get(param, param), value
        if op in (32, 33):
            return states.get(param, param), value
        if op in (34, 35):
            return levels.get(param, param), value
        if op in STAT_OPS:
            return stats.get(param, param), value >> shifts.get(param, 0)
        if op in range(15, 27):  # item stat compares: only the id (D2R's matcher shifts the value itself)
            return stats.get(param, param), value
        return param, value

    entries, folded, remapped, stats_ = [], 0, 0, Counter()
    for i, (r, (cost, eop, epar, eval_, elay2, elay1)) in enumerate(zip(rows, ext_rows)):
        mop, mpar, mval = num(r[c_op]), num(r[c_par]), num(r[c_val])
        if mop in range(3, 27):  # D2R's own checks read the stat by D2R's id
            new = str(stats.get(mpar, mpar))
            remapped += new != r[c_par]
            r[c_par] = new
        conds = []
        main_in_plugin = mop in STAT_OPS or mop in range(29, 43)
        if mop in range(29, 43):  # Median's added main ops: D2R passes them as unknown, the plugin evaluates them
            stats_[f"main {mop}"] += 1
        if eop in ITEM_OPS and (mop == 0 or main_in_plugin):
            # D2R's matcher checks the record's op/param/value on each input item: the ext item condition takes the
            # record's slot; a player condition that was there moves to the plugin
            if main_in_plugin:
                conds.append((mop, *migrate(mop, mpar, mval), elay1))
                stats_[f"main {mop} moved to the plugin (record slot taken by ext item op)"] += mop in STAT_OPS
            r[c_op], r[c_par], r[c_val] = str(eop), str(migrate(eop, epar, eval_)[0]), str(eval_)
            folded += 1
            stats_[f"ext item {eop} moved into the record"] += 1
            stats_["ext item layer lost"] += elay2 != 0
        else:
            if main_in_plugin and mop not in STAT_OPS:
                conds.append((mop, *migrate(mop, mpar, mval), elay1))
            if eop in PLUGIN_OPS:
                conds.append((eop, *migrate(eop, epar, eval_), elay2))
                stats_[f"ext {eop}"] += 1
            elif eop in ITEM_OPS:
                stats_[f"ext item {eop} NOT applied (record has item op {mop})"] += 1
            elif eop:
                stats_[f"ext {eop} NOT applied (unknown op)"] += 1
        if cost or conds:
            entries.append((i, cost, conds))
    if True:
        for bank in ("base", ""):
            p = base_dir / bank / "cubemain.txt"
            bh, brows = read_tsv(p)
            body = [r for r in brows if r and r[0] != "Expansion"]
            if len(body) != len(rows):
                print(f"{p}: {len(body)} recipes, expected {len(rows)}")
                return 1
            for dst, src in zip(body, rows):
                dst[c_op], dst[c_par], dst[c_val] = src[c_op], src[c_par], src[c_val]
            write_tsv(p, bh, brows)

    # act per migrated area id (op 35)
    lh, lrows = read_tsv(NAMED / "levels.txt")
    act_col = lh.index(LEVEL_ACT_COLUMN)
    acts = {levels.get(i, i): num(r[act_col]) for i, r in enumerate(lrows) if r}
    act_list = [acts.get(a, 0) for a in range(max(acts) + 1)]

    lines = ["// Generated by tools/cube_tables.py; do not edit.", "#pragma once", "", "#include <cstdint>", "",
             "namespace mxl_cube {", "",
             "// Recipes whose cubemainext data the plugin evaluates: the Median-only conditions (cubemain ops 29-42,",
             "// cubemainext's second condition when it is a player condition) and the gold cost. Conditions are",
             "// {op, param, value, layer} with param/value in D2R numbering (stat compares: value already >> ValShift).",
             "struct Cond {", "\tint32_t op;", "\tint32_t param;", "\tint32_t value;", "\tint32_t layer;", "};",
             "struct Entry {", "\tint32_t recipe;  // cubemain row", "\tint32_t cost;    // x 100 gold",
             "\tint32_t count;", "\tCond cond[2];", "};",
             f"inline constexpr int32_t RecipeCount = {len(rows)};", "inline constexpr Entry Entries[] = {"]
    for i, cost, conds in entries:
        cs = [f"{{{', '.join(map(str, c))}}}" for c in conds] + ["{0, 0, 0, 0}"] * (2 - len(conds))
        lines.append(f"    {{{i}, {cost}, {len(conds)}, {{{cs[0]}, {cs[1]}}}}},")
    lines += ["};", f"inline constexpr int32_t ActCount = {len(act_list)};",
              "inline constexpr uint8_t ActOfArea[] = {" + ", ".join(map(str, act_list)) + "};", "",
              "}  // namespace mxl_cube", ""]
    done.write_text("cubemain.txt converted by tools/cube_tables.py")
    (ROOT / "plugin" / "src" / "mxl_cube.h").write_text("\n".join(lines), encoding="utf-8")
    print(f"cube: {len(entries)} recipes with plugin conditions or a cost -> plugin/src/mxl_cube.h; "
          f"{folded} ext item conditions moved into cubemain, {remapped} main stat ids remapped")
    for k, v in sorted(stats_.items()):
        print(f"  {k}: {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

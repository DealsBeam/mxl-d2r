"""Monster AI data fixes for D2R (docs/subsystems/monsters-ai.md). Runs after txt_migrate.py, in place on work/mod.

D2R dispatches a monster's AI through a fixed table of 155 entries (monai.txt rows 0-154, the game's table at RVA
0x2396e90, docs/d2r-addresses.md section 12). monstats' AI column is a monai row; an id past the table silently
becomes row 0 ("None", a think function that does nothing), so a monster with it stands still forever.

Median has one AI D2R lacks: "andariel_ai" (D2Sigma D2AI.cpp 0x4a820, the think function of 47 bosses and quest
monsters: Andariel, Maghda, Radament, Zoltun Kulle, Kentril Dumon, Lilith, the Scholars, ...). txt_migrate appends its
monai row as id 155, one past the end. This step gives those monsters D2R's own "Andariel" AI (melee plus two skills,
far/near choice) with parameters in that AI's meaning, and removes the row. It is an interim: Median's AI is a
multi-phase boss AI (up to four skills selected by aip5 bits, a hit point phase, a run-speed charge) that needs a
native port (docs/subsystems/monsters-ai.md section 5).

Parameter meaning, D2R "Andariel" think function 0x5aba70 (aip1-4, per difficulty):
  aip1  chance (%) of skill 1 when the target is in reach
  aip2  chance (%) of waiting 5 frames when the target is out of reach (so a low value approaches quickly)
  aip3  chance (%) of using a skill instead of walking when out of reach
  aip4  chance (%) of skill 1 over skill 2 in that case
Median's andariel_ai aip1 and aip4 are chances too (skill and secondary skill), so they are kept; aip2 and aip3 mean a
distance / phase gate there, so they get the values Median's own vanilla-"Andariel" monsters mostly use. aip5-8 are
cleared (unused by the replacement).

Usage: python tools/monster_ai_data.py [--mod DIR]   (DIR = a mod root with data/global/excel/{base,}/; default work/mod)
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from txt_migrate import read_tsv, write_tsv  # noqa: E402

ENGINE_AI_ROWS = 155  # D2R 3.3 build 93847: entries of the AI table (0x2396e90, stride 0x20)
BANKS = ("base", "")
DIFFS = ("", "(N)", "(H)")
# Median-only AI -> (D2R AI to use, fixed aip values; None = keep Median's)
REPLACEMENTS = {
    "andariel_ai": {"ai": "Andariel", "aip": {"aip1": None, "aip2": "3", "aip3": "25", "aip4": None}},
}


def plan_monster(row: dict, spec: dict) -> dict:
    """Column -> new value for one monster row (pure; see the module docstring)."""
    out = {"AI": spec["ai"]}
    for diff in DIFFS:
        for name, fixed in spec["aip"].items():
            if fixed is not None:
                out[name + diff] = fixed
            elif row.get(name + diff, "") == "" and row.get(name, "") != "":
                out[name + diff] = row[name]  # a blank difficulty column falls back to the base column
        for n in range(5, 9):
            out[f"aip{n}{diff}"] = ""
    return out


def fix_bank(excel: Path, report: list[str]) -> int:
    path = excel / "monstats.txt"
    if not path.exists():
        return 0
    header, rows = read_tsv(path)
    col = {c: i for i, c in enumerate(header)}
    changed = []
    for r in rows:
        spec = REPLACEMENTS.get(r[col["AI"]])
        if spec is None:
            continue
        row = {c: r[i] for c, i in col.items()}
        for name, value in plan_monster(row, spec).items():
            if name in col:
                r[col[name]] = value
        changed.append(r[col["Id"]])
    if changed:
        write_tsv(path, header, rows)
    ai_path = excel / "monai.txt"
    dropped = 0
    if ai_path.exists():
        ah, arows = read_tsv(ai_path)
        kept = [r for r in arows if r[0] not in REPLACEMENTS]
        dropped = len(arows) - len(kept)
        if dropped:
            write_tsv(ai_path, ah, kept)
        if len(kept) > ENGINE_AI_ROWS:
            report.append(f"- WARNING {excel.name or 'rotw'}: monai has {len(kept)} rows, the engine table has {ENGINE_AI_ROWS}")
    report.append(f"- bank `{excel.name or 'rotw'}`: {len(changed)} monsters moved off Median-only AI, {dropped} monai rows removed")
    return len(changed)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mod", default=str(ROOT / "work" / "mod"))
    mod = Path(ap.parse_args().mod)
    replaced = ", ".join(k + " -> " + v["ai"] for k, v in REPLACEMENTS.items())
    report = ["# Monster AI data", "", "Median-only AI ids replaced: " + replaced, ""]
    total = 0
    for bank in BANKS:
        total += fix_bank(mod / "data" / "global" / "excel" / bank, report)
    (mod / "monster_ai_report.md").write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())

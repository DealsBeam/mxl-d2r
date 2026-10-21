"""Monster AI data check for D2R (docs/subsystems/monsters-ai.md). Runs after txt_migrate.py, read-only on work/mod.

D2R dispatches a monster's AI through a fixed table of 155 entries (monai.txt rows 0-154, the game's table at RVA
0x2396e90, docs/d2r-addresses.md section 12). monstats' AI column is a monai row; an id past the table silently
becomes row 0 ("None", a think function that does nothing), so a monster with it stands still forever.

Median has one AI D2R lacks: "andariel_ai" (D2Sigma D2AI.cpp 0x4a820, the think function of 47 bosses and quest
monsters: Andariel, Maghda, Radament, Zoltun Kulle, Kentril Dumon, Lilith, the Scholars, ...). txt_migrate appends its
monai row as id 155, one past the end. The plugin (plugin/src/monster_ai.cpp) hooks the table getter and
serves id 155 with a native port of Median's think, so the data keeps the row and the monsters' own parameters (aip1-5
in Median's meaning; with the "medianai" word of mxl-core-off.txt the plugin maps them to D2R's Andariel think itself).
This step therefore changes nothing; it checks that the contract with the plugin holds and writes the report:

  - the `andariel_ai` row of monai.txt sits at exactly id 155 (the id the plugin serves), in both banks;
  - monsters with that AI that name no skill 1 / 2 are listed (the think falls back to the plain attack for a skill the row lacks).

Usage: python tools/monster_ai_data.py [--mod DIR]   (DIR = a mod root with data/global/excel/{base,}/; default work/mod)
Exit code 1 when the contract is broken (the build stops).
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from txt_migrate import read_tsv  # noqa: E402

ENGINE_AI_ROWS = 155  # D2R 3.3 build 93847: entries of the AI table (0x2396e90, stride 0x20)
BANKS = ("base", "")
# Median-only AI served by the plugin (monster_ai.cpp, GetterHook) -> the id it is registered under
NATIVE_AI = {"andariel_ai": 155}


def check_bank(excel: Path, report: list[str]) -> int:
    """Number of contract violations in one bank."""
    bank = excel.name or "rotw"
    problems = 0
    ai_path = excel / "monai.txt"
    if not ai_path.exists():
        return 0
    _, arows = read_tsv(ai_path)
    index = {r[0]: n for n, r in enumerate(arows) if r and r[0]}
    for name, want in NATIVE_AI.items():
        have = index.get(name)
        if have != want:
            report.append(f"- ERROR {bank}: monai row `{name}` is id {have}, the plugin serves id {want}")
            problems += 1
    if len(arows) > ENGINE_AI_ROWS + len(NATIVE_AI):
        report.append(f"- ERROR {bank}: monai has {len(arows)} rows, more than the engine's {ENGINE_AI_ROWS} plus the plugin's {len(NATIVE_AI)}")
        problems += 1
    path = excel / "monstats.txt"
    users = 0
    if path.exists():
        header, rows = read_tsv(path)
        col = {c: i for i, c in enumerate(header)}
        for r in rows:
            if r[col["AI"]] not in NATIVE_AI:
                continue
            users += 1
            for skill in ("Skill1", "Skill2"):
                if not r[col[skill]].strip():
                    report.append(f"- note {bank}: `{r[col['Id']]}` names no {skill} (the plain attack stands in)")
    report.append(f"- bank `{bank}`: {users} monsters on a plugin AI, monai {len(arows)} rows")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mod", default=str(ROOT / "work" / "mod"))
    mod = Path(ap.parse_args().mod)
    served = ", ".join(f"{k} = id {v}" for k, v in NATIVE_AI.items())
    report = ["# Monster AI data", "", "Median-only AI ids served by the plugin: " + served, ""]
    problems = 0
    for bank in BANKS:
        problems += check_bank(mod / "data" / "global" / "excel" / bank, report)
    (mod / "monster_ai_report.md").write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join(report))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

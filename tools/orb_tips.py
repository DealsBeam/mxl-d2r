"""Per-orb tooltips for Median's 101 mystic orbs (audit T12; D2Sigma D2ItemDesc 0x100793f0).

Every orb item has the same spelldescstr ("Right-Click to Apply", 85 rows, or "Rare items only / Right-Click to Apply",
16 rows), so the tooltip does not say what the orb adds. D2Sigma appends to it, for an item of type `myst`:

    Limit per item: <MaxPerItem>               (string 12387, only when MaxPerItem > 0)
    ÿc3+<LevelRequirement> Required Level      (string ModitemRLZOMG, only when > 0)
    <the orb's properties>                     (up to six, as the item shows them once applied)
    ÿc5<typeString>                            ((Weapon Only), (Armor Only), ..., strings 11655-11658, 12385, 12386)

The port cannot hook that builder, so the build writes the same text as one string per orb (`mxlorb_<row>`, all 13
languages) and txt_migrate points the orb's misc.txt spelldescstr at it (`Migrator.orb_tooltips`). The property lines are
rendered here from the migrated itemstatcost (descfunc / descval / descstrpos / descstrneg, dgrp* for the grouped
properties such as "All Resistances"), the way D2R's item description would show them after the orb is applied.

Usage (stand-alone check): py -3 tools/orb_tips.py [orb row ...]   (needs work/ from a build up to txt_migrate)
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import strings_ns  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
KEY_PREFIX = "mxlorb_"
LIMIT_KEY, LEVEL_KEY = "12387", "ModitemRLZOMG"  # "Limit per item:", "Required Level"
SKILL_STAT_TEXT = "ItemModifierNonClassSkill"  # D2R's "%+d to %s" for descfunc 28 (the skill's name is the argument)
COLOUR_BLUE, COLOUR_GREY = "ÿc3", "ÿc5"
# property functions that set a fixed stat, not the stat1 of the property row (D2 properties.txt)
FUNC_MIN_DMG, FUNC_MAX_DMG, FUNC_ENH_DMG, FUNC_DMG_RANGE_MIN, FUNC_DMG_RANGE_MAX = 5, 6, 7, 15, 16
EVENT_SKILL_FUNC = 11  # chance (min), skill (param), level (max)


class Renderer:
    """Property lines of an orb in one language."""

    def __init__(self, lang: str, texts: dict[str, str], stats: dict[str, dict[str, str]], skill_name):
        self.lang, self.t, self.stats, self.skill_name = lang, texts, stats, skill_name
        self.skipped: list[str] = []

    def text(self, key: str) -> str:
        return self.t.get(strings_ns.bare(key), "") if key else ""

    @staticmethod
    def fmt(template: str, *args) -> str:
        try:
            return template % args
        except (TypeError, ValueError):
            return template

    def number(self, func: str, val: str, text: str, v: int) -> str:
        """D2's descfunc 1-4, 20, 21 (descval 1: value before the text, 2: after, empty: no value)."""
        if "%" in text:  # Median's text formats the value itself (function 19, or printf text on an older function)
            return self.fmt(text, v)
        if func == "20":
            body = f"-{abs(v)}%"
        elif func == "21":
            body = f"-{abs(v)}"
        else:
            body = (f"{v:+d}" if func in ("1", "4") else f"{v:d}") + ("%" if func in ("2", "4") else "")
        return f"{body} {text}" if val == "1" else f"{text} {body}" if val == "2" else text

    def stat_line(self, name: str, v: int, par: int, hi: int | None = None) -> str | None:
        s = self.stats.get(name)
        if not s:
            return None
        func, val = s["descfunc"], s["descval"]
        pos = self.text(s["descstrneg"] if v < 0 and s["descstrneg"] else s["descstrpos"])
        if func == "15":  # chance to cast: chance, level, skill
            return self.fmt(pos, v, hi if hi is not None else 0, self.skill_name(par))
        if func == "28" or name == "item_nonclassskill":
            sk = self.skill_name(par)
            return self.fmt(self.text(SKILL_STAT_TEXT) or "%+d to %s", v, sk) if sk else None
        if func == "31":  # Median: the stat's parameter is a string id
            return self.text(str(par)).replace("ÿc8 ", "ÿc8", 1).strip() or None
        if func in ("1", "2", "3", "4", "19", "20", "21") and pos:
            return self.number(func, val, pos, v)
        return None

    def group_line(self, stat: dict[str, str], v: int) -> str | None:
        func, val = stat["dgrpfunc"], stat["dgrpval"]
        pos = self.text(stat["dgrpstrneg"] if v < 0 and stat["dgrpstrneg"] else stat["dgrpstrpos"])
        return self.number(func, val, pos, v) if pos and func in ("1", "2", "3", "4", "19", "20", "21") else None

    def prop_lines(self, funcs: list[tuple[str, str]], par: int, lo: int, hi: int) -> list[str]:
        """funcs: (property function, stat) of the property row's slots; the value is lo (hi for ranges)."""
        fset = {f for f, _n in funcs}
        if str(FUNC_DMG_RANGE_MIN) in fset:  # "Adds 3-6 Damage"
            return [self.fmt(self.text("strModMinDamageRange"), lo, hi)]
        fixed = {FUNC_ENH_DMG: lambda: self.fmt(self.text("strModEnhancedDamage"), lo),
                 FUNC_MIN_DMG: lambda: self.stat_line("mindamage", lo, par),
                 FUNC_MAX_DMG: lambda: self.stat_line("maxdamage", lo, par)}
        for f, make in fixed.items():
            if str(f) in fset:
                return [make() or ""]
        # stats without a description (Median's orb exclusion bookkeeping, property 403) show nothing
        names = [n for _f, n in funcs if self.stats.get(n, {}).get("descfunc")]
        # grouped properties ("All Resistances"): two or more stats of one dgrp, all the same value
        grp = [self.stats[n] for n in names if self.stats[n]["dgrp"]]
        if len(grp) >= 2 and len({g["dgrp"] for g in grp}) == 1:
            line = self.group_line(grp[0], lo)
            if line:
                return [line]
        out: list[str] = []
        for f, n in funcs:
            if n not in names:
                continue
            line = self.stat_line(n, lo, par, hi if f == str(EVENT_SKILL_FUNC) else None)
            if line:
                out.append(line)
            else:
                self.skipped.append(n)
        return out


def load_orbs(sigma_orbs: Path) -> list[dict]:
    """work/sigma/mysticorbs.txt rows as dicts (D2Sigma's field names, Median row numbers in Param / properties)."""
    lines = sigma_orbs.read_text("utf-8-sig", errors="replace").splitlines()
    header = lines[0].split("\t")
    return [dict(zip(header, line.split("\t"))) for line in lines[1:] if line.strip()]


def orb_text(orb: dict, base: str, rend: Renderer, props: dict[int, list[tuple[str, str]]]) -> str:
    """The tooltip of one orb: its old description, then D2Sigma's limit / level / properties / applicability."""
    lines = [base.rstrip("\n")] if base.strip() else []
    limit, level = int(orb["MaxPerItem"] or 0), int(orb["LevelRequirement"] or 0)
    if limit > 0:
        lines.append(f"{rend.text(LIMIT_KEY)} {limit}")
    if level > 0:
        lines.append(f"{COLOUR_BLUE}+{level} {rend.text(LEVEL_KEY)}")
    for k in range(1, 7):
        p = int(orb[f"Prop{k}"])
        if p < 0:
            continue
        for line in rend.prop_lines(props.get(p, []), int(orb[f"Param{k}"]), int(orb[f"Min{k}"]), int(orb[f"Max{k}"])):
            if line:
                lines.append(line if line.startswith("ÿc") else f"{COLOUR_BLUE}{line}")
    applies = rend.text(orb["Description"]) if orb["Description"].isdigit() and int(orb["Description"]) > 0 else ""
    if applies:
        lines.append(f"{COLOUR_GREY}{applies}")
    return "\n".join(lines)


def write_strings(texts: dict[str, dict[str, str]]) -> None:
    """key -> {language: text} into both median.json files and the namespace key list (earlier mxlorb_ records replaced)."""
    for folder in ("strings", "strings-legacy"):
        path = strings_ns.LNG / folder / strings_ns.NAMESPACE / "median.json"
        if not path.exists():
            continue
        recs = [e for e in json.loads(path.read_text("utf-8-sig")) if not e["Key"].startswith(KEY_PREFIX)]
        recs += [{"Key": k, **t} for k, t in texts.items()]
        path.write_text("﻿" + json.dumps(recs, ensure_ascii=False, indent=2).replace("\n", "\r\n"),
                        encoding="utf-8", newline="")
    ns = json.loads(strings_ns.NAMESPACE_FILE.read_text("utf-8"))
    ns["keys"] = [k for k in ns["keys"] if not k.startswith(KEY_PREFIX)] + list(texts)
    strings_ns.NAMESPACE_FILE.write_text(json.dumps(ns, ensure_ascii=False, indent=0), encoding="utf-8")


def languages() -> list[str]:
    p = strings_ns.MOD_STRINGS / strings_ns.NAMESPACE / "median.json"
    return [k for k in json.loads(p.read_text("utf-8-sig"))[0] if k != "Key"]

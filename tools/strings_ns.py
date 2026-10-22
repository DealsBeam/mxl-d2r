"""The `medianxl` string namespace for the table and layout tools (docs/strings-namespace.md).

Group 1 = keys D2R's own string files have (unqualified in table cells = `d2r:`; Median's text for them is a `d2r:<key>`
record in `strings/medianxl/overrides.json`); group 2 = Median-only keys (namespaced, `medianxl:<key>` in table cells,
`@medianxl:<key>` in HD layouts). A key is group 2 when it is not in D2R's string files
(extract/d2r/93847/data/local/lng/{strings,strings-legacy}/*.json). D2R's own files are not part of the mod.

  d2r_keys()             every key of D2R's own string files (the group 1 set)
  load_group2()          group 2 keys: work/mod/strings_namespace.json (tbl2json) when it exists, else computed the same way
  d2r_loaded()           key -> records of D2R's strings/ files (the keys the loader knows as `d2r:<key>`)
  overrides()            the mod's `d2r:<key>` override records, by bare key
  effective_texts(lang)  bare key -> displayed text (D2R's, the override's, or the Median-only key's)
  native_exceptions()    the keys of tools/strings_native.json (must be empty)
  is_localization(t, c)  True if column c of table t holds a string key (LOCALIZATION below)
  qualify(ref, group2)   table / JSON reference as written: `medianxl:<key>` for group 2, anything else unchanged
  plan_aliases / load_aliases / write_aliases / retarget
                         short aliases `mxlname_<n>` for keys that do not fit a fixed-size cell (see below)
  split_key(ref)         ('medianxl', key) / (None, ref)
  bare(ref)              `medianxl:<key>` -> `<key>`
  qualify_layout(...)    every `@key` of a layout
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAMESPACE = "medianxl"
PREFIX = NAMESPACE + ":"
D2R_LNG = ROOT / "extract" / "d2r" / "93847" / "data" / "local" / "lng"
NAMESPACE_FILE = ROOT / "work" / "mod" / "strings_namespace.json"
EXCEPTIONS_FILE = ROOT / "tools" / "strings_native.json"
OVERRIDE_FILE = "overrides.json"
D2R_PREFIX = "d2r:"
MOD_STRINGS = ROOT / "work" / "mod" / "data" / "local" / "lng" / "strings"
ALIAS_FILE = ROOT / "work" / "mod" / "strings_alias.json"
ALIAS_PREFIX = "mxlname_"
LNG = ROOT / "work" / "mod" / "data" / "local" / "lng"

NUMERIC = re.compile(r"-?\d+(\.\d+)?")

# Localization columns per table, lower case. Source: D2R's data guide (global/dataguide/data/files/*.js) fields typed
# `string`, plus the text fields whose description says "string key" (hireling, lowqualityitems, objects, rareprefix,
# raresuffix, runes, setitems, uniqueitems), the affix names (magicprefix/magicsuffix/automagic/unique*, stored in a
# fixed-size field, LOCALIZATION.md) and levels LevelName / LevelWarp / LevelEntry (the guide types them `text`
# but describes them as string fields). armor / weapons / misc share namestr through shareditems. Median's levels
# column EntryFile (leftover bytes of the .bin) is not mapped to LevelEntry, so it never reaches the output.
LOCALIZATION = {
    "armor": {"namestr"},
    "weapons": {"namestr"},
    "misc": {"namestr", "spelldescstr", "spelldescstr2"},
    "automagic": {"name"},
    "magicprefix": {"name"},
    "magicsuffix": {"name"},
    "rareprefix": {"name"},
    "raresuffix": {"name"},
    "uniqueprefix": {"name"},
    "uniquesuffix": {"name"},
    "uniqueappellation": {"name"},
    "uniqueitems": {"index"},
    "setitems": {"index"},
    "sets": {"name"},
    "runes": {"name"},
    "lowqualityitems": {"name"},
    "charstats": {"strallskills", "strskilltab1", "strskilltab2", "strskilltab3", "strclassonly"},
    "hireling": {"hiredesc", "namefirst", "namelast"},
    "itemstatcost": {"descstrpos", "descstrneg", "descstr2", "dgrpstrpos", "dgrpstrneg", "dgrpstr2"},
    "levelgroups": {"namestring"},
    "levels": {"levelname", "levelwarp", "levelentry"},
    "monstats": {"namestr", "descstr"},
    "montype": {"strplur"},
    "objects": {"name"},
    "pettype": {"name", "overridename1", "overridename2", "overridename3", "overridename4"},
    "shrines": {"stringname", "stringphrase"},
    "skilldesc": {"str name", "str short", "str long", "str alt", "item proc text",
                  *(f"{p}{c}{n}" for p, rng in (("desctext", 6), ("dsc2text", 5), ("dsc3text", 7)) for c in "ab"
                    for n in range(1, rng + 1))},
    "superuniques": {"name"},
}
# Columns that name a string key but are read as a numeric range (hireling NameFirst..NameLast: every ID from the first
# to the last is a name). A qualified key gets a loader-assigned number, so the range could not follow it: these stay
# native (group 1); txt_migrate skips them and validate.py fails if one names a group 2 key.
RANGE_COLUMNS = {"hireling": {"namefirst", "namelast"}}


# Localization columns whose value is never displayed, so a value that is no string key is no finding: automagic affixes
# are applied "regardless of the item's quality ... without changing the item's name" (D2R's data guide, automagic.js);
# Median fills the column with labels ("InnateAreaEffect", "Angelic 1H") that its own string tables lack as well.
LABEL_COLUMNS = {"automagic": {"name"}}
# Placeholder rows (value never shown): objects.Name `dummy_<n>`.
PLACEHOLDER_VALUES = {("objects", "name"): re.compile(r"dummy_\d+")}


def is_label_column(table: str, column: str) -> bool:
    return column.lower() in LABEL_COLUMNS.get(table.lower(), ())


def is_placeholder(table: str, column: str, value: str) -> bool:
    rx = PLACEHOLDER_VALUES.get((table.lower(), column.lower()))
    return bool(rx and rx.fullmatch(value))


def is_localization(table: str, column: str) -> bool:
    return column.lower() in LOCALIZATION.get(table.lower(), ())


def is_range_column(table: str, column: str) -> bool:
    return column.lower() in RANGE_COLUMNS.get(table.lower(), ())


def d2r_keys() -> set[str]:
    """Every key of D2R's string files, both folders (the group 1 set)."""
    return {e["Key"] for f in ("strings", "strings-legacy") if (D2R_LNG / f).exists()
            for p in (D2R_LNG / f).glob("*.json") for e in json.loads(p.read_text("utf-8-sig"))}


def d2r_loaded() -> dict[str, list[dict]]:
    """key -> its records in D2R's strings/ files: what the loader knows as `d2r:<key>` (a key can repeat in D2R's own
    files). Keys that exist only in strings-legacy/ are not in it."""
    out: dict[str, list[dict]] = {}
    for p in sorted((D2R_LNG / "strings").glob("*.json")):
        for e in json.loads(p.read_text("utf-8-sig")):
            out.setdefault(e["Key"], []).append(e)
    return out


def overrides() -> dict[str, dict]:
    """bare key -> record of the mod's `d2r:<key>` overrides (work/mod/.../strings/medianxl/overrides.json)."""
    p = MOD_STRINGS / NAMESPACE / OVERRIDE_FILE
    if not p.exists():
        return {}
    return {e["Key"][len(D2R_PREFIX):]: e for e in json.loads(p.read_text("utf-8-sig")) if e["Key"].startswith(D2R_PREFIX)}


def effective_texts(lang: str = "enUS") -> dict[str, str]:
    """bare key -> the text the player reads: D2R's strings/ text, replaced by the mod's override, plus the
    Median-only keys of medianxl/median.json (a key has one text; D2R and Median-only key sets do not overlap)."""
    out = {k: recs[0].get(lang, "") for k, recs in d2r_loaded().items()}
    out.update({k: e[lang] for k, e in overrides().items() if lang in e})
    p = MOD_STRINGS / NAMESPACE / "median.json"
    if p.exists():
        out.update({e["Key"]: e.get(lang, "") for e in json.loads(p.read_text("utf-8-sig"))})
    return out


def native_exceptions() -> set[str]:
    """tools/strings_native.json. Must stay empty: with a mod namespace an unqualified record is a `medianxl:` record
    and its id is ignored, so no string can be native with a fixed id (validate.py fails otherwise)."""
    return set(json.loads(EXCEPTIONS_FILE.read_text("utf-8"))["keys"]) if EXCEPTIONS_FILE.exists() else set()


def load_group2() -> set[str]:
    """Median-only keys that need the `medianxl:` prefix."""
    if NAMESPACE_FILE.exists():
        return set(json.loads(NAMESPACE_FILE.read_text("utf-8"))["keys"])
    # Not written yet (tbl2json has not run): the keys of the namespace file
    p = MOD_STRINGS / NAMESPACE / "median.json"
    return {e["Key"] for e in json.loads(p.read_text("utf-8-sig"))} if p.exists() else set()


def qualify(ref: str, group2: set[str], aliases: dict[str, str] | None = None) -> str:
    """Table / JSON reference to a key: `medianxl:<key>` for group 2 (`medianxl:<alias>` for a key with a short alias,
    see load_aliases); empty cells, numbers, group 1 keys, native exceptions and already qualified references stay
    as they are."""
    if not ref or NUMERIC.fullmatch(ref) or ref.startswith(PREFIX) or ref not in group2:
        return ref
    return PREFIX + (aliases if aliases is not None else load_aliases()).get(ref, ref)


# ---- short aliases for keys that do not fit a fixed-size field ----------------------------------------------------
# uniqueitems.index and setitems.index (31 characters) and levels.LevelWarp (39) store the key in the cell itself, so
# the whole `medianxl:<key>` must fit. A Median-only key that does not ("Sunstone of the Twin Seas") gets a short
# alias `mxlname_<n>`: txt_migrate decides which keys (plan_aliases), writes the alias map (work/mod/strings_alias.json),
# adds an alias record with the same 13 texts to the namespace files (write_aliases) and writes the alias in every
# cell that names the key (localization cells as `medianxl:<alias>`, reference cells such as cubemain inputs and
# treasure class items as the bare alias). The long key stays defined as well (the plugin and idkeys use it).

def plan_aliases(keys) -> dict[str, str]:
    """key -> alias for the given keys, numbered 1.. in key order (deterministic for the same set)."""
    return {k: f"{ALIAS_PREFIX}{n}" for n, k in enumerate(sorted(set(keys)), 1)}


def load_aliases() -> dict[str, str]:
    """key -> alias, as written by txt_migrate (empty when the file does not exist)."""
    return json.loads(ALIAS_FILE.read_text("utf-8"))["aliases"] if ALIAS_FILE.exists() else {}


def retarget(cell: str, aliases: dict[str, str]) -> str:
    """A cell that refers to an aliased key by name (cubemain input / output, treasure class item, with optional
    quotes and a `,qty=2` suffix) names the bare alias instead."""
    quoted = len(cell) > 1 and cell[0] == cell[-1] == '"'
    name, sep, rest = (cell[1:-1] if quoted else cell).partition(",")
    if name not in aliases:
        return cell
    body = aliases[name] + sep + rest
    return f'"{body}"' if quoted else body


def write_aliases(aliases: dict[str, str]) -> None:
    """Write the alias map and the alias records (same texts as the long key) into median.json of both string
    folders and strings_namespace.json. Idempotent: earlier alias records are replaced."""
    ALIAS_FILE.write_text(json.dumps({"namespace": NAMESPACE, "aliases": aliases}, ensure_ascii=False, indent=0),
                          encoding="utf-8")
    for folder in ("strings", "strings-legacy"):
        path = LNG / folder / NAMESPACE / "median.json"
        if not path.exists():
            continue
        recs = [e for e in json.loads(path.read_text("utf-8-sig")) if not e["Key"].startswith(ALIAS_PREFIX)]
        by_key = {e["Key"]: e for e in recs}
        recs += [{**by_key[k], "Key": a} for k, a in aliases.items()]
        path.write_text("﻿" + json.dumps(recs, ensure_ascii=False, indent=2).replace("\n", "\r\n"),
                        encoding="utf-8", newline="")
    ns = json.loads(NAMESPACE_FILE.read_text("utf-8"))
    ns["keys"] = [k for k in ns["keys"] if not k.startswith(ALIAS_PREFIX)] + list(aliases.values())
    NAMESPACE_FILE.write_text(json.dumps(ns, ensure_ascii=False, indent=0), encoding="utf-8")


def split_key(ref: str) -> tuple[str | None, str]:
    """('medianxl', 'Key') for 'medianxl:Key', (None, ref) for an unqualified cell. Other namespaces (d2r:,
    d2rloader:) are returned as written."""
    if ":" in ref:
        ns, key = ref.split(":", 1)
        if ns and re.fullmatch(r"[A-Za-z0-9_.-]+", ns):
            return ns, key
    return None, ref


def bare(ref: str) -> str:
    """`medianxl:<key>` -> `<key>` (lookups in median.json); other values unchanged."""
    return ref[len(PREFIX):] if ref.startswith(PREFIX) else ref


_REF = re.compile(r"@(?![A-Za-z0-9_]+:[A-Za-z0-9_])([A-Za-z0-9_]+)")


def qualify_layout(text: str, group2: set[str], native: set[str]) -> str:
    """Qualify every `@key` of a layout (JSON text): `@medianxl:<key>` for group 2, `@d2r:<key>` for D2R's own keys.
    With the mod's namespace active an unqualified `@key` means the mod's own strings (LOCALIZATION.md: copied native
    layouts must qualify the native text they retain with `d2r:`). Already qualified references and unknown keys stay."""
    def sub(m: re.Match) -> str:
        k = m.group(1)
        if k in group2:
            return f"@{PREFIX}{k}"
        return f"@d2r:{k}" if k in native else m.group(0)
    return _REF.sub(sub, text)

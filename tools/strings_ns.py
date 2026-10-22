"""The `medianxl` string namespace, as seen by the table tools (docs/strings-namespace.md).

Group 1: keys that exist in D2R's own string files; cells naming them stay unqualified (unqualified means `d2r:`).
Group 2: Median-only keys; cells naming them are written `medianxl:<key>` in the localization columns.

Shared by tools/txt_migrate.py (writes the prefix), tools/validate.py (checks it) and any tool that reads
localization cells or layouts:

  load_group2()          -> set of group 2 keys (work/mod/strings_namespace.json, else computed the same way)
  load_d2r_keys()        -> set of every key in D2R's own string files
  is_localization(t, c)  -> True if column c of table t holds a string key
  qualify(cell, group2)  -> the cell as it must be written
  split_key(cell)        -> (namespace or None, key)
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
D2R_LNG = ROOT / "extract" / "d2r" / "93847" / "data" / "local" / "lng"
MOD_LNG = ROOT / "work" / "mod" / "data" / "local" / "lng" / "strings"
NAMESPACE_FILE = ROOT / "work" / "mod" / "strings_namespace.json"

NAMESPACE = "medianxl"
PREFIX = NAMESPACE + ":"

NUMERIC = re.compile(r"-?\d+(\.\d+)?")

# Localization columns per table, lower case. Source: D2R's data guide (global/dataguide/data/files/*.js) fields typed
# `string`, plus the text fields whose description says "string key" (hireling, lowqualityitems, objects, rareprefix,
# raresuffix, runes, setitems, uniqueitems), the affix names (magicprefix/magicsuffix/automagic/unique*, stored in a
# fixed-size field, LOCALIZATION.md) and levels LevelName / LevelWarp / LevelEntry (the guide types them `text`
# but describe them as string fields). armor / weapons / misc share namestr through shareditems. Median's levels
# column EntryFile (leftover bytes of the .bin) is not mapped to LevelEntry, so it never reaches the output.
_NUMBERED = {
    "skilldesc": [f"{p}{c}{n}" for p, rng in (("desctext", 6), ("dsc2text", 5), ("dsc3text", 7)) for c in "ab"
                  for n in range(1, rng + 1)],
}
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
    "hireling": {"hiredesc"},
    "itemstatcost": {"descstrpos", "descstrneg", "descstr2", "dgrpstrpos", "dgrpstrneg", "dgrpstr2"},
    "levelgroups": {"namestring"},
    "levels": {"levelname", "levelwarp", "levelentry"},
    "monstats": {"namestr", "descstr"},
    "montype": {"strplur"},
    "objects": {"name"},
    "pettype": {"name", "overridename1", "overridename2", "overridename3", "overridename4"},
    "shrines": {"stringname", "stringphrase"},
    "skilldesc": {"str name", "str short", "str long", "str alt", "item proc text", *_NUMBERED["skilldesc"]},
    "superuniques": {"name"},
}
# Columns that name a string key but are read as a numeric range (hireling NameFirst..NameLast: every ID from the first
# to the last is a name). A qualified key gets a loader-assigned number, so the range could not follow it: these stay
# native (group 1); validate.py fails if one names a group 2 key.
RANGE_COLUMNS = {"hireling": {"namefirst", "namelast"}}

# Median-only keys that must stay native (group 1 style: unqualified cell, fixed ID in the string files) because their
# cell is a fixed-size text field that cannot hold `medianxl:<key>`: levels.LevelWarp (39 characters),
# uniqueitems.index and setitems.index (31). validate.py fails when another key overflows its field, so a new one
# must be added here; tools/tbl2json.py keeps these keys (and their IDs) in the native files, load_group2() never
# returns them. Listed in docs/strings-namespace.md under "Exceptions".
NATIVE_EXCEPTIONS = frozenset({
    "To The Dimensional Labyrinth 10",
    "To The Island of the Sunless Sea",
    "MRAchilios' Sledgehammer",
    "MRHunter's Trackless Step",
    "MRImperius' Winged Feet",
    "MRVasily's Falling Star",
    "MRVasily's Shepherd Moon",
    "MRWarmage's Flameshroud",
    "MRWitchhunter's Crucifix",
    "MRZann Esu's Binding Circle",
    "Armor of the Old Religion",
    "Body of the Forbidden One",
    "Claw of the Spirit Wolf",
    "DurandaltheBlazingSword",
    "Gift of the Spiritbeast",
    "Spirit Teacher's Headdress",
    "Staff of Herding Design",
    "Sunstone of the Twin Seas",
    "UpgradedBossSummonerRing",
})


def is_localization(table: str, column: str) -> bool:
    return column.lower() in LOCALIZATION.get(table.lower(), ())


def is_range_column(table: str, column: str) -> bool:
    return column.lower() in RANGE_COLUMNS.get(table.lower(), ())


def _keys(paths) -> set[str]:
    keys = set()
    for p in paths:
        keys |= {e["Key"] for e in json.loads(p.read_text("utf-8-sig"))}
    return keys


def load_d2r_keys() -> set[str]:
    """Every key in D2R's own string files (the group 1 set)."""
    return _keys(p for f in ("strings", "strings-legacy") for p in sorted((D2R_LNG / f).glob("*.json")))


def load_group2() -> set[str]:
    """The Median-only keys. work/mod/strings_namespace.json (written by tools/tbl2json.py) when it exists; until
    then the same rule: a key of Median's string files that D2R's own string files do not have."""
    if NAMESPACE_FILE.exists():
        d = json.loads(NAMESPACE_FILE.read_text("utf-8-sig"))
        return set(d["keys"]) - NATIVE_EXCEPTIONS
    return _keys(sorted(MOD_LNG.glob("*.json"))) - load_d2r_keys() - NATIVE_EXCEPTIONS


def split_key(cell: str) -> tuple[str | None, str]:
    """('medianxl', 'Key') for 'medianxl:Key', (None, cell) for an unqualified cell. Other namespaces (d2r:,
    d2rloader:) are returned as written."""
    if ":" in cell:
        ns, key = cell.split(":", 1)
        if ns and re.fullmatch(r"[A-Za-z0-9_.-]+", ns):
            return ns, key
    return None, cell


def qualify(cell: str, group2: set[str]) -> str:
    """The cell as a localization column must hold it: `medianxl:<key>` for a group 2 key, everything else (group 1
    keys, empty cells, numbers, already qualified cells) unchanged."""
    if not cell or NUMERIC.fullmatch(cell) or cell.startswith(PREFIX) or cell not in group2:
        return cell
    return PREFIX + cell

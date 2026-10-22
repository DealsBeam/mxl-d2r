"""The `medianxl` string namespace for generators (docs/strings-namespace.md).

Group 1 = keys D2R's own string files have (native, unqualified, = `d2r:`); group 2 = Median-only keys (namespaced,
`medianxl:<key>`; `@medianxl:<key>` in HD layouts). A key is group 2 when it is not in D2R's string files
(extract/d2r/93847/data/local/lng/{strings,strings-legacy}/*.json) and not a native exception (tools/strings_native.json).

load_group2() reads work/mod/strings_namespace.json (tbl2json) when it exists; before that it computes the same set.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAMESPACE = "medianxl"
D2R_LNG = ROOT / "extract" / "d2r" / "93847" / "data" / "local" / "lng"
NAMESPACE_FILE = ROOT / "work" / "mod" / "strings_namespace.json"
EXCEPTIONS_FILE = ROOT / "tools" / "strings_native.json"


def d2r_keys() -> set[str]:
    return {e["Key"] for f in ("strings", "strings-legacy") if (D2R_LNG / f).exists()
            for p in (D2R_LNG / f).glob("*.json") for e in json.loads(p.read_text("utf-8-sig"))}


def load_group2() -> set[str]:
    """Median-only keys that need the `medianxl:` prefix."""
    if NAMESPACE_FILE.exists():
        return set(json.loads(NAMESPACE_FILE.read_text("utf-8"))["keys"])
    # Not written yet: every key of the namespace file's folder is group 2 once tbl2json ran; without it the
    # keys are the ones in the old native Median file that D2R does not have.
    native = d2r_keys()
    p = ROOT / "work" / "mod" / "data" / "local" / "lng" / "strings" / "medianxl" / "median.json"
    if p.exists():
        return {e["Key"] for e in json.loads(p.read_text("utf-8-sig"))} - native
    p = ROOT / "work" / "mod" / "data" / "local" / "lng" / "strings" / "item-modifiers.json"
    exceptions = set(json.loads(EXCEPTIONS_FILE.read_text("utf-8"))["keys"]) if EXCEPTIONS_FILE.exists() else set()
    return ({e["Key"] for e in json.loads(p.read_text("utf-8-sig"))} - native - exceptions) if p.exists() else set()


def qualify(key: str, group2: set[str]) -> str:
    """Table / JSON reference to `key`: `medianxl:<key>` for group 2, unchanged otherwise."""
    return f"{NAMESPACE}:{key}" if key in group2 and ":" not in key else key


def bare(ref: str) -> str:
    """`medianxl:<key>` -> `<key>` (lookups in median.json); other values unchanged."""
    return ref[len(NAMESPACE) + 1:] if ref.startswith(NAMESPACE + ":") else ref


_REF = re.compile(r"@(?![A-Za-z0-9_]+:[A-Za-z0-9_])([A-Za-z0-9_]+)")


def qualify_layout(text: str, group2: set[str], native: set[str]) -> str:
    """Qualify every `@key` of a layout (JSON text): `@medianxl:<key>` for group 2, `@d2r:<key>` for D2R's own keys.
    With the mod's namespace active an unqualified `@key` means the mod's own strings (LOCALIZATION.md: copied native
    layouts must qualify the native text they retain with `d2r:`). Already qualified references and unknown keys stay."""
    def sub(m: re.Match) -> str:
        k = m.group(1)
        if k in group2:
            return f"@{NAMESPACE}:{k}"
        return f"@d2r:{k}" if k in native else m.group(0)
    return _REF.sub(sub, text)

"""Convert Median XL's string tables (.tbl) into D2R string JSON files.

Input:  extract/mxl/data/local/lng/eng/{string,expansionstring,patchstring}.tbl (tools/mpq_extract.py)
        extract/d2r/<build>/data/local/lng/{strings,strings-legacy}/*.json (tools/casc_extract.py)
Output: work/mod/data/local/lng/strings/*.json and strings-legacy/*.json (complete copies of D2R's native
        files: a mod's string file replaces the game's file), plus strings/medianxl/median.json and
        strings-legacy/medianxl/median.json (Median-only keys, namespace "medianxl", no ids),
        work/mod/strings_namespace.json, work/mod/strings_idkeys.json, strings_report.md

String IDs: string.tbl index, patchstring 10000 + index, expansionstring 20000 + index (what D2Sigma's tables
and code use). Rules:
  * Strings Median only looks up by ID share the throwaway key "x" (or none); they get the unique key
    "mxl_<id>". Numeric keys ("115" = "Axe (1)") are unique and used by item namestr cells, so they stay.
    strings_idkeys.json maps every Median ID to its final key.
  * A key in several tables: patchstring wins over expansionstring over string (1.13c lookup order).
  * Group 1: keys D2R already has are overridden (every occurrence, every file) unless the two texts' printf
    placeholders differ: D2R formats some strings itself (e.g. sysmsg7-9, the "was slain by" fragments it
    joins to names and then passes through sprintf), so a Median "%s" there reads a missing argument and
    crashes the game. Those keys keep D2R's text (listed in strings_report.md). Table cells keep plain keys (= d2r:).
  * Group 2: every other key goes to the D2RLoader namespace "medianxl" (docs/strings-namespace.md): median.json,
    {"Key", 13 languages}, no id, so no numeric ID can collide with D2R's again (bug #7). The full key is
    "medianxl:<key>"; strings_namespace.json lists them, strings_idkeys.json maps IDs to the full key.
  * Exceptions: group 2 keys listed in tools/strings_native.json (or NATIVE_EXCEPTIONS) stay native instead: appended
    to item-modifiers.json with a fixed ID below 65536 (free 16-bit value after D2R's highest), plain key in the idkeys.
  * Median ships English only. By default its text fills every language (otherwise other clients would show
    vanilla wording that contradicts Median); --english-only changes enUS alone.

Usage: python tools/tbl2json.py [--english-only]
"""
import argparse
import json
import re
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TBL = ROOT / "extract" / "mxl" / "data" / "local" / "lng" / "eng"
D2R_LNG = ROOT / "extract" / "d2r" / "93847" / "data" / "local" / "lng"
OUT = ROOT / "work" / "mod"
TABLES = (("string", 0), ("patchstring", 10000), ("expansionstring", 20000))
PRECEDENCE = ("patchstring", "expansionstring", "string")  # first wins for duplicate keys
NEW_KEYS_FILE = "item-modifiers.json"  # native home of the exceptions (group 2 keys that keep a fixed numeric ID)
NAMESPACE = "medianxl"  # install_mod.py writes the same name to d2rloader/metadata.json
NAMESPACE_FILE = "median.json"
# Keys that must stay native with a fixed ID: the plugin hands D2R a numeric string ID for them. Add them to
# tools/strings_native.json ({"keys": [...]}, docs/strings-namespace.md "Exceptions") or to this set.
NATIVE_EXCEPTIONS: set[str] = set()
EXCEPTIONS_FILE = ROOT / "tools" / "strings_native.json"
LANGS = ["enUS", "zhTW", "deDE", "esES", "frFR", "itIT", "koKR", "plPL", "esMX", "jaJP", "ptBR", "ruRU", "zhCN"]


def read_tbl(path: Path) -> list[tuple[str, str]]:
    """1.13c .tbl: header (crc u16, count u16, hash size u32, version u8, data start u32, max tries u32,
    file size u32), count u16 indices into the hash table, then 17-byte hash nodes
    (used u8, index u16, hash u32, key offset u32, string offset u32, length u16). Median wrote its text as
    UTF-8 ("ÿ" = C3 BF); anything that is not valid UTF-8 is read as cp1252, the classic 8-bit encoding."""
    b = path.read_bytes()
    _crc, count, hash_size = struct.unpack_from("<HHI", b, 0)
    header = 21
    indices = struct.unpack_from(f"<{count}H", b, header)
    nodes_at = header + count * 2

    def cstr(off: int) -> str:
        raw = b[off:b.index(b"\0", off)]
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError:
            return raw.decode("cp1252", errors="replace")

    out = []
    for i in range(count):
        node = nodes_at + indices[i] * 17
        _used, _idx, _h, key_off, str_off, _len = struct.unpack_from("<BHIIIH", b, node)
        out.append((cstr(key_off), cstr(str_off)))
    return out


PLACEHOLDER = re.compile(r"%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.\d+)?(?:hh|h|ll|l|I64|z)?[a-zA-Z%]")


def placeholders(text: str) -> list[str]:
    """printf conversions in order ("%%" is literal), e.g. "%+d%% to %s" -> ["d", "s"]."""
    return [m.group()[-1] for m in PLACEHOLDER.finditer(text or "") if m.group() != "%%"]


def load_median() -> tuple[dict[int, tuple[str, str]], dict[str, int]]:
    """id -> (original key, text); key -> winning id."""
    by_id, rank = {}, {}
    for name, base in TABLES:
        for i, (key, text) in enumerate(read_tbl(TBL / f"{name}.tbl")):
            by_id[base + i] = (key, text)
            rank[base + i] = PRECEDENCE.index(name)
    winner: dict[str, int] = {}
    for sid in sorted(by_id, key=lambda s: (rank[s], s)):
        winner.setdefault(by_id[sid][0], sid)
    return by_id, winner


def load_exceptions() -> set[str]:
    extra = json.loads(EXCEPTIONS_FILE.read_text("utf-8"))["keys"] if EXCEPTIONS_FILE.exists() else []
    return NATIVE_EXCEPTIONS | set(extra)


def write_json(path: Path, recs: list) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(recs, ensure_ascii=False, indent=2).replace("\n", "\r\n")
    path.write_text("\ufeff" + text, encoding="utf-8", newline="")


def final_key(sid: int, key: str) -> str:
    return f"mxl_{sid}" if key in ("x", "") else key  # numeric keys stay: item namestr cells use them


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--english-only", action="store_true")
    args = ap.parse_args()

    by_id, winner = load_median()
    idkeys = {sid: final_key(sid, key) for sid, (key, _t) in by_id.items()}
    texts: dict[str, str] = {}
    for sid, (key, text) in by_id.items():
        fk = idkeys[sid]
        if fk.startswith("mxl_") or winner[key] == sid:
            texts[fk] = text
    shadowed = sum(1 for sid, (key, _t) in by_id.items() if not idkeys[sid].startswith("mxl_") and winner[key] != sid)

    # sanity: the .tbl parse must agree with the first extraction (key<TAB>text, newlines written as \n)
    old_dir = ROOT.parent / "mxl_extracted" / "2_txt" / "strings"
    agree = 0
    for name, base in TABLES:
        if not (old_dir / f"{name}.txt").exists():
            continue  # the first extraction sits outside the repo (absent in agent worktrees)
        for i, line in enumerate((old_dir / f"{name}.txt").read_text("utf-8").splitlines()):
            key, _, old = line.partition("\t")
            agree += (key, old.replace("\\n", "\n")) == by_id[base + i]

    report = ["# tbl2json report", "",
              f"- Median strings: {len(by_id)} (string {sum(1 for s in by_id if s < 10000)}, patchstring "
              f"{sum(1 for s in by_id if 10000 <= s < 20000)}, expansionstring {sum(1 for s in by_id if s >= 20000)})",
              f"- Keys given an ID-based name (\"x\" or empty): {sum(1 for k in idkeys.values() if k.startswith('mxl_'))}",
              f"- Duplicate keys resolved (patch > expansion > string): {shadowed} entries shadowed",
              f"- Agreement with the first extraction: " + (f"{agree}/{len(by_id)} strings identical" if agree else "not checked"),
              f"- Languages: {'enUS only' if args.english_only else 'English text in all 13 languages'}"]

    folders = {f: {p.name: json.loads(p.read_text("utf-8-sig")) for p in sorted((D2R_LNG / f).glob("*.json"))}
               for f in ("strings", "strings-legacy") if (D2R_LNG / f).exists()}
    d2r_keys = {e["Key"] for files in folders.values() for recs in files.values() for e in recs}

    # Group 2 = Median-only keys (not in D2R's files): namespaced, no numeric IDs (docs/strings-namespace.md). The
    # exceptions are group 2 keys the plugin needs as a fixed native ID; they stay appended to NEW_KEYS_FILE with an
    # ID below 65536 (D2R matches string IDs by their low 16 bits, bug #7: Median's 262189 "ArcaneApe" answered for
    # 45), after D2R's own and skipping every 16-bit value in use.
    wanted = load_exceptions()
    exceptions = sorted(k for k in wanted if k in texts and k not in d2r_keys)
    for k in sorted(wanted - set(exceptions)):
        print(f"tbl2json: exception {k!r} is not a Median-only key, ignored")
    group2 = [k for k in texts if k not in d2r_keys and k not in exceptions]
    all_ids = [e["id"] for files in folders.values() for recs in files.values() for e in recs]
    used16 = {i & 0xffff for i in all_ids}
    next_id = max(i for i in all_ids if i < 0x10000) + 1
    exc_ids: dict[str, int] = {}
    for k in exceptions:
        while next_id in used16:
            next_id += 1
        if next_id > 0xffff:
            raise SystemExit("tbl2json: more native exceptions than free 16-bit string IDs")
        exc_ids[k] = next_id
        used16.add(next_id)

    def lang_rec(k: str) -> dict:
        return {lang: texts[k] if (lang == "enUS" or not args.english_only) else "" for lang in LANGS}

    group1 = len({k for k in texts if k in d2r_keys})
    report.insert(2, f"- Groups: {group1} group 1 (D2R keys, native overrides), {len(group2)} group 2 "
                     f"(`{NAMESPACE}:` namespace), {len(exceptions)} exceptions (native, fixed ID)")
    for folder, files in folders.items():
        overridden, kept = set(), {}
        for recs in files.values():
            for e in recs:
                if e["Key"] in texts:
                    t = texts[e["Key"]]
                    if placeholders(t) != placeholders(e.get("enUS", "")):
                        kept[e["Key"]] = (e.get("enUS", ""), t)
                        continue
                    for lang in (["enUS"] if args.english_only else LANGS):
                        if lang in e:
                            e[lang] = t
                    overridden.add(e["Key"])
        if exceptions:
            files.setdefault(NEW_KEYS_FILE, []).extend({"id": exc_ids[k], "Key": k, **lang_rec(k)} for k in exceptions)
        dest = OUT / "data" / "local" / "lng" / folder
        shutil.rmtree(dest / NAMESPACE, ignore_errors=True)
        for name, recs in files.items():
            write_json(dest / name, recs)
        # D2RLoader's docs name only strings/ for namespaced files; strings-legacy/ gets the same file in case the
        # loader reads it for classic-mode text too (unverified, docs/strings.md).
        write_json(dest / NAMESPACE / NAMESPACE_FILE, [{"Key": k, **lang_rec(k)} for k in group2])
        report.append(f"- {folder}: {len(files)} files, {len(overridden)} D2R keys overridden (group 1), "
                      f"{len(group2)} Median-only keys in {NAMESPACE}/{NAMESPACE_FILE} (group 2), "
                      f"{len(exceptions)} native exceptions (in {NEW_KEYS_FILE}), "
                      f"{len(kept)} kept D2R's text (placeholders differ)")
        if folder == "strings":
            report += [f"  - `{k}`: D2R {d!r} / Median {m!r}" for k, (d, m) in sorted(kept.items())]
    if exc_ids:
        report.append(f"- Exception IDs (below 65536, free in 16 bits, shared by both folders): "
                      + ", ".join(f"`{k}`={i}" for k, i in exc_ids.items()))

    # strings_idkeys.json: Median ID -> full key. Group 2 gets the namespace prefix; group 1 and exceptions stay plain.
    g2 = set(group2)
    idkeys = {sid: (f"{NAMESPACE}:{k}" if k in g2 else k) for sid, k in idkeys.items()}
    (OUT / "strings_namespace.json").write_text(
        json.dumps({"namespace": NAMESPACE, "keys": group2}, ensure_ascii=False, indent=0), encoding="utf-8")

    (OUT / "strings_idkeys.json").write_text(json.dumps({str(k): v for k, v in sorted(idkeys.items())}, indent=0),
                                             encoding="utf-8")
    (OUT / "strings_report.md").write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())

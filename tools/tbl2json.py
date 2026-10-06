"""Convert Median XL's string tables (.tbl) into D2R string JSON files.

Input:  extract/mxl/data/local/lng/eng/{string,expansionstring,patchstring}.tbl (tools/mpq_extract.py)
        extract/d2r/<build>/data/local/lng/{strings,strings-legacy}/*.json (tools/casc_extract.py)
Output: work/mod/data/local/lng/strings/*.json and strings-legacy/*.json (complete copies of D2R's native
        files: a mod's string file replaces the game's file), work/mod/strings_idkeys.json, strings_report.md

String IDs: string.tbl index, patchstring 10000 + index, expansionstring 20000 + index (what D2Sigma's tables
and code use). Rules:
  * Strings Median only looks up by ID share the throwaway key "x" (or none); they get the unique key
    "mxl_<id>". Numeric keys ("115" = "Axe (1)") are unique and used by item namestr cells, so they stay.
    strings_idkeys.json maps every Median ID to its final key.
  * A key in several tables: patchstring wins over expansionstring over string (1.13c lookup order).
  * Keys D2R already has are overridden (every occurrence, every file) unless the two texts' printf
    placeholders differ: D2R formats some strings itself (e.g. sysmsg7-9, the "was slain by" fragments it
    joins to names and then passes through sprintf), so a Median "%s" there reads a missing argument and
    crashes the game. Those keys keep D2R's text (listed in strings_report.md). New keys are appended to NEW_KEYS_FILE
    with IDs above D2R's highest. Native loading (no D2RLoader namespace), so table cells keep plain keys.
  * Median ships English only. By default its text fills every language (otherwise other clients would show
    vanilla wording that contradicts Median); --english-only changes enUS alone.

Usage: python tools/tbl2json.py [--english-only]
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TBL = ROOT / "extract" / "mxl" / "data" / "local" / "lng" / "eng"
D2R_LNG = ROOT / "extract" / "d2r" / "93847" / "data" / "local" / "lng"
OUT = ROOT / "work" / "mod"
TABLES = (("string", 0), ("patchstring", 10000), ("expansionstring", 20000))
PRECEDENCE = ("patchstring", "expansionstring", "string")  # first wins for duplicate keys
NEW_KEYS_FILE = "item-modifiers.json"
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
        for i, line in enumerate((old_dir / f"{name}.txt").read_text("utf-8").splitlines()):
            key, _, old = line.partition("\t")
            agree += (key, old.replace("\\n", "\n")) == by_id[base + i]

    report = ["# tbl2json report", "",
              f"- Median strings: {len(by_id)} (string {sum(1 for s in by_id if s < 10000)}, patchstring "
              f"{sum(1 for s in by_id if 10000 <= s < 20000)}, expansionstring {sum(1 for s in by_id if s >= 20000)})",
              f"- Keys given an ID-based name (\"x\" or empty): {sum(1 for k in idkeys.values() if k.startswith('mxl_'))}",
              f"- Duplicate keys resolved (patch > expansion > string): {shadowed} entries shadowed",
              f"- Agreement with the first extraction: {agree}/{len(by_id)} strings identical",
              f"- Languages: {'enUS only' if args.english_only else 'English text in all 13 languages'}"]

    folders = {f: {p.name: json.loads(p.read_text("utf-8-sig")) for p in sorted((D2R_LNG / f).glob("*.json"))}
               for f in ("strings", "strings-legacy") if (D2R_LNG / f).exists()}
    # One ID per new key, the same in both string sets. D2R matches string IDs by their low 16 bits (bug #7: Median's
    # 262189 "ArcaneApe" answered for 45, GheedIntroGossip1), so new IDs stay below 65536, start after D2R's own
    # (next_string_id.txt: 28106; commands.json's lone 251780 is left out) and skip every 16-bit value in use.
    all_ids = [e["id"] for files in folders.values() for recs in files.values() for e in recs]
    used16 = {i & 0xffff for i in all_ids}
    next_id = max(i for i in all_ids if i < 0x10000) + 1
    new_ids: dict[str, int] = {}
    for files in folders.values():
        d2r_keys = {e["Key"] for recs in files.values() for e in recs}
        for k in texts:
            if k not in d2r_keys and k not in new_ids:
                while next_id in used16:
                    next_id += 1
                if next_id > 0xffff:
                    raise SystemExit("tbl2json: more new strings than free 16-bit string IDs")
                new_ids[k] = next_id
                used16.add(next_id)
    for folder, files in folders.items():
        d2r_keys = {e["Key"] for recs in files.values() for e in recs}
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
        new = [k for k in texts if k not in d2r_keys]
        home = files.setdefault(NEW_KEYS_FILE, [])
        for k in new:
            rec = {"id": new_ids[k], "Key": k}
            for lang in LANGS:
                rec[lang] = texts[k] if (lang == "enUS" or not args.english_only) else ""
            home.append(rec)
        dest = OUT / "data" / "local" / "lng" / folder
        dest.mkdir(parents=True, exist_ok=True)
        for name, recs in files.items():
            text = json.dumps(recs, ensure_ascii=False, indent=2).replace("\n", "\r\n")
            (dest / name).write_text("﻿" + text, encoding="utf-8", newline="")
        report.append(f"- {folder}: {len(files)} files, {len(overridden)} D2R keys overridden, {len(new)} new keys "
                      f"(in {NEW_KEYS_FILE}), {len(kept)} kept D2R's text (placeholders differ)")
        if folder == "strings":
            report += [f"  - `{k}`: D2R {d!r} / Median {m!r}" for k, (d, m) in sorted(kept.items())]
    report.append(f"- New-key IDs {min(new_ids.values(), default=0)}..{max(new_ids.values(), default=0)} (below 65536, free in 16 bits), shared by both folders")

    (OUT / "strings_idkeys.json").write_text(json.dumps({str(k): v for k, v in sorted(idkeys.items())}, indent=0),
                                             encoding="utf-8")
    (OUT / "strings_report.md").write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())

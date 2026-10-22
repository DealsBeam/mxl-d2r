# Strings: tbl2json

Tool: `tools/tbl2json.py` (reads Median's `.tbl` files directly; output in `work/mod/`, not committed).

- Median ships 21,268 English strings in three tables. String ID = string.tbl index, patchstring 10000 + index,
  expansionstring 20000 + index (what D2Sigma's code and tables use; see docs/sigma-tables.md).
- Text is UTF-8 inside the `.tbl` files. The parse agrees with the first extraction on 21,267/21,268 strings
  (the one difference contains a literal backslash-n that the extraction's text format cannot distinguish).
- **ID-only strings:** 532 strings carry the throwaway key `x` (or none). Median looks them up by ID only. They
  get the key `mxl_<id>`. The 1,414 numeric keys (`115` = "Axe (1)") are unique and used by item `namestr`
  cells, so they stay as they are;
  `work/mod/strings_idkeys.json` maps every Median ID to its final key (for Sigma tables and plugins).
- **Duplicate keys:** 377 entries are shadowed; patchstring wins over expansionstring over string (1.13c order).
- **Loading (hybrid, contract: docs/strings-namespace.md):**
  - **Group 1, overrides (8,375 keys):** keys D2R already has load natively. The output files are complete copies of
    D2R's native string files (a mod file replaces the game's file) in both `strings/` and `strings-legacy/`, with
    Median's text and D2R's own IDs (8,079 / 8,075 overridden; 296 / 295 keep D2R's text because the printf
    placeholders differ, listed in `strings_report.md`). Table cells keep plain keys (= `d2r:`).
  - **Group 2, Median-only (12,516 keys):** written to `lng/strings/medianxl/median.json` as
    `{"Key", <13 languages>}` with no `id`; full key `medianxl:<key>`. `install_mod.py` writes
    `"namespace": "medianxl"` into `d2rloader/metadata.json`. No numeric ID, so no collision with D2R's IDs (bug #7).
    `work/mod/strings_namespace.json` (`{"namespace", "keys"}`) lists them for the other tools.
  - **`strings_idkeys.json`:** Median ID -> final key; `medianxl:<key>` for group 2, the plain key otherwise.
  - **Exceptions:** group 2 keys listed in `tools/strings_native.json` (`{"keys": [...]}`) stay native: appended to
    `item-modifiers.json` with a fixed ID below 65,536 (the first free 16-bit value above D2R's highest, skipping every
    low-16-bit value in use) and a plain key in the idkeys. Currently none; the list is filled by the plugin lanes
    (docs/strings-namespace.md "Exceptions").
  - **`strings-legacy/`:** the same `medianxl/median.json` is written there too. D2RLoader's documentation
    (`LOCALIZATION.md`) only names `data/local/lng/strings/` for namespaced files and says nothing about
    `strings-legacy/`, so whether the loader reads the legacy copy (for classic-mode text) is unverified; the extra
    copy is harmless if it is ignored. `tbl2json.py` prints a per-folder count in `strings_report.md`.
  - **Packaging:** the mod is installed as a folder-style MPQ (a directory), so no `(listfile)` is needed. If it is ever
    packed into a real `.mpq`, the namespace file (an extra filename) needs a `(listfile)` entry.
- **Languages:** Median is English-only. By default the English text fills all 13 languages so non-English
  clients do not show vanilla text that contradicts Median; `--english-only` changes enUS alone.
- Pool: ~24,700 strings in total, well inside the 65,535 two-byte table-reference pool.

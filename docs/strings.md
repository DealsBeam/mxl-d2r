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
- **Loading:** native D2R loading (no D2RLoader namespace), so table cells keep plain keys. Output files are
  complete copies of D2R's native string files (a mod file replaces the game's file), in both `strings/` and
  `strings-legacy/`:
  - 8,375 D2R keys overridden with Median's text (every occurrence),
  - 12,516 new keys appended to `item-modifiers.json`, IDs 251,781..264,301 (above D2R's highest; the same ID
    for a key in both folders). D2R's own 121 duplicate IDs are left as they are.
- **Languages:** Median is English-only. By default the English text fills all 13 languages so non-English
  clients do not show vanilla text that contradicts Median; `--english-only` changes enUS alone.
- Pool: ~24,700 strings in total, well inside the 65,535 two-byte table-reference pool.

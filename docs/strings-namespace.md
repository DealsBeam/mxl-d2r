# Strings: the `medianxl` namespace (hybrid)

Decision (owner, 2026-10-22): Median-only strings move into a D2RLoader string namespace so they can never collide
with D2R's string IDs again (bug #7). D2R's own keys keep native loading. Reference: `third_party/PluginSDK/LOCALIZATION.md`.

## Contract (every lane works against this)

- **Namespace name:** `medianxl`. `install_mod.py` writes `"namespace": "medianxl"` next to `schemaVersion` in
  `d2rloader/metadata.json`.
- **Group 1, overrides (native, unchanged):** every key that exists in D2R's own string files
  (`extract/d2r/93847/data/local/lng/{strings,strings-legacy}/*.json`). tbl2json keeps writing Median's text into
  the complete copies of D2R's files, with D2R's numeric IDs. Table cells referencing them stay unqualified.
- **Group 2, Median-only (namespaced):** every key that is not in D2R's files (today the 12,516 keys appended to
  `item-modifiers.json` with IDs 28106..). They are written instead to
  `work/mod/data/local/lng/strings/medianxl/median.json` as `{"Key": "<key>", "enUS": ..., <all 13 languages>}`,
  **no `id`**. Their full key is `medianxl:<key>`; the key text itself does not change.
- **Key list:** tbl2json writes `work/mod/strings_namespace.json`: `{"namespace": "medianxl", "keys": [<group 2 keys>]}`.
  Every other tool reads this file to decide whether a key needs the prefix (until it exists, compute it the same
  way: key not in D2R's files).
- **Table cells:** every localization cell (namestr, NameStr, LevelName, LevelWarp, EntryFile, str name / short /
  long / alt, descstrpos / descstrneg / descstr2, dgrpstr*, and any other string-key column) that names a group 2
  key is written as `medianxl:<key>` by txt_migrate, in both banks. Unqualified cells always mean `d2r:`.
- **Layouts (HD JSON):** a reference to a group 2 key is written `@medianxl:<key>`.
- **Plugin / ID map:** `strings_idkeys.json` maps each Median string ID to its full key (`medianxl:<key>` for group 2).
  Code that hands D2R a numeric string ID for a group 2 string must get that number from the loader at run time
  (research lane `ns-research`); if the loader gives no stable number, the string stays native (group 1 style, fixed
  ID) and is listed in "Exceptions" below.
- **Validator:** validate.py fails on any unqualified cell or layout reference to a group 2 key, and on a
  `medianxl:` reference to a key that is not in the namespace file.

## Exceptions (strings that must stay native with a fixed ID)

(filled in by ns-research / ns-plugin)

## Generated plugin headers and layouts (ns-ui)

- `plugin/src/mxl_npc_actions.h` `Slot` has `labelKey` / `promptKey` (`const char*`, nullptr when the string is native).
  A namespaced string has `label` / `prompt` = 0 and the full key (`medianxl:<key>`) in the key field; the plugin
  resolves the key to the string D2R shows at run time (a slot whose `label` is 0 but `labelKey` is set still has a label).
- Layouts written by `tools/ui_layouts.py` qualify every reference: `@d2r:<key>` for D2R's own keys (the layouts are
  copies of D2R's, LOCALIZATION.md), `@medianxl:<key>` for Median-only keys (none today).
- `tools/strings_ns.py` is the shared helper (`load_group2()`, `qualify_layout()`, `bare()`).

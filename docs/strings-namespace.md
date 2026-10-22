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

Research ([strings-namespace-research.md](strings-namespace-research.md)) found no column where a namespaced cell fails and no
string that needs a fixed numeric ID: the loader assigns table references itself, and code that needs text calls
`getStringByKey("medianxl:<key>")` (no numeric ID exists for a group 2 string; the temporary pool numbers move with the
data). One open risk makes these keys exceptions until shown otherwise: name columns store the key in a fixed-size field
(32 bytes), and `medianxl:` takes 9. Group 2 keys longer than 23 characters in those columns stay native (group 1 style,
fixed ID, unqualified cell):

- setitems `index`: `MRAchilios' Sledgehammer`, `MRHunter's Trackless Step`, `MRVasily's Shepherd Moon`,
  `MRWitchhunter's Crucifix`, `MRZann Esu's Binding Circle`
- uniqueitems `index`: `Armor of the Old Religion`, `Body of the Forbidden One`, `Spirit Teacher's Headdress`,
  `Sunstone of the Twin Seas`, `UpgradedBossSummonerRing`

(A 36-character `medianxl:` key in `uniqueitems.index` raised no loader error, so these may turn out to work; if a later test
shows the namespaced long form displays correctly, move them back. The rule for any such column: key length + 9 <= 32.)

Plugin rule that follows from the research: a string whose numeric ID the plugin would write into game memory or pass to D2R's
lookup (`0x3e2b730`) cannot be group 2. Resolve text through the service instead; if a case turns up where only an ID works,
list the key here and keep it native.

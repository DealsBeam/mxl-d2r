# Strings namespace: research answers (lane `ns-research`)

Contract: [strings-namespace.md](strings-namespace.md). Loader docs: `third_party/PluginSDK/LOCALIZATION.md`,
`include/D2RLPlugin/localization.h`.

Evidence: the PluginSDK, strings in `D2RCore.dll` (D2RLoader 1.3.1; its code is not readable statically, the sections are
protected, so the answers come from the experiment) and in-game runs on slots 2 and 3 (2026-10-22, background mode,
D2R 3.3 build 93847). Reproduce with `tools/ns_probe.py` (runs `h str ...` in game), `tools/ns_cols.py` (column survey);
the scripts that edit a slot's installed mod and put it back are in `tools/ns_research/`. The runs are in
`%USERPROFILE%\mxl-d2r-runs\checks-20261022-135932-s2`, `-140809-s3`, `-141130-s3`, `-1431*-s2`, `-1420*-s3`.

## Summary

| # | Question | Answer |
|---|---|---|
| 1 | Numeric ID of a namespaced string | **None that code may rely on.** A namespaced key has no ID by itself. Only a key that a table cell references (`medianxl:Key`) gets a temporary two-byte number, assigned by the loader when the table loads: the lowest numbers D2R's own strings leave free, in table load order (seen 513, 700, 725, 726, 728, 729, 733, 734, 737, 747). The same data gives the same numbers on every launch (slot 2 and slot 3 agreed), but **any change to which keys the tables reference moves them** (dropping the itemstatcost cells moved the skill strings from 728.. to 513, 700, 725, 726 and the item to 729). The plugin service cannot return them: `getStringById` answers only original game IDs (`str id 513` and `str id 737` = NotFound). D2R's own numeric lookup `0x143e2b730` does resolve them (`str native 737` = the text), and also in classic graphics (G). A plugin gets Median text with `getStringByKey("medianxl:Key")` (full key; an unqualified key searches the plugin's own namespace `mxl-core` and returns NotFound). |
| 2 | Excel columns | Accepted, and all resolved at load: `namestr` (misc, armor, weapons), monstats `NameStr` and `DescStr`, skilldesc `str name / short / long / alt`, itemstatcost `descstrpos / descstrneg / descstr2 / dgrpstrpos`, levels `LevelName` (seen on the automap; also `LevelWarp`, `LevelEntry`: no loader error), uniqueitems `index`, setitems `index`, sets `name`, magicprefix / magicsuffix `Name`, rareprefix `name`, runes `Name`, superuniques `Name`, lowqualityitems `Name`, misc `spelldescstr`. Test: a nonexistent `medianxl:NoSuchKey` in the column stops startup with `String reference error ... No loaded string table defines this key` (the column is a localization column). Not reproduced for raresuffix `name` (one run without a complaint, it is the sibling of rareprefix, which complained). **No column was found where a namespaced cell does not work**; the limit is the field width of the name columns (below). |
| 3 | 13 languages | Not required by the loader (a record needs one translation), but a missing language does **not** fall back to English: a key defined only in `deDE` gave `NotFound` from `getStringByKey` in an `enUS` game and the cell shows **"Missing string"**. Keep all 13 languages in every record (tbl2json already copies English into the other 12). |
| 4 | `"namespace"` and the mod's other string files | **Group 1 keeps working unchanged.** With the namespace on, the complete D2R copies still override D2R's keys (`d2r:bhm` = Median's "Bone Helm"; `str id 1973` and the native lookup agree; vanilla says "Spiny Shield"). Other records in those files whose key is not in D2R's files (Median-only keys with the old IDs 28106..) are **no longer native strings**: `native 28106` = "Missing string", and `medianxl:mxl_0` exists (BufferTooSmall for a 1,024-byte buffer, i.e. found) while `d2r:mxl_0` = NotFound. So the loader files unqualified Median-only records under `medianxl:` and ignores their IDs; they could even stay where they are, but the contract (median.json, key list) is cleaner. Lookups through the mod namespace also see D2R's keys (`medianxl:strCancel` = "CANCEL", `medianxl:bhm` = Median's text). |
| 5 | 65,535 pool | Namespaced strings that no table references take no slot at all (the unreferenced test keys had no number; `getStringByKey` still finds them). A referenced key takes one number of the free low-16-bit space: D2R uses 12,117 distinct IDs, so about 53,400 are free. Median's tables reference at most about 7,650 Median-only keys (counted over the string columns of `work/mod`), so the pool is no constraint. A full pool or a missing key stops startup with an error (`Excel TXT table could not be loaded: ... a localization key is missing or the two-byte string reference pool is full`), it never wraps. |

## What the plugin and the UI lanes must do

- **Never** hand D2R a numeric ID taken from `strings_idkeys.json` for a group 2 string, and never save a pool number: it only
  exists while the tables that reference the key are loaded, and it shifts with the data. (`native 28106` is "Missing
  string" under the namespace.)
- Ask the loader at the moment of use: `LocalizationService::getStringByKey(context, "medianxl:<key>", buf, size, &required)`,
  copy the text, hand the `const char*` to the UI function. The NPC menu line adder `0x1e9030` takes text, not an ID
  (`plugin/src/npc_actions.cpp` calls the lookup thunk `0x3e2b730` only to get that text), so Median's menu lines can be
  resolved by key. Query the service once in `LoadPlugin` (`context->QueryService(&localization)`; the
  `HasLocalizationServiceField` check as in the SDK sample). Not tested: that `AddListLine` copies the text (read the
  function before passing a stack buffer).
- Language: `getStringByKey` uses the game's current language and refreshes on a language change; call it per use, not at
  startup.
- Harness: `h str key <full key>`, `h str id <n>`, `h str native <n>` (D2R's numeric lookup, 0..65535), `h str find <text>`
  (scans 0..65535 for text; this is how the temporary numbers were read).

## Evidence

Test data (`tools/ns_research/prep_cells.py`): `"namespace": "medianxl"` in `metadata.json`, `strings/medianxl/median.json`
with 14 records (13 languages, text `NSTEST_<Key>`), table cells set to `medianxl:<Key>`: misc `Healing Potion` namestr
(NsItem), armor `crn` namestr (NsItemDe, deDE only), monstats `zombie1` NameStr (NsMon), levels `Rogue Encampment`
LevelName / LevelWarp / LevelEntry and `Blood Moor` LevelName, skilldesc `attack` str name / short / long / alt, itemstatcost
`strength` descstrpos / descstrneg / descstr2 / dgrpstrpos; in both banks (`excel/` and `excel/base/`).

- `getStringByKey`: `medianxl:NsItem`, `NsMon`, `NsLvl*`, `NsSk*`, `NsStat*`, `NsGrpPos` all `Success`; `NsItemDe` NotFound;
  `mxl-core:NsItem` and `NsItem` NotFound; `d2r:strCancel` = "CANCEL".
- Native lookup scan for `NSTEST` (run 1): 513 NsStatPos, 700 NsStatNeg, 725 NsStat2, 726 NsGrpPos, 728..734 the four skill
  strings (729 / 733 / 734 shown), 737 NsItem, 747 NsMon; 736 = "Missing string" (the deDE-only item cell); the level
  strings have no number (levels are read by key). Same table in run 2 (other slot). Run 3 without the itemstatcost cells:
  513 NsSkName, 700 NsSkShort, 725 NsSkLong, 726 NsSkAlt, 729 NsItem, 733 NsMon.
- Automap in Blood Moor / Rogue Encampment shows "NSTEST NsLvlBM" / "NSTEST NsLvlName" in the area name (screenshots
  `ns_automap_*.png`).
- Classic graphics (G): `medianxl:NsItem` and native 737 / 513 still resolve; the namespaced file in `strings/` is enough.
- Not seen: an item tooltip or monster name bar with the namespaced text (the harness hover did not raise a tooltip). The
  cell -> pool number -> native lookup chain is what D2R uses for those names, and the lookup returned the text.
- Column survey (`tools/ns_cols.py`): see the table row 2; every listed column complained about `NoSuchKey`.

## Fixed-size name fields (risk, not settled)

LOCALIZATION.md: "Some text fields, including affix names, store the key in a fixed-size field; the full `namespace:key` must
fit". `medianxl:` is 9 characters. The name columns are 32 bytes in D2R's tables, so keys longer than 23 characters would
not fit. A 36-character `medianxl:` key in `uniqueitems.index` produced **no** loader error, so it is either stored
elsewhere or cut silently; not settled (no way to read the name back in background mode). Group 2 keys in those columns
(uniqueitems, setitems, sets, magic / rare affixes, runes, superuniques): 17 keys, **10 of them longer than 23
characters**: setitems `MRAchilios' Sledgehammer`, `MRHunter's Trackless Step`, `MRVasily's Shepherd Moon`,
`MRWitchhunter's Crucifix`, `MRZann Esu's Binding Circle`; uniqueitems `Armor of the Old Religion`, `Body of the Forbidden
One`, `Spirit Teacher's Headdress`, `Sunstone of the Twin Seas`, `UpgradedBossSummonerRing`. They are listed as
exceptions in strings-namespace.md until someone shows the long form works.

## Not covered

- `@medianxl:Key` references in HD layouts (ns-ui lane) were not exercised.
- Other languages than enUS were not run (the no-fallback result comes from the key lookup and the "Missing string" cell).
- The in-game item tooltip and monster name were not read (above).
- The slot test characters kept a Healing Potion (and on slot 3 a Crown) from the probe.

# Strings namespace: research answers (lane `ns-research`)

Contract: [strings-namespace.md](strings-namespace.md). Loader docs: `third_party/PluginSDK/LOCALIZATION.md`,
`include/D2RLPlugin/localization.h`. Evidence below comes from the PluginSDK, strings in `D2RCore.dll` (D2RLoader 1.3.1)
and an in-game experiment on slot 2 (2026-10-22, background mode, run folder `checks-20261022-135932-s2`, reproduced by
`tools/ns_probe.py`; the plugin got a new harness verb `h str`, below).

## Summary

| # | Question | Answer |
|---|---|---|
| 1 | Numeric ID of a namespaced string | **None by itself.** A namespaced key gets a number only when a table cell references it (`medianxl:Key`); the loader then hands D2R a "temporary" two-byte reference. Seen: 513, 700, 725, 726, 728, 729, 733, 734, 737, 747 (the lowest numbers D2R's own strings do not use, in table load order). The number depends on which keys the tables reference, so it is **not a stable ID** and the plugin must not hard-code or save it. The plugin service cannot return it: `getStringById` accepts original game IDs only. A plugin gets Median text with `getStringByKey("medianxl:Key")`. D2R's own numeric lookup (`0x143e2b730`) does resolve the temporary numbers. |
| 2 | Excel columns | `medianxl:Key` cells are accepted in: `namestr` (misc / armor / weapons), monstats `NameStr`, skilldesc `str name / short / long / alt`, itemstatcost `descstrpos / descstrneg / descstr2 / dgrpstrpos` (each got a pool number and D2R's native lookup returned the text). See the table in section 2 for the columns that were not confirmed. |
| 3 | 13 languages | Not required by the loader (a record needs at least one translation), but a missing language does **not** fall back: `getStringByKey` returned `NotFound` for a key that only has `deDE` while the game ran in `enUS`. Keep filling all 13 (tbl2json already fills non-English with the English text). |
| 4 | `"namespace"` and the mod's other string files | **Group 1 keeps working unchanged.** With the namespace on, Median's text in the complete D2R copies still overrides D2R's own keys (`d2r:bhm` = Median's "Bone Helm", `str id 1973` and the native lookup agree). Median-only keys left in `item-modifiers.json` with their old IDs (28106..) are **not** native strings any more (`native 28106` = "Missing string"); the loader reads them as `medianxl:` keys and ignores the IDs. |
| 5 | 65,535 pool | Namespaced strings that no table references use no slot. Referenced ones take one each from the free part of the two-byte space (about 53,400 free after D2R's 12,117 IDs). Median's tables reference about 7,650 Median-only keys at most, so the pool is no constraint. A full pool is a load error, not a wrap. |

## Plugin and UI code

- Never call D2R's numeric lookup with an ID taken from `strings_idkeys.json` for a group 2 string. Ask the loader:
  `getStringByKey(context, "medianxl:<key>")` (the plugin's own id is `mxl-core`, so an unqualified key looks in `mxl-core`
  and fails). Copy the text and hand the `const char*` to the UI function (the NPC menu line adder `0x1e9030` takes text,
  not an ID; only the Talk / Trade labels come from IDs).
- The plugin harness has `h str key <full key>`, `h str id <n>`, `h str native <n>`, `h str find <text>` (scans D2R's numeric
  lookup for 0..65535) for checking this in game.

(Sections 1-5 below are filled in with the evidence.)

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
    low-16-bit value in use) and a plain key in the idkeys. Currently none (the keys too long for a fixed-size cell use short
    aliases `mxlname_<n>` instead, txt_migrate, docs/strings-namespace.md "Exceptions").
  - **`strings-legacy/`:** the same `medianxl/median.json` is written there too. D2RLoader's documentation
    (`LOCALIZATION.md`) only names `data/local/lng/strings/` for namespaced files and says nothing about
    `strings-legacy/`, so whether the loader reads the legacy copy (for classic-mode text) is unverified; the extra
    copy is harmless if it is ignored. `tbl2json.py` prints a per-folder count in `strings_report.md`.
  - **Packaging:** the mod is installed as a folder-style MPQ (a directory), so no `(listfile)` is needed. If it is ever
    packed into a real `.mpq`, the namespace file (an extra filename) needs a `(listfile)` entry.
- **Languages:** Median is English-only. By default the English text fills all 13 languages so non-English
  clients do not show vanilla text that contradicts Median; `--english-only` changes enUS alone.
- Pool: ~24,700 strings in total, well inside the 65,535 two-byte table-reference pool.

## How the pieces fit (hybrid)

The decision and the contract every tool works against are in [strings-namespace.md](strings-namespace.md). In short:

| | Group 1: native overrides | Group 2: Median-only (`medianxl` namespace) | Exceptions |
|---|---|---|---|
| Keys | every key D2R's own string files have (8,375 here) | every other Median key (12,516) | group 2 keys listed in `tools/strings_native.json` (the ones too long for a fixed-size field, strings-namespace.md "Exceptions") |
| File | complete copies of D2R's `strings/` and `strings-legacy/` files, Median's text, D2R's ids | `lng/strings/medianxl/median.json`, no `id` | appended to `item-modifiers.json` with a fixed id below 65,536 |
| Table cell | plain key (= `d2r:`) | `medianxl:<key>` (txt_migrate) | plain key |
| HD layout | `@d2r:<key>` | `@medianxl:<key>` | `@d2r:<key>` |
| Why | D2R reads them by id and key unchanged | no numeric id, so no collision with D2R's ids (bug #7) | the whole `namespace:key` must fit a fixed-size field (`levels.LevelWarp` 39 characters, `uniqueitems.index` / `setitems.index` 31) |

`work/mod/strings_namespace.json` lists the group 2 keys; `work/mod/strings_idkeys.json` maps every Median id to its full
key. The group 2 test is shared in `tools/strings_ns.py` (`load_group2()`, `qualify()`, `qualify_layout()`).

**Plugins resolve keys at run time.** A plugin never hard-codes Median text and has no numeric id for a group 2 string. It
asks the PluginSDK localization service for the full key (`StringText("medianxl:<key>")`, `plugin/src/strings.h`) and shows
the active language. Where D2R itself wants a number (u16 string ids of an NPC menu record) the plugin hands out ids of its
own in a range it owns and answers D2R's lookup of them with the key's text. Generated headers carry keys instead of text
or ids: `mxl_npc_actions.h` `labelKey` / `promptKey`, `mxl_items.h` trophy `key`. Details: strings-namespace.md "Plugin uses".

## Validator rules (tools/validate.py)

For every localization column (`strings_ns.LOCALIZATION`: namestr, name, index, LevelName, LevelWarp, str name / short /
long / alt, descstr*, ...):

- **error** an unqualified cell that names a group 2 key (or another spelling of its case); a `medianxl:` cell whose key is
  not in the namespace list; a `d2r:` cell whose key D2R does not have; a range column (`hireling.NameFirst` / `NameLast`)
  that names a namespaced key; a cell longer than its field (the whole `medianxl:<key>` must fit; put the key into
  `tools/strings_native.json` otherwise); a native exception that is not in the native string files.
- **warn** an unqualified cell that is no known key (not D2R's, not an exception, not in Median's strings either). D2R
  looks keys up without regard to case (vanilla automagic.txt has `of Darkness` for the key `Of Darkness`), so a case-only
  difference is fine. Not a finding: a value D2R's own table holds in the same column (vanilla quirks such as the objects
  `DesertShrineArmor` or the levels `To The Pandemonium Run 1`), the `automagic.Name` labels (affixes applied without a
  name, never displayed) and `objects.Name` `dummy_<n>` placeholders. What stays are Median rows without any text in
  Median's own tables (superuniques `su90`, runes `r37` / `r41`, set items `VD_*`, ...): data gaps, not conversion bugs.
- **error** a plugin key or id that does not exist (next section).

## Plugin keys (tools/plugin_keys.py, run by validate.py and so by build.py)

Every full key the plugin source writes as a string literal must exist, so the game never shows "Missing string":
`plugin/src/mxl_npc_actions.h` `labelKey` / `promptKey`, the `mxl_items.h` trophy keys, and the hard-coded ones in
`plugin/src/strings_logic.h` (dungeon lockout, goblins). A `medianxl:<key>` must be in
`work/mod/data/local/lng/strings/medianxl/median.json`, a `d2r:<key>` in D2R's string files, a key without a namespace is
refused. The numeric D2R ids (`Slot.label` / `prompt` of native menu lines, constants annotated `// "Yes" (D2R string 3344)`)
must be ids of D2R's string files, the annotated ones with that text. Comments are ignored. A new key in plugin code needs
no registration: write the full key literal and the build checks it. The check runs against the generated string files, so
it fails the build when a key is renamed or dropped in a Median update.


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

(filled in by ns-research / ns-plugin; the list itself is `tools/strings_native.json`, read by tbl2json, txt_migrate and validate)

- **Cells that cannot hold `medianxl:<key>` (ns-tables).** Fixed-size text fields must hold the whole `namespace:key`
  (`LOCALIZATION.md`): `levels.LevelWarp` 39 characters, `uniqueitems.index` and `setitems.index` 31. 19 Median keys
  ("Sunstone of the Twin Seas", "MRVasily's Falling Star", "To The Island of the Sunless Sea", ...) would be 9
  characters too long, so they stay native with a fixed ID and their cells stay unqualified. validate.py fails when
  another key overflows its field (add it to `strings_native.json`).
- Range columns (`hireling.NameFirst` / `NameLast`: every string ID from the first to the last is a name) cannot name a
  namespaced key; they hold D2R keys only, validate.py fails otherwise.
- **Plugin:** none from the plugin. The plugin does not need a loader-assigned number: it gives D2R ids of its own (below), so every
Median-only string can stay namespaced. (`tools/strings_native.json` stays empty on the plugin's account.)

## Generated plugin headers and layouts (ns-ui)

- `plugin/src/mxl_npc_actions.h` `Slot` has `labelKey` / `promptKey` (`const char*`, nullptr when the string is native).
  A namespaced string has `label` / `prompt` = 0 and the full key (`medianxl:<key>`) in the key field; the plugin
  resolves the key to the string D2R shows at run time (a slot whose `label` is 0 but `labelKey` is set still has a label).
- Layouts written by `tools/ui_layouts.py` qualify every reference: `@d2r:<key>` for D2R's own keys (the layouts are
  copies of D2R's, LOCALIZATION.md), `@medianxl:<key>` for Median-only keys (none today).
- `tools/strings_ns.py` is the shared helper (`load_group2()`, `qualify_layout()`, `bare()`).

## Plugin uses (ns-plugin)

How the plugin gets Median text. The service is `LocalizationService::getStringByKey` (PluginSDK `localization.h`);
`getStringById` only knows D2R's original ids, and the loader's temporary table numbers are internal and exist only for
strings a table references (menu labels are referenced by `npcext.bin`, which D2R never loads), so there is no number to
ask for. Code: `plugin/src/strings.h` / `strings.cpp` (service, id hook), `strings_logic.h` (pure helpers, unit-tested in
`plugin/tests/strings_test.cpp`).

**Mechanism for numbers.** D2R's NPC menu records hold u16 string ids and the builder calls the numeric lookup
`0x143e2b730` (a `jmp [rip+disp]` thunk into D2RCore). `InstallStrings` replaces that thunk's import slot (0x3e2ae10) with
`LookupHook`. The plugin keeps a registry of up to 64 ids counting down from 0xFE00; `StringId(Ref)` registers a
`medianxl:<key>` on first use (the first id for which D2R's own lookup has no text) and the hook answers that id with the
current-language text of the key (service call at lookup time, so a language change shows at once; the key text itself when
the service cannot find it). Every other id goes to D2R. Ids are resolved lazily at the first menu build (the strings are
loaded by then), never at `LoadPlugin`.

D2R's lookup answers an id it has no string for with the text "Missing string" (ns-research); that counts as free. If no
candidate looks free, the first ids of the range are taken unchecked.

**Table shape.** A generated table says `{id, key}`: D2R's numeric id for a native string, or `id = 0` and the full key.
`mxlstr::Ref` (strings_logic.h) is that pair.

| Use | Where | Before | Now |
|---|---|---|---|
| NPC menu line labels (Akara respec `AkaraMenu1b`, Charsi imbue `NpcMenuImbue`, Cain crafting `strCainMenuCraftMaterials` on 5 NPC rows, the Cain submenu `strCainMenuCMHoradricCube`, `...CMCatalyst1-3`) | `mxl_npc_actions.h` `Slot.label` -> `npc_actions.cpp` `PatchMenuTable`, `BuildSubmenu`; built by `tools/npc_actions_data.py` | numeric D2R id (`33257`, `28265`, `40516`-`40520`) | `labelKey` = `medianxl:<key>`; `label` = 0; id from `StringId` at menu build |
| Akara's respec confirm prompt | `Slot.prompt` / `promptKey` -> `OpenConfirm` (`g_string(text)` of the Yes/No page) | numeric id `33257` | `promptKey` `medianxl:AkaraMenu1b`, id from `StringId` |
| Yes / No of that page | `npc_actions_logic.h` `StringYes` / `StringNo` | D2R ids 3344 / 3345 | unchanged (native `d2r:` strings) |
| Native menu labels (Warriv `WarrivMenu1b/1c`, Meshif, Harrogath travel, Add sockets, Personalize) | `mxl_npc_actions.h` | numeric D2R ids | unchanged, `labelKey` = nullptr |
| Trophy challenge text (20 trophies) | `mxl_items.h` `Trophy` -> `item_spells.cpp` `RevealTrophy`; built by `tools/txt_migrate.py` | English text embedded in the header | `key` = `medianxl:Desc...Trophy`; text read from the service on right click, leading number line skipped (`SkipLeadingNumberLine`), split into chat lines |
| Dungeon lockout: "(Required Level: %d)" (Median 11189) | `client_messages_logic.h` -> `dungeon_lockout.cpp` `TellRefusal` | hard-coded English | `medianxl:mxl_11189` template + `Substitute`; English stays as fallback when the service is missing |
| Sealed level text (11192, two lines) | `dungeon_lockout.cpp` | two hard-coded lines | `medianxl:mxl_11192`, split into chat lines in text order |
| "Closing in: %s" (11191) and the exit warning (11193), chat line and the on-screen timer | `dungeon_lockout.cpp` | hard-coded English | `medianxl:mxl_11191` / `medianxl:mxl_11193` |
| Goblin notice suffix " appears!" (12320 `StrPushNotifGoblinAppear`) | `goblins.cpp` | hard-coded English | `medianxl:StrPushNotifGoblinAppear` |
| Stat columns (`stat_columns.cpp`, `mxl_statcols.h`) | | numbers and stat ids only | no string use |
| Build-time readers of `strings_idkeys.json` | `tools/npc_actions_data.py` (ns-ui); `tools/txt_migrate.py` trophies use `strings_ns.qualify` | | emit keys, see above |

**Kept in English (no Median string exists):** the quest-condition refusal ("You have not finished the quest for this
area"), "Requires <difficulty> difficulty" / "Only for the <class>" lines of the entry refusal, the quest reward notice
("You have received: ..."), and the goblin's name ("Treasure Goblin"; the real name is a monstats string the server part has
no client-side unit for). Median has no text for these; add a key when one is found.

**Caveats.** The chat lines are built where the server part runs, which is the player's own game in single player; in a
hosted game the text is the host's language. `StringText` copies into the caller's buffer, so a language change shows
with the next message. No chat or menu text is cached.

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

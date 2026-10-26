# Player releases

Owner decision 2026-10-26: the only supported install path is our own launcher (lane pub-launcher). It keeps a private
copy of D2R build 93847 with D2RLoader 1.3.1 and the mod inside, so the player's real D2R install and their other mods
are never touched. Mod managers (D2RMM and others) and Nexus are not supported. The zip stays as a manual / offline
fallback.

## What a release is

`py -3 tools/package_release.py` (on the game PC, after `tools/build.py`) writes into `dist/`:

| File | What |
|---|---|
| `release-manifest.json` | the launcher's feed (format below). Fixed name, so `.../releases/latest/download/release-manifest.json` always finds the newest one |
| `MedianXL-D2R-<version>.zip` | top folder `MedianXL/`, unzipped into `<D2R>\mods\` it is the folder `tools/install_mod.py` writes |
| `RELEASE_NOTES-<version>.md` | skeleton: requirements, install steps; fill in Changes / Known issues by hand |
| `SHA256SUMS` | sha256 of the three files above (`sha256sum -c` format) |

Layout of the mod folder (the zip's `MedianXL/`, the manifest's paths):

```
MedianXL.mpq/modinfo.json          {"name": "MedianXL", "savepath": "MedianXL/"}
MedianXL.mpq/data/...              install_mod.build_data: work/mod/data + global/dataversionbuild.txt (93847) + Median's
                                   tiles (ds1 ids renumbered) + HD scenes (maps_hd) + Median-only classic art
modinfo.json                       copy at the mod root (D2RLoader guide layout)
d2rloader/metadata.json            schemaVersion 1, namespace "medianxl", metadata.modVersion = <version>,
                                   metadata.website = https://github.com/Nugwa628/mxl-d2r
d2rloader/config/d2rloader.toml    [d2rcore.save_format] mode = "extended", [d2rcore.stash] add_shared_tabs = 7
d2rloader/plugins/mxl-core.dll     the RELEASE plugin (cmake -DMXL_RELEASE=ON)
d2rloader/patches/                 empty
```

install_mod.py and package_release.py share the code (`install_mod.build_data`, `install_mod.write_mod_files`); a dev
install and a release differ only in the plugin build, `modVersion` / `description` / `website`, and the save folder of
slots 2 and 3.

What never goes in (package_release refuses or leaves it out):
- the test harness: the release DLL is built with `-DMXL_RELEASE=ON` (plugin/CMakeLists.txt): `testharness.cpp` and
  `harness_cmds.cpp` are not compiled, `testharness_release.cpp` has no-op entry points, so `mxl-testharness.txt` does
  nothing. package_release refuses a DLL that contains the string `mxl-testharness` or lacks the PluginVersion. The `mxl`
  console command's first line says `mxl-core <version> (release)` or `(dev)`.
- `mxl-testharness*`, `mxl-core-off.txt`, `input-bindings.toml`, `imgui.ini`, `debugsettings.json`, saves and their
  sidecars (`.d2s .d2i .d2x .d2rl .ctc .ctl .key .map .ma0-9`), `.log .dmp .pdb .orig`: the run stops if one is in the staged folder.
- byte copies of Blizzard's files: every staged data file that is identical (same path, same bytes) to
  `extract/d2r/93847/data/...` is removed (D2R reads its own); `dataversionbuild.txt` is written by us, not copied.

The default dev build is unchanged (`tools/build_plugin.ps1` -> `build/plugin/Release`, harness compiled in, off unless
`mxl-testharness.txt` exists). The release build goes to `build/plugin-release/Release`.

## Release manifest (schemaVersion 1)

Simple JSON, one object. The launcher must reject a `schemaVersion` it does not know. New fields may be added within
schemaVersion 1 (ignore unknown fields); a change of meaning or a removed field raises schemaVersion.

```json
{
 "schemaVersion": 1,
 "name": "MedianXL",
 "version": "0.70.0",
 "pluginVersion": "0.70.0",
 "released": "2026-10-26",
 "complete": true,
 "d2r": {"build": "93847", "version": "3.3.0", "imageTimestamp": "0x6ab3782c"},
 "d2rloader": {"version": "1.3.1-beta", "url": "https://d2rloader.net/download/D2RLoader-1.3.1-beta.zip",
               "size": 47652455, "sha256": "9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7",
               "signerThumbprint": "8001082E968FADAA58BF2F73EFA1810A11D0C28F"},
 "launch": {"default_mod": "MedianXL", "launch_arguments": "-txt"},
 "modFolder": "MedianXL",
 "saveFolder": "MedianXL/",
 "preserve": ["d2rloader/config/d2rloader.toml"],
 "zip": {"name": "MedianXL-D2R-0.70.0.zip", "size": 512345678, "sha256": "..."},
 "fileCount": 8802,
 "totalSize": 791000000,
 "files": [
  {"path": "MedianXL.mpq/data/global/excel/armor.txt", "size": 123456, "sha256": "..."},
  {"path": "d2rloader/plugins/mxl-core.dll", "size": 874496, "sha256": "..."}
 ]
}
```

| Field | Meaning for the launcher |
|---|---|
| `version` | the release (tag `v<version>`); `pluginVersion` is the plugin's own PluginVersion (normally equal) |
| `complete` | `false` only for a CI `--no-data` package (no `MedianXL.mpq/data`): never install it for a player |
| `d2r.build` | the D2R build the private copy must be at. `imageTimestamp` is the PE TimeDateStamp of that `D2R.exe` (the plugin checks it, `plugin/src/build_guard_logic.h`) |
| `d2rloader` | the pinned D2RLoader: download `url`, check `size` and `sha256`, then the Authenticode signer thumbprint of `D2RLoader.exe` / `D2RCore.dll` (self-signed: check the thumbprint and an intact signature, docs/setup.md step 10). Unzip into the game folder |
| `launch` | keys for the game folder's own `d2rloader/config/d2rloader.toml` (section `[d2rloader]`, where D2RLoader keeps `default_mod` and `launch_arguments`): the mod starts by itself, and `-txt` makes D2R read the mod's `.txt` tables |
| `modFolder` | every `files[].path` is relative to `<game>/mods/<modFolder>/`, with `/` separators |
| `saveFolder` | characters live in `Saved Games/Diablo II Resurrected/mods/<saveFolder>` (modinfo.json `savepath`); the launcher never writes there |
| `preserve` | files the player may edit: write them only when missing, never overwrite |
| `files` | the complete mod folder, sorted by path. Update = for each entry, download when the local file is missing or its size / sha256 differ; then delete local files under the mod folder that are not listed (except `preserve` entries, and `d2rloader/logs/`, which D2RLoader writes) |
| `zip` | the same content as one archive (fallback / first install) |

Open point for the launcher lane: where single files are downloaded from. GitHub releases cannot hold ~9,000 assets; the
manifest has no URL per file yet. Options: the launcher fetches the zip once and extracts only the changed entries
(`MedianXL/<path>`; the zip is written deterministically: sorted entries, one timestamp); or a file host keyed by path or
sha256, which would add a `filesBaseUrl` field (still schemaVersion 1).

## Wrong game build

Up to 0.69.0 the plugin skipped its main hook block on another image but still installed the item text hooks and the test
harness (outside the check). Now (plugin/src/mxl_core.cpp, `build_guard_logic.h`, unit test
`plugin/tests/build_guard_test.cpp`): the plugin goes on only when the `D2R.exe` PE timestamp is `0x6ab3782c` and
D2RLoader's build name (when it gives one) is `93847`. Otherwise it installs nothing (no hook, patch or harness), writes
an ERROR line to `d2rloader.log`, draws the line at the top of the screen through D2RLoader's overlay service, and `mxl`
prints it:

    Median XL D2R 0.69.0 needs D2R build 93847; this is 94100. The mod's engine changes are off.

(For a different `D2R.exe` with the same build number: `this is 93847 with another D2R.exe (image 0x...)`.)
D2RLoader 1.3.1 itself checks the game build at start (`D2RCore.dll build compatibility verified`, `Loaded build
configuration for <hash>` in `d2rloader.log`); a build it has no configuration for may not start at all. Not tested in
game: no other D2R build is installed here; the decision is covered by the unit test.

## Making a release

1. `py -3 tools/build.py` (needs `extract/` and the native libs, docs/setup.md), D2R build 93847 installed for
   `maps_hd` (`--game`, default `MXL_D2R_GAME` or the slot 1 folder; only read).
2. `py -3 tools/package_release.py [--version X]`: builds the release plugin (CMake on PATH), stages, checks, writes
   `dist/`. `--plugin DLL` uses a release DLL built elsewhere (it is checked); `--data-from <mods\MedianXL\MedianXL.mpq\data>`
   copies a finished data folder instead of rebuilding it.
3. Fill in `RELEASE_NOTES-<version>.md`, tag `v<version>`, upload the four files.

CI (`.forgejo/workflows/release.yml`, runner `mxl-cross`, manual dispatch or tag `v*`): no game files on the
server, so it cannot run tools/build.py. It cross-builds the release DLL, runs the packager's unit tests and
`package_release.py --no-data`, and keeps `dist/` as the artifact `mxl-d2r-release` (`MedianXL-D2R-<v>-nodata.zip`,
manifest with `"complete": false`). It checks that the release plugin builds and packs; the player zip comes from the game PC.
No GitHub workflow uses a self-hosted runner.

## Manual install (fallback)

Into a D2R folder at build 93847 that has D2RLoader 1.3.1-beta (docs/setup.md step 10):

1. Unzip `MedianXL-D2R-<version>.zip` into `<D2R folder>\mods\`: `<D2R folder>\mods\MedianXL\MedianXL.mpq\modinfo.json`
   must exist.
2. In `<D2R folder>\d2rloader\config\d2rloader.toml` (written by D2RLoader at its first start) set
   `default_mod = "MedianXL"` and `launch_arguments = "-txt"`.
3. Start `D2RLoader.exe`. Saves: `Saved Games\Diablo II Resurrected\mods\MedianXL\`.
4. Update: delete `mods\MedianXL\MedianXL.mpq\data`, unzip the new release over the folder (keep your own
   `mods\MedianXL\d2rloader\config\d2rloader.toml` if you edited it).

## Tested (2026-10-26, plugin 0.69.0, slot 3)

- `package_release.py` on the game PC: `MedianXL-D2R-0.69.0.zip` 483 MB (10,471 files, 1,241 MB unpacked); 23 files left out
  as byte copies of D2R's own (`global/excel/{,base/}` armtype, composit, monmode, monplace, objmode, plrmode, soundenviron,
  storepage, uniqueappellation, uniqueprefix, uniquesuffix and `hd/overlays/common/lightning.json`). The excel folder holds
  only `.txt` tables, so `-txt` is required.
- Release DLL (`-DMXL_RELEASE=ON`): 874 KB against 1,153 KB for the dev DLL, no `mxl-testharness` string. Zip unpacked
  into slot 3 and started: `mxl-core 0.69.0 loaded (mod MedianXL, D2R 3.3.0, image 0x6ab3782c)` and every part logs `on`,
  no test harness line. The game then quits at "Preparing frontend launch task": slots 2 and 3 are copies on drive D:,
  and D2R there only gets past the Battle.net session cookie with the dev harness's `volserial` word (docs/testing.md).
  So the release DLL can only be played from the drive the game was installed on (slot 1, or a player's own copy); the
  launcher's private copy must be made so that this does not bite (open point for lane pub-launcher).
- Same release folder with the dev DLL swapped in: `game_checks.py --slot 3 all mxl` with a new character
  (`MXL_CHAR=PubRelNew`, created by the run): PASS (75 lines, all 20 feature lines), 0 crash files. The asserts in the
  log (`BC_ASSERT: 0`, `s_StatsPool.IsEmpty()` and the pool asserts at exit) appear in 188 earlier dev runs.
- Slot 3 put back with `install_mod.py --slot 3` and its own `d2rloader.toml`; the same check PASS there.

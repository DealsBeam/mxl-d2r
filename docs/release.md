# Player releases

Owner decision 2026-10-26: the only supported install path is our own launcher (lane pub-launcher). It keeps a private
copy of D2R build 93847 with D2RLoader 1.3.1 and the mod inside, so the player's real D2R install and their other mods
are never touched. Mod managers (D2RMM and others) and Nexus are not supported. For a manual / offline install the
release has a `.7z`. Owner decision 2026-10-26: **no zip files**, 7z only; the launcher installs from single-file
objects ("Hosting layout") and has no archive fallback.

## What a release is

`py -3 tools/package_release.py` (on the game PC, after `tools/build.py`) writes into `dist/`:

| File | What |
|---|---|
| `release-manifest.json` | the launcher's feed (format below). `tools/publish_release.py` sends it to the download host ("Hosting layout") |
| `D2R-XL-<version>.7z` | top folder `D2R-XL/`, unpacked into `<D2R>\mods\` it is the folder `tools/install_mod.py` writes. One solid LZMA2 7z (`-mx7`, multithreaded): the manual download for players. Release 0.69.0: 355 MB (the zip it replaces was 483 MB). Needs 7-Zip (`C:\Program Files\7-Zip\7z.exe`, else `7z` / `7zz` / `7za` / `7zr` on PATH): without it the packager stops with an error before doing anything. `--no-data` (CI) writes `D2R-XL-<version>-nodata.7z` |
| `RELEASE_NOTES-<version>.md` | skeleton: requirements, install steps; fill in Changes / Known issues by hand |
| `SHA256SUMS` | sha256 of the 7z, the manifest and the notes (`sha256sum -c` format) |

`-mx9` is no smaller than `-mx7` on this data and takes longer. The download host builds its own copy of this 7z from
the objects (tools/dl_ingest.py, "Hosting layout"), so the 355 MB never go over the uploader's line.

Reproducibility: the 7z is written from the sorted staging folder with all three time stamps off (`-mtm=off -mtc=off -mta=off`) and the staged files' mtimes set to
2026-01-01, so the same input gives the same bytes on the same machine with the same 7-Zip version (checked by
`tools/test_package_release.py`, and on the 0.69.0 data, see "Tested"). It is not byte-reproducible across machines or
7-Zip versions: `-mmt=on` splits the LZMA2 stream by the number of threads (0.69.0 with 2 threads: 353,685,822 bytes, other
sha256), and 7-Zip versions may encode differently. The published manifest records the sha256 of the 7z the server
built; `files[]` (the content) does not depend on any of this.

Layout of the mod folder (the 7z's `D2R-XL/`, the manifest's paths):

```
D2R-XL.mpq/modinfo.json          {"name": "D2R-XL", "savepath": "D2R-XL/"}
D2R-XL.mpq/data/...              install_mod.build_data: work/mod/data + global/dataversionbuild.txt (93847) + Median's
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

## Release manifest (schemaVersion 2)

Simple JSON, one object. The launcher must reject a `schemaVersion` it does not know. New fields may be added within
schemaVersion 2 (ignore unknown fields); a change of meaning or a removed field raises schemaVersion. 2 (2026-10-26)
removed `zip` (and with it the launcher's zip fallback) and added `archive` (the 7z) and `emptyDirs`; a schema 1 manifest
is refused by this launcher ("too new for the release"), a schema 3 one by a launcher of today ("too old": it offers the
manifest's `launcher` as a self-update). `package_release.py` writes the fields below except `objectsBase` and `launcher`
(added by `publish_release.py`) and `archive.path` (the server's ingest, which also replaces `archive` by the 7z it built).

```json
{
 "schemaVersion": 2,
 "name": "D2R-XL",
 "version": "0.70.0",
 "pluginVersion": "0.70.0",
 "released": "2026-10-26",
 "complete": true,
 "d2r": {"build": "93847", "version": "3.3.0", "imageTimestamp": "0x6ab3782c"},
 "d2rloader": {"version": "1.3.1-beta", "url": "https://d2rloader.net/download/D2RLoader-1.3.1-beta.zip",
               "size": 47652455, "sha256": "9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7",
               "signerThumbprint": "8001082E968FADAA58BF2F73EFA1810A11D0C28F"},
 "launch": {"default_mod": "D2R-XL", "launch_arguments": "-txt"},
 "modFolder": "D2R-XL",
 "saveFolder": "D2R-XL/",
 "preserve": ["d2rloader/config/d2rloader.toml"],
 "archive": {"format": "7z", "name": "D2R-XL-0.70.0.7z", "size": 372345678, "sha256": "...",
             "path": "releases/0.70.0/D2R-XL-0.70.0.7z"},
 "emptyDirs": ["d2rloader/patches"],
 "objectsBase": "objects/",
 "launcher": {"version": "0.3.0", "path": "launcher/0.3.0/D2R-XL-Launcher.exe", "size": 12345678, "sha256": "..."},
 "fileCount": 8802,
 "totalSize": 791000000,
 "files": [
  {"path": "D2R-XL.mpq/data/global/excel/armor.txt", "size": 123456, "sha256": "..."},
  {"path": "d2rloader/plugins/mxl-core.dll", "size": 874496, "sha256": "..."}
 ]
}
```

| Field | Meaning for the launcher |
|---|---|
| `version` | the release (tag `v<version>`); `pluginVersion` is the plugin's own PluginVersion (normally equal) |
| `complete` | `false` only for a CI `--no-data` package (no `D2R-XL.mpq/data`): never install it for a player |
| `d2r.build` | the D2R build the private copy must be at. `imageTimestamp` is the PE TimeDateStamp of that `D2R.exe` (the plugin checks it, `plugin/src/build_guard_logic.h`) |
| `d2rloader` | the pinned D2RLoader: download `url`, check `size` and `sha256`, then the Authenticode signer thumbprint of `D2RLoader.exe` / `D2RCore.dll` (self-signed: check the thumbprint and an intact signature, docs/setup.md step 10). Unzip into the game folder |
| `launch` | keys for the game folder's own `d2rloader/config/d2rloader.toml` (section `[d2rloader]`, where D2RLoader keeps `default_mod` and `launch_arguments`): the mod starts by itself, and `-txt` makes D2R read the mod's `.txt` tables |
| `modFolder` | every `files[].path` is relative to `<game>/mods/<modFolder>/`, with `/` separators |
| `saveFolder` | characters live in `Saved Games/Diablo II Resurrected/mods/<saveFolder>` (modinfo.json `savepath`); the launcher never writes there |
| `preserve` | files the player may edit: write them only when missing, never overwrite |
| `files` | the complete mod folder, sorted by path. Update = for each entry, download when the local file is missing or its size / sha256 differ; then delete local files under the mod folder that are not listed (except `preserve` entries, and `d2rloader/logs/`, which D2RLoader writes) |
| `emptyDirs` | folders of the mod folder that hold no file (`d2rloader/patches`): the launcher creates them, the 7z has them |
| `archive` | the same content as a solid `.7z` for manual installs: `format` `"7z"`, `name`, `size`, `sha256`; `archive.path` is where the host has it. The launcher only names it in messages; it never downloads it |
| `objectsBase` | (published manifests, required by the launcher) where the file bytes are, relative to the site root: `objects/`. The bytes of `files[i]` are at `<root>/<objectsBase><sha256[0:2]>/<sha256>` |
| `launcher` | (published manifests, optional) the launcher build published with this release: `version`, `path` relative to the site root, `size`, `sha256` |

## Hosting layout (tools/publish_release.py, tools/dl_ingest.py)

Owner decision 2026-10-26: the release files live on our own server, not on GitHub (the forge needs a sign-in for
everything). A static HTTPS host serves one folder; the root URL is `https://dl.werdegames.com/mxl/` ("the site root"
below; the launcher takes it as its one setting). Setting it up: docs/hosting.md.

```
channels/<channel>.json                   what a channel (stable, test) points to; the only file the launcher polls
releases/<v>/release-manifest.json        the packager's manifest + objectsBase, launcher, and the server's archive entry
releases/<v>/D2R-XL-<v>.7z          the manual download for players, built on the server (-nodata.7z for a CI package)
releases/<v>/SHA256SUMS                   sha256 of the 7z, the manifest, the notes (sha256sum -c format)
releases/<v>/RELEASE_NOTES-<v>.md
objects/<sha256[0:2]>/<sha256>            the raw bytes of a file, named by its sha256; shared by every version
launcher/<v>/D2R-XL-Launcher.exe       and D2R-XL-Launcher.exe.sha256
assets/<files>                            the asset pack (lane pub-assets)
```

Next to the site root, not served: `incoming/` (on the server `/srv/dl/incoming`, the site is `/srv/dl/mxl`), where
uploads wait for the server's check.

Channel file (`schemaVersion` 1, rules as for the manifest):

```json
{"schemaVersion": 1, "version": "0.70.0", "manifest": "releases/0.70.0/release-manifest.json",
 "manifestSha256": "...", "publishedAt": "2026-10-26T12:00:00Z"}
```

What the launcher does (all URLs are `<root>/` + the relative path):

1. `GET channels/<channel>.json` (the host sends `Cache-Control: no-cache`); reject an unknown `schemaVersion`.
2. `GET <manifest>`; its sha256 must equal `manifestSha256` (this also detects a half-updated host or a stale cache);
   `schemaVersion` must be 2.
3. For each entry of `files` that is missing locally or differs in size / sha256: `GET <objectsBase><sha[0:2]>/<sha>`, check
   size and sha256 of what arrived, then move it into place. Objects never change, so a partial download is resumed
   with a `Range` request and a cache may keep them forever. Several entries with the same sha256 are one download. Each
   object is tried 5 times (resumed, with a growing pause); then the install stops with "Download failed ... try again",
   and what was installed so far stays (the next Update continues from there). There is no other source.
4. Files under the mod folder that the release does not list are removed (except `preserve` and D2RLoader's logs);
   `emptyDirs` are created; the state file records the release.
5. Launcher self-update: `launcher.version` / `.path` / `.sha256` of the channel's manifest.

### Publishing: a delta upload, checked and finished on the server

The uploader's line is slow (about 160 KB/s up) and every SSH login costs seconds, so `publish` sends only what the host
lacks, packed, in as few logins as possible, and the server does everything else:

1. One login lists `objects/` (only the prefix folders the release uses), the release folder and `incoming/`.
2. The objects the host lacks (or has with another size) go into one solid 7z (`-mx7`), entries named by their sha256:
   `objects-<sha256 of the 7z, 16 hex>.7z`. It is kept in `dist/upload/<v>/` so a re-run sends the same bytes.
3. One login uploads, into `incoming/<v>.uploading/`: that 7z, the manifest (without `archive`, plus `objectsBase` and
   `launcher`), the notes and the launcher exe. Each under a temp name and renamed; the 7z under the fixed name
   `<name>.part-0`, continued with `reput` when a dropped connection left part of it. Then `READY` (JSON: version, channel,
   size and sha256 of every file, the 7z's name, `force`), **last**, and the folder is renamed to `incoming/<v>/`. When the
   connection drops, the upload is retried (5 times, a growing pause), each time resuming the 7z; when all fail, running
   the same command again resumes.
4. On the server, a systemd path unit sees the rename and runs `tools/dl_ingest.py` as the upload user. It checks every
   file against `READY`, the manifest (schema 2, safe paths, sizes), lists the objects 7z before unpacking it (plain files
   named by a sha256 the manifest uses, with that size; no folders, links or other names), unpacks it into
   `incoming/<v>/.work/`, checks every object's sha256 against its name and moves them into `objects/`. Every object the
   manifest names must then be on the host. It links the player's `D2R-XL/` folder together from the objects (each one
   hashed again) and packs `releases/<v>/D2R-XL-<v>.7z` with the packager's switches, writes the notes, the manifest
   (with `archive` of that 7z), `SHA256SUMS`, `launcher/<lv>/`, and the channel file **last**. Then it deletes
   `incoming/<v>/` and `*.part-*` files older than an hour anywhere in the site (leftovers of dead uploads).
5. Anything that fails a check is refused: nothing is published, the reason goes to the journal and to
   `incoming/<v>/REFUSED`, the folder stays for a look, and the next `publish` of that version says why
   (`publish --force` deletes it and uploads again). `dl_ingest.py --retry <v>` tries a refused upload again.

Rules the launcher can rely on:

- The channel file is written last, after the objects, the 7z and the manifest are in place; a half-finished publish is
  never visible. Every file is written under a temp name and renamed.
- A published version never changes (`releases/<v>/` and the objects it names): the ingest refuses another manifest under
  a published version unless the upload says `force` (`publish --force`). A new build is a new version.
- Objects are only added by the ingest; `gc --keep N` removes releases older than the newest N and the objects only they
  used, and never a version a channel points to.

```
py -3 tools/publish_release.py publish --version 0.70.0 --channel test --target sftp://user@host/mxl [--launcher EXE] [--assets DIR]
py -3 tools/publish_release.py promote 0.70.0 --channel stable --target ...
py -3 tools/publish_release.py list | gc --keep 3 [--dry-run] --target ...
py -3 tools/publish_release.py verify --url https://dl.werdegames.com/mxl/ --channel stable   # fetch + sha256 over HTTPS
py -3 tools/serve_dir.py <folder>                                                              # local Range-capable test host
python3 tools/dl_ingest.py --root <folder that holds incoming/ and mxl/> [--retry VERSION]     # what the server runs
```

`publish` reads the file bytes from the packager's 7z (the staging folder is deleted after packaging; it unpacks it once
into `dist/upload/<v>/`), checks every object against the manifest while packing, and refuses a `complete: false` package
for the stable channel (`--allow-incomplete` lets one onto `test`). It answers with one of: uploaded (the server takes it
from there, usually within a minute plus the time to build the 7z), waiting (an upload of this version is already in
`incoming/`), or published already (then it only sets the channel). `promote` and `gc` write the site directly
(the channel file, deletes). Targets: `dir:<path>` (a local folder: tests, or a run on the server; `<path>/../incoming`
is the incoming folder) and `sftp://user@host[:port]/<path>` (OpenSSH `sftp`, key login, no shell needed on the server).
`verify` downloads the channel, the manifest, every object (or `--sample N` of them) and the 7z and checks every sha256.

Tests: `tools/test_publish_release.py` (delta selection, READY last and the folder rename, two sftp logins for a
publish, resume after a dropped connection, a refused upload and `--force`, gc safety, the sftp backend against
`tools/fake_sftp.py`, publish -> ingest -> `serve_dir.py` -> `verify`) and `tools/test_dl_ingest.py` (every refusal: a
damaged file, an object with a wrong sha256, a missing object, an entry that is no object, a link, a file READY does not
list, bad paths, old schema; the channel written last; re-runs; temp-file cleanup; the 7z it builds equals the packager's).
Both use 7-Zip when it is installed, else `tools/fake_7z.py`.

## Wrong game build

Up to 0.69.0 the plugin skipped its main hook block on another image but still installed the item text hooks and the test
harness (outside the check). Now (plugin/src/mxl_core.cpp, `build_guard_logic.h`, unit test
`plugin/tests/build_guard_test.cpp`): the plugin goes on only when the `D2R.exe` PE timestamp is `0x6ab3782c` and
D2RLoader's build name (when it gives one) is `93847`. Otherwise it installs nothing (no hook, patch or harness), writes
an ERROR line to `d2rloader.log`, draws the line at the top of the screen through D2RLoader's overlay service, and `mxl`
prints it:

    D2R-XL 0.69.0 needs D2R build 93847; this is 94100. The mod's engine changes are off.

(For a different `D2R.exe` with the same build number: `this is 93847 with another D2R.exe (image 0x...)`.)
D2RLoader 1.3.1 itself checks the game build at start (`D2RCore.dll build compatibility verified`, `Loaded build
configuration for <hash>` in `d2rloader.log`); a build it has no configuration for may not start at all. Not tested in
game: no other D2R build is installed here; the decision is covered by the unit test.

## Making a release

1. `py -3 tools/build.py` (needs `extract/` and the native libs, docs/setup.md), D2R build 93847 installed for
   `maps_hd` (`--game`, default `MXL_D2R_GAME` or the slot 1 folder; only read).
2. `py -3 tools/package_release.py [--version X]`: builds the release plugin (CMake on PATH), stages, checks, writes
   `dist/`. `--plugin DLL` uses a release DLL built elsewhere (it is checked); `--data-from <mods\D2R-XL\D2R-XL.mpq\data>`
   copies a finished data folder instead of rebuilding it.
3. Fill in `RELEASE_NOTES-<version>.md`, tag `v<version>`.
4. `py -3 tools/publish_release.py publish --version <version> --channel test --target sftp://...` (when a launcher or asset
   pack goes along: `--launcher`, `--assets`), look at it with the launcher on the test channel, then
   `promote <version> --channel stable`. "Hosting layout" above; server setup in docs/hosting.md.

CI (`.forgejo/workflows/release.yml`, runner `mxl-cross`, manual dispatch or tag `v*`): no game files on the
server, so it cannot run tools/build.py. It cross-builds the release DLL, runs the packager's unit tests and
`package_release.py --no-data`, and keeps `dist/` as the artifact `mxl-d2r-release` (`D2R-XL-<v>-nodata.7z`,
manifest with `"complete": false`; the runner needs 7-Zip, the job checks for it first). It checks that the release plugin
builds and packs; the player release comes from the game PC.
No GitHub workflow uses a self-hosted runner.

## Manual install (fallback)

Into a D2R folder at build 93847 that has D2RLoader 1.3.1-beta (docs/setup.md step 10):

1. Unpack `D2R-XL-<version>.7z` into `<D2R folder>\mods\` (Windows 11 opens .7z files; otherwise use
   [7-Zip](https://www.7-zip.org)): `<D2R folder>\mods\D2R-XL\D2R-XL.mpq\modinfo.json` must exist.
2. In `<D2R folder>\d2rloader\config\d2rloader.toml` (written by D2RLoader at its first start) set
   `default_mod = "D2R-XL"` and `launch_arguments = "-txt"`.
3. Start `D2RLoader.exe`. Saves: `Saved Games\Diablo II Resurrected\mods\D2R-XL\`.
4. Update: delete `mods\D2R-XL\D2R-XL.mpq\data`, unpack the new release over the folder (keep your own
   `mods\D2R-XL\d2rloader\config\d2rloader.toml` if you edited it).

## Tested (2026-10-26, plugin 0.69.0, slot 3)

- 7z (lane pub-7z, 7-Zip on the game PC, release 0.69.0 data unpacked from the earlier zip): `package_release.py` wrote the
  same zip as before (482,667,265 bytes, same sha256) and `D2R-XL-0.69.0.7z` 354,928,967 bytes (355 MB, 69 s of the
  2 min 14 s run); a second run gave the same sha256. `7z t` ok, and unpacking it gave all 10,471 files with the
  manifest's sizes and sha256 plus the empty `d2rloader/patches/`. The same data with `-mmt=2`: 353,685,822 bytes, 200 s.

- `package_release.py` on the game PC: `D2R-XL-0.69.0.zip` 483 MB (10,471 files, 1,241 MB unpacked); 23 files left out
  as byte copies of D2R's own (`global/excel/{,base/}` armtype, composit, monmode, monplace, objmode, plrmode, soundenviron,
  storepage, uniqueappellation, uniqueprefix, uniquesuffix and `hd/overlays/common/lightning.json`). The excel folder holds
  only `.txt` tables, so `-txt` is required.
- Release DLL (`-DMXL_RELEASE=ON`): 874 KB against 1,153 KB for the dev DLL, no `mxl-testharness` string. Zip unpacked
  into slot 3 and started: `mxl-core 0.69.0 loaded (mod D2R-XL, D2R 3.3.0, image 0x6ab3782c)` and every part logs `on`,
  no test harness line. The game then quits at "Preparing frontend launch task": slots 2 and 3 are copies on drive D:,
  and D2R there only gets past the Battle.net session cookie with the dev harness's `volserial` word (docs/testing.md).
  So the release DLL can only be played from the drive the game was installed on (slot 1, or a player's own copy); the
  launcher's private copy must be made so that this does not bite (open point for lane pub-launcher).
- Same release folder with the dev DLL swapped in: `game_checks.py --slot 3 all mxl` with a new character
  (`MXL_CHAR=PubRelNew`, created by the run): PASS (75 lines, all 20 feature lines), 0 crash files. The asserts in the
  log (`BC_ASSERT: 0`, `s_StatsPool.IsEmpty()` and the pool asserts at exit) appear in 188 earlier dev runs.
- Slot 3 put back with `install_mod.py --slot 3` and its own `d2rloader.toml`; the same check PASS there.

# D2R-XL Downloader

The product is **D2R-XL** (owner decision 2026-10-26), a port of Median XL by the Median XL team; the repository
stays `mxl-d2r`, the plugin `mxl-core.dll`.

Owner decision 2026-10-27: the 0.1.x launcher (a private copy of D2R with D2RLoader and the mod, and a Play button)
becomes the **D2R-XL Downloader**, `D2R-XL-Downloader.exe` (source in `launcher/`; the folder, the CMake target and
the `Launcher` class keep their old names). It does one thing: put the D2R-XL release into the player's own D2R, in
`<D2R>\mods\D2R-XL\`, and keep it up to date. It never starts the game and never installs D2RLoader:

1. The player installs **D2RLoader 1.3.1** from <https://d2rloader.net/> (the downloader checks for it and links there).
2. The player runs `D2R-XL-Downloader.exe` and presses **Install**.
3. The player starts D2R-XL the D2RLoader way (below, "How the player starts D2R-XL").

D2RMM, other mod managers and Nexus are neither used nor supported (docs/release.md). The release's `.7z` stays the
manual-install fallback, and the downloader uses the same `.7z` for a fresh install.

## What it does

### Finding D2R

- **Battle.net**: the Uninstall key `SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Diablo II
  Resurrected`, value `InstallLocation` (HKLM, HKCU, both views). **Steam**: `SteamPath`, `libraryfolders.vdf`, the
  `appmanifest_*.acf` named "Diablo II: Resurrected" (unit tests only: no Steam D2R on the game PC). Then the default
  folders, `C:\Program Files (x86)\Diablo II Resurrected` first.
- **D2R folder** (the button, `--game-dir` on the command line) picks another folder; it must hold `D2R.exe` and
  `Data`. The choice is saved. The old launcher's game copy (it has `d2r-xl-copy.json`) and the official Median XL's
  folders are refused.
- **Build**: `.build.info` (the `Version` of the active row, e.g. `3.3.93847`), `D2R.exe`'s FileVersion as the
  fallback. Not the release's `d2r.build` (93847): a red status line ("D2R build 91636: D2R-XL needs 93847. Update
  D2R in Battle.net.", or "... Wait for a D2R-XL update." for a newer build) and a question before Install, which is
  still allowed. The plugin itself refuses to load on another build.

### D2RLoader

Not downloaded and not installed (owner decision 2026-10-27). The downloader reads the file version of
`<D2R>\D2RLoader.exe` and compares it with the release's `d2rloader.version` (`1.3.1-beta`; equal numbers are
enough: `1.3.1`, `1.3.1.0` and `1.3.1-beta` match, `1.3.0` and `1.4.0` do not, `paths.cpp` `LoaderVersionMatches`).
Missing or another version: the status says "D2RLoader missing: D2R-XL needs D2RLoader 1.3.1-beta" (or "D2RLoader
1.2.0: ...") and a **Get D2RLoader** button opens
<https://d2rloader.net/> in the browser (`ShellExecute`). Right: "D2RLoader: 1.3.1-beta (OK)". Install works in both
cases.

### Where it writes

- **`<D2R>\mods\D2R-XL\`**: exactly the release's mod folder (`modFolder` of the manifest; one plain name, nothing
  that can leave `mods\`, `paths.cpp` `SafeModFolderName` / `ModTargetDir`). Nothing else in the D2R folder is
  written by the downloader. D2RLoader itself writes into the mod folder when the game runs (it rewrites
  `d2rloader\config\d2rloader.toml`, a `preserve` entry, and fills `d2rloader\logs\`), and into its game-wide
  `d2rloader\config\d2rloader.toml` when the player sets the default mod ("How the player starts D2R-XL").
- **`mods\D2R-XL\d2r-xl-install.json`**: the marker (`made_by` "D2R-XL Downloader", an id, the version). It is written
  before anything else, and its id must match the state's for Uninstall. A `mods\D2R-XL` without it (a manual or
  developer install) is never written into without asking: the window asks "Replace it with D2R-XL <v>?", the CLI needs
  `--replace`. A marker of the downloader with another id (state deleted, another Windows user) is taken over.
- **`mods\D2R-XL\d2r-xl-download\`**: downloads in progress (objects as `<sha>.part`, the unpacked 7z in `7z\`).
  Inside the mod folder on purpose: it is on the same volume, so every finished file is renamed into place, and
  D2RLoader lists every folder of `mods\` as a mod, so a staging folder next to `D2R-XL` would show up in its mod list.
  Removed when the install is complete. An update never deletes the marker or this folder (`RuntimeOutput`).
- **`%LOCALAPPDATA%\D2R-XL\`**: `launcher.json` (state), `launcher.log`, `download\` (the release 7z while it is
  downloaded; deleted after the install).
- Next to the exe: `D2R-XL-Downloader.exe.download` / `.old` during a self-update.

The exe runs `asInvoker`. The usual Battle.net folder lets Users write, so no administrator rights are needed. When
the folder is not writable (another install location, a locked-down PC), the message says so: "Cannot write into
<folder>: Access is denied. Run the D2R-XL Downloader as administrator (right-click it, Run as administrator), or give
your Windows user write access to the D2R folder."

D2R, D2RLoader, `Saved Games` (characters, `Settings.json`) and other mods are never written. The downloader refuses
to install, repair or uninstall while a process runs from the D2R folder (D2R or D2RLoader).

### Install, Update, Repair

One button: **Install** (nothing installed, or a foreign folder), **Update** (a newer release on the channel, or the
channel could not be read), **Up to date** (disabled). Each run:

1. `GET channels/<channel>.json`, then the manifest; its sha256 must be the channel's `manifestSha256`, its schema 2
   (docs/release.md). The channel's read errors: 404 "No release has been published on the <channel> channel yet.",
   another status "The download site answered <status> for <url>", no answer "Cannot reach the download site (<url>):
   <WinHTTP error>".
2. The plan (`manifest.cpp` `PlanInstall`): every listed file whose local copy is missing or has another sha256 is
   fetched (size and mtime from the state skip the hash of unchanged files; **Repair** hashes every file); files under
   the mod folder that the release does not list are deleted, except `preserve` entries, D2RLoader's `d2rloader/logs/`
   and `crashes/`, the marker and `d2r-xl-download/`.
3. The files come from the release **7z** or as single **objects** (below), are checked (size, sha256) and renamed into
   place one by one; the old file stays until the new one is complete. `emptyDirs` are created, the marker gets the
   version, the state records the release.

Stopping (the **Stop** button, closing the window, a killed process, a dropped line) keeps everything finished; the
next Install continues: the 7z download resumes, objects resume from their `.part`, installed files are not fetched
again.

### 7z or objects

The owner's requirement (2026-10-27): "make sure the downloader can pull the full folder as a 7z and unzip it."

- **Rule** (`app.cpp` `UseSevenZip`): the 7z when the objects to fetch add up to more bytes than the 7z (`archive.size`
  of the manifest). A fresh install of 0.69.1 is about 1.1 GB of objects against a 355 MB 7z, so it takes the 7z; a small
  update or a Repair of a few files takes the objects. `--via auto|7z|objects` forces one (tests). When the 7z cannot
  be used (an old manifest without `archive.path`, a failed download or unpack), the install continues with objects;
  `--via 7z` stops instead.
- **7z download**: `archive.path` into `%LOCALAPPDATA%\D2R-XL\download\<name>.part`, resumed with a Range request, 5
  tries with a pause of 2, 4, 8, 16 s; a 404 / 403 / 410 stops at once. Then its sha256 must be `archive.sha256`: a
  mismatch deletes the part and downloads it again (within the same 5 tries). Free space is checked first.
- **Unpack** (`src/core/sevenzip.cpp`, the LZMA SDK 25.01 C decoder in `launcher/third_party/lzma`, public domain,
  NOTICE.md): the release 7z is two solid blocks, LZMA2 with a 128 MB dictionary (`-mx7`) for the data and BCJ +
  LZMA2 for `mxl-core.dll`. A block of a single LZMA2 / LZMA / Copy coder is **streamed**: the decoder writes 1 MB at a
  time through its dictionary, cut into files as it goes; a block with a filter (the DLL's) is decoded in memory, up to
  64 MB. Only files of the plan are written, into `mods\D2R-XL\d2r-xl-download\7z\`; each one's CRC (7z) and sha256
  (manifest) are checked, then it is renamed into place. Entries the manifest does not list are skipped. The 7z is
  deleted after a successful install, kept (as `.part` or whole) when something failed.
- **Memory**: the dictionary (128 MB for the release) plus 1 MB output, 1 MB input buffer and the 7z header. Measured
  peak working set of the whole exe (CLI, 2026-10-27): **159 MB** for the fresh install on slot 2 (1,184 MB unpacked),
  75 MB for an objects update or Repair.
- **Objects**: `objects/<sha[0:2]>/<sha>`, 8 downloads in parallel (one WinHTTP session each), into
  `d2r-xl-download\<sha>.part`, resumed with Range, 5 tries each, checked, renamed. Several paths with one sha256 are one
  download.
- Both paths give the same folder (tested, "Testing").

### Uninstall

Removes `mods\D2R-XL` only, and only with the marker of this installation (`Uninstall` refuses a folder without it:
"it has no D2R-XL Downloader marker ... so the downloader did not install it"). It also clears the state and
`%LOCALAPPDATA%\D2R-XL\download`. Characters (`Saved Games\Diablo II Resurrected\mods\D2R-XL`), D2RLoader, its
configuration and other mods stay. The game-wide `default_mod = "D2R-XL"` the player set stays too (the downloader
never wrote it); the player picks another mod, or none, the same way (gear, Default Mod).

### How the player starts D2R-XL

How D2RLoader 1.3.1 picks the mod (read from its config and log on slot 2, 2026-10-27):

- `D2RLoader.exe` started with no arguments reads the game-wide `<D2R>\d2rloader\config\d2rloader.toml`, section
  `[d2rloader]`: `default_mod` and `launch_arguments` ("-mod is not allowed here"), and starts D2R with
  `<launch_arguments> -mod <default_mod>` (the log: `D2R command line: ...\D2RLoader.exe -w -mod D2R-XL`).
- The per-mod `mods\D2R-XL\d2rloader\config\d2rloader.toml` (shipped with the release, `preserve`) only overrides the
  `[d2rcore.*]` settings of the active mod ("Active mods can override these"; the `[d2rloader]` settings "Mods cannot
  override"). It cannot choose the mod.
- D2RLoader's own options set the default mod: the gear on the character screen, **Default Mod**, Apply. It writes
  `default_mod` into the game-wide file.

So the downloader writes **nothing outside `mods\D2R-XL`**: the player makes the one choice in D2RLoader, once. The
window, README and release notes say:

> How to play: start D2RLoader.exe in your D2R folder. The first time, click the gear on the character screen, set
> Default Mod to D2R-XL, Apply, and start D2RLoader again.

`-txt` is **not** needed (the 0.69.0 notes and the 0.1.x launcher set `launch_arguments = "-txt"`). Slot 2 with
`launch_arguments = "-w"` only: D2RLoader logs "TXT compilation: the mod has no custom BIN set; using current TXT files
and supported legacy BINs", compiles 139 of the mod's 142 `.txt` tables (the other three, `objtype` twice and
`base/sounds`, have no BIN in D2R) "for this launch only", and `game_checks.py all mxl` passes. The manifest's `launch`
now says `"launch_arguments": ""` (informational; nothing reads it).

### From launcher 0.1.x

The 0.1.x launcher kept a private copy of D2R (`%LOCALAPPDATA%\D2R-XL\game` or `<drive>\D2R-XL\game`, marker
`d2r-xl-copy.json`) and the D2RLoader zip in `%LOCALAPPDATA%\D2R-XL\cache`. The downloader converts its state: the copy
is remembered under `legacy`, everything else is dropped (the old D2R folder becomes the D2R folder). The window then
asks once: "The old D2R-XL launcher (0.1.x) left its own copy of D2R in <folder> ... Delete it?". Yes deletes the copy
(only when its marker's `made_by` is "D2R-XL launcher" and its id is the state's `copy_id`, never a folder that overlaps
the D2R folder) and the cache; No is remembered. CLI: `--cli remove-old-copy`. The old launcher's Settings backup
(`%LOCALAPPDATA%\D2R-XL\backup`) and log stay. The private-copy feature itself (copying D2R, the drive rule, Play, the
D2RLoader download and its signer pin, `--any-drive`) is gone.

A 0.1.x launcher sees the 0.2.0 downloader in the release manifest's `launcher` entry and offers it as a self-update; the
new exe then keeps the old file name (`D2R-XL-Launcher.exe`).

### Coexisting with the official Median XL

D2R-XL installs next to the official Median XL (the classic game with its own launcher, launcher.median-xl.com) and
never touches its files. `paths.h` `OfficialMedianXLPath` names its folders; the downloader refuses a data folder or D2R
folder there (unit test `TestCoexist`).

| | Official Median XL | D2R-XL |
|---|---|---|
| Program | `%LOCALAPPDATA%\mxl-launcher\` (`launcher.exe`, `launcher.json`, `logs.log`, `median-xl.msi`) | `D2R-XL-Downloader.exe` wherever the player put it; data in `%LOCALAPPDATA%\D2R-XL\` |
| Game files | the classic `Diablo II` folder | `<D2R>\mods\D2R-XL` |
| Saves | `%APPDATA%\MedianXL\save` | `Saved Games\Diablo II Resurrected\mods\D2R-XL` |
| Updates | `get.median-xl.com` | `dl.werdegames.com/mxl/` |
| Window class, exe, version resource | (theirs) | `D2RXLDownloader`, `D2R-XL-Downloader.exe`, FileDescription `D2R-XL Downloader`, manifest identity `D2R-XL.Downloader` |

The downloader never reads or writes `%LOCALAPPDATA%\mxl-launcher`, `%APPDATA%\MedianXL`, `%LOCALAPPDATA%\MedianXL` or a
folder named `Diablo II`, and creates no mutex, registry key, shell association or shortcut.

### Self-update

`publish_release.py --launcher EXE` puts the exe under `launcher/<v>/D2R-XL-Downloader.exe` and its `version`, `path`,
`size` and `sha256` into the release manifest. After reading the channel the window says "A newer downloader is
available: X ... Update now? The downloader updates itself" (CLI: `--cli self-update`). It is offered only when the
version is higher **and** the sha256 differs from the running exe (the 0.1.2 fix: no loop on a mislabelled exe), also
when the release is refused because it needs a newer downloader (`launcherMin`, manifest schema 3).

- Downloaded next to the running exe (`.download`); size and **sha256 must equal the manifest's** (HTTPS, the channel's
  `manifestSha256`, the manifest's sha256 of the exe). A mismatch deletes it and changes nothing.
- A signature is logged, never required: a new certificate or an unsigned build updates the same way.
- Swap: the running exe becomes `.old`, the download takes its name, the window restarts it; the next start deletes
  `.old`. A folder that is not writable gets the download URL instead.

### Window and CLI

The window (`launcher/src/win/window.cpp`) keeps the 0.1.x look: the official Median XL launcher's art (temple
background, stone panel, Exocet and Roboto Condensed, NOTICE.md) and the D2R XL logo (`launcher/res/logo.gif`).

- Title "D2R-XL Downloader 0.2.0"; the panel: D2R folder and build, D2RLoader, D2R-XL (installed / available), the mod
  folder; warnings in red. Below it the current step and a progress bar (download in MB with minutes left, unpacking in
  MB, objects as files and MB).
- A big **Install / Update** button (**Stop** while working), then **Repair**, **Uninstall** (asks first), **Get
  D2RLoader** (only when needed), **D2R folder** (browse), **Mods folder** (opens `mods\D2R-XL`, or `mods`, never
  created), and the issue-tracker icon.
- The how-to-play line, then the footer "Downloader 0.2.0 · D2R-XL, a port of Median XL ...".
- Nothing takes focus or the cursor (no `SetForegroundWindow`; the 0.1.x Play was the only user).
- `--snapshot FILE` draws the window once into a PNG after the start-up check, without showing it.

```
D2R-XL-Downloader.exe --cli status|detect|install|update|repair|uninstall|self-update|remove-old-copy
    [--data-dir DIR] [--site URL|DIR] [--channel stable|test] [--game-dir D2R_DIR] [--replace]
    [--via auto|7z|objects] [--snapshot FILE]
```

`--source` is still read as `--game-dir`. The exe is a GUI-subsystem program: from PowerShell use `Start-Process
-NoNewWindow -Wait`.

Build: `cmake -S launcher -B build/launcher -A x64 && cmake --build build/launcher --config Release`: one static exe, no
runtime to install (C++20 and the LZMA SDK's C, WinHTTP, Win32 + GDI+), `asInvoker`, long paths, per-monitor DPI. CI
(`.forgejo/workflows/ci.yml`): `launcher` cross-builds it with `plugin/cmake/xwin-toolchain.cmake` (clang-cl), and
`launcher-tests` runs `launcher/tests` with g++ on Linux. The portable core (`launcher/src/core`: sha256, JSON,
manifest and plan, paths and marker, detection parsing, 7z, TOML, SVG paths) has no Windows headers.

## Testing

Unit tests (`launcher/tests`, MSVC and g++): detection, manifest and plan, paths and marker (`TestPaths`), the 7z
decoder on `tests/data/sample.7z` (7,917 bytes, made by `tests/data/make_sample_7z.py` with 7-Zip: a streamed LZMA2
block and a BCJ block, an empty file and an empty folder; a flipped byte, a cut-off archive and junk are refused),
coexistence; 163 checks. Tools tests: `tools/test_*.py` (dl_ingest accepts both exe names, refuses two).

E2E 2026-10-27 (downloader 0.2.0, test releases 0.69.1-lt1 / lt2 from `launcher_testsite.py derive --savepath
D2R-XL-lt/` with the dev plugin, published to a local folder site, `dl_ingest.py`, `serve_dir.py`):

| Step | Result |
|---|---|
| Fresh install, 7z (scratch D2R folder) | 339 MB download + 31 s unpack (1,184 MB), 52 s; 1 streamed + 1 in-memory block, largest buffer 128 MB |
| Fresh install, objects (`--via objects`) | 8,509 objects, 56 s |
| 7z vs objects | the two folders are identical (10,474 entries, sha256 of every file; the markers differ only in their id) |
| 7z download killed at 39 MB, started again | "Resuming ... at 39 MB of 339 MB" (HTTP 206), install OK |
| A byte of the finished 7z flipped | "The downloaded 7z does not match the release manifest ...; downloading it again", install OK |
| Slot 2 (`D:\D2R-93847`, build 93847, D2RLoader 1.3.1-beta) fresh install | 7z, 35 s, peak working set 159 MB; `launcher_isolation.py`: the D2R folder (11,350 files) and Saved Games unchanged outside `mods\D2R-XL` |
| Play the player's way | `default_mod = "D2R-XL"`, `launch_arguments = "-w"`, `game_ctl.py start` (D2RLoader with no arguments, background mode, harness `volserial tag=s2`), `game_checks.py all mxl` with `MXL_ATTACH=1`: **PASS** (75 lines, 20 feature lines, 0 crash files) |
| Update lt1 to lt2 | objects: 4 files (3 objects, 9 MB), 3 files removed (`lt/dummy2.txt` and two test-harness files), the edited `d2rloader.toml` kept; the folder matches the lt2 manifest |
| Repair after one flipped byte (size and mtime kept) | Update: "up to date" (it trusts size and mtime); Repair: 1 file fetched, folder matches again |
| Uninstall | only `mods\D2R-XL` removed; D2R folder and Saved Games unchanged (snapshot compare) |

Window snapshots (`--snapshot`): not installed, installed, D2RLoader missing, wrong D2R build (kept outside the
repository).

To run a check on a game the player's way:

```
D2R-XL-Downloader.exe --cli install --game-dir D:\D2R-93847 --data-dir <test data> --site http://127.0.0.1:<port>/
MXL_SLOT=2 MXL_RUN=<lock> py -3 tools/game_ctl.py start          # D2RLoader with no arguments, background mode
MXL_SLOT=2 MXL_RUN=<lock> MXL_CHAR=<own character> MXL_ATTACH=1 py -3 tools/game_checks.py all mxl
```

`package_release` writes savepath `D2R-XL/`, the player's real save folder, so a test release needs
`launcher_testsite.py derive --savepath D2R-XL-lt/` (it sets `D2R-XL.mpq\modinfo.json`, the one D2R reads, and the
manifest's `saveFolder`).

## Release and signing

**Version.** The downloader's version is `MXL_LAUNCHER_VERSION` in `src/win/app.h` (0.2.0). CMake reads it for the code
and the exe's version resource; `publish_release.py --launcher` reads it back from the resource and refuses a different
`--launcher-version`. Raise it whenever the exe changes.

Signing is a separate, last step:

1. **Build** (CI job `launcher`): the unsigned `D2R-XL-Downloader.exe` and `D2R-XL-Downloader.exe.sha256`.
2. **Sign or not**: `tools/sign_launcher.py --exe <build>/D2R-XL-Downloader.exe --out dist-launcher` plus nothing
   (unsigned), `--command "signtool sign ... {file}"` or `--signed FILE`. It checks that a signed exe is the same
   program as the build (`tools/test_sign_launcher.py`) and writes the exe, its `.sha256` and `signing.json`.
3. **Publish**: `tools/publish_release.py publish ... --launcher dist-launcher/D2R-XL-Downloader.exe`.

The download host's `tools/dl_ingest.py` accepts the new name and the 0.1.x name `D2R-XL-Launcher.exe` and publishes the
exe under the name it was uploaded with.

## Code signing (not bought; options for the owner)

An unsigned exe from the web gets SmartScreen's "Windows protected your PC" (More info, Run anyway); Smart App Control
may block it. A signature alone does not remove the warning (reputation builds per certificate over weeks), and EV no
longer bypasses SmartScreen (Microsoft, 2024). The official Median XL launcher is signed with an SSL.com OV certificate.

| | SignPath Foundation | Azure Artifact Signing | SSL.com IV / OV certificate |
|---|---|---|---|
| Price | free for OSS | $9.99/month (Basic) | $129/year + eSigner from $180/year |
| Who | OSS projects with an OSI licence for all components | organisations in listed countries; individuals only in the US / Canada | individuals (IV) or companies (OV) |
| Our problem | the embedded Median XL art and the commercial Exocet font: likely refused | an individual outside US / CA cannot get it | none known |

Recommendation: an SSL.com IV (or OV with a company) certificate with eSigner, signing from CI. Nothing in the build has
to change for any of them.

## Open questions for the owner

1. Steam installs are untested on a real Steam D2R (none on the game PC).
2. Signing (above): which certificate, in whose name.
3. `preserve` for `d2rloader.toml`: an edited file also misses new keys a release adds.
4. Self-update: tested from the CLI; the window's question and restart were not run (a dialog would take focus).
5. Fonts: `exocet.ttf` is a commercial typeface (Emigre), embedded because the official launcher ships it; we have no
   licence of our own. Before the exe goes public: a licence, the Median XL team's word, or a free look-alike (removing
   the resource is enough: the window falls back to Georgia).
6. **The download host's `tools/dl_ingest.py` must be updated** to this version before a 0.2.0 downloader is published
   (the server copy only accepts `D2R-XL-Launcher.exe`).
7. After Uninstall, `default_mod` still names D2R-XL; what D2RLoader does with a default mod that is gone was not
   tested.

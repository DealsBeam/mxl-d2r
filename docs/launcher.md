# Median XL launcher

Goal (owner, 2026-10-26): "people should be able to use this mod without having to change anything else". The
launcher, `MedianXL-Launcher.exe` (source in `launcher/`), is the only supported install path. D2RMM, other mod
managers and Nexus are neither used nor supported (docs/release.md); the launcher's job toward them is never to
disturb them. The release zip remains a manual fallback.

What the player does: download one exe, run it, press **Play**. The first start makes a **private copy** of their
D2R install. The pinned D2RLoader and the Median XL release go into that copy only. Their own D2R folder, their other
mods and their Battle.net settings are never written.

## Research (Phase A, 2026-10-26)

### D2RLoader 1.3.1-beta: install and start

Sources: https://d2rloader.net/download.html (download page and changelog), Nexus mod 1119
(https://www.nexusmods.com/diablo2resurrected/mods/1119), and, as research into how other tools install and start
D2RLoader (not as a supported path), D2RMM Custom's `D2RLoaderInstaller.ts`, `D2RLoader.ts`, `BridgeAPI.ts`
(`prepareD2RLoaderLaunch`) and `useGameLaunchArgs.tsx`.

- The download is `https://d2rloader.net/download/D2RLoader-1.3.1-beta.zip`; `/downloads/latest` redirects (302) to
  it. The zip is 47,652,455 bytes, sha256 `9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7`, the
  same value the site lists. The site also lists the signing certificate thumbprint
  `8001082E968FADAA58BF2F73EFA1810A11D0C28F`.
- The zip holds `D2RLoader.exe`, `D2RCore.dll`, `d2rloader.mpq` and `d2rloader/config/d2rloader.toml`, plus the
  empty folders `mods/` and `d2rloader/{config,data,plugins,logs,crashes}`.
- **Install = extract into the D2R folder.** D2RMM does that, then writes `d2rloader.default_mod = "<mod>"` into the
  TOML and starts `D2RLoader.exe -mod <mod> -txt`. The mod is the folder `<game>\mods\<mod>`, which holds
  `<mod>.mpq\modinfo.json` (its `savepath` picks the save folder) and `d2rloader\plugins\*.dll`.
- **Started without arguments**, D2RLoader builds the D2R command line from its TOML: `<launch_arguments> -mod
  <default_mod>` (`d2rloader.log`: `D2R command line: ...\D2RLoader.exe -txt -w -mod MedianXL`). The launcher uses
  that: it writes `default_mod` and `launch_arguments` (the release's `launch` keys, docs/release.md) into the copy's
  TOML and starts `D2RLoader.exe` with no arguments, so a start by hand from the copy behaves the same.
- **Only 1.3.1-beta can be downloaded.** The site lists checksums of 1.0.0 to 1.3.0, but serves no files for them.
- **All D2RLoader state is per game folder** (`<game>\d2rloader\config`, `logs`, `crashes`). The only exception is
  the backups in `Saved Games\Diablo II Resurrected\D2RLoader Backups\<mod>`, which are kept per mod.
- **D2RLoader cannot run from outside the D2R folder**: it starts the `D2R.exe` next to itself. So a side-by-side
  setup needs a whole D2R folder of its own, which is the private copy below.
- **Redistribute or download? Download.** The site publishes no licence text, so we do not ship D2RLoader. The
  launcher downloads exactly 1.3.1-beta from that URL and checks the zip sha256 and every extracted file against
  pinned values (`launcher/src/win/app.h`, `app.cpp` `LoaderFiles`). It also checks the Authenticode signer of
  `D2RLoader.exe` and `D2RCore.dll`: the thumbprint must be `8001082E...C28F` and the signature intact. The
  certificate is self-signed, so an untrusted root is accepted, but a changed file is not. It never fetches
  "latest". A file with a different hash is refused.

### Finding D2R and its build

- **Battle.net**: the Uninstall key `SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Diablo II
  Resurrected`, value `InstallLocation`, in HKLM (also HKCU and the native view). D2R has no other registry keys of
  its own. The HKCU "Diablo II" keys belong to classic D2 / PD2.
- **Steam**: `HKCU\Software\Valve\Steam\SteamPath`, then `steamapps\libraryfolders.vdf` (every library), then each
  `steamapps\appmanifest_*.acf` whose `name` is "Diablo II: Resurrected", then `steamapps\common\<installdir>`.
  Steam is installed on the game PC but D2R is not on it, so this path is covered by unit tests only.
- The default folders under Program Files are checked last.
- **Build**: `.build.info`, the `Version` column of the row with `Active` = 1, e.g. `3.3.93847`. `D2R.exe`'s
  FileVersion (`3.3.93847`) is the fallback. The build must be 93847 (`launcher/src/core/detect.cpp` `CheckBuild`):
  - older: "update D2R in Battle.net"
  - newer: "Median XL needs D2R build 93847; this D2R is build X. Waiting for a Median XL update for the new build."
  - With a complete copy already made, a newer source build does not matter: the copy stays at 93847 (it is never
    patched; see "Updates of D2R" below).

### Private game copy

The owner's direction (2026-10-26): the launcher copies the player's D2R install to its own folder and installs
D2RLoader and the mod there only. Research found no blocker, but it found one rule, the drive rule below.

**Copy, not hardlinks.** Battle.net patches rewrite CASC files in place. On the game PC, `Data\data\data.024` and
`Data\data\090000000f.idx` were rewritten on 2026-10-21 10:14 under the same file names. A hardlinked copy would get
the new data under its old index, or the other way round, and break both installs. The game also writes into its own
folder at every start (`Data\data\shmem`, `Data\ecache\shmem`, `Data\osi\shmem`), and so does D2RLoader
(`Data\hd\objects\dummy\d2rloader_stash_proxy.json`). So the copy is a full copy.

**Block cloning.** The copy uses `CopyFile2`. On ReFS, including a Dev Drive, that block-clones by itself
(copy-on-write: instant and no extra space until a file changes). On NTFS it is an ordinary copy: 27.6 GB, about
40 s from NVMe to NVMe on the game PC.

**What is copied**: everything in the D2R folder except `mods\`, all D2RLoader files and folders (`D2RLoader*.exe`,
`D2RCore.dll`, `d2rloader.mpq`, `d2rloader\`), `logs\`, `crashes\`, `*.log`, `*.dmp`, the stash proxy above and our
own `*.mxlpart` / marker files (`launcher/src/core/gamecopy.cpp` `ExcludedFromCopy`). Junctions and other reparse
points are not followed.

**Drive rule (the one blocker-class finding).** D2RLoader decrypts
`%LOCALAPPDATA%\Blizzard Entertainment\ClientSdk\cookie.bin`, the Battle.net session record behind D2R's "online in
the last 30 days" check. The key is derived from the machine (MachineGuid) and the **volume serial of the drive the
loader runs from** (docs/testing.md "Parallel slots"; the plugin test harness word `volserial` fakes the serial for
slots 2 and 3, which run from D:). The folder path is not part of the key. Tested on 2026-10-26 with the release plugin
(`-DMXL_RELEASE=ON`) and no harness file, launcher-installed copies of the same build:

- **Same volume (C:, where Battle.net's install is):** the copy ran for 114 s and reached the main menu window.
  `d2rloader.log` shows `D2R startup complete.` after "Preparing frontend launch task" (the cookie step).
- **Other volume (D:):** the copy died at about 8 s, in "Preparing frontend launch task", as documented.

In both runs `cookie.bin` kept its sha256 and its mtime.

**Who writes `cookie.bin`.** There is one file per Windows user, at a fixed path; there is no file per install. D2R
writes it during a start from Battle.net (Play), when it signs in online. On the game PC its mtime
(2026-10-21 11:52:56) falls 29 s after Battle.net's `LaunchBinary: uid=osi` and inside the game process's 37 s run
(`%LOCALAPPDATA%\Battle.net\Logs`). Offline D2RLoader starts read it and leave it alone: slot 1's run on 2026-10-25 and
the two runs above did not change it. A copy therefore cannot have its own cookie. Refreshing it "from the copy" would
mean pointing Battle.net at the copy, which changes Battle.net's install record and makes the cookie unreadable for
the real install. That is not a legitimate route for us. The legitimate refresh is the normal one: play the real D2R
once from Battle.net, online. The copy on the same volume reads the new cookie, because the key is the same.

So:

- the copy must be on the same volume as the Battle.net D2R install;
- the default is `%LOCALAPPDATA%\MedianXL\game` when that is on the D2R drive, else `<D2R drive>\MedianXL\game`;
- a folder the player picks on another volume is **refused**, with a message that says why (volume serials are
  compared, so another partition of the same disk counts as another volume);
- `--any-drive` lifts the rule, for tests only;
- no volume-serial shim and nothing that touches Battle.net's session data is shipped (owner decision, 2026-10-26).

**Battle.net does not need to be running or logged in.** The slots start with Battle.net closed. What D2R needs is an
online start from Battle.net within the last 30 days (the cookie). Without one, D2R shows "You have not been online
in the last 30 days" at the main menu. Play logs a hint when `cookie.bin` is missing or older than 25 days.

**Updates of D2R.** The copy is never patched. When Battle.net updates the player's real D2R, the copy stays at
93847 and Median keeps working. A Median release for a new build will need a fresh copy: the release manifest names
its `d2r_build`, and the launcher refuses a release whose build does not match the copy.

### Shared state outside the copy

| State | Shared? | What the launcher does |
|---|---|---|
| `Saved Games\Diablo II Resurrected\Settings.json` (graphics, audio, options) | yes, by every D2R install | Neither D2R nor D2RLoader can use another settings root (D2RCore resolves it with `SHGetKnownFolderPath`). D2R writes the file only when options are changed. The first Play backs it up once to `%LOCALAPPDATA%\MedianXL\backup\Settings.json`; no swapping. |
| Saves | no | `MedianXL.mpq\modinfo.json` `savepath` = `MedianXL/`, i.e. `Saved Games\Diablo II Resurrected\mods\MedianXL`. Vanilla and other mods' saves are never touched. Uninstall keeps the saves. |
| D2RLoader backups | per save folder | `Saved Games\...\D2RLoader Backups\<savepath>` (`MedianXL`, the same folder a manual install uses) |
| Registry | none written | D2R has no registry state beyond the Uninstall key; the launcher only reads. |
| Single-instance mutex | none | D2R / D2RLoader run side by side (docs/testing.md); the launcher does not need to stop another D2R. |
| `<game>\d2rloader\config\*.toml` | per game folder | Written only in the copy. Note: `tools/install_mod.py` (the developers' path) edits `<game>\d2rloader\config\input-bindings.toml` of the folder it installs into, which is shared by every mod that D2RLoader runs there. The launcher never runs install_mod.py and never writes outside its copy. |

## Design

### Files

- `%LOCALAPPDATA%\MedianXL\launcher.json`: state (game folder, site and channel, the installed release's manifest,
  size / mtime / sha256 of every installed file).
- `%LOCALAPPDATA%\MedianXL\launcher.log`: log.
- `%LOCALAPPDATA%\MedianXL\cache\D2RLoader-1.3.1-beta.zip`: the loader download.
- `%LOCALAPPDATA%\MedianXL\backup\Settings.json`: the settings backup.
- The game copy (`...\MedianXL\game`) holds `medianxl-copy.json`, a marker (made_by, id, source, build). Uninstall
  removes a folder only when the marker is there and its id matches the state. Install refuses to put a copy into a
  non-empty folder without it.

### Steps (Install / Update)

1. **Find D2R** and check its build (above). The player can pick another folder ("Choose D2R folder").
2. **Copy**: list the files, check free space (the missing bytes + 3 GB), write the marker, copy. Each file goes to
   `<file>.mxlpart` and is renamed when complete. Files whose size and mtime already match are skipped, so an
   interrupted copy resumes (tested: killed at 23 GB, the rerun copied the last 4.5 GB in 3 s). Then the copy's
   build is checked again.
3. **D2RLoader**: download (or use the cached zip), check sha256, extract the pinned files, check the signer of the two
   binaries. The copy's `d2rloader.toml` comes from the zip's template only when missing.
4. **Median XL** from the download site (below): only new or changed files, with resume and verification. Then the
   copy's `d2rloader.toml` gets the release's `launch` keys (`default_mod = "MedianXL"`, `launch_arguments = "-txt"`)
   and `check_for_updates = false`.
5. **Play**: rewrite those keys (extra test arguments such as `-w` are appended to `launch_arguments`), then start
   `D2RLoader.exe` from the copy with no arguments. `-txt` makes D2R read the mod's text tables at every start; the
   release's excel folder holds only `.txt` tables, so it is required. The launcher waits 30 s and reports an error if
   the game is gone by then.

**Repair** runs the same steps but hashes every installed file and the loader's signatures again instead of trusting
size and mtime. Its copy check uses size and mtime. **Uninstall** removes the copy, the cache and the state. It keeps
saves, the settings backup and the log, and never touches the real D2R.

### Download site

Owner decision 2026-10-26: releases are served from our own static HTTPS host, not from GitHub or the forge (the
forge needs a sign-in for everything). The layout is lane pub-publish's (docs/release.md "Hosting layout",
`tools/publish_release.py`); the launcher's one setting is the site root, default `https://dl.werdegames.com/mxl/`
(`--site`, saved in the state; `http://`, `file://` or a plain local folder work for tests). `--channel` picks
`stable` (default) or `test`.

1. `GET channels/<channel>.json`: schemaVersion 1, `version`, `manifest`, `manifestSha256`. A login page or other
   HTML is "not usable", not a crash.
2. `GET <manifest>`; its sha256 must equal `manifestSha256` ("the site may be in the middle of an update"), and its
   `version` must be the channel's.
3. The install plan (below). For each missing or different file: `GET <objectsBase><sha[0:2]>/<sha>`. Files with the
   same content are one download. Eight workers, each with its own WinHTTP session (keep-alive). Every object goes
   to `<game>\medianxl-download\<sha>.part`, is resumed with a Range request when a part is already there, is checked
   (size, sha256; a bad one is deleted and fetched again, up to three tries), and is then renamed into place
   (`MoveFileEx` with replace; for a second path with the same content a copy goes through `.mxlpart`). The download
   folder is removed when everything is in place.
4. Objects that fail (404, three bad tries) fall back to the release zip at `zip.path`: its central directory and the
   wanted entries are fetched by byte range (entries less than 1 MB apart are merged), inflated, checked (CRC,
   sha256) and written through `.mxlpart`. A server that ignores Range (answers 200) still works: the bytes before
   each wanted offset are skipped.
5. The state file `launcher.json` gets the installed manifest and size / mtime / sha256 of every installed file
   (saved every 5 s during a download, so an interrupted install continues where it stopped).
6. `launcher` in the manifest (when published with one): a newer version is reported with its URL; the launcher does
   not replace itself yet.

D2RLoader still comes from d2rloader.net (pinned URL, size, sha256, signer), not from our host: its site publishes no
licence, so we do not redistribute it (Research, "Redistribute or download?").

How the launcher uses the manifest's fields:

- `schemaVersion` other than 1 is refused; unknown fields are ignored (a unit test has one).
- `complete: false` (a CI package without data) is refused.
- `d2r.build` must be 93847. `d2rloader.version`, `.sha256` and `.signerThumbprint` must equal the launcher's own pin;
  a release that names another D2RLoader needs a new launcher. The launcher always downloads its own pinned URL.
- `d2r.imageTimestamp` is parsed but **not compared with the file**. `0x6ab3782c` is the TimeDateStamp of the
  unpacked image in memory, which the plugin's build guard reads. The `D2R.exe` file of build 93847 has `0x6a6b96a4`.
  Found in the isolation test below; for lane pub-release, the docs/release.md wording "PE TimeDateStamp of that
  D2R.exe" should say "of the running image".
- `launch` goes into the copy's `d2rloader.toml`. `modFolder` is the zip's top folder and `<copy>\mods\<modFolder>`.
- `files` and `fileCount` / `totalSize`: paths are checked (no `..`, no drive letters, no case-insensitive
  duplicates), and the counts must match the list.
- `preserve` (the mod's `d2rloader.toml`): written only when missing, never overwritten, also not by Repair.
  Owner question: that also hides new keys a release adds to that file.
- Update rule, as docs/release.md says: fetch every entry whose file is missing or whose size / sha256 differ, then
  delete every file under the mod folder that the release does not list, except `preserve` entries,
  `d2rloader/logs/` and `d2rloader/crashes/`. Crash reports are also D2RLoader output; proposed for docs/release.md.
- `objectsBase`, `zip.path`, `launcher.path` must be safe relative paths under the site root (`objectsBase` ends in
  `/`); an absolute URL is refused.
- `launcherMin` (optional, a launcher-lane addition within schemaVersion 1): the oldest launcher that may install
  the release; an older one says "download the new launcher".

### Window and CLI

The window (`launcher/src/win/window.cpp`) is modelled on the official Median XL launcher (launcher.median-xl.com, a
Flet app: only its look is reused, not its code) and uses its art, embedded in the exe (`launcher/res`, credited in
NOTICE.md):
- the temple background (`background.png`, 900 x 600: the window's canvas), the burning logo (`logo.gif`, 55 frames,
  40 ms each) at the top;
- the stone panel (`popup-window.png`) with the status (D2R, game copy, Median XL version, D2RLoader), the current
  step and a progress bar (a moving piece while the amount is unknown);
- a big **Play** button (installs first if needed; while working it becomes **Stop**, which cancels the current step),
  then **Update**, **Repair**, **Uninstall** (asks first), **Saves folder**, **D2R folder** (choose another D2R
  install), in Exocet (`exocet.ttf`); body text in Roboto Condensed;
- the credits line and a link icon (`comment.svg`, Font Awesome) for **Report a problem** (GitHub issues). The
  official launcher's Discord / Reddit / Patreon / guide icons are left out: we have no links of our own for them.

Toolkit: Win32 + GDI+, both part of Windows, so the exe stays a single file with no runtime. GDI+ decodes the PNGs and
every GIF frame (pre-scaled once, then drawn as cached bitmaps on a 20 ms timer that only repaints the logo), loads the
fonts from the exe's resources (`PrivateFontCollection`; Georgia / Segoe UI if one cannot be loaded) and draws
anti-aliased text and the icon. The icon's SVG path is read by `launcher/src/core/svgpath.cpp` (unit-tested) and
filled as a GDI+ path. The buttons stay real BUTTON windows (owner-drawn), so Tab / Space / Enter and screen readers
work. The canvas is scaled to the monitor's DPI (smaller if it would not fit the work area); the title bar is dark.

`--snapshot FILE` draws the window once into a PNG after the start-up check, without showing it (for docs and checks
from the background).

Work runs on a worker thread. At start the launcher reads the channel and says whether an update is available.

```
MedianXL-Launcher.exe --cli status|detect|install|update|repair|play|uninstall
    [--data-dir DIR] [--site URL|DIR] [--channel stable|test] [--source D2R_DIR] [--game-dir DIR] [--any-drive]
    [--args "-w"] [--background] [--loader-url file:///...]
```

The exe is a GUI-subsystem program. From PowerShell use `Start-Process -NoNewWindow -Wait`, because `& exe` does not
wait. Note that `-Wait` also waits for the game that `play` started.

Build: `cmake -S launcher -B build/launcher -A x64 && cmake --build build/launcher --config Release` gives a 2 MB
static exe (1.5 MB of it the art): C++20, no runtime to install, WinHTTP (TLS 1.2 / 1.3, no HTTPS-to-HTTP redirects), `asInvoker` (never
asks for admin), long paths, per-monitor DPI. CI (`.forgejo/workflows/ci.yml`) has two jobs: `launcher` cross-builds
the exe with `plugin/cmake/xwin-toolchain.cmake`, and `launcher-tests` runs `launcher/tests` with g++ on Linux. The
portable core (`launcher/src/core`: sha256, JSON, manifest and plan, inflate, zip, detection parsing, copy filter,
TOML, SVG paths) has no Windows headers. `launcher/res/launcher.rc` includes no SDK header either, so llvm-rc needs none.

## Testing

- Unit tests: `launcher/tests/core_tests.cpp`, 123 checks: release-manifest parsing (docs/release.md format, unknown
  fields, count checks, the published fields `objectsBase` / `zip.path` / `launcher` and their refusals), channel files
  (schema, unsafe paths, an HTML login page), site roots and object paths, grouping of same-content files, the install
  plan (fetch / remove unlisted / preserve / D2RLoader output kept), versions and URLs, `.build.info` / vdf / acf
  parsing and the build verdict, zip / inflate / range planning, the copy filter, TOML edits, SVG path data (relative
  commands, numbers run together, S / Q to cubics, refusals) and every embedded icon.
- Window (2026-10-26, nothing shown in front, no focus or cursor use): `--snapshot` at rest and with a progress bar
  (a test build), and the live window started with `SW_SHOWNOACTIVATE` and captured with `PrintWindow`: same picture
  as the snapshot, the logo animates, the foreground window never changed. Hover needs the real cursor and was not
  tested.
- Scratch tests (D:, no slot): fresh install; update over HTTP fetching only 3 changed files; repair against a server
  without Range; refusal of foreign folders, non-empty folders and the wrong drive; uninstall leaving the source and
  its mods untouched.
- End-to-end, 2026-10-26, slot 2 locked:
  - Source `D:\D2R-93847`, copy to a new folder on C: (`--any-drive`, since the source is on D: but Battle.net's
    install and its cookie are on C:).
  - Release: a manifest of slot 2's mod folder, served on localhost (before the download site; superseded by the
    test below).
  - Install with a kill mid-copy and a resume. D2RLoader downloaded from d2rloader.net and verified. Mod install with
    a kill and a resume, then repair.
  - `play --background --args -w` started the game from the C: copy without the `volserial` word (dev plugin,
    harness `tag=s2` only, for game_checks) and it stayed up.
  - `game_checks.py all mxl` with `MXL_ATTACH=1` gave **PASS**: all 20 feature lines, image hash match, 0 cursor or
    focus changes.
  - Uninstall removed the copy; `Settings.json` was unchanged (same hash as the backup); slot 2 was untouched; the
    throwaway character was deleted.
- **Isolation test** (owner, 2026-10-26), release.md manifest format, slot 2 locked:
  - Stand-in for the player's real D2R: a build 93847 folder on C: (robocopy of slot 2 without its mods and loader).
    Inside it:
    - a D2RLoader that is not the pinned one: the 1.3.1 files with a changed `D2RLoader.exe`, since older builds
      cannot be downloaded;
    - D2RMM's config: `default_mod = "D2RMM"`, its own `input-bindings.toml` and a log;
    - D2RMM's output mod `mods\D2RMM` (`D2RMM.mpq\modinfo.json` savepath `D2RMM/`, excel and strings files);
    - a D2RMM save in `Saved Games\...\mods\D2RMM`.
  - `tools/launcher_isolation.py snapshot` hashed that folder (370 files, 27.7 GB) and all of `Saved Games\Diablo II
    Resurrected` (1,211 files, Settings.json included), leaving out only the test release's own save folder
    `mods\MedianXL-lt`.
  - The launcher installed from that source with the normal drive rule (both on C:, no `--any-drive`):
    - The copy holds only `mods\MedianXL`; the D2RMM mod and the other loader were not copied, the pinned loader
      was installed and its signer checked.
    - Play: `d2rloader.log` shows `D2R command line: ...\D2RLoader.exe -txt -w -mod MedianXL`.
    - `game_checks all mxl` (`MXL_ATTACH=1`): **PASS**, 0 cursor or focus changes.
  - `compare` after play and again after Uninstall: **all 370 + 1,211 files unchanged** (sha256, size and mtime).
    D2RLoader wrote no backups during the run.
  - Cleanup: the stand-in folder, the test save folder and the D2RMM save were deleted.
- **Download site** (owner, 2026-10-26), slot 2 locked, a new copy on C: (`--any-drive`, source on D:):
  - Releases: `tools/package_release.py` of slot 2's data with the release DLL, then `tools/launcher_testsite.py
    derive` (savepath `MedianXL-lt/`, the dev `mxl-core.dll` for the harness, two dummy files) as `0.70.0-lt1`. From
    that, `0.70.0-lt2`: two files changed, two added with the same content, the two dummies removed. Both published
    with `tools/publish_release.py --target dir:<site>` (lt1 on `stable`, lt2 with `--no-channel`) and served by
    `tools/serve_dir.py` on localhost (`--site http://127.0.0.1:<port>/`).
  - **Empty state → full install**: 8,521 objects (1,053 MB) in about 50 s; all 10,496 files match the manifest
    (sha256), nothing else in the mod folder; the server log has exactly 8,521 object requests. **Play → `game_checks
    all mxl` PASS** (20 feature lines, image match, 0 cursor or focus changes, no crash files).
  - **`promote 0.70.0-lt2` → Update**: channel, manifest and 3 object requests (6 MB) for the 4 new or changed files;
    the 2 dropped files removed (and two harness logs, which no release lists); a before/after hash of the mod folder
    shows only those changes. **`mxl` PASS.**
  - **One byte of `median.json` changed → Repair**: one object request; the file is back to the manifest's sha256;
    nothing else touched.
  - Also: a half-downloaded `.part` resumed with one `206` request; an object missing on the site (404) fell back to the
    zip (HEAD plus 3 ranged requests); a channel with a wrong `manifestSha256` was refused with a clear message; a local
    folder as `--site` works.
  - Cleanup: uninstall, the test save folder (own character only), the site, the server and the copy removed.

To run a check on a launcher-started game:

```
MXL_SLOT=2 MXL_D2R_GAME=<copy> MXL_HARNESS_WORDS="tag=s2" py -3 tools/game_ctl.py harness on
MedianXL-Launcher.exe --cli play --data-dir <test data> --background --args -w
MXL_SLOT=2 MXL_D2R_GAME=<copy> MXL_RUN=<lock> MXL_CHAR=<own character> MXL_ATTACH=1 py -3 tools/game_checks.py all mxl
```

`MXL_ATTACH=1` makes the first start of game_checks use the running game instead of restarting it. `package_release`
writes savepath `MedianXL/`, the player's real save folder, so a test release needs `launcher_testsite.py derive
--savepath MedianXL-lt/` (it sets `MedianXL.mpq\modinfo.json`, the one D2R reads, and the manifest's `saveFolder`).
game_ctl reads the save folder from that same file.

## Code signing (not bought; options for the owner)

An unsigned exe downloaded from the web gets SmartScreen's "Windows protected your PC" screen; the player has to click
"More info", then "Run anyway". A signed exe still needs download reputation, but it builds up per certificate instead
of per file hash.

| Option | Cost | Notes |
|---|---|---|
| Unsigned | free | SmartScreen prompt on every new version until it gets reputation by hash; some antivirus tools flag unsigned downloaders |
| SignPath Foundation | free for OSI-licensed open source | the certificate is issued to SignPath Foundation, not to us; needs a public repo with an OSI licence, builds on CI (an origin it can verify) and a code-signing policy page; the mod's licence status (Median XL content) may be a problem |
| Azure Trusted Signing (now "Artifact Signing") | Basic $9.99/month (5,000 signatures), Premium $99.99/month | cheapest real option; identity validation of an individual or company; signs from CI; short-lived certificates, reputation follows the validated identity |
| OV certificate (Sectigo, SSL.com, ...) | about $69–440/year | hardware token or cloud HSM required since 2023; reputation builds over time |
| EV certificate | about $249–600/year | since 2024 no longer gives instant SmartScreen reputation (Microsoft changed it), so little gain over OV for us |

Recommendation: Azure Trusted Signing Basic when the repo goes public, or SignPath if the licence question allows it.

## Open questions for the owner

1. (Decided: same volume only, no shim.) Whether to drop `--any-drive` from the player build.
2. Steam installs are untested on a real Steam D2R (none on the game PC).
3. The cookie / volume-serial behaviour is D2RLoader's. A future D2RLoader or D2R build could change it; the pin
   makes that our choice, not a surprise.
4. Signing (above).
5. With lane pub-release:
   - the `imageTimestamp` wording;
   - `d2rloader/crashes/` kept like `logs/`;
   - the optional `launcherMin`.
6. `preserve` semantics for `d2rloader.toml` (an edited file also misses new keys from a release).
7. The launcher reports a newer launcher from the manifest's `launcher` entry but does not replace itself yet.
8. D2RLoader is downloaded from d2rloader.net (no licence to mirror it); if that site goes away, new installs stop
   until we get permission to mirror it or pin a new source.
9. Fonts of the window: `exocet.ttf` says "Copyright (C) 1992 Emigre Graphics, Designed by Jonathan Barnbrook";
   Exocet is a commercial typeface. It is embedded because the official launcher ships it and the owner asked for it,
   but we have no licence of our own for it. Before the repo or the exe goes public: a licence from Emigre, the Median XL
   team's word that theirs covers us, or a free look-alike (removing the resource is enough: the window falls back to
   Georgia). Roboto Condensed is Apache 2.0; the icon is Font Awesome Free (CC BY 4.0).

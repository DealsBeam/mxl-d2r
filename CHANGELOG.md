# Changelog

D2R-XL releases (the mod, version = the plugin's `PluginVersion`) and D2R-XL Downloader releases. Newest first.
Downloads: the downloader always installs the newest release; each release's 7z is at
`https://files.werdegames.com/mxl/releases/<version>/D2R-XL-<version>.7z`. Known bugs: [docs/bugs.md](docs/bugs.md).

All releases need Diablo II: Resurrected **build 93847** (Battle.net) and **D2RLoader 1.3.1**. The Steam build (94995) is
not supported yet.

## D2R-XL

### 0.69.6 (2026-10-27)
- The in-game `mxl` console line and `mxl-core.log` now name any **other D2RLoader plugins** that are loaded. That helps
  with crash reports. ([#44](docs/bugs.md))
- README: **Known conflicts.** D2R-XL does not support other gameplay plugins alongside it (for example pSpell, vendor
  refresh, Qual-Kehk); disable them. Their features are planned as part of D2R-XL itself.
- The Den of Evil boss (Corrupted Necromancer) was tested in HD and legacy graphics: no crash with D2R-XL alone
  ([#42](docs/bugs.md)).

### 0.69.5 (2026-10-27)
- **Fixed:** the Barbarian's **wolf companion** and the **raven familiar** died about two seconds after being summoned.
  Their attack counter is now set the way Median sets it. ([#36](docs/bugs.md))

### 0.69.4 (2026-10-27)
- Released with **Downloader 0.2.2**. No game changes.

### 0.69.3 (2026-10-27)
- The release version and the plugin version are now always the same number (the `mxl` line and the wrong-build message
  show it). Released with **Downloader 0.2.1**. No game changes.

### 0.69.2 (2026-10-27)
- Installs into your own D2R folder (`mods\D2R-XL`) with the new **D2R-XL Downloader 0.2.0**.
- D2RLoader no longer needs the `-txt` launch argument.

### 0.69.1 (2026-10-26)
- The mod is now called **D2R-XL**: mod folder `mods\D2R-XL`, saves in `Saved Games\Diablo II Resurrected\mods\D2R-XL`.

### 0.69.0 (2026-10-26)
- **First public build.** A port of Median XL 2.14.4 to D2R 3.3, playable single-player in HD graphics mode:
  - Median's items, skills, monsters and quests;
  - the eight-page skill trees;
  - Median's engine rules in the D2RLoader plugin;
  - the Median-only areas and waypoints.

  Median-only monsters, effects and maps use the closest vanilla HD stand-ins. See the README, "Where it stands".

## D2R-XL Downloader

### 0.2.2 (2026-10-27)
- **Fixed:** "D2R or D2RLoader is running, close the game first" while both were closed. This happened when the
  downloader was saved inside the D2R folder. Now only `D2R.exe` and `D2RLoader*.exe` count, and the message names the
  program it found. ([#40](docs/bugs.md))

### 0.2.1 (2026-10-27)
- Downloads come from `files.werdegames.com` (a CDN-backed mirror). `dl.werdegames.com` stays as the backup
  (`--site`).

### 0.2.0 (2026-10-27)
- The launcher became the **D2R-XL Downloader**. It installs D2R-XL into your D2R folder's `mods\D2R-XL` and does not
  start the game.
- A fresh install downloads one 355 MB 7z and unpacks it; updates and Repair fetch only the changed files.
- It shows whether **D2RLoader 1.3.1** is installed, with a link to d2rloader.net, and warns about an unsupported D2R
  build.
- It offers once to delete the old private game copy of launcher 0.1.x.

### 0.1.2 (2026-10-27)
- **Fixed:** the launcher kept offering to update itself to the same version.

### 0.1.1 (2026-10-26)
- New D2R XL logo.

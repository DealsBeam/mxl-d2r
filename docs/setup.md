# Setup on a new PC

Done on the game PC, 2026-10-20 (Windows 11, from a fresh clone; about an hour, most of it the two extractions and the
build). Every command runs from the repo root.

## External inputs

Release rule (2026-10-23): the port is for players who own **only D2R**. Everything from Median XL and all custom art is
in the **asset pack** (`assets/` after `tools/asset_pack.py fetch`; not in git, pinned by `assets.lock`); only Blizzard's
base game files are taken from the D2R install and are never committed or packed.

| Input | Where | Notes |
|---|---|---|
| Diablo II: Resurrected 3.3, build 93847 | `C:\Program Files (x86)\Diablo II Resurrected` | read through CascLib; game must be installed (not running) |
| Median XL (classic) 2.14.4 | not needed | Median's own files are in the asset pack (`assets/median/`); the install (`C:\Games\median-xl`) is only needed to re-extract with `tools/mpq_extract.py` |
| Asset pack | `assets-<version>.zip`, about 540 MB (868 MB unpacked), pinned in `assets.lock` | downloaded by `tools/asset_pack.py fetch` (step 4); no Git LFS needed |
| `..\mxl_extracted` | not needed to build | the decoded tables the build reads are in the asset pack (`assets/median/2_txt`, `item_names.txt`, `skill_names.txt`). The Ghidra output (`3_ghidra`, `4_sigma_source`, ...) is still only here, for the research tools `sigma_fn.py`, `sigma_patches.py` and `sigma_evidence.py`; NOT in git. On the game PC it lives in `Desktop\sugma\mxl_extracted`, so a junction points to it: `mklink /J %USERPROFILE%\mxl_extracted %USERPROFILE%\Desktop\sugma\mxl_extracted` |
| Python 3.12 | `py -3` | `python` is the Store stub |
| CMake | `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` | not on PATH; add it for the build shells |
| MSVC | Visual Studio 18 Community, "Desktop development with C++" | |
| D2RLoader 1.3.1-beta | https://d2rloader.net/download.html | see below |

## Steps

1. `git clone --recurse-submodules` the repo (or `git submodule update --init`).
2. `py -3 -m pip install --user -r tools/requirements.txt` (pillow, capstone).
3. Native libraries: `tools\build_native.ps1` (with CMake on PATH) -> `build\StormLib`, `build\CascLib`.
4. Median's files and the HD art (`assets/`, the asset pack): `py -3 tools/asset_pack.py fetch`. It downloads the pack that
   `assets.lock` pins (version, sha256, size, URLs), checks the sha256, unpacks into `assets/` and checks every file against
   the pack's manifest; run again, it only says "already the pinned pack". `tools/build.py` does the same check first and
   stops (exit 2) with this instruction when `assets/` is missing or differs; it never builds without it.
   - Offline / local copy: `py -3 tools/asset_pack.py fetch --from <pack file | folder holding it | unpacked tree>`, or set
     `MXL_ASSETS` to that path (then `tools/build.py` fetches a missing `assets/` by itself). With `--link` and an unpacked
     tree, `assets/` becomes a junction/symlink to the tree (no copy; CI and the game PC use this).
   - Download cache: `~/.cache/mxl-d2r/assets` (`MXL_ASSETS_CACHE` moves it). A release of a private repo needs a token in
     `MXL_ASSETS_TOKEN` (or `GITHUB_TOKEN`); it is only sent to github.com.
   - A checkout from before the pack with `assets/` already there (Git LFS): `py -3 tools/asset_pack.py adopt` hashes it and,
     if it equals the pinned pack, writes the manifest (no download). `verify` re-checks (`--full` hashes everything;
     the default trusts an unchanged stat snapshot of the last full check; `--strict` also fails on extra files).
   - Changing assets: edit `assets/`, then `MXL_SKIP_ASSET_CHECK=1 tools/build.py` builds with a warning. A new pack:
     `py -3 tools/asset_pack.py build --version <N> --write-lock` (writes `build/asset-pack/assets-<N>.zip` + manifest and
     `assets.lock`; `build` first scans the text files with `tools/check_public.py`'s rules and refuses on a hit, `asset_pack.py scan` runs only that); the coordinator uploads the zip to the download host with
     `tools/publish_release.py --assets` (it lands in `https://dl.werdegames.com/mxl/assets/`, docs/hosting.md) and commits `assets.lock`. Same tree, same bytes (sorted entries, fixed timestamps).
   - Our own files that used to sit in `assets/` are tracked now: [assets-median.md](assets-median.md) (what the Median tree
     is), [hd-wereowl-assets.md](hd-wereowl-assets.md) (the Wereowl model README), `tools/sprite3d/data/wereowl/cams.json`.
5. Extract the D2R baseline, all sets the build reads:
   ```
   py -3 tools/casc_extract.py --set excel --set strings --set layouts --set dataguide --set meta --set legacy-ui --set legacy-art --set legacy-maps --set hd-ui
   ```
   -> `extract/d2r/93847/` (35,124 files). The default sets alone are not enough: `gfx_placeholders.py` (art file lists),
   `missile_hd.py`, `item_hd.py`, `skill_icons.py` (palette), `ui_panels.py` read `legacy-*` and `hd-ui`.
6. (no step: the vanilla 1.13c view comes from the D2R extraction of step 5.) `mxl_names.py` and `func_slots.py` need
   the tables of Median's vanilla rows only as a row count, row order, key names and a few function slots; D2R's own tables
   hold the 1.13c rows as their first rows. `tools/vanilla113.py` cuts the 33 tables the build uses to their 1.13c row counts
   and puts back the few cells D2R changed (`py -3 tools/vanilla113.py` lists them). No classic Diablo II files or
   `..\D2R Modding Tools` folder are read. Against the build that read the classic tables `work/mod` is byte-identical
   except 214 comment cells that carry D2R's wording instead of 1.13c's (misc.txt `name`, 63 rows; cubemain.txt
   `description`, 151 rows; both columns are typed "comment" in D2R's data guide) and the lines of validate_report.md that quote them.
7. Nothing to do (already in the asset pack): `assets/median/data/global/excel/` holds the pre-hotfix `skills.bin`,
   `skillscode.bin` and `states.bin` that match `assets/median/2_txt` (the installed `medianxl-YmludGJsdHh0.mpq`,
   dated 2026-10-03, has hotfixed ones with which `formula.py` finds no formula columns in skills; those are kept in
   `%USERPROFILE%\tmp-ext\mxl-hotfix-bins`). Re-running `tools/mpq_extract.py` writes the hotfixed ones over them:
   restore the three from the pack afterwards (`asset_pack.py fetch --force`). Regenerating `2_txt` from the hotfix needs the patched d2bin2txt (not in this repo).
8. `py -3 tools/build.py` -> `work/mod/`. Result: validate.py **0 errors** (both banks), 3,994 plugin items,
   48 (base) / 89 (rotw) warnings. The 2 errors in `validation.md` were fixed by an earlier commit.
9. Plugin (CMake on PATH): `tools\build_plugin.ps1` -> `build\plugin\Release\mxl-core.dll`, **mxl-core 0.36.0**.
10. D2RLoader 1.3.1-beta (the pinned version):
    - Download `https://d2rloader.net/download/D2RLoader-1.3.1-beta.zip` (47,652,455 bytes) into an empty folder.
    - SHA-256 must be `9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7` (the value on the download page).
    - Unzip into a second folder and check `D2RLoader.exe` and `D2RCore.dll` with `Get-AuthenticodeSignature`:
      signer `CN=D2RLoader`, thumbprint `8001082E968FADAA58BF2F73EFA1810A11D0C28F` (the thumbprint the download page
      publishes), file version 1.3.1-beta. Status shows `UnknownError` ("root certificate not trusted"): the
      certificate is self-signed, so the check is the thumbprint plus an intact signature (not HashMismatch).
    - Copy the zip contents into the game folder (`D2RLoader.exe`, `D2RCore.dll`, `d2rloader.mpq`, `d2rloader\`).
      The zip has no `mods\` folder; `install_mod.py` creates it.
11. `py -3 tools/install_mod.py` -> `<game>\mods\D2R-XL` (8,802 files, 791 MB, plugin mxl-core.dll included).
    It needs `D2RLoader.exe` in the game folder (step 10) and `work/mod` (step 8).

## By hand

- D2RLoader settings for unattended runs (`<game>\d2rloader\config\d2rloader.toml`):
  `default_mod = "D2R-XL"`, `launch_arguments = "-w -txt"`, `skip_title_screen = true`,
  `check_for_updates = false`, `write_crash_dumps = true`, `developer.enable_console = true`,
  and developer logs enabled for `excel_files`, `bin_validation`, `extension_details`, `archive_mounts`.
  The original is kept as `d2rloader.toml.orig`; `check_for_updates = false` keeps the pinned 1.3.1.
- `-txt` compiles all 380 tables at start (about 15 s total), this launch only; no manual `d2rl compile D2R-XL` is needed.
- mxl-core 0.37.0 loads (ABI 4). Three startup asserts are expected; see [plugin-spec.md](plugin-spec.md).
- Test characters: none were on this PC (below). Create new ones; read `save-format.md` before editing saves.
## Ghidra

- Ghidra 12.1.4 (Java 21). The scripts expect it at `..\tools\ghidra_12.1.4_PUBLIC`; on the game PC that is a junction to
  the install in Downloads.
- `..\ghidra\sigma` (project `D2Sigma`, `D2Sigma.dll` from the Median install, base 0x10000000) and `..\ghidra\d2game`
  (project `D2Game`, Median's `D2Game.dll` imported as `D2Game_113.dll`, base 0x6fc20000): `analyzeHeadless` import
  with auto-analysis, about 1.5 minutes each.
- `..\ghidra\project` (the D2R image): needs a memory dump of the running game. Start D2RLoader, then
  `py -3 tools/dump_image.py` -> `..\ghidra\D2RLoader_<time>_dump.exe`, then `tools/ghidra_analyze.ps1 -MaxMem 16G` (hours; worked on the 64 GB machine).
- Parallel lanes each query their own copy of a project (`..\ghidra-lanes\<lane>\sigma`): two headless runs can't
  open one project at once.

## Slot copies for parallel test games

Up to three games can run side by side (docs/testing.md, "Parallel slots"). Slot 1 is the Battle.net install; slots 2
and 3 are full copies of the same build 93847 folder (29 GB each) on any drive (D: here; the harness reports C:'s
volume serial so the Battle.net session cookie still decrypts):

```
robocopy "C:\Program Files (x86)\Diablo II Resurrected" D:\D2R-93847     /E /MT:8 /XD logs     :: slot 2 (an older copy was already there)
robocopy "C:\Program Files (x86)\Diablo II Resurrected" D:\D2R-93847-s3  /E /MT:8 /XD logs     :: slot 3 (about 5 minutes)
py -3 tools/game_slots.py setup          :: modinfo savepath D2R-XL-s2 / -s3, toml from slot 1, save folders, D2RLoader-sN.exe
py -3 tools/install_mod.py --slot all    :: mod data + plugin into every slot (built once, copied)
py -3 tools/game_slots.py status
```

Other folders: env `MXL_SLOT2_DIR` / `MXL_SLOT3_DIR` (also `MXL_SLOT1_DIR`). `MXL_D2R_GAME` / `install_mod.py --game`
pointing at a slot folder keeps that slot's save folder. Never run two slots on one folder. A slot needs
`D2R.exe`, `D2RLoader.exe`, `D2RCore.dll`, `d2rloader.mpq`, `.build.info` and the `Data` folder, which the robocopy gives it.

Rename to D2R-XL (2026-10-26): the mod folder is `mods\D2R-XL` and the save folders `mods\D2R-XL`, `D2R-XL-s2`,
`D2R-XL-s3` (were `MedianXL`, `MedianXL-s2`, `-s3`). A slot set up before needs, under its lock,
`py -3 tools/install_mod.py --slot N` (writes `mods\D2R-XL`) and `py -3 tools/game_slots.py setup N` (modinfo savepath,
`default_mod = "D2R-XL"` in the slot's `d2rloader\config\d2rloader.toml`; slot 1's toml is set by hand or with
`setup 1`). The old `mods\MedianXL` folder can then be deleted; the old save folders are moved by hand (characters
are not converted by any tool).

## Old test characters

No `mxl-manual-backups` folder and no `D2R-XL` save folder exist on the game PC: not in `Saved Games\Diablo II Resurrected`
(only `Backups`, `mods\RMD-MP`, unrelated), OneDrive (empty), Documents or Desktop. They must come from the PC that
made them, into `Saved Games\Diablo II Resurrected\mods\D2R-XL` (the mod's `savepath`).

## CI

CI runs on a private CI server (`<ci-server>`; its address is deliberately not published) with a Forgejo runner scoped to `WerdeGames/mxl-d2r`, label `mxl-cross`, a Docker image with clang-cl, lld, xwin and g++, capacity 1, and a cap of 6 CPUs / 8 GB. The runner is not exposed to GitHub: pull requests on GitHub are applied to the forge by maintainers and then run there.

`.forgejo/workflows/ci.yml` runs on every PR and push to `main`. `plugin` cross-builds `mxl-core.dll` with clang-cl + lld-link and the xwin MSVC CRT/SDK (`/opt/xwin/xwin`) via `plugin/cmake/xwin-toolchain.cmake` (pass it as an absolute path). `tests` builds `plugin/tests` (a standalone CMake project) with g++ and runs the unit test binaries listed in the workflow. `tools` runs `python3 -m compileall -q tools`, checks that the build reads Median's files from `assets/median`, and runs `tools/check_public.py` (no private paths, host names, addresses or credentials in tracked text).

No game files go to the server. The boot check and the in-game smoke run (`py -3 tools/game_ctl.py smoke`) need the game and run on the game PC.

Job logs are not exposed by API v1; reproduce a failing job on the CI server with `docker run` of the CI image.

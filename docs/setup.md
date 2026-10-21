# Setup on a new PC

Done on the game PC, 2026-10-20 (Windows 11, from a fresh clone; about an hour, most of it the two extractions and the
build). Every command runs from the repo root.

## External inputs

| Input | Where | Notes |
|---|---|---|
| Diablo II: Resurrected 3.3, build 93847 | `C:\Program Files (x86)\Diablo II Resurrected` | read through CascLib; game must be installed (not running) |
| Median XL (classic) 2.14.4 | `C:\Games\median-xl` | classic install with the `medianxl-*.mpq` files |
| `..\mxl_extracted` | next to the repo (`C:\Users\<you>\mxl_extracted`) | the first extraction (`2_txt`, `1_bin_from_mpq`, Ghidra output); NOT in git. On the game PC it lives in `Desktop\sugma\mxl_extracted`, so a junction points to it: `mklink /J %USERPROFILE%\mxl_extracted %USERPROFILE%\Desktop\sugma\mxl_extracted` |
| `..\D2R Modding Tools\D2Compare_102\TXT\113c` | next to the repo | vanilla 1.13c tables read by `mxl_names.py`; rebuilt here from the classic MPQs (below) |
| Python 3.12 | `py -3` | `python` is the Store stub |
| CMake | `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` | not on PATH; add it for the build shells |
| MSVC | Visual Studio 18 Community, "Desktop development with C++" | |
| D2RLoader 1.3.1-beta | https://d2rloader.net/download.html | see below |

## Steps

1. `git clone --recurse-submodules` the repo (or `git submodule update --init`).
2. `py -3 -m pip install --user -r tools/requirements.txt` (pillow, capstone).
3. Native libraries: `tools\build_native.ps1` (with CMake on PATH) -> `build\StormLib`, `build\CascLib`.
4. Extract Median's MPQs: `py -3 tools/mpq_extract.py` -> `extract/mxl/` (8,528 files, 0 conflicts).
5. Extract the D2R baseline, all sets the build reads:
   ```
   py -3 tools/casc_extract.py --set excel --set strings --set layouts --set dataguide --set meta --set legacy-ui --set legacy-art --set legacy-maps --set hd-ui
   ```
   -> `extract/d2r/93847/` (35,124 files). The default sets alone are not enough: `gfx_placeholders.py` (art file lists),
   `missile_hd.py`, `item_hd.py`, `skill_icons.py` (palette), `ui_panels.py` read `legacy-*` and `hd-ui`.
6. Vanilla 1.13c tables (once): extract the 92 `data\global\excel\*.txt` listed below from `patch_d2.mpq`, then
   `d2exp.mpq`, then `d2data.mpq` (first hit wins) into `..\D2R Modding Tools\D2Compare_102\TXT\113c\`. The
   file names are the `*.txt` of a patch_d2 extraction (arena, armor, ... , weapons); `tools/native.py`
   (`open_mpq`, `extract`) does it. Without this folder `mxl_names.py` still runs but resolves far fewer names
   (3,769 validate errors).
7. Restore the extraction Median shipped before its hotfix. The installed `medianxl-YmludGJsdHh0.mpq` (bintbltxt,
   dated 2026-10-03) holds `skills.bin`, `skillscode.bin` and `states.bin` that differ from the 2026-10-02
   extraction behind `..\mxl_extracted\2_txt`; `formula.py` then finds no formula columns in skills. Copy those three
   files from `..\mxl_extracted\1_bin_from_mpq\data\global\excel\` over the ones in `extract/mxl/data/global/excel/`
   (the hotfixed originals are kept in `%USERPROFILE%\tmp-ext\mxl-hotfix-bins`). Both say version 2.14.4. Decide later
   whether to regenerate `2_txt` from the hotfix (needs the patched d2bin2txt, not in this repo).
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
11. `py -3 tools/install_mod.py` -> `<game>\mods\MedianXL` (8,802 files, 791 MB, plugin mxl-core.dll included).
    It needs `D2RLoader.exe` in the game folder (step 10) and `work/mod` (step 8).

## By hand

- D2RLoader settings for unattended runs (`<game>\d2rloader\config\d2rloader.toml`):
  `default_mod = "MedianXL"`, `launch_arguments = "-w -txt"`, `skip_title_screen = true`,
  `check_for_updates = false`, `write_crash_dumps = true`, `developer.enable_console = true`,
  and developer logs enabled for `excel_files`, `bin_validation`, `extension_details`, `archive_mounts`.
  The original is kept as `d2rloader.toml.orig`; `check_for_updates = false` keeps the pinned 1.3.1.
- `-txt` compiles all 380 tables at start (about 15 s total), this launch only; no manual `d2rl compile MedianXL` is needed.
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

## Old test characters

No `mxl-manual-backups` folder and no `MedianXL` save folder exist on the game PC: not in `Saved Games\Diablo II Resurrected`
(only `Backups`, `mods\RMD-MP`, unrelated), OneDrive (empty), Documents or Desktop. They must come from the PC that
made them, into `Saved Games\Diablo II Resurrected\mods\MedianXL` (the mod's `savepath`).

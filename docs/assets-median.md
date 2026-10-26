# Median XL 2.14.4: the mod's own files (build input, `assets/median/` of the asset pack)

This file used to be `assets/median/README.md`; `assets/` is not in git any more (the asset pack, `tools/asset_pack.py`,
docs/setup.md step 4), so the README is tracked here. Paths below are relative to `assets/median/`.

The port is released to players who own only Diablo II: Resurrected, so everything the build takes from Median XL is
in the asset pack and no Median XL install is needed. Blizzard's base game files are never committed: the build reads them
from the D2R install (`tools/casc_extract.py` -> `extract/d2r/`).

| Path | What | From |
|---|---|---|
| `data/` | the contents of Median's 56 `medianxl-*.mpq` archives (8,528 files: tables `.bin`, art `.dc6`/`.dcc`/`.dt1`/`.ds1`, `.tbl` strings, animdata) | `tools/mpq_extract.py` from `C:\Games\median-xl`, 0 conflicts |
| `manifest.json` | file -> source archive, written by `mpq_extract.py`; not tracked in git on purpose (it names the extraction machine's paths); the pack's own `assets-manifest.json` (path, size, sha256) is what `asset_pack.py verify` checks | |
| `version.mxl` | Median's version file | |
| `2_txt/` | Median's `.bin` tables decoded to text (column names lost, see `tools/mxl_names.py`), `strings/`, `sigma_raw_dumps/` | the first extraction (`mxl_extracted`, patched d2bin2txt, 2026-10-02) |
| `item_names.txt`, `skill_names.txt` | item and skill name lists of that extraction | same |

`data/global/excel/skills.bin`, `skillscode.bin` and `states.bin` are the pre-hotfix versions that match `2_txt`
(docs/setup.md step 7); `mpq_extract.py` overwrites them with the hotfixed ones, so restore them from the pack after a re-extract (`asset_pack.py fetch --force`).

**Files over 20 MB are stored xz-compressed** (`<name>.xz`; decompress with `xz -dk`): a leftover of the Git LFS days, when the
forge's proxy dropped uploads longer than about two minutes (~20 MB). Kept as is, so the pack equals the old tree. Today that is only the classic splash animation
`data/global/themes/classic_sigma/menus/splash/splash-animation-{0,1,2}.dc6` (25 MB each, 3.5-4.5 MB as .xz), which nothing in
the build reads (the HD menus are D2R's). `manifest.json` keeps the original names. A tool that needs such a file decompresses it.

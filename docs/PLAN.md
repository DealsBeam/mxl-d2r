# Port plan

Target: retail D2R 3.3 + D2RLoader 1.3.1 (SDK 0.3.0, ABI 4), extended save format.
Median XL tables fit the 32,768-row limits (largest: cubemain 7,500, missiles 5,609, monstats 3,901, skills 3,018).

## Phase 0: Setup
- [x] Repo, private GitHub remote (https://github.com/Nugwa628/mxl-d2r)
- [x] Toolchain: git, gh, CMake, MSVC (VS 18), Python 3.14
- [x] StormLib / CascLib submodules
- [x] D2R 3.3.93847 at `C:\Program Files (x86)\Diablo II Resurrected` (same build as the RuffnecKk research)
- [x] D2RLoader 1.3.1-beta installed there (signer thumbprint matches the official one)
- [x] CascLib reads D2R data (`data:data/global/excel/skills.txt`; base-game tables are under `excel/base/`)
- [ ] First launch of D2RLoader.exe (owner: user)
- [ ] Ask the Median XL team for permission (owner: user)

## Phase 1: Tools (Python CLI)
- [x] `tools/mpq_extract.py`: all 56 Median MPQs -> `extract/mxl/` (8,528 files, 832 MB, manifest.json)

Remaining:
casc-extract, casc-extract, bin2txt (resolve placeholders), Sigma table schemas,
formula codec (fn5/fn7), txt-migrate (1.13c -> D2R columns), tbl2json (`mxl:` namespace), validate/diff.

## Phase 2: Data port
Migrate 94 standard tables, `d2rl init` / `d2rl compile`, boot with `-txt`, enable extended saves.

## Phase 3: Triage D2Sigma
One note per subsystem in `docs/subsystems/`, each feature bucketed:
(a) data-only, (b) SDK service, (c) native hook, (d) UI rebuild, (e) cut/defer.

## Phase 4: Plugins
mxl-core, mxl-skills, mxl-items, mxl-monsters, mxl-npc, mxl-ui. Every hook checks expected bytes.

## Phase 5: Graphics
First test whether legacy-graphics mode loads modded DCC/DC6. Then decide per asset.

## Phase 6: Test and release
Smoke tests, saves/reload, TCP/IP, `d2rl pack`.

**First milestone:** a playable vertical slice (one class, its items, Act 1).

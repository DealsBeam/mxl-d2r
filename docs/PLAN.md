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
- [x] D2RLoader.exe launches to the character screen (2026-10-05)
- [x] Median XL team approved the project (2026-10-05)

## Phase 1: Tools (Python CLI)
- [x] `tools/mpq_extract.py`: all 56 Median MPQs -> `extract/mxl/` (8,528 files, 832 MB, manifest.json)
- [x] `tools/casc_extract.py`: D2R 93847 baseline -> `extract/d2r/93847/` (1,072 files). See [d2r-baseline.md](d2r-baseline.md)
- [x] `tools/mxl_names.py`: placeholder names resolved, Median rows renumbered after D2R's -> `work/named/`. See [id-mapping.md](id-mapping.md)
- [~] Sigma table schemas: tooling + structure for all 37 tables, 14 named or partly named. See [sigma-tables.md](sigma-tables.md)
- [x] `tools/formula.py`: all 17,116 formulas decoded from bytecode, 11,329 table cells rewritten with D2R names. See [formulas.md](formulas.md)
- [x] `tools/txt_migrate.py`: all tables in D2R layout, merged with D2R's rows, both banks -> `work/mod/`. See [txt-migrate.md](txt-migrate.md)

- [x] `tools/tbl2json.py`: 21,268 strings -> D2R JSON (native loading, both string folders). See [strings.md](strings.md)

Remaining: validate (dangling references, compile with `d2rl compile`),
finish Sigma field names (with Phase 3 triage), link skills2code once skills2 is named.

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

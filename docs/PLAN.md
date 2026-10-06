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

- [x] `tools/validate.py` + `tools/build.py`: D2R-schema validation; 2 errors left (Median's own broken formulas). See [validation.md](validation.md)

Remaining: finish Sigma field names (with Phase 3 triage), link skills2code once skills2 is named.
Next: Phase 2 (install into D2R, `d2rl compile`, first boot).

## Phase 2: Data port
- [x] `tools/install_mod.py`: mod installed at `<game>/mods/MedianXL` (folder MPQ, own save folder, extended saves)
- [ ] `d2rl compile MedianXL` from the developer console; first boot (owner: user)
Migrate 94 standard tables, `d2rl init` / `d2rl compile`, boot with `-txt`, enable extended saves.

## Phase 3: Triage D2Sigma
One note per subsystem in `docs/subsystems/`, each feature bucketed:
(a) data-only, (b) SDK service, (c) native hook, (d) UI rebuild, (e) cut/defer.

## Phase 4: Plugins
mxl-core, mxl-skills, mxl-items, mxl-monsters, mxl-npc, mxl-ui. Every hook checks expected bytes.

## Phase 5: Graphics
First test whether legacy-graphics mode loads modded DCC/DC6. Then decide per asset.

Status (2026-10-06):
- Installed for classic mode (install_mod.py, Median-only files; ones D2R has stay D2R's): map tiles/presets
  (`global/tiles`, also needed in HD), missile and overlay animations (265 + 107 .dcc), item pictures (542 .dc6).
  Every item picture name the item tables use exists (D2R's or Median's). 11 missile CelFiles and 15 overlay
  files exist nowhere (several are vanilla's own dead rows: nul, gore20, pentacle; Median's: extra/icestormimpactvar3,
  extra/spikefiendmamamissile, extra/auraresistcoldcast, extra/firesmoke.dcc, extra/lifedrain.dcc, extra/genericsplash,
  'expansion|baaldeathghostie01'): classic mode may fail on those if they ever spawn.
- Test pending: does classic mode (G) draw these from the mod (docs/testing.md)?
- Next: monsters (383 MB). gfx_placeholders.py swaps Median's 254 unknown monster codes to D2R donors because the
  classic COF is read even in HD (timing). Shipping Median's monster COF/DCC would let monsters keep their own codes
  (Median's animation timing; classic art), with monsters.json still mapping each to a donor HD model; needs a check
  that HD models play with Median's frame counts, and Median's animdata.d2 entries. Then HD art: items.json /
  missiles.json look-alikes today (item_hd.py, missile_hd.py); converting Median's DC6/DCC to HD .sprite is the
  way to real HD pictures. Skill tree backgrounds and tab titles (bug #3) are art too.

## Phase 6: Test and release
Smoke tests, saves/reload, TCP/IP, `d2rl pack`.

**First milestone:** a playable vertical slice (one class, its items, Act 1).

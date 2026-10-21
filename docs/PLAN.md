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
- [x] First boot 2026-10-21 (unattended, `-txt` compiles all 380 tables at start; no manual compile needed)
Migrate 94 standard tables, `d2rl init` / `d2rl compile`, boot with `-txt`, enable extended saves.

## Phase 3: Triage D2Sigma
Triage table: [subsystems/README.md](subsystems/README.md) (2026-10-14, 109 source modules bucketed; largest open: quests, requirements, NPC actions, crafting, hirelings).
One note per subsystem in `docs/subsystems/`, each feature bucketed:
(a) data-only, (b) SDK service, (c) native hook, (d) UI rebuild, (e) cut/defer.

## Phase 4: Plugins
mxl-core, mxl-skills, mxl-items, mxl-monsters, mxl-npc, mxl-ui. Every hook checks expected bytes.

## Phase 5: Graphics
First test whether legacy-graphics mode loads modded DCC/DC6. Then decide per asset.

Status (2026-10-14):
- Installed for classic mode (install_mod.py, Median-only files; ones D2R has stay D2R's): map tiles/presets
  (`global/tiles`, also needed in HD), missile and overlay animations (265 + 107 .dcc), item pictures (542 .dc6).
  Every item picture name the item tables use exists (D2R's or Median's). 11 missile CelFiles and 15 overlay
  files exist nowhere (several are vanilla's own dead rows: nul, gore20, pentacle; Median's: extra/icestormimpactvar3,
  extra/spikefiendmamamissile, extra/auraresistcoldcast, extra/firesmoke.dcc, extra/lifedrain.dcc, extra/genericsplash,
  'expansion|baaldeathghostie01'): classic mode may fail on those if they ever spawn.
- Classic mode draws the mod's item pictures (confirmed 2026-10-07).
- Monsters (2026-10-07): install_mod.py ships Median's monster files D2R lacks (5,994 files, 363 MB, COF + DCC);
  tools/animdata_merge.py merges animdata.d2 (D2R base, +2,529 Median records, Median's speed on 245 shared ones;
  7 with a different frame count keep D2R's). gfx_placeholders.py: 1,193 monsters now keep their own Code and
  monstats2 row (Median timing, Median art in classic mode) and only borrow a D2R HD model; 24 still borrow the
  donor's graphics (10 codes Median's files do not complete). Startup clean.
- HD body animations for Median's sequence skills (2026-10-14): D2R's HD player models pick a sequence clip by
  skill ID (state machine: modeId 18 + skillIndex). tools/hd_skill_anims.py gives each Median sequence skill with
  the same seqnum as a D2R one a copy of that state (29 entries: whirlwind_2 and windform -> Whirlwind's spin,
  leap_2 -> Leap, cautious_strike/iron_spiral_2 -> Double Swing, smg -> Blade Fury, ...). Skills whose sequence
  has no D2R clip still have no HD body animation.
- HD item pictures from Median's own art (2026-10-14): tools/item_art.py converts each Median-only inventory DC6
  (542 files, 474 used) to D2R's HD sprite (98 px per cell, bilinear x3.5; .lowend 49 px) under
  hd/global/ui/items/<folder>/mxl/; item_hd.py points 987 base items, 45 uniques and 22 set items at them. Items
  whose picture D2R also has keep D2R's HD art; the rest still borrow a look-alike.
- Inventory and cube sizes (2026-10-14): Median's 15 x 10 player inventory and 15 x 10 Horadric Cube (D2R 10 x 4,
  3 x 4). inventory.txt gridX/gridY (txt_migrate inventory_sizes); HD panels use Median's own panel art
  (tools/ui_panels.py) and Median's positions scaled to D2R's panel (tools/ui_layouts.py). The 14 x 14 stash (personal and shared
  pages) followed the same day. Trade window, NPC stores and the cube view in the stash followed (2026-10-21): Median's inventory.bin has
  Trade Page 1/2 as two 7 x 15 grids side by side (trade.dc6) and "Monster", the stores' grid, as 15 x 15 (store.dc6);
  inventory.txt, three HD layouts and panel art (tools/ui_panels.py: trade, store with its pressed tab replaced,
  stash materials tab with a cube grid pasted over D2R's 3 x 4 recess) done, in-game check pending. Open: classic-mode panels.
- Next: HD art. items.json / missiles.json / monsters.json use D2R look-alikes today (item_hd.py, missile_hd.py,
  gfx_placeholders.py); converting Median's DC6/DCC to HD .sprite is the way to real HD pictures. Skill tree
  backgrounds and tab titles (bug #3): tools/skill_tree_art.py, to test (titles are drawn into per-class 8-frame sprites). Deferred checks: Median monster art in classic mode, Median-only
  areas, Whirlpool (docs/testing.md).

## Phase 6: Test and release
Smoke tests, saves/reload, TCP/IP, `d2rl pack`.

**First milestone:** a playable vertical slice (one class, its items, Act 1).

## Next (2026-10-20)
Work happens on the forge (git.werdegames.com/WerdeGames/mxl-d2r): one branch and PR per lane.
0. **Setup on this PC** (the game PC lost its generated files): D2RLoader 1.3.1, extractions, `tools/build.py`, plugin build,
   install, Ghidra projects (`..\ghidra\{sigma,d2game,project}`). Written down in [setup.md](setup.md).
1. **Test backlog**: ~70 rows wait in [testing.md](testing.md). Played as per-character sessions
   ([test-sessions.md](test-sessions.md)). No new features once the waiting list passes ~10 rows again.
2. **Milestone: Barbarian, Act 1**: a new Barbarian to Andariel with its tree (tab titles and backgrounds, bug #3),
   Act 1 quests and vendors, cube recipes (cube conditions, D2Craft.cpp), save/reload, no crash.
3. **Engine (Phase 4), in the order players meet it**: requirements + quests/ubers, NPC services, hirelings,
   monster mods/AI/goblins, damage/combat/properties leftovers, objects/events/respec.
4. **Graphics/UI (Phase 5)**: skill tree art, character sheet, trade window, HD effects of common skills, Median
   monster HD models last.
5. **Phase 6** (TCP/IP, saves, `d2rl pack`), then Phase 7.

## Phase 7: Private realm (after single-player works; preliminary)
Characters stored on our own server, games hosted through D2RLoader TCP/IP (the D2R Reimagined / Project Lage
model). Draft plan, edit before starting: [realm-plan.md](realm-plan.md).
- [ ] 7a `mxl-realm` save sync plugin (download on start, upload `.d2s` + `.d2rl` + `.d2i` on save and exit)
- [ ] 7b Realm API (accounts, game tokens, save storage with revisions)
- [ ] 7c Launch script (token file, realm savepath); launcher with signed packages later
- [ ] 7d Hosting: player-hosted TCP/IP first, an always-on host later
- [ ] 7e Optional: seasons, chat, death feed, trade listings, server-side save checks

Rule for Phases 4-6 meanwhile: Median logic stays server-side in the plugins, and Median's state lives in the save files.

## Progress estimate (2026-10-14, updated after the Phase 5/3 hour)

Judgement estimates, not measurements. "Phase done" total is weighted by each phase's share of the work.

| Phase | Share of total work | Phase done | Adds to overall |
|---|---|---|---|
| 0 Setup | 2% | 100% | 2.0% |
| 1 Tools (extract, decode, convert) | 12% | 90% | 10.8% |
| 2 Data port (tables, strings, install, booting) | 10% | 94% | 9.4% |
| 3 Triage of Median's engine code | 8% | 80% | 6.4% |
| 4 Plugins (engine behaviour) | 30% | 64% | 19.2% |
| 5 Graphics | 28% | 27% | 7.6% |
| 6 Test and release | 10% | 5% | 0.5% |
| **Total** | **100%** | **~55%** | **~55%** |

Phase 7 (private realm) is outside this estimate.

Open: bug #22 (monsters not targetable, states fix; 0.34.1 under test), the docs/testing.md backlog, Phase 5 HD art,
Median's remaining systems (Ubers and challenges, cube extras, character and stash screens), Phase 6.

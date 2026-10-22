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
- [~] Sigma table schemas: tooling + structure for all 37 tables; 32 named or partly named, 27 complete (2026-10-21). See [sigma-tables.md](sigma-tables.md)
- [x] `tools/formula.py`: all 17,116 formulas decoded from bytecode, 11,329 table cells rewritten with D2R names. See [formulas.md](formulas.md)
- [x] `tools/txt_migrate.py`: all tables in D2R layout, merged with D2R's rows, both banks -> `work/mod/`. See [txt-migrate.md](txt-migrate.md)
- [x] `tools/tbl2json.py`: 21,268 strings -> D2R JSON (hybrid: native overrides plus the `medianxl` namespace, both string folders). See [strings.md](strings.md)

- [x] `tools/validate.py` + `tools/build.py`: D2R-schema validation; 2 errors left (Median's own broken formulas). See [validation.md](validation.md)

Remaining: the Sigma fields no code reaches (armor2/weapons2, missiles2, itemtypesext, propertiesext, parts of statscreen/misc2; named with Phase 3 triage when a reader turns up). skills2 is named and its formula fields are linked to skills2code.
Next: Phase 2 (install into D2R, `d2rl compile`, first boot).

## Phase 2: Data port
- [x] `tools/install_mod.py`: mod installed at `<game>/mods/MedianXL` (folder MPQ, own save folder, extended saves)
- [x] First boot 2026-10-21 (unattended, `-txt` compiles all 380 tables at start; no manual compile needed)
- [x] Strings, hybrid (2026-10-22): keys D2R has stay native overrides (group 1), Median-only keys move to the D2RLoader `medianxl` namespace (group 2, `medianxl:<key>` in cells, no numeric ids, so no id collisions), the keys that do not fit a fixed-size field stay native (exceptions); plugins read text by key; validate.py checks cells and the plugin's keys. See [strings.md](strings.md), [strings-namespace.md](strings-namespace.md)
Migrate 94 standard tables, `d2rl init` / `d2rl compile`, boot with `-txt`, enable extended saves.

## Phase 3: Triage D2Sigma
Triage table: [subsystems/README.md](subsystems/README.md) (2026-10-14, 109 source modules bucketed; largest open: quests, requirements, NPC actions, crafting, hirelings).
One note per subsystem in `docs/subsystems/`, each feature bucketed:
(a) data-only, (b) SDK service, (c) native hook, (d) UI rebuild, (e) cut/defer.
Covered so far (2026-10-21): quests, requirements, NPC and hirelings, ubers and events, craft flags
(`docs/subsystems/`: quests-requirements.md, npc-hirelings.md, ubers-events.md).

## Phase 4: Plugins
mxl-core, mxl-skills, mxl-items, mxl-monsters, mxl-npc, mxl-ui. Every hook checks expected bytes.

Status (2026-10-21, mxl-core 0.42.0). All installed and switching on at boot; gameplay not yet verified by play.
D2R addresses: [d2r-addresses.md](d2r-addresses.md).
- Cube conditions: 542 recipes
- Craft flags and gold: 256 effects
- Act 1 quests: Griswold's Heart, Andariel catalyst, Warriv re-issue
- Dungeon lockout: 162 levels, stairs and portals
- Treasure goblins
- NPC actions: Akara respec, Cain crafting materials
- Skill tree: 8 pages (9 for the Sorceress)

## Phase 5: Graphics
First test whether legacy-graphics mode loads modded DCC/DC6. Then decide per asset.

Status (2026-10-14):
- HD monsters (2026-10-21): tools/hd_monsters.py picks donor, scale and colour per Median-only monster (docs/hd-monsters.md); real models later.
- Installed for classic mode (install_mod.py, Median-only files; ones D2R has stay D2R's): map tiles/presets
  (`global/tiles`, also needed in HD), missile and overlay animations (265 + 107 .dcc), item pictures (542 .dc6).
  Every item picture name the item tables use exists (D2R's or Median's). 12 missile CelFiles and 12 overlay
  files (+ 2 names written with a ".dcc") existed nowhere (vanilla's dead rows: nul, gore20, pentacle; Median's typos of D2R names or
  art it never shipped: extra/icestormimpactvar3, extra/spikefiendmamamissile, extra/auraresistcoldcast, extra/firesmoke.dcc,
  extra/lifedrain.dcc, extra/genericsplash, 'expansion|baaldeathghostie01'). 2026-10-22 (lane data-leftovers): tools/art_fix.py
  (build step after txt_migrate) repairs the names and points the rest at the closest existing art (REDIRECT table; 74 rows in both
  banks, work/mod/art_fix.md), so classic mode draws something and missile_hd.py / effects_hd.py give them the HD effect of that art;
  validate.py fails when a new animation name is found in neither D2R nor Median's files.
- Map ids (2026-10-21, lane areas): Median's .ds1 maps named objects through Median's flat ObjPreset table and Median-only
  monsters by Median's MonStats row; D2R resolves objects per act and moved Median's rows from 734. Fixed in the pipeline:
  txt_migrate adds Median's table to ObjPreset.txt, tools/ds1_remap.py (called by install_mod.py) renumbers the maps
  (27,556 objects, 4,095 monsters in 470 maps). tools/area_audit.py audits levels/lvlprest/lvltypes/lvlmaze/lvlwarp and
  the maps; details in docs/subsystems/areas-drlg.md. Needs a rebuild + install, then the in-game check (testing.md).
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
  stash materials tab with a cube grid pasted over D2R's 3 x 4 recess) done, in-game check pending.
- Classic-mode panels (G, 2026-10-21): inventory, cube, stash (personal + shared page), trade, stores and the character sheet use Median's art scaled to D2R's 320 x 432 classic panel (x 0.6136, DC6 in ui/panel/mxl/, quantized to the UI palette: indices 225-254 are hot pink there) with Median's grid / slot / box positions at the same factor; grids and slots take "scale" (items shrink with them). Checked in game (screenshots in mxl-d2r-runs/classic-work/shots) except the trade window (needs two players). Known: the +1 stat buttons of the character sheet do not scale, so they sit in a row under the attributes; vitals show the current value only; D2R's close button moved to the bottom right; classic stash has no extra tabs.
- Character sheet (2026-10-21, in-game check pending): Median's statsheet page 0 (stat-screen.dc6: name bar, life / mana /
  stamina / experience bars, 4 attributes, 4 resistances, defense, damage + attack rating of the left and right skill)
  is D2R's HD panel background (ui_panels.py 'charsheet') and tools/ui_layouts.py char_sheet() moves D2R's stat widgets
  into its boxes. The doubled "ATTACK Damage / Attack Rating" rows were not a string bug: D2R's panel shows the left
  skill's and the right skill's stats; in Median's layout they sit beside the two weapon portraits as value-only
  boxes. Median's pages 2 and 3 (stat-screen.dc6 pages 1-2: ~60 boxes of extra stats such as resistances
  by type, damage and speed modifiers, find / absorb values; table decoded in docs/subsystems/stat-screen.md): D2R's
  Advanced Stats panel (the button on the sheet's right edge) lists itemstatcost rows by their `advdisplay` column, so
  the 96 stats of those pages are rows of that panel by data (txt_migrate.py advanced_stats; checked in game: 13 stats always,
  the others while not 0). Not done: Median's page art and grouping as a second panel (plugin), the computed Str / Dex damage
  bonus. Also missing: the portrait.
- HD skill effects (2026-10-21, docs/hd-effects.md): D2R HD effects are PopcornFX particles plus JSON, no sprite component; only
  flipbook sprite effect is the UI campfire. tools/dcc.py + d2r_texture.py + missile_art.py turn Median DCCs into patched
  flipbook effects (Barbarian first); not in build.py until an effect is confirmed in game (python tools/missile_art.py runs it).
- Classic mode (G) is deferred (owner, 2026-10-21): no classic-only work until all HD work is done; the classic panels
  above stay as they are.
- Next: HD art. items.json / missiles.json / monsters.json use D2R look-alikes today (item_hd.py, missile_hd.py,
  gfx_placeholders.py); converting Median's DC6/DCC to HD .sprite is the way to real HD pictures. Skill tree
  backgrounds and tab titles (bug #3): tools/skill_tree_art.py, to test (titles are drawn into per-class 8-frame sprites). All 8 pages are selectable and show their skills since 2026-10-21 (plugin/src/skill_tree.cpp: D2R's panel only handled 3 pages, in reverse order; tools/skill_icons.py splits skilldesc rows shared by several classes). Deferred checks: Median monster art in classic mode, Median-only
  areas, Whirlpool (docs/testing.md).
- Status (2026-10-21): trade window 7 x 15 (two pages), NPC stores 15 x 15, in-stash cube view: done, in-game check
  pending. Skill tree: 8 pages with per-class icons. Character sheet: still D2R's, in progress.

## Phase 6: Test and release
Smoke tests, saves/reload, TCP/IP, `d2rl pack`.
- [x] Automated smoke run (`tools/game_ctl.py`: boot, create and play MxlBarb, panels, save and reload, log scan) passes
- [x] CI on the CI server (`.forgejo/workflows/ci.yml`): plugin cross-build with clang-cl, unit tests, tools syntax

**First milestone:** a playable vertical slice (one class, its items, Act 1).

## Next (2026-10-20)
Work happens on the forge (git.werdegames.com/WerdeGames/mxl-d2r): one branch and PR per lane.
0. **Setup on this PC** (the game PC lost its generated files): D2RLoader 1.3.1, extractions, `tools/build.py`, plugin build,
   install, Ghidra projects (`..\ghidra\{sigma,d2game,project}`). Written down in [setup.md](setup.md). Done.
1. **Test backlog**: ~70 rows wait in [testing.md](testing.md). Played as per-character sessions
   ([test-sessions.md](test-sessions.md)). No new features once the waiting list passes ~10 rows again.
2. **Milestone: Barbarian, Act 1**: a new Barbarian to Andariel with its tree (tab titles and backgrounds, bug #3),
   Act 1 quests and vendors, cube recipes (cube conditions, D2Craft.cpp), save/reload, no crash. Code done;
   verification pending.
3. **Engine (Phase 4), in the order players meet it**: requirements + quests/ubers, NPC services, hirelings,
   monster mods/AI/goblins, damage/combat/properties leftovers, objects/events/respec. In progress.
4. **Graphics/UI (Phase 5)**: skill tree art, character sheet, trade window, HD effects of common skills, Median
   monster HD models last.
5. **Phase 6** (TCP/IP, saves, `d2rl pack`), then Phase 7.

## Removed or deferred for now (owner, 2026-10-21)
Switched off or not worked on until the HD single-player port works; revisit later.
Everything else follows Median XL (owner, 2026-10-21); gaps and approximations are tracked in [unported.md](unported.md).
- **Custom HD art is the very last step** (owner, 2026-10-22): everything Median draws that vanilla D2R can't reproduce
  gets the closest vanilla HD stand-in first. That covers about 330 missile and overlay animations, monster models, the
  121 Median areas without a D2R HD scene (and Median-edited vanilla maps), and UI art. All other work on the port
  (code, data, maps) is completed and verified before the custom HD pass starts. Stand-ins are listed in unported.md
  as "custom art, last".
- **Classic graphics mode (G):** no classic-only work until all HD work is done. The classic panels already merged stay
  as they are.
- **16:9 legacy graphics (G)** (owner, 2026-10-22: later, after HD and after the classic pass): today the G view is
  D2R's 4:3 legacy image with side bars; the 3D view is already 16:9 and the server already sends the wider area, so it
  is client drawing only. Steps: find the legacy canvas size, letterbox step and camera centre in D2R (research lane
  first: a few constants = cheap, sizes baked in many places or hard-sized buffers = expensive); widen canvas, camera,
  tile / wall / unit culling and the mouse-to-world mapping; fix fixed 800-wide buffers; classic bottom bar (800-wide
  art: centre with filler pieces), classic panels, automap and message log positions; an off word in mxl-core-off.txt
  keeps 4:3. A cosmetic deviation from Median (800 x 600); old 1.13c widescreen hacks show the approach, their code
  does not apply.
- **Terror zones** (D2R's "desecrated zones": "Corrupted tremors strike ..." messages, hourly zones with boosted monster
  level, experience and drops): off. tools/terror_zones.py writes D2R's desecratedzones.json with every
  ladder_enabled / non_ladder_enabled flag false into the mod.

## Phase 7: Private realm (after single-player works; preliminary)
Characters stored on our own server, games hosted through D2RLoader TCP/IP (the D2R Reimagined / Project Lage
model). Draft plan, edit before starting: [realm-plan.md](realm-plan.md).
- [ ] 7a `mxl-realm` save sync plugin (download on start, upload `.d2s` + `.d2rl` + `.d2i` on save and exit)
- [ ] 7b Realm API (accounts, game tokens, save storage with revisions)
- [ ] 7c Launch script (token file, realm savepath); launcher with signed packages later
- [ ] 7d Hosting: player-hosted TCP/IP first, an always-on host later
- [ ] 7e Optional: seasons, chat, death feed, trade listings, server-side save checks

Rule for Phases 4-6 meanwhile: Median logic stays server-side in the plugins, and Median's state lives in the save files.

## Progress estimate (2026-10-23, plugin 0.55.0)

Judgement estimates, not measurements. Same method as the tables below. Since 0.53.0: the `medianxl` string namespace
switch, the Median rules audit (124 rules) and the D2Sigma patch audit (2,027 patch sites, 767 functions) with their
top items ported (no corpse, level cap 150, sockets / automagic for every quality, cooldowns, damage reduction, skill
damage scaling, item rules, quests, travel, gamble, stamina / mana, monster scaling, collide types, start functions,
function slot remap), the first generated 3D model in game (Wereowl). Game checks on 0.55.0: 88 checks, 94 results PASS,
9 FAIL / 4 BLOCKED (lane release-055-fixes; mostly test-state problems), 2 for a human to review.

| Phase | Share of total work | Phase done | Adds to overall |
|---|---|---|---|
| 0 Setup | 2% | 100% | 2.0% |
| 1 Tools (extract, decode, convert) | 12% | 100% | 12.0% |
| 2 Data port (tables, strings, install, booting) | 10% | 100% | 10.0% |
| 3 Triage of Median's engine code | 8% | 100% | 8.0% |
| 4 Plugins (engine behaviour) | 30% | 97% | 29.1% |
| 5 Graphics | 28% | 45% | 12.6% |
| 6 Test and release | 10% | 45% | 4.5% |
| **Total** | **100%** | **78.2%** | **78.2%** |

Phase 7 (private realm) is outside this estimate.

## Progress estimate (2026-10-22, plugin 0.53.0)

Judgement estimates, not measurements. Same method as the tables below. Game checks on 0.53.0: 31 of 33 pass, tooltip
needs a human to read the screenshots, bonusdrops fails for the Summoner (lane summoner-catalyst).

| Phase | Share of total work | Phase done | Adds to overall |
|---|---|---|---|
| 0 Setup | 2% | 100% | 2.0% |
| 1 Tools (extract, decode, convert) | 12% | 100% | 12.0% |
| 2 Data port (tables, strings, install, booting) | 10% | 99% | 9.9% |
| 3 Triage of Median's engine code | 8% | 99% | 7.9% |
| 4 Plugins (engine behaviour) | 30% | 93% | 27.9% |
| 5 Graphics | 28% | 42% | 11.8% |
| 6 Test and release | 10% | 33% | 3.3% |
| **Total** | **100%** | **74.8%** | **74.8%** |

Phase 7 (private realm) is outside this estimate.

## Progress estimate (2026-10-21)

Judgement estimates, not measurements. Same method as the 2026-10-14 table below.

| Phase | Share of total work | Phase done | Adds to overall |
|---|---|---|---|
| 0 Setup | 2% | 100% | 2.0% |
| 1 Tools (extract, decode, convert) | 12% | 90% | 10.8% |
| 2 Data port (tables, strings, install, booting) | 10% | 97% | 9.7% |
| 3 Triage of Median's engine code | 8% | 90% | 7.2% |
| 4 Plugins (engine behaviour) | 30% | 72% | 21.6% |
| 5 Graphics | 28% | 30% | 8.4% |
| 6 Test and release | 10% | 12% | 1.2% |
| **Total** | **100%** | **60.9%** | **60.9%** |

Phase 7 (private realm) is outside this estimate.

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

# mxl-d2r

Port of Median XL to Diablo II: Resurrected, built on D2RLoader 1.3.1 + PluginSDK 0.3.0.

**Private.** Median XL data, code and art belong to the Median XL team; Diablo II belongs to Blizzard.
Neither is committed here. See `.gitignore`.

## Layout

| Path | Contents |
|---|---|
| `tools/` | Python CLI tools (MPQ/CASC extraction, bin2txt, formula codec, txt migration, validation) |
| `plugins/` | D2RLoader plugins (C++20, CMake) that reimplement D2Sigma.dll |
| `mod/` | Generated D2R mod (`mods/MedianXL`) sources: migrated tables, strings, layouts |
| `docs/` | Plan and one note per D2Sigma subsystem (`docs/subsystems/`) |
| `research/` | D2R address registry and RE notes (no game binaries) |
| `third_party/` | StormLib, CascLib (submodules) |

Ignored working folders: `extract/` (raw game/MXL extractions), `work/` (scratch output).

## External inputs (not in repo)

- Median XL install: `C:\Games\median-xl`
- Median extraction: `..\mxl_extracted`
- D2R modding toolkit: `..\D2R Modding Tools`

## Plan

See [docs/PLAN.md](docs/PLAN.md). Handoff notes for porting another classic mod: [docs/porting-playbook.md](docs/porting-playbook.md).

# D2R-XL

**D2R-XL** is a port of **Median XL 2.14.4** to **Diablo II: Resurrected 3.3**. It runs on retail D2R (build 93847) through
[D2RLoader](https://d2rloader.net) 1.3.1 and a D2RLoader plugin (`mxl-core`, C++20) that re-implements the engine side of
Median XL's own `D2Sigma.dll`. The tables, strings, maps and items are converted from Median XL's data.

The repository is `mxl-d2r`. This is a fan project. Median XL belongs to the Median XL team, Diablo II to Blizzard Entertainment; see
[NOTICE.md](NOTICE.md). It is not affiliated with or endorsed by either.

## Where it stands

Playable single-player in **HD graphics mode**, not finished. In short (details in [docs/PLAN.md](docs/PLAN.md) and
[docs/unported.md](docs/unported.md), which lists every gap, 28 of them still open at the time of writing):

- **Works:** Median's item, skill, monster and quest tables (the 94 standard tables plus the strings) in D2R's format; the
  eight-page skill trees; Median's engine rules in the plugin (damage, caps, cube recipes, craft effects, NPC services,
  treasure goblins, dungeon lockouts, the Median-only areas and waypoints, requirements, ubers and events), each checked
  by an automated in-game test (66 rows "verified", 8 more waiting for a human check).
- **Approximated:** everything Median draws that D2R cannot show with a table edit. Median-only monsters, about 330
  skill effects and the Median-only maps use the closest vanilla HD stand-in (a recoloured, rescaled D2R model, effect or
  biome). The character sheet shows Median's stat pages through D2R's Advanced Stats panel, not Median's own art.
  Custom HD art is the very last step of the plan.
- **Deferred:** the classic (legacy) graphics mode. Use D2R's HD mode.
- **Not done yet:** some UI conveniences (stash sort, boss life bars with portraits, the loot filter's extra fields),
  Median's own audio, terror zones (switched off on purpose), and anything that needs two players has had little human
  testing (trade window, party features). Online play on Battle.net is not a goal: it is single-player and D2RLoader's
  TCP/IP games.
- **Overall:** roughly 80% of the planned work, by our own estimate; Phases 0-4 (tooling, data, engine) are complete,
  graphics and release polish are not.

Expect bugs. Please report them (below).

## Requirements

- Your own copy of **Diablo II: Resurrected** (PC, Battle.net), build **93847**. Nothing from Blizzard is distributed here.
- **D2RLoader 1.3.1** from [d2rloader.net](https://d2rloader.net/), installed in your D2R folder.
- Windows 10 or 11, and about **1.6 GB of free disk space** (1.2 GB for the mod, plus the 355 MB download while it
  installs).

## Install

1. Install **D2RLoader 1.3.1** from https://d2rloader.net/ into your D2R folder (it puts `D2RLoader.exe` next to
   `D2R.exe`).
2. Download the **D2R-XL Downloader** (`D2R-XL-Downloader.exe`) from https://dl.werdegames.com/mxl/ (the link is also
   on the [Releases](https://github.com/Nugwa628/mxl-d2r/releases) page), run it and press **Install**. It finds your
   D2R folder (or use **D2R folder** to pick it) and puts the mod in `<D2R folder>\mods\D2R-XL`. Run it again later
   and press **Update** for a new version, or **Repair** if a file got damaged.
3. **How to play:** start `D2RLoader.exe` in your D2R folder. The first time, click the gear on the character screen,
   set **Default Mod** to **D2R-XL**, Apply, and start D2RLoader again. From then on D2RLoader starts D2R-XL directly.

The downloader writes only `mods\D2R-XL` (and its own settings and log in `%LOCALAPPDATA%\D2R-XL`); it does not
download or change D2RLoader, D2R or other mods. D2R-XL characters are saved in their own folder
(`Saved Games\Diablo II Resurrected\mods\D2R-XL`). **Uninstall** removes `mods\D2R-XL` and nothing else. If D2R is
in `Program Files` and Windows refuses the write, the downloader says so: run it as administrator. Details:
[docs/launcher.md](docs/launcher.md).

Manual install: each release also has a `.7z` file (`D2R-XL-<version>.7z`, about 355 MB) with a `D2R-XL/` folder
(`D2R-XL.mpq/` and `d2rloader/`). Windows 11 opens .7z files; otherwise use [7-Zip](https://www.7-zip.org). Unpack it
into the `mods` folder of your D2R install and follow step 3. Steps and the file layout are in
[docs/release.md](docs/release.md).

## Reporting bugs

Open an issue on [GitHub](https://github.com/Nugwa628/mxl-d2r/issues/new/choose) and pick a form:

- **Bug report**: something is wrong or missing compared to Median XL.
- **Crash**: the game closed or froze. Attach the D2RLoader log.
- **Balance or Median difference**: a number, drop, skill or monster behaves differently from Median XL 2.14.4.

Include the mod version (`mxl` in the in-game console prints it), the D2R build, the downloader version, and the D2RLoader
log (`d2rloader/logs/d2rloader.log`). Never attach Blizzard game files. How labels are used: [docs/labels.md](docs/labels.md).

## Building from source

Setup on a fresh PC (toolchain, D2R extraction, build, install, test slots): [docs/setup.md](docs/setup.md).
The short version: `git clone --recurse-submodules`, get the asset pack, `py -3 tools/build.py`, then
`tools\build_plugin.ps1`. Contributions are welcome: [.github/CONTRIBUTING.md](.github/CONTRIBUTING.md).

## Layout (for developers)

| Path | Contents |
|---|---|
| `tools/` | Python CLI tools (MPQ/CASC extraction, bin2txt, formula codec, txt migration, validation) |
| `plugin/` | D2RLoader plugin (C++20, CMake) that reimplements D2Sigma.dll |
| `launcher/` | `D2R-XL-Downloader.exe` (C++20, CMake): installs and updates `mods\D2R-XL` ([docs/launcher.md](docs/launcher.md)) |
| `work/mod/` | Generated D2R mod sources: migrated tables, strings, layouts |
| `docs/` | Plan and one note per D2Sigma subsystem (`docs/subsystems/`) |
| `research/` | D2R address registry and RE notes (no game binaries) |
| `third_party/` | StormLib, CascLib, PluginSDK (submodules) |

Ignored working folders: `extract/` (raw game extractions) and `work/` (build output).

Plan: [docs/PLAN.md](docs/PLAN.md). Handoff notes for porting another classic mod:
[docs/porting-playbook.md](docs/porting-playbook.md). The main repository is
[the forge](https://git.werdegames.com/WerdeGames/mxl-d2r); GitHub is a mirror that takes issues and releases. CI: see
[docs/setup.md#ci](docs/setup.md#ci).

## License

Our code and documentation are MIT ([LICENSE](LICENSE)). Median XL's data and art, Blizzard's games and D2RLoader are not
covered by it: see [NOTICE.md](NOTICE.md).

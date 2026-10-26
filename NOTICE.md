# Notice

## What the MIT license covers

The MIT license in [LICENSE](LICENSE) covers the code and documentation written for this project: the D2RLoader
plugin in `plugin/`, the tools in `tools/`, the notes in `docs/` and `research/`, the launcher, the build scripts and
the CI configuration.

## What it does not cover

- **Median XL** (the mod, its data tables, strings, maps, models, sounds and art, including the files under
  `assets/median` and the HD art derived from them) belongs to the Median XL team. The port is a fan project; the team
  approved it (see docs/PLAN.md, Phase 0). Nothing in this repository grants you rights to Median XL's content beyond
  those the team grants itself. Median XL's site: <https://www.median-xl.com>.
- **Diablo II** and **Diablo II: Resurrected** belong to Blizzard Entertainment. This project is not affiliated with or
  endorsed by Blizzard Entertainment or the Median XL team. No Blizzard game files are in this repository or in our
  releases: you need your own copy of Diablo II: Resurrected (build 93847), which the launcher copies on your PC.
- **D2RLoader** is a separate project (<https://d2rloader.net>) with its own license. It is not part of this repository;
  the launcher downloads it from its official site, or you install it yourself.

## Third-party code in `third_party/`

These are Git submodules that keep their own licenses (all MIT):

| Library | Author | License |
|---|---|---|
| [StormLib](https://github.com/ladislav-zezula/StormLib) | Ladislav Zezula | MIT |
| [CascLib](https://github.com/ladislav-zezula/CascLib) | Ladislav Zezula | MIT |
| [PluginSDK](https://github.com/D2RLoader/PluginSDK) | D2RLoader contributors | MIT |

Their license texts are in their own folders (`third_party/<name>/LICENSE`).

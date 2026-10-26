# Notice

## What the MIT license covers

The MIT license in [LICENSE](LICENSE) covers the code and documentation written for this project: the D2RLoader
plugin in `plugin/`, the tools in `tools/`, the notes in `docs/` and `research/`, the launcher's code, the build scripts and
the CI configuration.

## What it does not cover

D2R-XL is a port of Median XL by the Median XL team. Diablo II by Blizzard Entertainment. Not affiliated with Blizzard.


- **Median XL** (the mod, its data tables, strings, maps, models, sounds and art, including the files under
  `assets/median` and the HD art derived from them) belongs to the Median XL team. The port is a fan project; the team
  approved it (see docs/PLAN.md, Phase 0). Nothing in this repository grants you rights to Median XL's content beyond
  those the team grants itself. Median XL's site: <https://www.median-xl.com>.
- **Diablo II** and **Diablo II: Resurrected** belong to Blizzard Entertainment. This project is not affiliated with or
  endorsed by Blizzard Entertainment or the Median XL team. No Blizzard game files are in this repository or in our
  releases: you need your own copy of Diablo II: Resurrected (build 93847), which the launcher copies on your PC.
- **D2RLoader** is a separate project (<https://d2rloader.net>) with its own license. It is not part of this repository;
  the launcher downloads it from its official site, or you install it yourself.

## The launcher's art (`launcher/res`)

The launcher window uses the art of the official Median XL launcher (<https://launcher.median-xl.com>), embedded in
`D2R-XL-Launcher.exe`. It is not covered by the MIT license:

| File | What | By | License |
|---|---|---|---|
| `background.png`, `logo.gif`, `popup-window.png`, `icon.ico` | background, animated logo, panel, icon | the Median XL team | theirs, not ours to license; the team approved the port (docs/PLAN.md, Phase 0) |
| `exocet.ttf` | Exocet Heavy | Jonathan Barnbrook, Emigre Graphics (1992) | commercial typeface, as shipped by the official launcher; see docs/launcher.md, open question 9 |
| `RobotoCondensed.ttf` | Roboto Condensed | Google | [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0) |
| `comment.svg` | "comment" icon | [Font Awesome Free](https://fontawesome.com) 6.6.0, Fonticons, Inc. | [CC BY 4.0](https://fontawesome.com/license/free) (attribution kept in the file) |

## Third-party code in `third_party/`

These are Git submodules that keep their own licenses (all MIT):

| Library | Author | License |
|---|---|---|
| [StormLib](https://github.com/ladislav-zezula/StormLib) | Ladislav Zezula | MIT |
| [CascLib](https://github.com/ladislav-zezula/CascLib) | Ladislav Zezula | MIT |
| [PluginSDK](https://github.com/D2RLoader/PluginSDK) | D2RLoader contributors | MIT |

Their license texts are in their own folders (`third_party/<name>/LICENSE`).

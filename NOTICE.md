# Notice

## What the MIT license covers

The MIT license in [LICENSE](LICENSE) covers the code and documentation written for this project: the D2RLoader
plugin in `plugin/`, the tools in `tools/`, the notes in `docs/` and `research/`, the downloader's code, the build scripts and
the CI configuration.

## What it does not cover

D2R-XL is a port of Median XL by the Median XL team. Diablo II by Blizzard Entertainment. Not affiliated with Blizzard.


- **Median XL** (the mod, its data tables, strings, maps, models, sounds and art, including the files under
  `assets/median` and the HD art derived from them) belongs to the Median XL team. The port is a fan project; the team
  approved it (see docs/PLAN.md, Phase 0). Nothing in this repository grants you rights to Median XL's content beyond
  those the team grants itself. Median XL's site: <https://www.median-xl.com>.
- **Diablo II** and **Diablo II: Resurrected** belong to Blizzard Entertainment. This project is not affiliated with or
  endorsed by Blizzard Entertainment or the Median XL team. No Blizzard game files are in this repository or in our
  releases: you need your own copy of Diablo II: Resurrected (build 93847); the D2R-XL Downloader puts the mod into its
  `mods` folder.
- **D2RLoader** is a separate project (<https://d2rloader.net>) with its own license. It is not part of this repository
  or our releases, and the downloader does not download it: you install it yourself from its official site.

## The downloader's art (`launcher/res`)

The downloader's window uses the art of the official Median XL launcher (<https://launcher.median-xl.com>), embedded in
`D2R-XL-Downloader.exe`. It is not covered by the MIT license:

| File | What | By | License |
|---|---|---|---|
| `background.png`, `popup-window.png`, `icon.ico` | background, panel, icon | the Median XL team | theirs, not ours to license; the team approved the port (docs/PLAN.md, Phase 0) |
| `logo.gif` | the animated "D2R XL" logo (55 frames): the X and L traced from the official Median XL logo, D, 2 and R drawn to match, fire generated | derived from the Median XL team's logo | as the row above |
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

## The downloader's 7z decoder (`launcher/third_party/lzma`)

`D2R-XL-Downloader.exe` unpacks the release's `.7z` with the C 7z decoder of the
[LZMA SDK](https://www.7-zip.org/sdk.html) 25.01 by Igor Pavlov, copied unchanged (only the files the decoder needs).
The LZMA SDK is placed in the **public domain** (`launcher/third_party/lzma/lzma-sdk.txt`); no license text is
required, the credit is ours.

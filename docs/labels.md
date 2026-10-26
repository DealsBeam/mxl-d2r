# Issue labels

The label set for the GitHub issues. The issue forms in `.github/ISSUE_TEMPLATE/` set `crash`, `balance` and `needs-repro`
automatically; maintainers add the rest.

| Label | Use for |
|---|---|
| `crash` | The game closes, freezes or shows an assert / error dialog |
| `item` | Item stats, tooltips, uniques, sets, runewords, gems, charms, drops of a specific item |
| `skill` | Skill damage, cost, synergies, skill tree pages, cooldowns, auras and effects |
| `quest` | Quests, NPC services, vendors, hirelings, crafting NPCs, cube recipes |
| `monster` | Monster stats, AI, mods, bosses, ubers, treasure goblins, spawns and areas |
| `ui` | Panels, tooltip layout, character sheet, skill tree and inventory UI, HUD, automap |
| `balance` | A number or rule differs from Median XL 2.14.4 (see the Balance or Median difference form) |
| `save` | Character or stash save problems: loading, corruption, lost items, extended save format |
| `install` | Install, update and launch problems: launcher, manual zip, D2RLoader, game build |
| `launcher` | The Median XL Launcher itself (download, game copy, updates, Play button) |
| `needs-repro` | We cannot reproduce it yet; waiting for steps, a log or a save |
| `unported` | A known gap that has a row in `docs/unported.md`; use it to link issues to that table |

Rules of thumb: one area label per issue (add more only if it truly spans areas); `crash` and `save` issues are looked at
first; remove `needs-repro` once someone has reproduced it; close duplicates with a link to the first issue.

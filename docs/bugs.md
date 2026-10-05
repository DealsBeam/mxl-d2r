# In-game bug list

Found while play-testing the port. "Interim" fixes are data stand-ins until the D2Sigma behaviour is ported.

| # | Reported | Status | Cause / notes |
|---|---|---|---|
| 1 | Skill icons missing | fixed | IconCel lives at skilldesc.bin +0x16 (D2SkillIcons.cpp); `tools/skill_icons.py` |
| 2 | Skill icons small | fixed | D2R draws icons at sprite size; sheets capped at 62 icons at 130 px (Amazon 118 px) |
| 3 | Skill tree only had 3 tabs | fixed (experiment worked) | `tools/ui_layouts.py` 8-tab layout. Still missing: tab names/art, Median's tab rules (devotion locks, "pick up to three") |
| 4 | Can't allocate skill points | interim, works in game | skpoints used D2Sigma's cap variables (`mlvl`, `skcp`, `skcn`) -> cost 999. Now 1 point, maxlvl 20 (approximate; real cap grows with "Maximum Skill Level Increased by" items) |
| 5 | Keybinds lost on exit | interim, retest | Likely the skills could not be bound (rightskill unset on 448 of 470 tree skills: D2Sigma's picker ignored it) or were not owned (#4) |
| 6 | Town portal skill does nothing | fix to test | srvdofunc 155 is D2Sigma's itemless portal; 113 needs a scroll/book. Now D2R's itemless portal 109 ("Townportal O Skill") + rightskill 1 (docs/plugin-spec.md). The other D2Sigma do-functions moved to 172-190 for the plugin |
| 7 | NPC shows random text (e.g. "Arcane Ape") on first talk | open | The text is new Median string `ArcaneApe` (id 262189 = first new id + 10408). Not an id collision with D2R's json. Look at how D2R picks the intro/gossip string |
| 8 | Vendors sell the wrong items (Gheed sells mystic orbs) | fixed, retest | No item had vendor columns (Median stocks shops from D2Sigma's npcstores.bin). Converted: docs/vendors.md. Gheed does sell mystic orbs in Median, plus 22 equipment items |
| 9 | Skill tooltips assert (`stringIndex1 != strdummy`) | open | Median's descriptions are built by D2Sigma (plugin work) |
| 10 | Potions do nothing | fix to test | Median's potions use D2Sigma's item spell 22; D2R maps item spells 0-19 (0x236fe30) and ignores 20+. 22 is a timed potion, the same as D2R's 3: remapped in txt_migrate.py (item_spells) |
| 11 | Other D2Sigma item spells | open | misc pSpell 12-21 (quest/cube items 13, 14, 16, 18; mystic orbs 20; others) run D2R's own 12-15 or nothing (16+). Needs the plugin (or data remaps where D2R has an equivalent) |
| 12 | Crash when the player died | fix to test | D2R joins the "was slain by" fragments (sysmsg7-9) to the names and passes the result to sprintf (D2Client UI/text.cpp, 0x199cb0 case 6); Median's texts are "%s was slain by %s" templates, so %s read a missing argument. tbl2json.py now keeps D2R's text when the placeholders differ (296 keys, strings_report.md). (The missile frames in the first dump were stale stack values.) |
| 13 | Flamefront: no points | not a bug | Median prerequisite: needs a point in Molten Core (reqskill1) |
| 14 | States beyond D2R's 512 corrupt memory | open (engine) | Median's 1,014 states make D2R put each unit's "state changed" bits past their reserved space (statlist+0xb70), on every state change. Likely source of random crashes/odd behaviour until fixed (docs/plugin-spec.md, States limit) |

# In-game bug list

Found while play-testing the port. "Interim" fixes are data stand-ins until the D2Sigma behaviour is ported.

| # | Reported | Status | Cause / notes |
|---|---|---|---|
| 1 | Skill icons missing | fixed | IconCel lives at skilldesc.bin +0x16 (D2SkillIcons.cpp); `tools/skill_icons.py` |
| 2 | Skill icons small | fixed | D2R draws icons at sprite size; sheets capped at 62 icons at 130 px (Amazon 118 px) |
| 3 | Skill tree only had 3 tabs | fixed (experiment worked) | `tools/ui_layouts.py` 8-tab layout. Still missing: tab names/art, Median's tab rules (devotion locks, "pick up to three") |
| 4 | Can't allocate skill points | interim, works in game | skpoints used D2Sigma's cap variables (`mlvl`, `skcp`, `skcn`) -> cost 999. Now 1 point, maxlvl 20 (approximate; real cap grows with "Maximum Skill Level Increased by" items) |
| 5 | Keybinds lost on exit | interim, retest | Likely the skills could not be bound (rightskill unset on 448 of 470 tree skills: D2Sigma's picker ignored it) or were not owned (#4) |
| 6 | Town portal skill does nothing | open (srvdofunc 113 did not help) | srvdofunc 155 is D2Sigma's; set to D2R's 113. 185 skills use D2Sigma do-functions 153-171 and need the plugin |
| 7 | NPC shows random text (e.g. "Arcane Ape") on first talk | open | The text is new Median string `ArcaneApe` (id 262189 = first new id + 10408). Not an id collision with D2R's json. Look at how D2R picks the intro/gossip string |
| 8 | Vendors sell the wrong items (Gheed sells mystic orbs) | fixed, retest | No item had vendor columns (Median stocks shops from D2Sigma's npcstores.bin). Converted: docs/vendors.md. Gheed does sell mystic orbs in Median, plus 22 equipment items |
| 9 | Skill tooltips assert (`stringIndex1 != strdummy`) | open | Median's descriptions are built by D2Sigma (plugin work) |

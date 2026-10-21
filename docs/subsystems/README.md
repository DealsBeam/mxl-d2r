# D2Sigma subsystem triage (Phase 3)

D2Sigma.dll's code, grouped by its own source files (assert strings `..\<file>.cpp`, Ghidra `StringXrefs .cpp`,
2026-10-14). "Functions" counts functions that name the file in an assert, so it is a lower bound of each module's
size. Buckets from docs/PLAN.md: **(a)** data-only, **(b)** D2RLoader service, **(c)** native hook (mxl-core plugin),
**(d)** UI rebuild (D2R layouts/sprites), **(e)** cut or deferred. Status: **done** (ported, maybe untested),
**partial**, **open** (not started), **n/a** (D2R or D2RLoader covers it), **to read** (purpose not confirmed).

## Gameplay

| Module | Functions | What it is | Bucket | Status | Notes |
|---|---|---|---|---|---|
| D2SkillDo.cpp | 45 | added/replaced skill server functions (srvdofunc 153-171 and replaced vanilla ones) | c | done | plugin-spec.md "Skill server functions": all used ones but 156 (skills_374) and 162 (skills_667, random Param skill) |
| D2SkillStart.cpp | 9 | skill start functions | c | partial | the vanilla starts Median uses run as D2R's; D2Sigma's own starts not reviewed |
| D2SkillCltDo.cpp, D2SkillCltStart.cpp | 13, 1 | client skill functions | c | done | cltdofunc 97-99/11/15/22/95 mapped (txt_migrate skill_rules) |
| D2SkillCalc.cpp | 11 | skill calc codes 73-110 | c | done | calc_codes.cpp; still 0: mode, pdmn, pdmx (3 skills) |
| D2Skills.cpp | 14 | tabpoints, base levels | c | done | calc_codes.cpp |
| D2SkillDesc.cpp | 1 | tooltip lines (type 77 mana cost) | a | done | bug #9 |
| D2SkillValidation.cpp, D2SkillIcons.cpp | 3, 3 | skill use checks, icon frames | a/d | done | skill_icons.py; validation via D2R's checks |
| D2MslHitFuncs.cpp, D2MslCltHitFuncs.cpp | 33, 3 | missile hit functions | c | done | missile_hits.cpp, 13 slots |
| D2MissileDo.cpp, D2MissileCltDo.cpp, D2Missiles.cpp | 3, 3, 4 | missile do functions | c | done | phalanx (srvdo 46, cltdo 14) |
| D2ItemSpell.cpp | 16 | item use functions (orbs, dyes, keys, containers) | c | done | item_spells.cpp (bugs #10, #11) |
| D2MysticOrbs.cpp | 1 | mystic orb rules | c | done | item_spells.cpp |
| D2Damage.cpp, D2Combat.cpp | 8, 12 | damage block, hit rolls, mitigation: D2Sigma's copy of the vanilla server rules | c | read (2026-10-21) | [combat-stats.md](combat-stats.md); deltas ported (avoid/dodge/evade and block caps, mxl-core combat.cpp); absorb stats 184/189/417 and the max-resist cap open |
| D2Stats.cpp, D2ItemStats.cpp, D2Properties.cpp, D2ItemCalc.cpp | 5, 5, 7, 1 | resist limits, ethereal, requirements; 9 replaced property functions; propertiesext = default skill parameter | c | read (2026-10-21) | [combat-stats.md](combat-stats.md); property funcs 27-29 (properties_ext.cpp), ethereal +25% (combat.cpp), propertiesext defaults (txt_migrate.py); stat_list.cpp layers |
| D2Requirements.cpp | 42 | boolean condition trees (requirements table, 33 rows; 37 types, Median uses 6) gating NPC services only | c | read | [quests-requirements.md](quests-requirements.md); ships as plugin data |
| D2Quests.cpp, D2QuestFlags.cpp | 60, 1 | copy of D2Game's act quests plus ~15 reward hooks (Griswold's Heart, catalyst drops, dialog rewards, Baal relic orb) | c | read | [quests-requirements.md](quests-requirements.md); no uber content here |
| D2SpecialEvents.cpp, D2Events.cpp | 7, 4 | not events: a per-game join filter (min level, name lists; D2GS) and a "call at frame N" queue | e | read (2026-10-21) | [ubers-events.md](ubers-events.md); cut |
| D2Goblins.cpp | 3 | treasure goblins: 15 s per-player tick, chance from levels +0x15c, 20 goblin rows in monstats3 | c | read (2026-10-21) | [ubers-events.md](ubers-events.md) section 4; not ported (M) |
| D2Monsters.cpp, D2MonUMods.cpp, D2AI.cpp, D2MonDesc.cpp | 2, 1, 9, 3 | monster sounds, unique mods, AI, monster tooltips | a/d | read (2026-10-21) | [monsters-ai.md](monsters-ai.md): vanilla D2Game rebuilds except `andariel_ai` (47 bosses; D2R's AI table ends at 155, **fixed in data**: tools/monster_ai_data.py + validate rule; native port open, 3-5 d), the Summoner resist test and a nest init (skipped); mods are data; tooltips are D2R's (d) |
| D2Hirelings.cpp | 23 | hire-time class stat bonuses; the rest is minion scaling (done, pet_scaling.cpp) | a/b | read | [npc-hirelings.md](npc-hirelings.md); hireling.txt already converted |
| D2Resurrect.cpp | 2 | resurrect | c | done | resurrect_2 -> D2R Revive; Habeas Corpus |
| D2Experience.cpp | 1 | experience rules | a | to read | |
| D2Shrines.cpp, D2ObjectFn.cpp, D2Objects.cpp | 2, 10, 5 | object functions (objects2) | c | read (2026-10-21) | Sigma overrides operate fns 15, 31, 35, 36, 37, 70 (rainbow/one-way portal, Halloween chest/exit, level portal helpers); shrines vanilla; [ubers-events.md](ubers-events.md); objects 573+ renumbered (txt-migrate.md) |
| D2Dungeon.cpp, D2Drlg.cpp, D2DrlgWarp.cpp, D2DrlgLevel.cpp, D2Landscape.cpp | 11, 4, 2, 1, 1 | D2Dungeon = the dungeon lockout clock (per-level seal after N seconds, lock groups, packet 0x88); D2DrlgWarp = entry requirements (difficulty, level, class); Drlg/Landscape not read | c | partial | lockout read (2026-10-21), [ubers-events.md](ubers-events.md) section 3, **not ported (L)**; level_types.cpp (automap rows); Median-only areas untested |
| D2Town.cpp | 5 | town rules (act -> town level table) | c | read (2026-10-21) | vanilla; nothing to port |
| D2Craft.cpp | 32 | cube recipe conditions: ops 29-42 beyond D2R's 1-28 (41 recipes, incl. the Cow Level), the player's craft flags; cubemainext's fields mirror op/param/value | c | read (2026-10-20) | ported in mxl-core 0.37.0; details in plugin-spec.md "Cube conditions" |
| D2Runeword.cpp, D2SetItems.cpp | 1, 4 | runeword/set rules | a | to read | |
| D2ItemGeneration.cpp, D2Items.cpp, D2ItemActions.cpp, D2ItemEventFunc.cpp | 1, 2, 7, 5 | item generation and item events | c | partial | D2ItemActions (inventory/stash/belt moves) and D2ItemEventFunc (skill-on-event chance caps) skimmed, not uber-related; rest to read |
| D2Currency.cpp | 8 | Median's currencies (e.g. arcane shards) | c | partial | containers and shard help done (item_spells.cpp) |
| D2Respec.cpp | 4 | respec | c | read (2026-10-21) | [combat-stats.md](combat-stats.md) 1.6: the same sequence as D2R's `0x580f20`; nothing to port |
| D2Achievements.cpp | 2 | achievements: a kill counter (stat 0x58) per flagged monster (monstats3 Achievement, 479 rows) | e | read (2026-10-21) | [ubers-events.md](ubers-events.md); cut |
| D2Linkers.cpp, linker.cpp, D2TxtCompile.cpp | 45, 1, 10 | Sigma table loading and linking | a | done | decoded by the tools (sigma_tables.py); D2R compiles the TXT |
| charstats.cpp | 5 | charstatsexp | a | partial | |

## NPCs and vendors

| Module | Functions | What it is | Bucket | Status | Notes |
|---|---|---|---|---|---|
| D2NPC.cpp, D2NpcActions.cpp, npc_speech.cpp, D2Interact.cpp | 22, 39, 7, 6 | data-driven NPC menus: 16 slots per NPC running npcactions (respec, imbue, sockets, personalize, travel, Cain's gifts), class greetings | c/d | read | [npc-hirelings.md](npc-hirelings.md); vendor stock done (vendors.md); Atma/Geglash shops are gated off (cut); D2Interact n/a |
| D2Stash.cpp | 21 | Median's stash | n/a | n/a | D2R's shared stash and D2RLoader's save format replace it (bug #20 aside) |

## Interface (client)

| Module | Functions | What it is | Bucket | Status | Notes |
|---|---|---|---|---|---|
| D2GameInterface.cpp, D2InventoryScreen.cpp, D2InventoryGrid.cpp, D2InventorySort.cpp | 16, 6, 19, 1 | Median's panels (themes/*/game/panels) | d | open | D2R layouts; skill tree has 8 tabs (ui_layouts.py) but no tab titles/backgrounds yet (bug #3) |
| infographics.cpp | 4 | help panels (infographics) | d | open | |
| D2ItemDesc.cpp | 5 | item tooltips | d | partial | help lines in tooltips (item_spells.cpp) |
| D2Lifebars.cpp, D2Notifications.cpp, D2Chat.cpp, D2ChatCommands.cpp | 9, 6, 13, 1 | HUD, messages, chat | d/e | open | D2R has its own |
| D2Automap.cpp | 10 | automap | c | partial | automap tiles for Median's level types |
| D2LootFilter.cpp, D2AutoPickup.cpp, D2Hotkeys.cpp, D2Input.cpp, player_config.cpp, D2Config.cpp | 12, 3, 4, 6, 17, 4 | convenience features | e | n/a | D2R and D2RLoader have their own loot filter / settings |
| D2HoverUnit.cpp, D2Gfx.cpp, D2CellFiles.cpp, D2Resource.cpp, text.cpp | 1, 1, 1, 5, 3 | drawing | d | partial | HD art: items done (item_art.py), monsters/missiles look-alikes |

## Platform (replaced by D2R / D2RLoader)

D2PacketClient.cpp (18), D2Packet.cpp (11), D2PacketServer.cpp (10), D2Player.cpp (13), D2Party.cpp (5),
D2SaveFile.cpp (6), D2Bnetd.cpp, D2BnetClient.cpp, D2GS.cpp, D2Server.cpp, D2Games.cpp, D2GameClient.cpp,
D2Clients.cpp, D2Multiplayer.cpp, D2RosterUnit.cpp, D2Guard.cpp, mempool.cpp, mpq.cpp, patcher.cpp, dirs.cpp,
os_utils.cpp, text_utils.cpp, D2Math.cpp: networking, saves, loading and the launcher. **n/a**: D2R and D2RLoader
(extended save format, wide states, mod loading) do this; only packets that carry Median-only data would need
D2RLoader's packet services if a ported feature sends them.

## Next to read (largest open modules)
1. ~~D2Quests.cpp + D2Requirements.cpp~~ read (quests-requirements.md). ~~Where the ubers live~~ read ([ubers-events.md](ubers-events.md)):
   they are data plus item spell 16, the dungeon lockout (D2Dungeon.cpp) and goblins; remaining: the Drlg/Landscape files. ~~D2AI.cpp / D2MonDesc.cpp~~ read ([monsters-ai.md](monsters-ai.md)). (The cube craft flags are set by recipe output slots, see plugin-spec.md "Cube conditions".)
2. ~~D2NpcActions.cpp + npcext~~ read (npc-hirelings.md).
3. ~~D2Craft.cpp (32) + cubemainext~~ read: fields are cost/100, second condition (op, param, value), the two stat layers (plugin-spec.md "Cube conditions"). The craft-flag setters are cubemain output-slot effects (slot byte 7/9 sets a player/game flag, 6 pays gold; flags live in game memory only), found 2026-10-21; port spec in the same section, needs the D2R transmute output dispatcher address.
4. ~~D2Hirelings.cpp~~ read (npc-hirelings.md).

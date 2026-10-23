# mxl-core D2RLoader plugin. Build: cmake -S plugin -B build/plugin -A x64 && cmake --build build/plugin --config Release
# (or tools/build_plugin.ps1). tools/install_mod.py copies build/plugin/Release/mxl-core.dll into the mod.
cmake_minimum_required(VERSION 3.29)
project(mxl_core LANGUAGES CXX RC)

set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(../third_party/PluginSDK PluginSDK EXCLUDE_FROM_ALL)

add_library(mxl_core SHARED src/mxl_core.cpp src/d2r.cpp src/sigma_skills.cpp src/states_fix.cpp src/level_types.cpp src/calc_codes.cpp src/item_spells.cpp src/pet_scaling.cpp src/auras.cpp src/area_damage.cpp src/teleport.cpp src/stat_list.cpp src/missile_hits.cpp src/mana_costs.cpp src/cooldowns.cpp src/skill_restrict.cpp src/heat.cpp src/trophy_timer.cpp src/state_trace.cpp src/whirlwind.cpp src/cube_conditions.cpp src/craft_flags.cpp src/requirements.cpp src/quests_act1.cpp src/quests_act2_5.cpp src/dungeon_lockout.cpp src/goblins.cpp src/monster_ai.cpp src/npc_actions.cpp src/npc_services.cpp src/combat.cpp src/stat_columns.cpp src/properties_ext.cpp src/items_ext.cpp src/item_gen.cpp src/requirement_gates.cpp src/hirelings.cpp src/skill_tree.cpp src/waypoints.cpp src/collide_types.cpp src/skill_starts.cpp src/sigma_data.cpp src/testharness.cpp src/item_text.cpp src/tooltip_probe.cpp src/client_messages.cpp src/game_end.cpp src/item_events.cpp src/monster_looks.cpp src/strings.cpp src/mxl_core.rc src/strings_probe.cpp src/no_corpse.cpp src/map_memory.cpp src/info_panels.cpp src/travel.cpp src/skill_damage.cpp src/item_rules.cpp src/quests_rewards.cpp src/gamble.cpp src/engine_gaps.cpp src/stat_rules.cpp src/monster_rules.cpp)
target_sources(mxl_core PRIVATE src/harness_cmds.cpp)  # test harness gameplay commands (off unless mxl-testharness.txt exists)
target_link_libraries(mxl_core PRIVATE D2RLPlugin::D2RLPlugin)
set_target_properties(mxl_core PROPERTIES OUTPUT_NAME "mxl-core")
if(MSVC)
	target_compile_options(mxl_core PRIVATE /W4 /permissive- /EHsc /utf-8)
	set_property(TARGET mxl_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")  # no VC++ runtime dependency
endif()

# Unit tests for the pure parts (not in the plugin DLL): tests/CMakeLists.txt
add_subdirectory(tests)

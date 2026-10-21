# mxl-core D2RLoader plugin. Build: cmake -S plugin -B build/plugin -A x64 && cmake --build build/plugin --config Release
# (or tools/build_plugin.ps1). tools/install_mod.py copies build/plugin/Release/mxl-core.dll into the mod.
cmake_minimum_required(VERSION 3.29)
project(mxl_core LANGUAGES CXX RC)

set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(../third_party/PluginSDK PluginSDK EXCLUDE_FROM_ALL)

add_library(mxl_core SHARED src/mxl_core.cpp src/d2r.cpp src/sigma_skills.cpp src/states_fix.cpp src/level_types.cpp src/calc_codes.cpp src/item_spells.cpp src/pet_scaling.cpp src/auras.cpp src/area_damage.cpp src/teleport.cpp src/stat_list.cpp src/missile_hits.cpp src/mana_costs.cpp src/state_trace.cpp src/whirlwind.cpp src/cube_conditions.cpp src/craft_flags.cpp src/requirements.cpp src/quests_act1.cpp src/dungeon_lockout.cpp src/goblins.cpp src/npc_actions.cpp src/sigma_data.cpp src/mxl_core.rc)
target_link_libraries(mxl_core PRIVATE D2RLPlugin::D2RLPlugin)
set_target_properties(mxl_core PROPERTIES OUTPUT_NAME "mxl-core")
if(MSVC)
	target_compile_options(mxl_core PRIVATE /W4 /permissive- /EHsc /utf-8)
	set_property(TARGET mxl_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")  # no VC++ runtime dependency
endif()

# Unit tests for the pure parts (not in the plugin DLL): cmake --build build/plugin --config Release --target mxl_tests
add_executable(mxl_tests EXCLUDE_FROM_ALL tests/requirements_test.cpp tests/quests_test.cpp tests/dungeon_test.cpp tests/npc_actions_test.cpp src/requirements.cpp)
target_compile_features(mxl_tests PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(mxl_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_executable(craft_tests EXCLUDE_FROM_ALL tests/craft_test.cpp)
target_compile_features(craft_tests PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(craft_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_executable(goblin_tests EXCLUDE_FROM_ALL tests/goblins_test.cpp)
target_compile_features(goblin_tests PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(goblin_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

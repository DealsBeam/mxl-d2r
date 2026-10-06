# mxl-core D2RLoader plugin. Build: cmake -S plugin -B build/plugin -A x64 && cmake --build build/plugin --config Release
# (or tools/build_plugin.ps1). tools/install_mod.py copies build/plugin/Release/mxl-core.dll into the mod.
cmake_minimum_required(VERSION 3.29)
project(mxl_core LANGUAGES CXX RC)

set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(../third_party/PluginSDK PluginSDK EXCLUDE_FROM_ALL)

add_library(mxl_core SHARED src/mxl_core.cpp src/d2r.cpp src/sigma_skills.cpp src/states_fix.cpp src/level_types.cpp src/calc_codes.cpp src/item_spells.cpp src/pet_scaling.cpp src/auras.cpp src/area_damage.cpp src/teleport.cpp src/mxl_core.rc)
target_link_libraries(mxl_core PRIVATE D2RLPlugin::D2RLPlugin)
set_target_properties(mxl_core PROPERTIES OUTPUT_NAME "mxl-core")
if(MSVC)
	target_compile_options(mxl_core PRIVATE /W4 /permissive- /EHsc /utf-8)
	set_property(TARGET mxl_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")  # no VC++ runtime dependency
endif()

# Median XL launcher (docs/launcher.md). One static exe, no runtime to install: WinHTTP, CopyFile2, Win32 controls.
#   cmake -S launcher -B build/launcher -A x64 && cmake --build build/launcher --config Release
# CI cross-builds it like the plugin: -DCMAKE_TOOLCHAIN_FILE=plugin/cmake/xwin-toolchain.cmake (clang-cl + lld-link).
cmake_minimum_required(VERSION 3.20)
project(mxl_launcher LANGUAGES CXX RC)

set(core src/core/sha256.cpp src/core/json.cpp src/core/manifest.cpp src/core/inflate.cpp src/core/zip.cpp src/core/detect.cpp
         src/core/gamecopy.cpp)
add_executable(mxl_launcher WIN32 src/win/main.cpp src/win/app.cpp src/win/game.cpp src/win/http.cpp src/win/util.cpp ${core}
               res/launcher.rc)
set_target_properties(mxl_launcher PROPERTIES OUTPUT_NAME "MedianXL-Launcher")
target_compile_features(mxl_launcher PRIVATE cxx_std_20)
target_compile_definitions(mxl_launcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(mxl_launcher PRIVATE winhttp comctl32 shell32 ole32 version advapi32 user32 gdi32 wintrust crypt32)
if(MSVC)
	target_compile_options(mxl_launcher PRIVATE /W4 /permissive- /EHsc /utf-8 /O1)
	target_link_options(mxl_launcher PRIVATE /MANIFEST:NO)  # the manifest comes from res/launcher.rc
	set_property(TARGET mxl_launcher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")  # no VC++ runtime to install
endif()

add_subdirectory(tests)

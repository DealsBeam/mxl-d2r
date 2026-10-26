# Unit tests of the launcher's portable core (no Windows headers). Included by ../CMakeLists.txt and buildable on its
# own, as CI does on Linux: cmake -S launcher/tests -B build-launcher-tests && cmake --build build-launcher-tests
cmake_minimum_required(VERSION 3.20)
project(mxl_launcher_tests LANGUAGES CXX)

set(core ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)
add_executable(launcher_tests core_tests.cpp ${core}/sha256.cpp ${core}/json.cpp ${core}/manifest.cpp ${core}/inflate.cpp
               ${core}/zip.cpp ${core}/detect.cpp ${core}/gamecopy.cpp)
target_compile_features(launcher_tests PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(launcher_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
	target_compile_options(launcher_tests PRIVATE -Wall -Wextra)
endif()

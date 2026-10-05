# Builds StormLib.dll and CascLib.dll into build\<lib>\Release (used by the Python tools via ctypes).
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
git submodule update --init

cmake -S third_party\StormLib -B build\StormLib -A x64 -DBUILD_SHARED_LIBS=ON -DSTORM_USE_BUNDLED_LIBRARIES=ON -DSTORM_SKIP_INSTALL=ON
cmake --build build\StormLib --config Release

# CascLib still declares an old cmake_minimum_required.
cmake -S third_party\CascLib -B build\CascLib -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build build\CascLib --config Release

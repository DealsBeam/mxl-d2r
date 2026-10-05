# Builds the mxl-core D2RLoader plugin into build\plugin\Release\mxl-core.dll (installed by tools\install_mod.py).
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
git submodule update --init third_party/PluginSDK

cmake -S plugin -B build\plugin -A x64
cmake --build build\plugin --config Release

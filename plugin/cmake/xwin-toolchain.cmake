# Cross-build mxl-core.dll on Linux with clang-cl + lld-link and the MSVC CRT / Windows SDK from xwin (CI: .forgejo/workflows/ci.yml).
#   cmake -S plugin -B build -DCMAKE_TOOLCHAIN_FILE=plugin/cmake/xwin-toolchain.cmake -DCMAKE_BUILD_TYPE=Release

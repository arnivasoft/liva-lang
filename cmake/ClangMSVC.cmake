# ClangMSVC.cmake - Toolchain file for building with LLVM Clang (MSVC ABI)
# Requires: C:\LLVM (official LLVM release with built-in MSVC integration)
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/ClangMSVC.cmake -B build-clang

set(CMAKE_SYSTEM_NAME Windows)

# Compiler (C:\LLVM auto-detects MSVC headers, libs, and target)
set(CMAKE_C_COMPILER "C:/LLVM/bin/clang.exe")
set(CMAKE_CXX_COMPILER "C:/LLVM/bin/clang++.exe")

set(CMAKE_C_FLAGS_INIT "-D_CRT_SECURE_NO_WARNINGS")
set(CMAKE_CXX_FLAGS_INIT "-D_CRT_SECURE_NO_WARNINGS")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")

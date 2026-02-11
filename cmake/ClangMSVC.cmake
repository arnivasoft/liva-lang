# ClangMSVC.cmake - Toolchain file for building with llvm-mingw's Clang targeting MSVC ABI
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/ClangMSVC.cmake -G Ninja -B build-clang

set(CMAKE_SYSTEM_NAME Windows)

# Compiler
set(CMAKE_C_COMPILER "C:/llvm-mingw/bin/clang.exe")
set(CMAKE_CXX_COMPILER "C:/llvm-mingw/bin/clang++.exe")
set(CMAKE_LINKER "C:/llvm-mingw/bin/ld.lld.exe")

# Target MSVC ABI (required for linking against MSVC-built LLVM libs)
set(CMAKE_C_FLAGS_INIT "--target=x86_64-pc-windows-msvc -D_CRT_SECURE_NO_WARNINGS")
set(CMAKE_CXX_FLAGS_INIT "--target=x86_64-pc-windows-msvc -D_CRT_SECURE_NO_WARNINGS")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")

# MSVC include/lib paths
set(MSVC_TOOLS "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC/14.44.35207")
set(WIN_SDK "C:/Program Files (x86)/Windows Kits/10")
set(WIN_SDK_VER "10.0.26100.0")
set(CLANG_BUILTIN "C:/llvm-mingw/lib/clang/21/include")

# System includes (order matters for include_next)
set(_SYS_INCS "-isystem \"${WIN_SDK}/Include/${WIN_SDK_VER}/ucrt\" -isystem \"${MSVC_TOOLS}/include\" -isystem \"${WIN_SDK}/Include/${WIN_SDK_VER}/shared\" -isystem \"${WIN_SDK}/Include/${WIN_SDK_VER}/um\" -isystem \"${CLANG_BUILTIN}\"")
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} ${_SYS_INCS}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${_SYS_INCS}")

# Library paths
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT} -L\"${MSVC_TOOLS}/lib/x64\" -L\"${WIN_SDK}/Lib/${WIN_SDK_VER}/ucrt/x64\" -L\"${WIN_SDK}/Lib/${WIN_SDK_VER}/um/x64\"")

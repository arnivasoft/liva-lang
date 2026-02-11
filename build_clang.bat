@echo off
echo === Building Liva with Clang (MSVC ABI) ===
cmake -G "MinGW Makefiles" ^
  -DCMAKE_TOOLCHAIN_FILE="F:/Cpp_Projects/liva-lang/cmake/ClangMSVC.cmake" ^
  -DCMAKE_MAKE_PROGRAM="C:/llvm-mingw/bin/mingw32-make.exe" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -B "F:\Cpp_Projects\liva-lang\build-clang" ^
  -S "F:\Cpp_Projects\liva-lang" 2>&1
if %ERRORLEVEL% neq 0 (
    echo === CMake Configure FAILED ===
    exit /b 1
)
echo === CMake Configure OK, Building... ===
cmake --build "F:\Cpp_Projects\liva-lang\build-clang" 2>&1
if %ERRORLEVEL% neq 0 (
    echo === Build FAILED ===
    exit /b 1
)
echo === Build SUCCESS ===
echo livac: F:\Cpp_Projects\liva-lang\build-clang\livac.exe
echo Tests: ctest --test-dir F:\Cpp_Projects\liva-lang\build-clang --output-on-failure

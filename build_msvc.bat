@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
echo === MSVC Environment Ready ===
echo CL: %VCToolsInstallDir%

REM wxWidgets configuration:
REM   1. Install: vcpkg install wxwidgets:x64-windows-static
REM   2. Set wxWidgets_ROOT_DIR below to your wxWidgets installation path
REM   3. Or pass -DLIVA_HAS_WXWIDGETS=OFF to build without UI support
REM
REM If wxWidgets is not installed, set LIVA_HAS_WXWIDGETS=OFF
set LIVA_HAS_WXWIDGETS=ON
set VCPKG_ROOT=C:/Users/Kadir/.vcpkg-clion/vcpkg/installed/x64-windows-static

cmake -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DLIVA_HAS_WXWIDGETS=%LIVA_HAS_WXWIDGETS% ^
  -DCMAKE_PREFIX_PATH="%VCPKG_ROOT%" ^
  -DCMAKE_C_COMPILER=cl ^
  -DCMAKE_CXX_COMPILER=cl ^
  -B "F:\Cpp_Projects\liva-lang\build-msvc" ^
  -S "F:\Cpp_Projects\liva-lang" 2>&1
if %ERRORLEVEL% neq 0 (
    echo === CMake Configure FAILED ===
    echo If wxWidgets not found, set LIVA_HAS_WXWIDGETS=OFF above
    exit /b 1
)
echo === CMake Configure OK, Building... ===
cmake --build "F:\Cpp_Projects\liva-lang\build-msvc" --target livac 2>&1
if %ERRORLEVEL% neq 0 (
    echo === Build FAILED ===
    exit /b 1
)
echo === Build SUCCESS ===

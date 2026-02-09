@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
echo === MSVC Environment Ready ===
echo CL: %VCToolsInstallDir%
cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -B "F:\Cpp_Projects\liva-lang\build-msvc" -S "F:\Cpp_Projects\liva-lang" 2>&1
if %ERRORLEVEL% neq 0 (
    echo === CMake Configure FAILED ===
    exit /b 1
)
echo === CMake Configure OK, Building... ===
cmake --build "F:\Cpp_Projects\liva-lang\build-msvc" --target livac 2>&1
if %ERRORLEVEL% neq 0 (
    echo === Build FAILED ===
    exit /b 1
)
echo === Build SUCCESS ===

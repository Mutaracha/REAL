@echo off
setlocal

REM Configures the Visual Studio project with CMake.
REM Note: the "Visual Studio 18 2026" generator requires CMake 4.2 or newer.
REM If CMake is not available, use build.bat instead: it needs nothing but MSVC.

cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo [cmake] CMake was not found in PATH.
    echo [cmake] Either install it (winget install Kitware.CMake) or use build.bat.
    pause
    exit /b 1
)

cmake -S . -B build -A x64
if errorlevel 1 (
    echo [cmake] Default generator failed, trying "Visual Studio 18 2026"...
    cmake -S . -B build -G "Visual Studio 18 2026" -A x64
)

if errorlevel 1 (
    echo [cmake] Trying "Visual Studio 17 2022"...
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
)

if errorlevel 1 (
    echo [cmake] Configuration failed. Check the CMake installation and the generator name.
    pause
    exit /b 1
)

echo [cmake] Building Release...
cmake --build build --config Release --parallel

if errorlevel 1 (
    echo [cmake] Build failed.
    pause
    exit /b 1
)

echo [cmake] Done: %~dp0build\Release\REAL.exe
pause
endlocal

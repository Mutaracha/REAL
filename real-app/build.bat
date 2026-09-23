@echo off
setlocal

REM Builds REAL.exe with the MSVC toolchain only (no CMake required).
REM Works with Visual Studio 2017/2019/2022/2026 and with the standalone
REM "Build Tools for Visual Studio" installation.

cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [build] vswhere.exe not found. Install "Build Tools for Visual Studio" with the C++ workload.
    exit /b 1
)

set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"

if not defined VS_PATH (
    echo [build] No Visual C++ toolset found.
    exit /b 1
)

echo [build] Using %VS_PATH%
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [build] Could not initialise the MSVC environment.
    exit /b 1
)

if not exist build mkdir build

echo [build] Compiling resources...
rc /nologo /fo build\real-app.res res\real-app.rc
if errorlevel 1 exit /b 1

rc /nologo /fo build\manifest.res res\manifest.rc
if errorlevel 1 exit /b 1

echo [build] Compiling sources...
cl /nologo /O2 /MT /EHsc /std:c++17 /utf-8 /W3 ^
    /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DSPDLOG_WCHAR_FILENAMES ^
    /I"deps\expected\include" /I"deps\json" /I"deps\spdlog\include" ^
    /Fo:build\ /Fd:build\REAL.pdb /Fe:build\REAL.exe ^
    src\*.cpp src\Windows\*.cpp src\Http\*.cpp ^
    /link /SUBSYSTEM:WINDOWS /NOLOGO ^
    build\real-app.res build\manifest.res ^
    winhttp.lib ole32.lib oleaut32.lib uuid.lib shell32.lib advapi32.lib user32.lib gdi32.lib

if errorlevel 1 (
    echo [build] Build failed.
    exit /b 1
)

echo [build] Done: %~dp0build\REAL.exe
endlocal

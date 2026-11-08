@echo off
rem Builds spike 01 (Release) with MSVC + Ninja into build\spike01.
setlocal

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"

if not defined VSDIR (
    echo Visual Studio with C++ tools not found
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set "SRC=%~dp0."
set "OUT=%~dp0..\..\build\spike01"

cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%OUT%" || exit /b 1

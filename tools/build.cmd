@echo off
rem Configure, build and test Harmonia on Windows (used locally and in CI).
rem Usage: tools\build.cmd [windows-release|windows-debug]
setlocal

set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=windows-release"

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"

if not defined VSDIR (
    echo Visual Studio with C++ tools not found
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

pushd "%~dp0.."
cmake --preset %PRESET% || goto :fail
cmake --build --preset %PRESET% || goto :fail
ctest --preset %PRESET% || goto :fail
popd
exit /b 0

:fail
popd
exit /b 1

@echo off
rem Builds build\installer\Harmonia-Setup-<version>.exe from the Release build (tools\build.cmd windows-release first).
rem Needs Inno Setup 6 (winget install JRSoftware.InnoSetup).
setlocal

set "ROOT=%~dp0.."
set "ARTEFACTS=%ROOT%\build\windows-release\src\app\Harmonia_artefacts"
set "EXE=%ARTEFACTS%\Release\Harmonia.exe"
set "ICON=%ARTEFACTS%\JuceLibraryCode\icon.ico"

if not exist "%EXE%" (
    echo No Release build: run tools\build.cmd windows-release first.
    exit /b 1
)

rem Inno Setup's compiler: on PATH, or where its own installer puts it (for everyone or for this user).
set "ISCC="
for %%I in (ISCC.exe) do set "ISCC=%%~$PATH:I"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"

if not defined ISCC (
    echo Inno Setup 6 is not installed: winget install JRSoftware.InnoSetup
    exit /b 1
)

"%ISCC%" /Q "/DSourceExe=%EXE%" "/DIconFile=%ICON%" "/DOutputDir=%ROOT%\build\installer" "%ROOT%\installer\Harmonia.iss"
exit /b %ERRORLEVEL%

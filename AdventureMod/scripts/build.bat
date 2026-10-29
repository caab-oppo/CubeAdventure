@echo off
REM ---------------------------------------------------------------------
REM build.bat -- baut AdventureMod.sln ueber MSBuild, ohne dass eine
REM Visual-Studio-"Developer Command Prompt" geoeffnet sein muss. Wird von
REM .vscode/tasks.json aufgerufen, kann aber auch direkt per Doppelklick
REM oder aus einer normalen cmd.exe heraus benutzt werden.
REM
REM Aufruf:  build.bat [Debug|Release]   (Standard: Debug)
REM ---------------------------------------------------------------------
setlocal enabledelayedexpansion

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Debug

REM vswhere.exe wird seit VS2017 von jedem VS-/Build-Tools-Installer immer
REM mitinstalliert, unabhaengig von Edition (Community/Professional/
REM Enterprise/BuildTools) -- deshalb der zuverlaessigste Weg, MSBuild.exe
REM zu finden, ohne Pfade fest zu verdrahten.
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe

if not exist "%VSWHERE%" (
    echo [Fehler] vswhere.exe wurde nicht gefunden unter:
    echo   %VSWHERE%
    echo Ist Visual Studio 2022 ^(oder die "Build Tools fuer Visual Studio 2022"^) installiert?
    exit /b 1
)

set MSBUILD=
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set MSBUILD=%%i
)

if not defined MSBUILD (
    echo [Fehler] MSBuild.exe wurde nicht gefunden. Stelle sicher, dass die
    echo Workload "Desktopentwicklung mit C++" in Visual Studio installiert ist.
    exit /b 1
)

echo Verwende MSBuild: %MSBUILD%
echo Konfiguration:    %CONFIG% ^| Win32
echo.

"%MSBUILD%" "%~dp0..\..\AdventureMod.sln" /p:Configuration=%CONFIG% /p:Platform=Win32 /m /nologo /v:minimal
set RC=%ERRORLEVEL%

if %RC% neq 0 (
    echo.
    echo [Fehler] Build fehlgeschlagen ^(ExitCode %RC%^).
    echo Haeufige Ursachen: DXSDK_DIR nicht gesetzt ^(siehe README.md,
    echo "Build-Voraussetzungen"^), oder MinHook_134_lib fehlt im Solution-Ordner.
)

exit /b %RC%

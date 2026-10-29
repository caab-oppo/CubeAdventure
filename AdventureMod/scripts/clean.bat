@echo off
REM ---------------------------------------------------------------------
REM clean.bat -- raeumt die Build-Ausgaben von AdventureMod.sln auf
REM (MSBuild-Target "Clean"). Gegenstueck zu build.bat, siehe dort fuer
REM Details zur MSBuild-Suche via vswhere.
REM
REM Aufruf:  clean.bat [Debug|Release]   (Standard: Debug)
REM ---------------------------------------------------------------------
setlocal enabledelayedexpansion

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Debug

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe

if not exist "%VSWHERE%" (
    echo [Fehler] vswhere.exe wurde nicht gefunden unter:
    echo   %VSWHERE%
    exit /b 1
)

set MSBUILD=
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set MSBUILD=%%i
)

if not defined MSBUILD (
    echo [Fehler] MSBuild.exe wurde nicht gefunden.
    exit /b 1
)

echo Verwende MSBuild: %MSBUILD%
echo Konfiguration:    %CONFIG% ^| Win32 ^(Clean^)
echo.

"%MSBUILD%" "%~dp0..\..\AdventureMod.sln" /t:Clean /p:Configuration=%CONFIG% /p:Platform=Win32 /nologo /v:minimal
exit /b %ERRORLEVEL%

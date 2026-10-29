@echo off
REM Startet Veroeffentlichen.ps1 (ohne die PowerShell-Ausfuehrungsrichtlinie dauerhaft zu aendern).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Veroeffentlichen.ps1"
pause

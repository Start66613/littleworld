@echo off
cd /d "%~dp0"
pwsh -NoProfile -File "%~dp0tools\run_visual_v4.ps1"
if errorlevel 1 pause

@echo off
rem Little World - launcher for the Windows desktop demo.
cd /d "%~dp0"
if exist "build\host\LittleWorld.exe" (
    start "" "build\host\LittleWorld.exe"
    exit /b 0
)
echo.
echo The demo has not been built yet.
echo Run this once from PowerShell:
echo     pwsh -NoProfile -ExecutionPolicy Bypass -File tools\build_demo.ps1
echo.
pause
exit /b 1

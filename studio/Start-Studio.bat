@echo off
setlocal
cd /d "%~dp0"
title Little World Studio
where node >nul 2>nul
if errorlevel 1 (
  echo Please install Node.js 24 LTS from https://nodejs.org then run this file again.
  pause
  exit /b 1
)
node -e "const [a,b]=process.versions.node.split('.').map(Number);process.exit(a>22||(a===22&&b>=18)?0:1)"
if errorlevel 1 (
  echo Node.js 22.18 or newer is required. Node.js 24 LTS is recommended.
  pause
  exit /b 1
)
if not exist node_modules\vite\bin\vite.js (
  echo First run: installing editor dependencies. No personal content is sent.
  call npm.cmd ci --no-fund --cache "%~dp0private\npm-cache"
  if errorlevel 1 goto failed
)
echo Building Little World Studio...
call npm.cmd run build
if errorlevel 1 goto failed
echo Keep this window open while editing. Close it to stop Studio.
node server\start.mjs --open
if errorlevel 1 goto failed
exit /b 0
:failed
echo Studio could not start. Check the message above. Your projects are in studio\private\projects.
pause
exit /b 1

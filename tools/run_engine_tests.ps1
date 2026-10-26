# Build and run the lw_engine host tests with MSVC.
#
# lw_engine is deliberately pure C: nine-slice geometry, scene projection and
# the one clock have no LVGL or ESP-IDF dependency, so every rule in them is
# verifiable on the build machine. This script is the only supported way to run
# them; it fails on any compiler warning (/W4 /WX) because a warning in this
# code is a coordinate that will be wrong on the panel.
#
# Usage: powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/run_engine_tests.ps1
# Exit code 0 = compiled cleanly and every check passed.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $root 'build/host_tests'
$logDir = Join-Path $root 'reports/logs'
New-Item -ItemType Directory -Force -Path $buildDir, $logDir | Out-Null
$logFile = Join-Path $logDir 'engine-tests.log'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw "vswhere.exe not found: $vswhere" }
$install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) { throw 'No Visual Studio installation with the C++ toolchain was found.' }
$vcvars = Join-Path $install 'VC/Auxiliary/Build/vcvars64.bat'
$dump = cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $dump) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path ("env:" + $Matches[1]) -Value $Matches[2] }
}
$cl = (Get-Command cl.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $cl) { throw 'cl.exe is not available after vcvars64.' }
Write-Output "compiler: $cl"

$include = @(
    ('/I' + (Join-Path $root 'firmware/components/lw_engine/include')),
    ('/I' + (Join-Path $root 'firmware/components/lw_core/include'))
)
$sources = @(
    (Join-Path $root 'tests/host/test_lw_engine.c'),
    (Join-Path $root 'firmware/components/lw_engine/src/lw_ui9.c'),
    (Join-Path $root 'firmware/components/lw_engine/src/lw_ui_asset.c'),
    (Join-Path $root 'firmware/components/lw_engine/src/lw_stage.c'),
    (Join-Path $root 'firmware/components/lw_engine/src/lw_day.c'),
    (Join-Path $root 'firmware/components/lw_core/src/lw_time.c')
)

$exe = Join-Path $buildDir 'lw_engine_tests.exe'
$obj = Join-Path $buildDir 'obj_engine/'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$clArgs = @('/nologo', '/std:c11', '/W4', '/WX', '/utf-8', '/O2', '/D_CRT_SECURE_NO_WARNINGS',
    ('/Fo' + $obj), ('/Fe' + $exe)) + $include + $sources

Set-Content -Path $logFile -Value '' -Encoding utf8
$out = & $cl @clArgs 2>&1
$out | Tee-Object -FilePath $logFile -Append | Write-Output
if ($LASTEXITCODE -ne 0) {
    Write-Output 'BUILD FAILED'
    exit 1
}
$run = & $exe 2>&1
$code = $LASTEXITCODE
$run | Tee-Object -FilePath $logFile -Append | Write-Output
exit $code

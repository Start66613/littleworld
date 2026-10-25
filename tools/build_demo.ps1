# Build (and optionally run) the Little World Windows demo.
#
# Usage:
#   pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build_demo.ps1
#   pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build_demo.ps1 -Run
#   pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build_demo.ps1 -Reconfigure

[CmdletBinding()]
param(
    [switch]$Run,
    [switch]$Reconfigure,
    [string]$LvglDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$hostDir = Join-Path $root 'host'
$buildDir = Join-Path $root 'build/host'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw "vswhere.exe not found: $vswhere" }
$install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) { throw 'No Visual Studio installation with the C++ toolchain was found.' }
$vcvars = Join-Path $install 'VC/Auxiliary/Build/vcvars64.bat'
$setup = cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $setup) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path ("env:" + $Matches[1]) -Value $Matches[2]
    }
}

# cmake/ninja come from the ESP-IDF tools already installed on this machine.
. (Join-Path $PSScriptRoot 'activate_env.ps1') | Out-Null
if (-not $LvglDir) {
    $LvglDir = Join-Path $root 'firmware/managed_components/lvgl__lvgl'
    if (-not (Test-Path -LiteralPath (Join-Path $LvglDir 'lvgl.h'))) {
        $LvglDir = 'D:\Espressif\LittleWorld\sources\waveshare-7b\examples\esp-idf\09_lvgl_demo_v9\managed_components\lvgl__lvgl'
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $LvglDir 'lvgl.h'))) { throw 'LVGL 9.5.0 sources missing; pass -LvglDir explicitly.' }

if ($Reconfigure -or -not (Test-Path (Join-Path $buildDir 'build.ninja'))) {
    Write-Host 'configuring...'
    & cmake -S $hostDir -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DLVGL_DIR=$LvglDir" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
}

Write-Host 'building...'
& ninja -C $buildDir
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

$exe = Join-Path $buildDir 'LittleWorld.exe'
Write-Output ''
Write-Output "demo: $exe"
Get-Item -LiteralPath $exe | Select-Object Name, Length, LastWriteTime | Format-List

if ($Run) {
    & $exe
}

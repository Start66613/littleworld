# Build and run the Little World persistence and service tests with MSVC.
#
# Covers the framed record store (lw_store) against a substitute NVS backend and
# the system service model (lw_system). Both are plain C with no ESP-IDF or LVGL
# dependency, so the same sources run on the build machine.
#
# Usage: pwsh -NoProfile -ExecutionPolicy Bypass -File tools/run_backend_tests.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $root 'build/host_tests'
$logDir = Join-Path $root 'reports/logs'
New-Item -ItemType Directory -Force -Path $buildDir, $logDir | Out-Null
$logFile = Join-Path $logDir 'backend-tests.log'

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
    ('/I' + (Join-Path $root 'firmware/components/lw_core/include')),
    ('/I' + (Join-Path $root 'firmware/components/lw_backend/include')),
    ('/I' + (Join-Path $root 'firmware/components/lw_system/include')),
    ('/I' + (Join-Path $root 'firmware/components/lw_net/include')),
    ('/I' + (Join-Path $root 'rpg/include'))
)

$common = @(
    (Join-Path $root 'firmware/components/lw_core/src/lw_state.c'),
    (Join-Path $root 'firmware/components/lw_core/src/lw_pet.c'),
    (Join-Path $root 'firmware/components/lw_core/src/lw_time.c'),
    (Join-Path $root 'firmware/components/lw_backend/src/lw_store.c'),
    (Join-Path $root 'firmware/components/lw_system/src/lw_system.c'),
    (Join-Path $root 'firmware/components/lw_net/src/lw_wifi_model.c'),
    (Join-Path $root 'firmware/components/lw_net/src/lw_time_service.c'),
    (Join-Path $root 'firmware/components/lw_net/src/lw_audio.c'),
    (Join-Path $root 'firmware/components/lw_net/src/lw_services.c'),
    (Join-Path $root 'tests/host/lw_net_esp_stub.c')
)

$suites = @(
    @{ name = 'store'; source = (Join-Path $root 'tests/host/test_lw_store.c') },
    @{ name = 'system'; source = (Join-Path $root 'tests/host/test_lw_system.c') },
    @{ name = 'net'; source = (Join-Path $root 'tests/host/test_lw_net.c') }
)

$failed = $false
Set-Content -Path $logFile -Value '' -Encoding utf8
foreach ($suite in $suites) {
    $exe = Join-Path $buildDir ("lw_{0}_tests.exe" -f $suite.name)
    $obj = Join-Path $buildDir ("obj_{0}/" -f $suite.name)
    New-Item -ItemType Directory -Force -Path $obj | Out-Null
    $clArgs = @('/nologo', '/std:c11', '/W4', '/WX', '/utf-8', '/O2', '/D_CRT_SECURE_NO_WARNINGS',
        ('/Fo' + $obj), ('/Fe' + $exe)) + $include + @($suite.source) + $common
    Write-Output ("building {0} tests..." -f $suite.name)
    $out = & $cl @clArgs 2>&1
    $out | Tee-Object -FilePath $logFile -Append | Write-Output
    if ($LASTEXITCODE -ne 0) {
        Write-Output ("BUILD FAILED: {0}" -f $suite.name)
        $failed = $true
        continue
    }
    $run = & $exe 2>&1
    $code = $LASTEXITCODE
    $run | Tee-Object -FilePath $logFile -Append | Write-Output
    if ($code -ne 0) { $failed = $true }
}

if ($failed) { Write-Output ''; Write-Output 'BACKEND TESTS FAILED'; exit 1 }
Write-Output ''
Write-Output 'BACKEND TESTS PASSED'
exit 0

# Build and run the Little World core unit tests with MSVC.
#
# The lw_core / arcade_core components are plain C with no ESP-IDF or LVGL
# dependency, so they can be compiled and executed on the build machine.
#
# Usage: pwsh -NoProfile -ExecutionPolicy Bypass -File tools/run_host_tests.ps1
# Exit code 0 = compiled cleanly and every check passed.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $root 'build/host_tests'
$logDir = Join-Path $root 'reports/logs'
New-Item -ItemType Directory -Force -Path $buildDir, $logDir | Out-Null
$logFile = Join-Path $logDir 'host-tests.log'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw "vswhere.exe not found: $vswhere" }
$install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) { throw 'No Visual Studio installation with the C++ toolchain was found.' }
$vcvars = Join-Path $install 'VC/Auxiliary/Build/vcvars64.bat'
$dump = cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $dump) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path ("env:" + $Matches[1]) -Value $Matches[2]
    }
}
$cl = (Get-Command cl.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $cl) { throw 'cl.exe is not available after vcvars64.' }
Write-Output "compiler: $cl"

$include = @(
    ('/I' + (Join-Path $root 'firmware/components/lw_core/include')),
    ('/I' + (Join-Path $root 'firmware/components/arcade_core/include'))
)

$sources = @(
    (Join-Path $root 'tests/host/test_lw_core.c'),
    (Join-Path $root 'firmware/components/lw_core/src/lw_time.c'),
    (Join-Path $root 'firmware/components/lw_core/src/lw_pet.c'),
    (Join-Path $root 'firmware/components/lw_core/src/lw_state.c'),
    (Join-Path $root 'firmware/components/lw_core/src/lw_memories.c'),
    (Join-Path $root 'firmware/components/lw_core/src/lw_platform.c')
)

$exe = Join-Path $buildDir 'lw_core_tests.exe'
$obj = Join-Path $buildDir 'obj/'
New-Item -ItemType Directory -Force -Path $obj | Out-Null

$clArgs = @(
    '/nologo', '/std:c11', '/W4', '/WX', '/utf-8', '/O2',
    '/D_CRT_SECURE_NO_WARNINGS',
    ('/Fo' + $obj),
    ('/Fe' + $exe)
) + $include + $sources

Write-Output 'building host tests...'
$compileOut = & $cl @clArgs 2>&1
$compileCode = $LASTEXITCODE
$compileOut | Tee-Object -FilePath $logFile | Write-Output
if ($compileCode -ne 0) {
    Write-Output "HOST TEST BUILD FAILED (exit $compileCode)"
    exit 1
}

Write-Output ''
Write-Output 'running host tests...'
$runOut = & $exe 2>&1
$runCode = $LASTEXITCODE
$runOut | Tee-Object -FilePath $logFile -Append | Write-Output
if ($runCode -ne 0) {
    Write-Output "HOST TESTS FAILED (exit $runCode)"
    exit 1
}
Write-Output ''
Write-Output 'HOST TESTS PASSED'
exit 0

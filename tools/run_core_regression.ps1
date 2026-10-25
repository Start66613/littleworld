# Run the pure-C host suites that do not need LVGL: duo model, RPG core,
# storage helpers, framed store and system services.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_core_regression.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $root 'build/host_tests'
$logDir = Join-Path $root 'reports/logs'
New-Item -ItemType Directory -Force -Path $buildDir, $logDir | Out-Null
$logFile = Join-Path $logDir 'core-regression.log'
Set-Content -Path $logFile -Value '' -Encoding utf8

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $install 'VC/Auxiliary/Build/vcvars64.bat'
$dump = cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $dump) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path ("env:" + $Matches[1]) -Value $Matches[2] }
}

$include = @(
    (Join-Path $root 'firmware/components/lw_core/include'),
    (Join-Path $root 'firmware/components/lw_duo/include'),
    (Join-Path $root 'firmware/components/lw_storage/include'),
    (Join-Path $root 'firmware/components/lw_backend/include'),
    (Join-Path $root 'firmware/components/lw_system/include'),
    (Join-Path $root 'rpg/include'),
    (Join-Path $root 'rpg/data')
) | ForEach-Object { '/I' + $_ }

$sources = @()
# ESP-only translation units are guarded by ESP_PLATFORM and would compile to
# empty files on the host, which /WX rejects.
$espOnly = @('lw_system_bsp.c', 'lw_hw.c', 'lw_store_nvs.c', 'lw_sd.c')
foreach ($dir in @('lw_core', 'lw_duo', 'lw_storage', 'lw_backend', 'lw_system')) {
    $sources += Get-ChildItem (Join-Path $root "firmware/components/$dir/src") -Filter *.c |
        Where-Object { $espOnly -notcontains $_.Name } |
        ForEach-Object { $_.FullName }
}
$sources += Get-ChildItem (Join-Path $root 'rpg/src') -Filter *.c | ForEach-Object { $_.FullName }
$sources += Get-ChildItem (Join-Path $root 'rpg/data') -Filter *.c | ForEach-Object { $_.FullName }

$suites = @('test_lw_duo', 'test_rpg', 'test_storage')
$failed = $false
foreach ($suite in $suites) {
    $exe = Join-Path $buildDir ("{0}.exe" -f $suite)
    $obj = Join-Path $buildDir ("obj_{0}/" -f $suite)
    New-Item -ItemType Directory -Force -Path $obj | Out-Null
    $args = @('/nologo', '/std:c11', '/W4', '/WX', '/utf-8', '/O2', '/D_CRT_SECURE_NO_WARNINGS',
        ('/Fo' + $obj), ('/Fe' + $exe)) + $include + @((Join-Path $root "tests/host/$suite.c")) + $sources
    Write-Output ("building {0}..." -f $suite)
    $out = & cl @args 2>&1
    if ($LASTEXITCODE -ne 0) {
        $out | Select-Object -Last 12 | Tee-Object -FilePath $logFile -Append | Write-Output
        Write-Output ("BUILD FAILED: {0}" -f $suite)
        $failed = $true
        continue
    }
    $exeArgs = @()
    if ($suite -eq 'test_storage') { $tmp = Join-Path $buildDir 'tmp_storage'; New-Item -ItemType Directory -Force -Path $tmp | Out-Null; $exeArgs = @($tmp) }
    $run = & $exe @exeArgs 2>&1
    $code = $LASTEXITCODE
    $run | Tee-Object -FilePath $logFile -Append | Write-Output
    if ($code -ne 0) { $failed = $true }
}

if ($failed) { Write-Output 'CORE REGRESSION FAILED'; exit 1 }
Write-Output 'CORE REGRESSION PASSED'
exit 0

# Windows regression for the OS V3 tree, without pwsh (this machine only has
# Windows PowerShell 5.1). Mirrors tools/run_os_v2.ps1 -Test but builds with
# build_demo.ps1 directly and keeps every artifact under reports/os-v2.
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
. (Join-Path $PSScriptRoot 'activate_env.ps1')

$python = 'D:\LittleWorld-Meowa\tools\venv\Scripts\python.exe'
$source = 'D:\Espressif\LittleWorld\sources\DesktopPet'
$pack = Join-Path $root 'private/pixel-duo/qianqian/qianqian.lwpx'

if (-not (Test-Path -LiteralPath $pack)) {
    Write-Output 'exporting the qianqian pack from the pinned DesktopPet source...'
    & $python tools/analyze_desktop_pet.py --source $source --output (Join-Path $root 'private/pixel-duo/qianqian') --report (Join-Path $root 'reports/pixel-duo/qianqian-analysis.json')
    if ($LASTEXITCODE -ne 0) { throw 'DesktopPet export failed' }
}

Write-Output 'building the Windows host...'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools/build_demo.ps1') -Reconfigure
if ($LASTEXITCODE -ne 0) { throw 'host build failed' }

$out = Join-Path $root 'reports/ui-v2/os-v2'
New-Item -ItemType Directory -Force -Path $out, (Join-Path $root 'reports/os-v2') | Out-Null

foreach ($exe in 'SystemModelTests', 'PixelDuoModelTests', 'RPGModelTests') {
    Write-Output "=== $exe ==="
    & (Join-Path $root "build/host/$exe.exe")
    if ($LASTEXITCODE -ne 0) { throw "$exe failed" }
}
Write-Output '=== StorageTests ==='
& (Join-Path $root 'build/host/StorageTests.exe') (Join-Path $root 'build/host/storage-fixtures')
if ($LASTEXITCODE -ne 0) { throw 'StorageTests failed' }

Write-Output '=== OS V3 prototype ==='
& (Join-Path $root 'build/host/LittleWorld.exe') --os-v2-test $pack $out
$result = $LASTEXITCODE
& $python tools/export_os_evidence.py --root $root --input 'reports/ui-v2/os-v2' --report-dir 'reports/os-v2'
if ($result -ne 0) { throw "OS prototype checks failed ($result)" }
Write-Output 'WINDOWS REGRESSION OK'

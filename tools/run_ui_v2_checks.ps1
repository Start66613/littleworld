[CmdletBinding()]
param(
    [string]$Python = 'D:\LittleWorld-Meowa\tools\venv\Scripts\python.exe',
    [switch]$SkipBaseline
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
. (Join-Path $PSScriptRoot 'activate_env.ps1')
New-Item -ItemType Directory -Force -Path "$root/reports/ui-v2/technical","$root/reports/ui-v2/baseline" | Out-Null
if (-not (Test-Path -LiteralPath $Python)) { throw 'Pass -Python pointing to a D-drive Python with Pillow 13.0.0; no paid generation API is used.' }
& powershell.exe -NoProfile -File "$PSScriptRoot/run_host_tests.ps1"
if ($LASTEXITCODE -ne 0) { throw 'Core tests failed' }
& powershell.exe -NoProfile -File "$PSScriptRoot/build_demo.ps1"
if ($LASTEXITCODE -ne 0) { throw 'Host build failed' }
$exe=Join-Path $root 'build/host/LittleWorld.exe'
& $exe --ui-v2-test "$root/reports/ui-v2/technical"
if ($LASTEXITCODE -ne 0) { throw 'UI resource/component tests failed' }
# Run sequentially: Windows locks an EXE while it runs; no parallel relink.
if (-not $SkipBaseline) {
    & $exe --auto "$root/reports/ui-v2/baseline"
    if ($LASTEXITCODE -ne 0) { throw 'Stable UI regression failed' }
}
& $Python -m unittest discover -s tests -p 'test_asset_audit.py' -v
if ($LASTEXITCODE -ne 0) { throw 'Asset validator tests failed' }
& $Python "$PSScriptRoot/capture_ui_v2.py" --root $root
if ($LASTEXITCODE -ne 0) { throw 'Screenshot evidence export failed' }
Write-Output 'Host checks passed. Final artwork acceptance and device tests have NOT been performed.'

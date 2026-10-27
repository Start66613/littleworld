[CmdletBinding()]
param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
New-Item -ItemType Directory -Force reports/logs,reports/ui-v2/v42-fallback,reports/ui-v2/v42-missing-art,reports/ui-v2/v42-legacy,reports/ui-v2/v42-technical | Out-Null
$run=@{Test=$true}; if ($SkipBuild) {$run.SkipBuild=$true}
& tools/run_visual_v4_2.ps1 @run *> reports/logs/v42-verified-host.log
if ($LASTEXITCODE -ne 0) {throw 'Full V4.2 OS regression failed'}
& build/host/LittleWorld.exe --os-v2-test - reports/ui-v2/v42-fallback *> reports/logs/v42-verified-fallback.log
if ($LASTEXITCODE -ne 0) {throw 'Missing character regression failed'}
try {
    $missing=Join-Path $root 'private/intentionally-missing-art'
    $env:LW_VISUAL_ASSETS=$missing; $env:LW_POLISH_ASSETS=$missing
    $env:LW_POLISH_PACK=$missing; $env:LW_UI_PACK=$missing
    & build/host/LittleWorld.exe --os-v2-test private/pixel-duo/qianqian/qianqian.lwpx reports/ui-v2/v42-missing-art *> reports/logs/v42-verified-missing-art.log
    if ($LASTEXITCODE -ne 0) {throw 'Missing optional art regression failed'}
} finally {
    $env:LW_VISUAL_ASSETS=Join-Path $root 'assets/visual-v4/runtime'
    $env:LW_POLISH_ASSETS=Join-Path $root 'assets/visual-v4-2/runtime'
    $env:LW_POLISH_PACK=Join-Path $env:LW_POLISH_ASSETS 'polish.lwp'
    $env:LW_UI_PACK=Join-Path $env:LW_POLISH_ASSETS 'ui.lwp'
}
& build/host/LittleWorld.exe --ui-v2-test reports/ui-v2/v42-technical *> reports/logs/v42-verified-resources.log
if ($LASTEXITCODE -ne 0) {throw 'Resource lifecycle regression failed'}
& tools/run_host_tests.ps1 *> reports/logs/v42-verified-core.log
if ($LASTEXITCODE -ne 0) {throw 'Core regression failed'}
& D:/LittleWorld-Meowa/tools/venv/Scripts/python.exe -m unittest discover -s tests -p 'test_*.py' -v *> reports/logs/v42-verified-python.log
if ($LASTEXITCODE -ne 0) {throw 'Python tools regression failed'}
& tools/gen_fonts.ps1 -Check *> reports/logs/v42-verified-fonts.log
if ($LASTEXITCODE -ne 0) {throw 'Font subset differs from source'}
& build/host/LittleWorld.exe --auto reports/ui-v2/v42-legacy *> reports/logs/v42-verified-legacy.log
if ($LASTEXITCODE -ne 0) {throw 'Legacy UI regression failed'}
Get-Content reports/logs/v42-verified-host.log -Tail 4
Get-Content reports/logs/v42-verified-fallback.log -Tail 2
Get-Content reports/logs/v42-verified-missing-art.log -Tail 2

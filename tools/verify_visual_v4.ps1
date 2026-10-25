[CmdletBinding()]
param([switch]$SkipBuild,[switch]$SkipLegacy)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
New-Item -ItemType Directory -Force reports/logs,reports/ui-v2/v4-fallback,reports/ui-v2/v4-missing-scene,reports/ui-v2/v4-legacy,reports/ui-v2/v4-technical | Out-Null
$argsRun=@{Test=$true}; if ($SkipBuild) {$argsRun.SkipBuild=$true}
& tools/run_visual_v4.ps1 @argsRun *> reports/logs/v4-verified-host.log
if ($LASTEXITCODE -ne 0) {throw 'Full V4 OS regression failed'}
& build/host/LittleWorld.exe --os-v2-test - reports/ui-v2/v4-fallback *> reports/logs/v4-verified-fallback.log
if ($LASTEXITCODE -ne 0) {throw 'Missing character regression failed'}
try {
    $env:LW_VISUAL_ASSETS=Join-Path $root 'private/intentionally-missing-scenes'
    & build/host/LittleWorld.exe --os-v2-test private/pixel-duo/qianqian/qianqian.lwpx reports/ui-v2/v4-missing-scene *> reports/logs/v4-verified-missing-scene.log
    if ($LASTEXITCODE -ne 0) {throw 'Missing scene regression failed'}
} finally {$env:LW_VISUAL_ASSETS=Join-Path $root 'assets/visual-v4/runtime'}
& build/host/LittleWorld.exe --ui-v2-test reports/ui-v2/v4-technical *> reports/logs/v4-verified-resources.log
if ($LASTEXITCODE -ne 0) {throw 'Resource lifecycle regression failed'}
& tools/run_host_tests.ps1 *> reports/logs/v4-verified-core.log
if ($LASTEXITCODE -ne 0) {throw 'Core regression failed'}
& D:/LittleWorld-Meowa/tools/venv/Scripts/python.exe -m unittest discover -s tests -p 'test_*.py' -v *> reports/logs/v4-verified-python.log
if ($LASTEXITCODE -ne 0) {throw 'Python tools regression failed'}
& tools/gen_fonts.ps1 -Check *> reports/logs/v4-verified-fonts.log
if ($LASTEXITCODE -ne 0) {throw 'Font subset differs from source'}
if (-not $SkipLegacy) {
    & build/host/LittleWorld.exe --auto reports/ui-v2/v4-legacy *> reports/logs/v4-verified-legacy.log
    if ($LASTEXITCODE -ne 0) {throw 'Legacy UI regression failed'}
}
Get-Content reports/logs/v4-verified-host.log -Tail 5
Get-Content reports/logs/v4-verified-fallback.log -Tail 3
Get-Content reports/logs/v4-verified-missing-scene.log -Tail 3
if (-not $SkipLegacy) {Get-Content reports/logs/v4-verified-legacy.log -Tail 3}

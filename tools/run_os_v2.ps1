[CmdletBinding()]
param([switch]$Test,[switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
. "$PSScriptRoot/activate_env.ps1"
$python='D:\LittleWorld-Meowa\tools\venv\Scripts\python.exe'
$source='D:\Espressif\LittleWorld\sources\DesktopPet'
$actual=& git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actual.Trim() -ne 'b00a7005f4889644737092dbc7379e2a9b011315' -or (& git -C $source status --porcelain)) { throw 'Pinned clean DesktopPet source required' }
$pack="$root/private/pixel-duo/qianqian/qianqian.lwpx"
if (-not (Test-Path -LiteralPath $pack)) {
    & $python tools/analyze_desktop_pet.py --source $source --output "$root/private/pixel-duo/qianqian" --report "$root/reports/pixel-duo/qianqian-analysis.json"
    if ($LASTEXITCODE -ne 0) {throw 'Source export failed'}
}
if (-not $SkipBuild) {
    & pwsh -NoProfile -File tools/build_demo.ps1 -Reconfigure
    if ($LASTEXITCODE -ne 0) {throw 'Host build failed'}
}
if ($Test) {
    $out="$root/reports/ui-v2/os-v2"
    New-Item -ItemType Directory -Force -Path $out,"$root/reports/os-v2" | Out-Null
    & "$root/build/host/SystemModelTests.exe"
    if ($LASTEXITCODE -ne 0) {throw 'System service tests failed'}
    & "$root/build/host/PixelDuoModelTests.exe"
    if ($LASTEXITCODE -ne 0) {throw 'Character model regression failed'}
    & "$root/build/host/RPGModelTests.exe"
    if ($LASTEXITCODE -ne 0) {throw 'Frozen RPG core tests failed'}
    & "$root/build/host/LittleWorld.exe" --os-v2-test $pack $out
    $result=$LASTEXITCODE
    & $python tools/export_os_evidence.py --root $root
    if ($result -ne 0 -or $LASTEXITCODE -ne 0) {throw 'OS prototype checks failed; see reports/logs and local screenshots'}
} else {
    & "$root/build/host/LittleWorld.exe" --os-v2 $pack
    if ($LASTEXITCODE -ne 0) {throw 'OS prototype exited with failure'}
}

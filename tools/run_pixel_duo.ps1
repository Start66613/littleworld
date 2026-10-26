[CmdletBinding()]
param(
    [switch]$Test,
    [string]$DesktopPet = 'D:\Espressif\LittleWorld\sources\DesktopPet',
    [string]$Python = 'D:\LittleWorld-Meowa\tools\venv\Scripts\python.exe',
    [switch]$SkipBuild
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
. (Join-Path $PSScriptRoot 'activate_env.ps1')
$actual=& git -C $DesktopPet rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actual.Trim() -ne 'b00a7005f4889644737092dbc7379e2a9b011315') { throw 'Expected pinned DesktopPet commit b00a700; do not guess another character source.' }
$sourceChanges=& git -C $DesktopPet status --porcelain --untracked-files=normal
if ($LASTEXITCODE -ne 0 -or $sourceChanges) { throw 'DesktopPet source must be clean to reproduce the pinned export; preserve local changes separately.' }
$private=Join-Path $root 'private/pixel-duo'
New-Item -ItemType Directory -Force -Path "$private/screenshots","$root/reports/pixel-duo" | Out-Null
& $Python "$PSScriptRoot/analyze_desktop_pet.py" --source $DesktopPet --output "$private/qianqian" --report "$root/reports/pixel-duo/qianqian-analysis.json"
if ($LASTEXITCODE -ne 0) { throw 'Source extraction failed' }
if (-not $SkipBuild) {
    & powershell.exe -NoProfile -File "$PSScriptRoot/build_demo.ps1" -Reconfigure
    if ($LASTEXITCODE -ne 0) { throw 'Host build failed' }
}
$exe=Join-Path $root 'build/host/LittleWorld.exe'
if ($Test) {
    & "$root/build/host/PixelDuoModelTests.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Duo model tests failed' }
    & $exe --pixel-duo-test "$private/qianqian/qianqian.lwpx" "$private/screenshots"
    if ($LASTEXITCODE -ne 0) { throw 'Real LVGL duo tests failed' }
    & $Python "$PSScriptRoot/export_pixel_evidence.py" --root $root
    if ($LASTEXITCODE -ne 0) { throw 'Screenshot export failed' }
} else {
    & $exe --pixel-duo "$private/qianqian/qianqian.lwpx"
    if ($LASTEXITCODE -ne 0) { throw 'Pixel preview failed' }
}

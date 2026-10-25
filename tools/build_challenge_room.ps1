[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot/activate_env.ps1" | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$install=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $install 'VC/Auxiliary/Build/vcvars64.bat'
$setup=cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $setup) { if ($line -match '^([^=]+)=(.*)$') {Set-Item -Path ("env:"+$Matches[1]) -Value $Matches[2]} }
$build=Join-Path $root 'build/challenge-room'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$lvgl='D:\LittleWorld-Codex\firmware\managed_components\lvgl__lvgl'
$library='D:\LittleWorld-Codex\build\host\lvgl_build\lib\lvgl.lib'
if (-not (Test-Path -LiteralPath $library)) {throw 'Build the existing OS host first; the helper reuses its pinned LVGL 9.5 library.'}
Push-Location $build
try {
    & cl /nologo /utf-8 /O2 /MD /DLV_CONF_INCLUDE_SIMPLE /DLV_LVGL_H_INCLUDE_SIMPLE "/I$root/host" "/I$root/firmware/components/lw_ui/include" "/I$lvgl" "$root/tools/challenge_room.c" "$root/host/win_display.c" "$root/firmware/components/lw_ui/src/lw_os_design.c" /Fe:challenge-room.exe /link $library user32.lib gdi32.lib
    if ($LASTEXITCODE -ne 0) {throw 'Room helper build failed'}
    & ./challenge-room.exe "$root/private/meowa-challenge/room-background.bmp"
    if ($LASTEXITCODE -ne 0) {throw 'Room framebuffer export failed'}
} finally {Pop-Location}

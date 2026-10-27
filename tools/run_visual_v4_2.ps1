[CmdletBinding()]
param([switch]$Test,[switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
$env:LW_DATA_DIR=Join-Path $root 'private/host-data'
New-Item -ItemType Directory -Force -Path $env:LW_DATA_DIR | Out-Null
$env:LW_VISUAL_ASSETS=Join-Path $root 'assets/visual-v4/runtime'
$env:LW_POLISH_ASSETS=Join-Path $root 'assets/visual-v4-2/runtime'
$env:LW_MALE_PACK=Join-Path $env:LW_POLISH_ASSETS 'male.lwp'
$env:LW_POLISH_PACK=Join-Path $env:LW_POLISH_ASSETS 'polish.lwp'
$env:LW_UI_PACK=Join-Path $env:LW_POLISH_ASSETS 'ui.lwp'
$lvgl='D:\Espressif\LittleWorld\sources\waveshare-7b\examples\esp-idf\09_lvgl_demo_v9\managed_components\lvgl__lvgl'
$run=@{LvglDir=$lvgl;OutputDir='reports/ui-v2/v42-final';ReportDir='reports/visual-v4-2/runtime'}
if ($Test) {$run.Test=$true}
if ($SkipBuild) {$run.SkipBuild=$true}
& "$PSScriptRoot/run_os_v2.ps1" @run
if ($LASTEXITCODE -ne 0) {throw 'V4.2 simulator failed; inspect reports/logs'}

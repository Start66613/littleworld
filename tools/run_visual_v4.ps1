[CmdletBinding()]
param([switch]$Test,[switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
$env:LW_DATA_DIR=Join-Path $root 'private/host-data'
New-Item -ItemType Directory -Force -Path $env:LW_DATA_DIR | Out-Null
$env:LW_VISUAL_ASSETS=Join-Path $root 'assets/visual-v4/runtime'
$env:LW_MALE_PACK=Join-Path $root 'assets/visual-v4/runtime/male.lwp'
# Use the same verified LVGL tree as the initial host build, even when IDF
# dependencies have since been copied into this isolated worktree.
$lvgl='D:\Espressif\LittleWorld\sources\waveshare-7b\examples\esp-idf\09_lvgl_demo_v9\managed_components\lvgl__lvgl'
$run=@{LvglDir=$lvgl;OutputDir='reports/ui-v2/v4-final';ReportDir='reports/visual-v4/runtime'}
if ($Test) {$run.Test=$true}
if ($SkipBuild) {$run.SkipBuild=$true}
& "$PSScriptRoot/run_os_v2.ps1" @run
if ($LASTEXITCODE -ne 0) {throw 'V4 simulator failed; inspect reports/logs'}

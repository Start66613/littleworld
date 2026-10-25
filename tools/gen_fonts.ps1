# Generate the Little World UI fonts from the Noto Sans SC subset OTF.
#
# Scans the UI/firmware/host sources for every non-ASCII character actually used
# and rasterises exactly those glyphs, so a missing glyph becomes a build-time
# error instead of a box on the screen.
#
# Requirements (already installed): node + lv_font_conv in tools/fonts, and the
# OTF files in tools/fonts/cache.
#
# Usage: pwsh -NoProfile -ExecutionPolicy Bypass -File tools/gen_fonts.ps1 [-Check]

[CmdletBinding()]
param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$fontDir = Join-Path $projectRoot 'tools/fonts'
if (-not (Test-Path -LiteralPath (Join-Path $fontDir 'cache/NotoSansSC-Regular.otf'))) {
    # Reuse the existing D-drive font toolchain read-only, without installing again.
    $fontDir='D:\LittleWorld-Main-DSH\tools\fonts'
}
$outDir = Join-Path $projectRoot 'firmware/components/lw_ui/fonts'
$regular = Join-Path $fontDir 'cache/NotoSansSC-Regular.otf'
$bold = Join-Path $fontDir 'cache/NotoSansSC-Bold.otf'

foreach ($p in @($regular, $bold)) {
    if (-not (Test-Path -LiteralPath $p)) { throw "Font source missing: $p" }
}

$chars = New-Object System.Text.StringBuilder
$sources = @()
foreach ($dir in @('firmware/components/lw_ui', 'firmware/components/lw_core', 'firmware/main', 'host')) {
    $full = Join-Path $projectRoot $dir
    if (Test-Path $full) {
        $sources += Get-ChildItem -Path $full -Recurse -File -Include *.c, *.h |
            Where-Object { $_.FullName -notmatch '[\\/](build|managed_components|fonts)[\\/]' }
    }
}

foreach ($file in $sources) {
    $text = [System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::UTF8)
    foreach ($ch in $text.ToCharArray()) {
        if ([int]$ch -lt 0x20) { continue }
        [void]$chars.Append($ch)
    }
}

$unique = ($chars.ToString().ToCharArray() | Sort-Object -Unique) -join ''
$unique = ($unique.ToCharArray() | Where-Object { [int]$_ -ge 0x7F -and [int]$_ -le 0xFFFF }) -join ''
Write-Output ("glyph set: {0} non-ASCII characters" -f $unique.Length)

$jobs = @(
    @{ Name = 'lw_font_18'; Size = 18; Font = $regular; Symbols = $unique },
    @{ Name = 'lw_font_22'; Size = 22; Font = $regular; Symbols = $unique },
    @{ Name = 'lw_font_28'; Size = 28; Font = $bold; Symbols = $unique },
    @{ Name = 'lw_font_44'; Size = 44; Font = $bold; Symbols = '' },
    @{ Name = 'lw_font_88'; Size = 88; Font = $regular; Symbols = '' }
)

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$lvFontConvJs = Join-Path $fontDir 'node_modules/lv_font_conv/lv_font_conv.js'
if (-not (Test-Path -LiteralPath $lvFontConvJs)) { throw "lv_font_conv is not installed in $fontDir" }

$stale = @()
$generatedDir=Join-Path $projectRoot 'build/fontgen'
New-Item -ItemType Directory -Force -Path $generatedDir | Out-Null
foreach ($job in $jobs) {
    $out = Join-Path $outDir ($job.Name + '.c')
    $target = Join-Path $generatedDir ($job.Name + '.c')

    $fontArgs = @($lvFontConvJs, '--font', $job.Font, '--range', '0x20-0x7E')
    if ($job.Symbols -ne '') { $fontArgs += @('--symbols', $job.Symbols) }
    $fontArgs += @('--size', $job.Size, '--bpp', 4, '--format', 'lvgl', '--no-compress',
        '--lv-include', 'lvgl.h', '--output', $target)

    if ($Check) {
        & node @fontArgs
        if ($LASTEXITCODE -ne 0) { throw "lv_font_conv failed for $($job.Name)" }
        $generated=[System.IO.File]::ReadAllText($target).Replace($target,$out).TrimEnd() + [char]10
        [System.IO.File]::WriteAllText($target,$generated,[System.Text.UTF8Encoding]::new($false))
        if (-not (Test-Path $out) -or (Get-FileHash $out).Hash -ne (Get-FileHash $target).Hash) { $stale += $job.Name }
        continue
    }

    Write-Output ("generating {0} ({1}px)..." -f $job.Name, $job.Size)
    & node @fontArgs
    if ($LASTEXITCODE -ne 0) { throw "lv_font_conv failed for $($job.Name)" }
    $generated=[System.IO.File]::ReadAllText($target).Replace($target,$out).TrimEnd() + [char]10
    [System.IO.File]::WriteAllText($target,$generated,[System.Text.UTF8Encoding]::new($false))
    if (-not (Test-Path -LiteralPath $out) -or (Get-FileHash $out).Hash -ne (Get-FileHash $target).Hash) {
        Copy-Item -LiteralPath $target -Destination $out -Force
    }
    Write-Output ("  -> {0} ({1:N0} bytes)" -f $out, (Get-Item -LiteralPath $out).Length)
}

if ($Check) {
    if ($stale.Count -gt 0) { Write-Output ("STALE FONT OUTPUTS: " + ($stale -join ', ')); exit 1 }
    Write-Output 'font outputs are up to date'
}

Copy-Item -LiteralPath (Join-Path $fontDir 'cache/OFL.txt') -Destination (Join-Path $outDir 'OFL.txt') -Force
Write-Output 'done.'

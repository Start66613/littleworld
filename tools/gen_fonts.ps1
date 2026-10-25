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
foreach ($dir in @('firmware/components/lw_ui', 'firmware/components/lw_core', 'firmware/main', 'host', 'rpg')) {
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

# Windows PowerShell encodes command-line arguments in the ANSI code page, so
# passing CJK text to node mangled every glyph (the font then contained wrong
# code points and the panel showed boxes). The charset travels as a UTF-8 file.
$genDir = Join-Path $projectRoot 'build/fontgen'
New-Item -ItemType Directory -Force -Path $genDir | Out-Null
$charsetFile = Join-Path $genDir 'charset.txt'
$charsetEmpty = Join-Path $genDir 'charset-empty.txt'
# 44/88 px are the numeric display sizes: printable ASCII plus the unit
# symbols the UI can put next to a big value. lv_font_conv's default range
# stops at 0x7E, which dropped U+00B7 (middle dot) and U+00D7 (times).
$symbols = -join ((32..126) | ForEach-Object { [char]$_ })
$symbols += [char]0x00B7 + [char]0x00D7 + [char]0x00B0 + [char]0x2103 + [char]0x2026
$charsetSymbols = Join-Path $genDir 'charset-symbols.txt'
[System.IO.File]::WriteAllText($charsetSymbols, $symbols, (New-Object System.Text.UTF8Encoding($false)))
[System.IO.File]::WriteAllText($charsetFile, $unique, (New-Object System.Text.UTF8Encoding($false)))
[System.IO.File]::WriteAllText($charsetEmpty, '', (New-Object System.Text.UTF8Encoding($false)))

$jobs = @(
    @{ Name = 'lw_font_18'; Size = 18; Font = $regular; Charset = $charsetFile },
    @{ Name = 'lw_font_22'; Size = 22; Font = $regular; Charset = $charsetFile },
    @{ Name = 'lw_font_28'; Size = 28; Font = $bold; Charset = $charsetFile },
    @{ Name = 'lw_font_44'; Size = 44; Font = $bold; Charset = $charsetSymbols },
    @{ Name = 'lw_font_88'; Size = 88; Font = $regular; Charset = $charsetSymbols }
)

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$genFontsJs = Join-Path $projectRoot 'tools/gen_fonts.js'
$lvFontConvJs = Join-Path $fontDir 'node_modules/lv_font_conv/lib/cli.js'
if (-not (Test-Path -LiteralPath $lvFontConvJs)) { throw "lv_font_conv is not installed in $fontDir" }
if (-not (Test-Path -LiteralPath $genFontsJs)) { throw "tools/gen_fonts.js is missing" }

$stale = @()
function Test-FontContent([string]$Path,[string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $actual=[System.IO.File]::ReadAllText($Path).Replace("`r`n","`n")
    return $actual -ceq $Expected
}
$generatedDir=Join-Path $projectRoot 'build/fontgen'
New-Item -ItemType Directory -Force -Path $generatedDir | Out-Null
foreach ($job in $jobs) {
    $out = Join-Path $outDir ($job.Name + '.c')
    $target = Join-Path $generatedDir ($job.Name + '.c')

    $fontArgs = @($genFontsJs, '--font', $job.Font, '--charset', $job.Charset,
        '--size', $job.Size, '--output', $target)


    if ($Check) {
        & node @fontArgs
        if ($LASTEXITCODE -ne 0) { throw "lv_font_conv failed for $($job.Name)" }
        $generated=[System.IO.File]::ReadAllText($target).Replace($target,('firmware/components/lw_ui/fonts/'+$job.Name+'.c')).Replace($job.Font,[IO.Path]::GetFileName($job.Font)).TrimEnd() + [char]10
        [System.IO.File]::WriteAllText($target,$generated,[System.Text.UTF8Encoding]::new($false))
        if (-not (Test-FontContent $out $generated)) { $stale += $job.Name }
        continue
    }

    Write-Output ("generating {0} ({1}px)..." -f $job.Name, $job.Size)
    & node @fontArgs
    if ($LASTEXITCODE -ne 0) { throw "lv_font_conv failed for $($job.Name)" }
    $generated=[System.IO.File]::ReadAllText($target).Replace($target,('firmware/components/lw_ui/fonts/'+$job.Name+'.c')).Replace($job.Font,[IO.Path]::GetFileName($job.Font)).TrimEnd() + [char]10
    [System.IO.File]::WriteAllText($target,$generated,[System.Text.UTF8Encoding]::new($false))
    if (-not (Test-FontContent $out $generated)) {
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

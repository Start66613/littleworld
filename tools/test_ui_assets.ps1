# Test the UI asset pipeline end to end.
#
# The point of this suite is that the PRODUCER and the CONSUMER are different
# programs: tools/build_ui_assets.py writes a .lwu file, and the firmware parser
# (lw_ui_asset.c, compiled for the host) reads it back. A round-trip test that
# only checks the Python encoder against itself would pass happily while the
# device rejected every file.
#
# Usage: powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/test_ui_assets.ps1
# Exit code 0 = every conversion and every refusal behaved as specified.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $root 'build/host_tests'
$outDir = Join-Path $root 'build/ui-assets'
$logDir = Join-Path $root 'reports/logs'
New-Item -ItemType Directory -Force -Path $buildDir, $logDir | Out-Null
$logFile = Join-Path $logDir 'ui-asset-tests.log'
Set-Content -Path $logFile -Value '' -Encoding utf8

$python = 'D:\LittleWorld-Meowa\tools\venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) {
    # Fall back to whatever python is on PATH, but never to the WindowsApps stub.
    $candidate = Get-Command python.exe -ErrorAction SilentlyContinue
    if (-not $candidate) { throw "no python interpreter found (looked for $python)" }
    $python = $candidate.Source
}

$checks = 0
$failures = 0
function Check([string]$name, [bool]$condition) {
    $script:checks++
    if (-not $condition) {
        $script:failures++
        Write-Output "FAIL $name"
    }
}
function Invoke-Tool([string[]]$arguments) {
    # The tool writes refusals to stderr; $ErrorActionPreference='Stop' would
    # turn that into a terminating error, so relax it locally and judge by the
    # exit code, which is the contract.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $python @arguments 2>&1 | Out-String
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    return @{ code = $code; output = $output }
}

# --- the engine test binary doubles as the firmware-side parser checker -------
$engineScript = Join-Path $PSScriptRoot 'run_engine_tests.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $engineScript 2>&1 | Tee-Object -FilePath $logFile -Append | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'engine tests failed; the parser checker is stale' }
$checker = Join-Path $buildDir 'lw_engine_tests.exe'
Check 'parser checker exists' (Test-Path -LiteralPath $checker)

# --- happy path: the three fixture kinds convert and re-parse -----------------
if (Test-Path -LiteralPath $outDir) { Remove-Item -LiteralPath $outDir -Recurse -Force }
$result = Invoke-Tool @('tools/build_ui_assets.py', '--source', 'tests/fixtures/ui', '--out', 'build/ui-assets', '--json')
$result.output | Tee-Object -FilePath $logFile -Append | Out-Null
Check 'conversion exit 0' ($result.code -eq 0)
Check 'three assets produced' ((Get-ChildItem $outDir -Filter *.lwu -ErrorAction SilentlyContinue).Count -eq 3)

foreach ($file in Get-ChildItem $outDir -Filter *.lwu) {
    $run = & $checker --check-asset $file.FullName 2>&1 | Out-String
    $run | Tee-Object -FilePath $logFile -Append | Out-Null
    Check "firmware accepts $($file.Name)" ($LASTEXITCODE -eq 0)
}

# Exact geometry, because "it parsed" is weaker than "it parsed into the shape
# the renderer needs".
$panel = & $checker --check-asset (Join-Path $outDir 'panel-card.lwu') 2>&1 | Out-String
Check 'panel is 24x24 one state' ($panel -match 'kind=1 24x24 states=1')
Check 'panel insets preserved' ($panel -match 'insets=6,6,6,6')
Check 'panel minimum is 13x13' ($panel -match 'min=13x13')
$button = & $checker --check-asset (Join-Path $outDir 'button-primary.lwu') 2>&1 | Out-String
Check 'button is 32x24 three states' ($button -match 'kind=2 32x24 states=3')
Check 'button payload is three bands' ($button -match 'payload=6912')
$icon = & $checker --check-asset (Join-Path $outDir 'icon-heart.lwu') 2>&1 | Out-String
Check 'icon has no insets' ($icon -match 'insets=0,0,0,0')
Check 'icon minimum is zero' ($icon -match 'min=0x0')

# --- determinism: same input, same bytes -------------------------------------
$hashes = @{}
foreach ($file in Get-ChildItem $outDir -Filter *.lwu) {
    $hashes[$file.Name] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
}
$again = Join-Path $root 'build/ui-assets-again'
if (Test-Path -LiteralPath $again) { Remove-Item -LiteralPath $again -Recurse -Force }
Invoke-Tool @('tools/build_ui_assets.py', '--source', 'tests/fixtures/ui', '--out', 'build/ui-assets-again') | Out-Null
foreach ($file in Get-ChildItem $again -Filter *.lwu) {
    $rehash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    Check "deterministic $($file.Name)" ($hashes[$file.Name] -eq $rehash)
}
Remove-Item -LiteralPath $again -Recurse -Force

# --- --check writes nothing ---------------------------------------------------
$checkOnly = Join-Path $root 'build/ui-assets-checkonly'
if (Test-Path -LiteralPath $checkOnly) { Remove-Item -LiteralPath $checkOnly -Recurse -Force }
$result = Invoke-Tool @('tools/build_ui_assets.py', '--source', 'tests/fixtures/ui', '--out', 'build/ui-assets-checkonly', '--check')
Check 'check mode exit 0' ($result.code -eq 0)
Check 'check mode wrote nothing' (-not (Test-Path -LiteralPath $checkOnly))

# --- refusals: every one of these is a real authoring mistake -----------------
$scratch = Join-Path $root 'build/ui-asset-refusals'
if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
New-Item -ItemType Directory -Force -Path $scratch | Out-Null

function Write-Png([string]$name, [int]$width, [int]$height, [string]$kind, [string]$insetsJson) {
    $script = @"
import sys
from PIL import Image, ImageDraw
image = Image.new('RGBA', ($width, $height), (247, 239, 226, 255))
ImageDraw.Draw(image).rectangle((0, 0, $width - 1, $height - 1), outline=(74, 58, 52, 255))
image.save(sys.argv[1])
"@
    $tmp = Join-Path $scratch '_mk.py'
    Set-Content -LiteralPath $tmp -Value $script -Encoding utf8
    & $python $tmp (Join-Path $scratch $name) 2>&1 | Out-Null
    if ($insetsJson) {
        $sidecar = [System.IO.Path]::ChangeExtension((Join-Path $scratch $name), '.json')
        Set-Content -LiteralPath $sidecar -Value "{ ""insets"": $insetsJson }" -Encoding utf8
    }
    Remove-Item -LiteralPath $tmp -Force
}

function Expect-Refusal([string]$name, [string]$pattern) {
    $result = Invoke-Tool @('tools/build_ui_assets.py', '--source', 'build/ui-asset-refusals', '--out', 'build/ui-asset-refusals-out')
    $result.output | Tee-Object -FilePath $logFile -Append | Out-Null
    Check "$name refused" ($result.code -eq 1)
    Check "$name explains itself" ($result.output -match $pattern)
    Get-ChildItem $scratch -Filter *.png | Remove-Item -Force
    Get-ChildItem $scratch -Filter *.json | Remove-Item -Force
}

Write-Png 'panel-tiny.png' 8 8 'panel' '[6,6,6,6]'
Expect-Refusal 'borders wider than the art' 'leave no centre'

Write-Png 'button-ragged.png' 32 70 'button' '[6,6,6,6]'
Expect-Refusal 'strip height not divisible by 3' 'not divisible by 3'

Write-Png 'panel-big.png' 2048 16 'panel' '[4,4,4,4]'
Expect-Refusal 'over the 1024 side limit' 'exceeds the 1024px limit'

Write-Png 'mystery.png' 16 16 '' ''
Expect-Refusal 'unclassified file name' 'cannot tell what this is'

# An icon must not carry insets: it has no centre to preserve.
Write-Png 'icon-bordered.png' 16 16 'icon' '[4,4,4,4]'
Expect-Refusal 'icon with insets' 'an icon has no borders'

# A fully transparent asset would draw nothing; refuse rather than ship it.
$script = @'
import sys
from PIL import Image
Image.new('RGBA', (16, 16), (0, 0, 0, 0)).save(sys.argv[1])
'@
$tmp = Join-Path $scratch '_mk.py'
Set-Content -LiteralPath $tmp -Value $script -Encoding utf8
& $python $tmp (Join-Path $scratch 'icon-empty.png') 2>&1 | Out-Null
Remove-Item -LiteralPath $tmp -Force
$result = Invoke-Tool @('tools/build_ui_assets.py', '--source', 'build/ui-asset-refusals', '--out', 'build/ui-asset-refusals-out')
Check 'fully transparent refused' ($result.code -eq 1)
Check 'fully transparent explains itself' ($result.output -match 'fully transparent')
Get-ChildItem $scratch -Filter *.png | Remove-Item -Force

# A corrupt file must be rejected by the FIRMWARE parser, not only by Python:
# hand-edit the magic of a good asset and re-run the checker.
$corrupt = Join-Path $scratch 'corrupt.lwu'
$bytes = [System.IO.File]::ReadAllBytes((Join-Path $outDir 'panel-card.lwu'))
$bytes[0] = [byte][char]'X'
[System.IO.File]::WriteAllBytes($corrupt, $bytes)
$run = & $checker --check-asset $corrupt 2>&1 | Out-String
$run | Tee-Object -FilePath $logFile -Append | Out-Null
Check 'firmware rejects a bad magic' ($LASTEXITCODE -ne 0)
Check 'firmware names the reason' ($run -match 'not a LWUI0001 asset')

# Truncate a good asset by one byte: the payload length check must catch it.
$truncated = Join-Path $scratch 'truncated.lwu'
$bytes = [System.IO.File]::ReadAllBytes((Join-Path $outDir 'panel-card.lwu'))
[System.IO.File]::WriteAllBytes($truncated, $bytes[0..($bytes.Length - 2)])
$run = & $checker --check-asset $truncated 2>&1 | Out-String
$run | Tee-Object -FilePath $logFile -Append | Out-Null
Check 'firmware rejects a truncated payload' ($LASTEXITCODE -ne 0)
Check 'firmware names the truncation' ($run -match 'payload size does not match')

Remove-Item -LiteralPath $scratch -Recurse -Force

Write-Output ''
if ($failures -ne 0) {
    Write-Output ("ui assets: {0} checks, {1} failure(s)" -f $checks, $failures)
    Write-Output 'UI ASSET TESTS FAILED'
    exit 1
}
Write-Output ("ui assets: {0} checks, 0 failure(s)" -f $checks)
Write-Output 'UI ASSET TESTS PASSED'
exit 0

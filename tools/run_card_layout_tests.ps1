# Build and run the end-to-end card-layout tests with MSVC.
#
# These are the tests that would have caught the doubled-directory bug: they
# build a directory tree with EXACTLY the shape of a real TF card
# (littleworld/littleworld-v4.json, content.lwcx, male.lwp, scene-N.lwr ...),
# run the real packaging tool into it, and then drive the shipping code in boot
# order - mount, verify, resolve, load.
#
# Unlike run_v4_resource_tests.ps1 this suite does NOT hand the code a scratch
# directory and a bare file name. That shortcut is what let
# /sdcard/littleworld/littleworld/content.lwcx survive in the firmware.
#
# The content file is produced by tools/package_v4_content.py from content/,
# the same source the release uses, so "the tool's output is locatable by the
# device" is proven rather than assumed.
#
# Usage: powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/run_card_layout_tests.ps1
# Exit code 0 = compiled cleanly and every check passed.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $root 'build/host_tests'
$logDir = Join-Path $root 'reports/logs'
$cardDir = Join-Path $buildDir 'v4-card-layout'
New-Item -ItemType Directory -Force -Path $buildDir, $logDir | Out-Null
if (Test-Path -LiteralPath $cardDir) { Remove-Item -LiteralPath $cardDir -Recurse -Force }

# The card root IS the littleworld directory, because that is what
# lw_sd_asset_root() returns on the board: /sdcard/littleworld.
$littleworld = Join-Path $cardDir 'littleworld'
New-Item -ItemType Directory -Force -Path $littleworld | Out-Null
$logFile = Join-Path $logDir 'card-layout-tests.log'

# --- 1. the real packaging tool writes the content file into the card tree ---
$py = if ($env:LW_PYTHON) { $env:LW_PYTHON } else { 'D:\LittleWorld-Meowa\tools\venv\Scripts\python.exe' }
if (-not (Test-Path -LiteralPath $py)) {
    $found = Get-Command python -ErrorAction SilentlyContinue
    if (-not $found) { Write-Output 'python was not found; set LW_PYTHON'; exit 1 }
    $py = 'python'
}
$env:PYTHONIOENCODING = 'utf-8'
# The tool writes into the card tree; keep an untouched copy outside it so the
# test can prove the file it copied is byte-identical to what the tool made.
$sampleDir = Join-Path $buildDir 'v4-card-sample'
New-Item -ItemType Directory -Force -Path $sampleDir | Out-Null
$sample = Join-Path $sampleDir 'content.lwcx'
$pack = & $py (Join-Path $root 'tools/package_v4_content.py') --source (Join-Path $root 'content') --out $littleworld --json 2>&1
$packCode = $LASTEXITCODE
$pack | Write-Output
if ($packCode -ne 0) {
    Write-Output ''
    Write-Output ("CARD LAYOUT TESTS FAILED: packaging tool exit " + $packCode)
    exit 1
}
$made = Join-Path $littleworld 'content.lwcx'
if (-not (Test-Path -LiteralPath $made)) {
    Write-Output ''
    Write-Output 'CARD LAYOUT TESTS FAILED: the packaging tool did not write content.lwcx'
    exit 1
}
Copy-Item -LiteralPath $made -Destination $sample -Force

# --- 2. compile the suite against the shipping sources -----------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw "vswhere.exe not found: $vswhere" }
$install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) { throw 'No Visual Studio installation with the C++ toolchain was found.' }
$vcvars = Join-Path $install 'VC/Auxiliary/Build/vcvars64.bat'
$dump = cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $dump) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path ("env:" + $Matches[1]) -Value $Matches[2] }
}
$cl = (Get-Command cl.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $cl) { throw 'cl.exe is not available after vcvars64.' }
Write-Output "compiler: $cl"

$include = @(
    ('/I' + (Join-Path $root 'firmware/components/lw_core/include')),
    ('/I' + (Join-Path $root 'firmware/components/lw_ui/include')),
    ('/I' + (Join-Path $root 'firmware/components/lw_duo/include')),
    ('/I' + (Join-Path $root 'firmware/components/lw_storage/include')),
    ('/I' + (Join-Path $root 'firmware/components/lw_content/include')),
    ('/I' + (Join-Path $root 'host')),
    ('/I' + (Join-Path $root 'firmware/managed_components/lvgl__lvgl'))
)

$sources = @(
    (Join-Path $root 'tests/host/test_lw_card_layout.c'),
    (Join-Path $root 'firmware/components/lw_core/src/lw_path.c'),
    (Join-Path $root 'firmware/components/lw_core/src/lw_time.c'),
    (Join-Path $root 'firmware/components/lw_content/src/lw_content.c'),
    (Join-Path $root 'firmware/components/lw_ui/src/lw_pack.c'),
    (Join-Path $root 'firmware/components/lw_ui/src/lw_assets.c'),
    (Join-Path $root 'firmware/components/lw_storage/src/lw_resource.c'),
    (Join-Path $root 'firmware/components/lw_storage/src/lw_resource_mount.c'),
    (Join-Path $root 'firmware/components/lw_storage/src/lw_storage.c')
)

$exe = Join-Path $buildDir 'lw_card_layout_tests.exe'
$obj = Join-Path $buildDir 'obj_card/'
New-Item -ItemType Directory -Force -Path $obj | Out-Null

# Same shim as run_v4_resource_tests.ps1: lw_assets.c needs LVGL's allocation
# hooks and the image-descriptor layout, not the drawing engine.
$shim = Join-Path $buildDir 'lvgl_shim_card.c'
@'
#include <stdlib.h>
#include <stdint.h>

#define LV_IMAGE_HEADER_MAGIC 0x19
typedef struct {
    uint32_t magic: 8;
    uint32_t cf: 8;
    uint32_t flags: 16;
    uint32_t w: 16;
    uint32_t h: 16;
    uint32_t stride: 16;
    uint32_t reserved_2: 16;
} lv_image_header_t;
typedef struct {
    lv_image_header_t header;
    uint32_t data_size;
    const uint8_t *data;
    const void *reserved;
    const void *reserved_2;
} lv_image_dsc_t;

void *lv_malloc(size_t n) { return malloc(n); }
void *lv_malloc_zeroed(size_t n) { return calloc(1, n); }
void lv_free(void *p) { free(p); }
void lv_image_cache_drop(const void *src) { (void)src; }
'@ | Set-Content -LiteralPath $shim -Encoding ascii

$common = @('/nologo', '/std:c11', '/W4', '/WX', '/utf-8', '/O2', '/D_CRT_SECURE_NO_WARNINGS',
    '/external:W0', '/external:I' + (Join-Path $root 'firmware/managed_components/lvgl__lvgl'))
$clArgs = $common + ('/Fo' + $obj) + ('/Fe' + $exe) + $include + @($shim) + $sources
Set-Content -Path $logFile -Value '' -Encoding utf8
$out = & $cl @clArgs 2>&1
$out | Tee-Object -FilePath $logFile -Append | Write-Output
if ($LASTEXITCODE -ne 0) {
    Write-Output ''
    Write-Output ('CARD LAYOUT TESTS BUILD FAILED (rc=' + $LASTEXITCODE + ')')
    exit 1
}

# --- 3. run against the tree ------------------------------------------------
# LW_RESOURCE_ROOT is what the host mount path reads; LW_CARD_ROOT is what the
# test itself uses. They are the same value on purpose: on the board the mount
# and every consumer share one root, and a test that let them differ would hide
# exactly the class of bug this suite exists for.
$env:LW_RESOURCE_ROOT = $littleworld
$env:LW_CARD_ROOT = $littleworld
$env:LW_CONTENT_SAMPLE = $sample
$run = & $exe 2>&1
$code = $LASTEXITCODE
$run | Tee-Object -FilePath $logFile -Append | Write-Output
if ($code -ne 0) {
    Write-Output ''
    Write-Output 'CARD LAYOUT TESTS FAILED'
    exit 1
}
Write-Output ''
Write-Output 'CARD LAYOUT TESTS OK'
exit 0

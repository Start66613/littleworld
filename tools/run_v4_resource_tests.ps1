# Build and run the V4 resource-layer tests with MSVC.
#
# Covers the LWPX0001 pack reader (clip records, frame addressing, appending a
# second pack for one actor, every rejection path), the LWSR0001 scene reader
# and the littleworld-v4.json manifest / version gate. All of it is plain C with
# no ESP-IDF or LVGL dependency, so a Windows machine can run the same code the
# device runs.
#
# Fixtures are generated into a scratch directory, not committed: they are
# 12 MB of synthetic payload whose only job is to exercise the reader.
#
# Usage: powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/run_v4_resource_tests.ps1
# Exit code 0 = compiled cleanly and every check passed.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $root 'build/host_tests'
$logDir = Join-Path $root 'reports/logs'
$fixtureDir = Join-Path $buildDir 'v4-resource-fixtures'
New-Item -ItemType Directory -Force -Path $buildDir, $logDir | Out-Null
if (Test-Path -LiteralPath $fixtureDir) { Remove-Item -LiteralPath $fixtureDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $fixtureDir | Out-Null
$logFile = Join-Path $logDir 'v4-resource-tests.log'

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
    (Join-Path $root 'tests/host/test_lw_v4_resources.c'),
    (Join-Path $root 'firmware/components/lw_core/src/lw_path.c'),
    (Join-Path $root 'firmware/components/lw_core/src/lw_time.c'),
    (Join-Path $root 'firmware/components/lw_content/src/lw_content.c'),
    (Join-Path $root 'firmware/components/lw_ui/src/lw_pack.c'),
    (Join-Path $root 'firmware/components/lw_ui/src/lw_assets.c'),
    (Join-Path $root 'firmware/components/lw_storage/src/lw_resource.c'),
    (Join-Path $root 'firmware/components/lw_storage/src/lw_resource_mount.c'),
    (Join-Path $root 'firmware/components/lw_storage/src/lw_storage.c')
)

$exe = Join-Path $buildDir 'lw_v4_resources_tests.exe'
$obj = Join-Path $buildDir 'obj_v4/'
New-Item -ItemType Directory -Force -Path $obj | Out-Null

# lw_assets.c uses lv_malloc / lv_image_cache_drop, which live in LVGL. The test
# only needs the registry and the raw-file reader, so linking LVGL's own sources
# is not worth an 8 MB build; this shim provides the two allocation hooks, a
# no-op cache drop, and the image-descriptor type LVGL would define. Anything
# else LVGL offers is only reachable by actually drawing.
$shim = Join-Path $buildDir 'lvgl_shim_v4.c'
@'
#include <stdlib.h>
#include <stdint.h>

/* Only the LVGL surface lw_assets.c touches is reproduced here, copied from
 * lvgl__lvgl/src/draw/lv_image_dsc.h so the bit-field layout and the
 * descriptor size match the real library exactly. A wrong layout here would
 * make the test crash instead of testing anything. */
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

# LVGL is only included for its colour-format enum and image descriptor, and its
# own headers trip /W4 C4244 in places the test never executes. /external:W0
# keeps /W4 /WX meaningful for OUR sources without importing LVGL's warnings.
$common = @('/nologo', '/std:c11', '/W4', '/WX', '/utf-8', '/O2', '/D_CRT_SECURE_NO_WARNINGS',
    '/external:W0', '/external:I' + (Join-Path $root 'firmware/managed_components/lvgl__lvgl'))
$clArgs = $common + ('/Fo' + $obj) + ('/Fe' + $exe) + $include + @($shim) + $sources
Set-Content -Path $logFile -Value '' -Encoding utf8
$out = & $cl @clArgs 2>&1
$out | Tee-Object -FilePath $logFile -Append | Write-Output
if ($LASTEXITCODE -ne 0) {
    Write-Output ''
    Write-Output ('V4 RESOURCE TESTS BUILD FAILED (rc=' + $LASTEXITCODE + ')')
    exit 1
}

$env:LW_RESOURCE_ROOT = $fixtureDir
$run = & $exe 2>&1
$code = $LASTEXITCODE
$run | Tee-Object -FilePath $logFile -Append | Write-Output
if ($code -ne 0) {
    Write-Output ''
    Write-Output 'V4 RESOURCE TESTS FAILED'
    exit 1
}
Write-Output ''
Write-Output 'V4 RESOURCE TESTS OK'
exit 0

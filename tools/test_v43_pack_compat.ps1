[CmdletBinding()]
param([string]$DshRoot='D:/LittleWorld-V43', [string]$Bundle='D:/LittleWorld-Meowa/handoff/codex-v43-frozen-2900569/runtime')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
$python='D:/LittleWorld-Meowa/tools/venv/Scripts/python.exe'
& $python tools/prepare_v43_pack_compat.py
if ($LASTEXITCODE -ne 0) {throw 'Patch preparation failed'}
$build=Join-Path $root 'build/v43-pack-compat'
$logs=Join-Path $root 'reports/logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$install=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $install 'VC/Auxiliary/Build/vcvars64.bat'
$taskCompilerEnv=cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach($line in $taskCompilerEnv){if($line -match '^([^=]+)=(.*)$'){Set-Item -Path ('env:'+$Matches[1]) -Value $Matches[2]}}
$lvgl=Join-Path $root 'firmware/managed_components/lvgl__lvgl'
$shim=Join-Path $build 'shim.c'
@'
#include "lvgl.h"
#include "lw_storage.h"
#include <stdlib.h>
/* No renderer/cache worker in this parser test. All real packs use stdio. */
void *lv_malloc(size_t n){return malloc(n);}
void *lv_malloc_zeroed(size_t n){return calloc(1,n);}
void lv_free(void *p){free(p);}
void lv_image_cache_drop(const void *p){(void)p;}
bool lw_file_cache_read(lw_file_cache_t *c,const char *p,uint64_t o,void *d,size_t b)
{(void)c;(void)p;(void)o;(void)d;(void)b;return false;}
bool lw_file_cache_prefetch(lw_file_cache_t *c,const char *p,uint64_t o,size_t b)
{(void)c;(void)p;(void)o;(void)b;return false;}
'@ | Set-Content -LiteralPath $shim -Encoding ascii
$flags=@('/nologo','/std:c11','/W4','/WX','/utf-8','/O2','/D_CRT_SECURE_NO_WARNINGS','/DLV_CONF_INCLUDE_SIMPLE','/external:W0',('/external:I'+$lvgl))
$includes=@('lw_core','lw_ui','lw_duo','lw_storage') | ForEach-Object { '/I'+(Join-Path $DshRoot ('firmware/components/'+$_+'/include')) }
$includes+=('/I'+(Join-Path $root 'host'))
$sources=@((Join-Path $root 'tests/host/test_v43_pack_compat.c'),$shim,(Join-Path $DshRoot 'firmware/components/lw_core/src/lw_path.c'),(Join-Path $DshRoot 'firmware/components/lw_ui/src/lw_assets.c'))
foreach($mode in @('original','patched')){
    $snapshot=Join-Path $build $mode
    $obj=Join-Path $snapshot 'obj/'
    New-Item -ItemType Directory -Force -Path $obj | Out-Null
    $exe=Join-Path $snapshot 'pack-compat.exe'
    $log=Join-Path $logs ('v43-pack-compat-'+$mode+'.log')
    & cl.exe @flags ('/I'+$snapshot) @includes ('/Fo'+$obj) ('/Fe'+$exe) (Join-Path $snapshot 'lw_pack.c') @sources *> $log
    if($LASTEXITCODE -ne 0){Get-Content $log -Tail 14;throw ($mode+' reader build failed')}
    $action=if($mode -eq 'original'){'probe'}else{'verify'}
    & $exe $Bundle $action >> $log
    if($LASTEXITCODE -ne 0){Get-Content $log -Tail 18;throw ($mode+' reader test failed')}
    Get-Content $log -Tail 7
}
& git -C $DshRoot apply --check (Join-Path $root 'reports/v43-integration-support/lw-pack-v43-compat.patch')
if($LASTEXITCODE -ne 0){throw 'DSH source changed; do not apply patch blindly'}
& git -C $DshRoot apply --check (Join-Path $root 'reports/v43-integration-support/lw-sprite-v43-scale.patch')
if($LASTEXITCODE -ne 0){throw 'DSH sprite source changed; review patch first'}
# Links the real built LVGL 9.5, not the parser allocation shim.
$cacheShim=Join-Path $build 'cache-shim.c'
@'
#include "lw_storage.h"
bool lw_file_cache_read(lw_file_cache_t *c,const char *p,uint64_t o,void *d,size_t b)
{(void)c;(void)p;(void)o;(void)d;(void)b;return false;}
bool lw_file_cache_prefetch(lw_file_cache_t *c,const char *p,uint64_t o,size_t b)
{(void)c;(void)p;(void)o;(void)b;return false;}
'@ | Set-Content -LiteralPath $cacheShim -Encoding ascii
$patched=Join-Path $build 'patched'
$spriteExe=Join-Path $build 'sprite-bridge.exe'
$spriteLog=Join-Path $logs 'v43-sprite-bridge.log'
$spriteSources=@((Join-Path $root 'tests/host/test_v43_sprite_bridge.c'),$cacheShim,(Join-Path $patched 'lw_pack.c'),(Join-Path $patched 'lw_sprite.c'),(Join-Path $DshRoot 'firmware/components/lw_core/src/lw_path.c'),(Join-Path $DshRoot 'firmware/components/lw_ui/src/lw_assets.c'))
& cl.exe @flags '/MD' ('/I'+$patched) @includes ('/Fo'+(Join-Path $patched 'obj/')) ('/Fe'+$spriteExe) @spriteSources (Join-Path $root 'build/host/lvgl_build/lib/lvgl.lib') *> $spriteLog
if($LASTEXITCODE -ne 0){Get-Content $spriteLog -Tail 16;throw 'Real LVGL sprite bridge build failed'}
& $spriteExe $Bundle >> $spriteLog
if($LASTEXITCODE -ne 0){Get-Content $spriteLog -Tail 16;throw 'Real LVGL sprite bridge failed'}
Get-Content $spriteLog -Tail 3

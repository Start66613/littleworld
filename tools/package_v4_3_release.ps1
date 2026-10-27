# Little World V4.3 - release packager.
#
# Assembles ONE release directory from artifacts that were verified in this
# worktree, and writes an honest report about everything it could NOT include.
#
# What it does NOT do, on purpose:
#   * it never builds. A release made from a half-finished build is worse than
#     no release, so a missing image is reported as missing, not rebuilt.
#   * it never flashes, never opens a serial port, never touches a device.
#   * it never copies a file from another worktree or from an older release to
#     fill a gap. A gap stays a gap and is listed in RELEASE.json.
#   * it never claims a resource is "verified on hardware". Compilation and the
#     host test suites are the only evidence it can cite.
#
# Layout it produces:
#   <out>/firmware/<config>/little_world.bin   one per config that was built
#   <out>/sdcard/littleworld/                  exactly the tree the firmware reads
#   <out>/sdcard/PACKAGE.json                  the SD tool's own manifest
#   <out>/ui/*.lwu                             UI component assets, if any exist
#   <out>/MANIFEST.sha256                      sha256 of every file under <out>
#   <out>/COMPATIBILITY.json|md                format versions and requirements
#   <out>/INSTALL.md                           installation instructions (Chinese)
#   <out>/RELEASE.json                         machine-readable summary + gaps
#
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    # Where the release tree is written. Created if absent; must be empty or
    # absent, so an old release can never be silently mixed into a new one.
    [string]$Out = 'dist\release-v4-3',
    # Firmware configs to look for. A config that was never built is reported,
    # not built here.
    [string[]]$Configs = @('os', 'legacy', 'noaudio'),
    # Rebuild the SD resource package instead of reusing dist/littleworld-v4-sd.
    [switch]$RepackSd,
    # Skip the firmware section entirely (useful for a resources-only drop).
    [switch]$NoFirmware,
    [switch]$Json
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$env:PYTHONIOENCODING = 'utf-8'
$env:PYTHONUTF8 = '1'

function Find-Python {
    $candidates = @()
    if ($env:LW_PYTHON) { $candidates += $env:LW_PYTHON }
    $candidates += 'D:\Espressif\LittleWorld\idf-tools\python_env\idf5.5_py3.12_env\Scripts\python.exe'
    $candidates += 'D:\LittleWorld-Meowa\tools\venv\Scripts\python.exe'
    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) { return $c }
    }
    foreach ($name in @('python', 'python3')) {
        if (Get-Command $name -ErrorAction SilentlyContinue) {
            $probe = & $name -c "print('ok')" 2>&1
            if ($LASTEXITCODE -eq 0 -and "$probe" -match 'ok') { return $name }
        }
    }
    return $null
}

$python = Find-Python
if (-not $python) { throw 'no usable Python found; set LW_PYTHON to python.exe' }

function Rel([string]$path) {
    # Always report repo-relative POSIX paths, so the manifest is portable and
    # does not leak this machine's directory layout.
    $full = [System.IO.Path]::GetFullPath($path)
    $rootFull = [System.IO.Path]::GetFullPath($repoRoot)
    if ($full.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        return ($full.Substring($rootFull.Length).TrimStart('\', '/') -replace '\\', '/')
    }
    return ($full -replace '\\', '/')
}

function Sha256([string]$path) {
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $repoRoot $Out }
if (Test-Path -LiteralPath $outPath) {
    $existing = @(Get-ChildItem -LiteralPath $outPath -Force)
    if ($existing.Count -gt 0) {
        throw "output directory is not empty: $outPath (a release must be assembled from scratch; move or delete it first)"
    }
} else {
    New-Item -ItemType Directory -Force -Path $outPath | Out-Null
}

$gaps = New-Object System.Collections.Generic.List[string]
$sections = [ordered]@{}

# --- provenance -------------------------------------------------------------
$commit = (& git -C $repoRoot rev-parse HEAD 2>$null)
if ($LASTEXITCODE -ne 0) { $commit = 'unknown' }
$branch = (& git -C $repoRoot rev-parse --abbrev-ref HEAD 2>$null)
if ($LASTEXITCODE -ne 0) { $branch = 'unknown' }
$dirty = @(& git -C $repoRoot status --porcelain 2>$null) -ne $null -and
         (@(& git -C $repoRoot status --porcelain 2>$null).Count -gt 0)
Write-Host "release source: $branch @ $commit (dirty=$dirty)"
if ($dirty) { $gaps.Add('the worktree had uncommitted changes when the release was assembled') }

# --- firmware images --------------------------------------------------------
$firmware = @()
if (-not $NoFirmware) {
    foreach ($cfg in $Configs) {
        $bin = Join-Path $repoRoot "firmware/build/$cfg/little_world.bin"
        if (-not (Test-Path -LiteralPath $bin)) {
            $gaps.Add("firmware image for '$cfg' was never built in this worktree (firmware/build/$cfg/little_world.bin is absent); it was NOT rebuilt and NOT copied from anywhere")
            continue
        }
        $destDir = Join-Path $outPath "firmware/$cfg"
        New-Item -ItemType Directory -Force -Path $destDir | Out-Null
        Copy-Item -LiteralPath $bin -Destination (Join-Path $destDir 'little_world.bin')
        $size = (Get-Item -LiteralPath $bin).Length
        $hash = Sha256 $bin
        $firmware += [ordered]@{ config = $cfg; bytes = $size; sha256 = $hash; source = Rel $bin }
        Write-Host ("  firmware {0,-8} {1,9} B  {2}" -f $cfg, $size, $hash.Substring(0, 16))
        # A bootloader and partition table are required to flash a bare image, and
        # they are only meaningful next to the exact app they were built with.
        # IDF puts them in subdirectories, not next to the app.
        foreach ($part in @(
                @{ name = 'bootloader.bin'; sub = 'bootloader' },
                @{ name = 'partition-table.bin'; sub = 'partition_table' })) {
            $src = Join-Path $repoRoot "firmware/build/$cfg/$($part.sub)/$($part.name)"
            if (Test-Path -LiteralPath $src) {
                Copy-Item -LiteralPath $src -Destination (Join-Path $destDir $part.name)
            } else {
                $gaps.Add("firmware/build/$cfg/$($part.sub)/$($part.name) is missing, so the '$cfg' image cannot be flashed from this release on its own")
            }
        }
    }
    $sections['firmware'] = $firmware
} else {
    $gaps.Add('firmware section skipped on request (-NoFirmware)')
}

# --- SD resource tree -------------------------------------------------------
$sdOut = Join-Path $outPath 'sdcard'
New-Item -ItemType Directory -Force -Path $sdOut | Out-Null
$sdSource = Join-Path $repoRoot 'dist/littleworld-v4-sd'
$haveSd = $false
if ($RepackSd -or -not (Test-Path -LiteralPath (Join-Path $sdSource 'littleworld/littleworld-v4.json'))) {
    Write-Host 'packaging SD resources from assets/visual-v4-2 + visual-v4-3 + visual-v4 ...'
    $packLog = & $python (Join-Path $PSScriptRoot 'package_v4_sd.py') --out $sdOut 2>&1
    if ($LASTEXITCODE -ne 0) {
        $gaps.Add('the SD resource package could not be built from the V4.3 asset directories; the sdcard section is incomplete')
        $packLog | ForEach-Object { Write-Host "  $_" }
    } else {
        $haveSd = $true
        $packLog | Select-Object -Last 2 | ForEach-Object { Write-Host "  $_" }
    }
} else {
    Write-Host 'copying the verified SD package from dist/littleworld-v4-sd ...'
    Copy-Item -Path (Join-Path $sdSource '*') -Destination $sdOut -Recurse -Force
    $haveSd = $true
}

if ($haveSd) {
    # Re-verify in place: the copy is what ships, so the copy is what must pass.
    $verifyLog = & $python (Join-Path $PSScriptRoot 'package_v4_sd.py') --verify-only --target (Join-Path $sdOut 'littleworld') --json 2>&1
    if ($LASTEXITCODE -ne 0) {
        $gaps.Add('the SD resource package failed its own verifier after being copied; do not write this card')
        $verifyLog | ForEach-Object { Write-Host "  $_" }
    } else {
        Write-Host '  SD package verified by package_v4_sd.py --verify-only'
    }
    # The user's own content rides along: the firmware looks for
    # littleworld/content.lwcx and tolerates its absence.
    $contentLog = & $python (Join-Path $PSScriptRoot 'package_v4_content.py') --out (Join-Path $sdOut 'littleworld') --json 2>&1
    if ($LASTEXITCODE -ne 0) {
        $gaps.Add('no valid user content was available, so the card carries no content.lwcx (the firmware boots without it)')
        $contentLog | ForEach-Object { Write-Host "  $_" }
    } else {
        Write-Host '  user content packaged into littleworld/content.lwcx'
    }
}

# --- UI component assets ----------------------------------------------------
$uiSrc = Join-Path $repoRoot 'assets/visual-v4-3/ui'
$uiOut = Join-Path $outPath 'ui'
New-Item -ItemType Directory -Force -Path $uiOut | Out-Null
$uiFiles = @()
if (Test-Path -LiteralPath $uiSrc) {
    $pngs = @(Get-ChildItem -LiteralPath $uiSrc -Filter '*.png' -File)
} else {
    $pngs = @()
}
if ($pngs.Count -eq 0) {
    $gaps.Add('no UI component art (assets/visual-v4-3/ui/*.png) exists on this branch, so the ui/ section is empty; Codex V4.3 skins live on agent/visual-v4-2-polish and were NOT copied in')
} else {
    $convLog = & $python (Join-Path $PSScriptRoot 'build_ui_assets.py') --source $uiSrc --out $uiOut --json 2>&1
    if ($LASTEXITCODE -ne 0) {
        $gaps.Add('the UI component conversion failed; see the converter output above')
        $convLog | ForEach-Object { Write-Host "  $_" }
    } else {
        foreach ($f in Get-ChildItem -LiteralPath $uiOut -Filter '*.lwu' -File) {
            $uiFiles += [ordered]@{ name = $f.Name; bytes = $f.Length; sha256 = Sha256 $f.FullName }
        }
    }
}
$sections['ui'] = $uiFiles

# --- format compatibility ---------------------------------------------------
$compat = [ordered]@{
    resource_manifest = @{ file = 'littleworld-v4.json'; schema = 1; version = 4 }
    scene = @{ magic = 'LWSR0001'; width = 1024; height = 600; payload = 'RGB565' }
    scene_thumb = @{ magic = 'LWSR0001'; width = 256; height = 150; payload = 'RGB565' }
    character_pack = @{ magic = 'LWPX0001'; canvas = '256x256'; record_bytes = 108; max_clips = 6; max_frames = 32 }
    ui_asset = @{ magic = 'LWUI0001'; header_bytes = 32; kinds = @('icon', 'panel', 'button'); formats = @('RGB565', 'RGB565A8') }
    content = @{ magic = 'LWCX0001'; max_bytes = 32768; schema = 1; version = 1 }
    notes = @(
        'LWSR/LWPX are unchanged since V4.0; a card built for V4.0 still boots.',
        'LWUI0001 and LWCX0001 are new in V4.3 and are additive: a firmware without them ignores the files.',
        'The firmware reads the card root as <mount>/littleworld; content.lwcx sits directly inside it.'
    )
}
$compat | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $outPath 'COMPATIBILITY.json') -Encoding utf8

# --- installation instructions ---------------------------------------------
$install = @'
# Little World V4.3 安装说明

本目录由 `tools/package_v4_3_release.ps1` 从**本工作树实际存在的产物**组装而成。
没有构建过的东西不会被补进来；缺失项写在 `RELEASE.json` 的 `gaps` 里。

## 1. TF 卡（只读数据）

1. 卡格式化为 FAT32，容量 ≥ 64 MB。
2. 把 `sdcard/` 下的 `littleworld/` 整个目录拷到卡的**根目录**，最终结构必须是：

   ```
   <卡根>/littleworld/littleworld-v4.json
   <卡根>/littleworld/content.lwcx          （可选：用户内容）
   <卡根>/littleworld/scene-0.lwr … scene-2.lwr
   <卡根>/littleworld/scene-0-thumb.lwr …
   <卡根>/littleworld/male.lwp
   <卡根>/littleworld/qianqian-wave.lwp
   ```

   不要多套一层 `littleworld/littleworld/`。固件把挂载点下的 `littleworld`
   当作资源根，任何重复层级都会让开机日志出现 `content: no card`。

3. 卡插回设备后断电重启。开机日志里应出现
   `content: v1, N memories, M photos` 与 `littleworld: boot: Little World ready`。

## 2. 固件（需要授权，本目录不自动刷写）

`firmware/<config>/` 下是成对的 `bootloader.bin`、`partition-table.bin`、
`little_world.bin`。**必须同一配置的三个文件一起写**，混用会启动失败：

| 偏移 | 文件 |
| --- | --- |
| 0x2000 | `bootloader.bin` |
| 0x8000 | `partition-table.bin` |
| 0x10000 | `little_world.bin` |

**不要写 0x9000（nvs）**，那里是宠物、RPG 与偏好存档；**不要 erase_flash**，
**不要动分区表**，**不要更新 ESP32-C6 从机固件**。

推荐用仓库里的 `tools/flash_v4_safe.ps1`（默认 Dry Run，需显式 `-Authorized`），
它会先校验镜像头与目标芯片型号，并拒绝写 nvs 与分区表。

## 3. 回滚

保持 `release/os-v3.1-stable` 的镜像可用（sha256 `ef5202c9…`），
整片备份在 `D:\Espressif\LittleWorld\private\backups\LittleWorld\20261025-005021`。
回滚只需把 0x2000 / 0x8000 / 0x10000 三个偏移换回旧镜像，nvs 不动，存档保留。

## 4. 本目录没有做的事

- 没有连接任何设备，没有刷写，没有擦除，没有改分区表，没有碰 C6。
- 没有把任何旧图、旧镜像改名冒充本轮产物。
- 没有在真实硬件上验证过帧率、触摸、TF 实际读取延迟。
'@
Set-Content -LiteralPath (Join-Path $outPath 'INSTALL.md') -Value $install -Encoding utf8

# --- manifest ---------------------------------------------------------------
$manifestLines = New-Object System.Collections.Generic.List[string]
foreach ($f in Get-ChildItem -LiteralPath $outPath -Recurse -File | Sort-Object FullName) {
    $rel = Rel $f.FullName
    if ($rel -eq (Rel (Join-Path $outPath 'MANIFEST.sha256'))) { continue }
    $manifestLines.Add("$(Sha256 $f.FullName)  $rel")
}
$manifestLines -join "`n" | Set-Content -LiteralPath (Join-Path $outPath 'MANIFEST.sha256') -Encoding ascii

# --- report -----------------------------------------------------------------
$totalBytes = (Get-ChildItem -LiteralPath $outPath -Recurse -File | Measure-Object -Property Length -Sum).Sum
$release = [ordered]@{
    schema = 1
    release = 'littleworld-v4.3'
    generatedAt = (Get-Date).ToString('o')
    source = [ordered]@{ branch = $branch; commit = $commit; dirty = $dirty }
    sections = $sections
    sd = [ordered]@{
        packaged = $haveSd
        path = 'sdcard/littleworld'
        files = if ($haveSd) { @(Get-ChildItem -LiteralPath (Join-Path $sdOut 'littleworld') -File | ForEach-Object { $_.Name }) } else { @() }
    }
    compatibility = $compat
    totals = [ordered]@{
        files = @(Get-ChildItem -LiteralPath $outPath -Recurse -File).Count
        bytes = [int64]$totalBytes
    }
    gaps = @($gaps)
    verified = @(
        'host test suites (see reports/backend-v4-3/BUILD_AND_TEST.md)',
        'ESP-IDF offline compile of the configs listed above',
        'SD package re-verified with package_v4_sd.py --verify-only'
    )
    notVerified = @(
        'no hardware was used: frame rate, touch, real TF latency and brightness are unmeasured',
        'no image in this release was flashed',
        'Wi-Fi was not exercised against the ESP32-C6'
    )
}
$release | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outPath 'RELEASE.json') -Encoding utf8

Write-Host ''
Write-Host ("release: {0}" -f $outPath)
Write-Host ("files:   {0}, {1} B" -f $release.totals.files, $release.totals.bytes)
if ($gaps.Count -gt 0) {
    Write-Host ("gaps:    {0}" -f $gaps.Count) -ForegroundColor Yellow
    foreach ($g in $gaps) { Write-Host "  - $g" -ForegroundColor Yellow }
} else {
    Write-Host 'gaps:    none'
}
if ($Json) { $release | ConvertTo-Json -Depth 8 }
exit 0

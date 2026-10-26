# Little World V4.1 — 把资源包复制到 TF 卡（一步一问，默认不写任何东西）
#
# 安全约定：
#   * 不给 -Target 时只打印资源包信息，绝不动磁盘；
#   * 只有明确给出目标路径并再次确认后才复制；
#   * 不格式化、不删除卡上的其他文件；
#   * 卡上已有 littleworld 时，把它改名备份（littleworld.bak-时间戳），不覆盖不删除；
#   * 复制完成后自动重新校验。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\install_v4_sd.ps1
#   powershell -ExecutionPolicy Bypass -File tools\install_v4_sd.ps1 -Target E:\
#   ... -Target E:\ -Yes          # 跳过交互确认（仍不会格式化/删除）

[CmdletBinding()]
param(
    [string]$Target = "",
    [switch]$Yes,
    [switch]$AllowNonRemovable
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$packageRoot = Join-Path $repoRoot 'dist\littleworld-v4-sd\littleworld'

function Show-Package {
    if (-not (Test-Path $packageRoot)) {
        Write-Host "[错误] 还没有生成资源包。" -ForegroundColor Red
        Write-Host "       先运行: powershell -ExecutionPolicy Bypass -File tools\package_v4_sd.ps1"
        return $false
    }
    $files = Get-ChildItem $packageRoot -File | Sort-Object Name
    $total = ($files | Measure-Object Length -Sum).Sum
    Write-Host ""
    Write-Host "资源包: $packageRoot" -ForegroundColor Cyan
    Write-Host ("文件 {0} 个，合计 {1:N0} 字节（{2:N1} MB）" -f $files.Count, $total, ($total / 1MB))
    foreach ($f in $files) {
        Write-Host ("  {0,-28} {1,10:N0}" -f $f.Name, $f.Length)
    }
    return $true
}

function Get-DriveInfo([string]$path) {
    $full = [System.IO.Path]::GetFullPath($path)
    $root = [System.IO.Path]::GetPathRoot($full)
    $drive = Get-PSDrive -Name $root.Substring(0, 1) -ErrorAction SilentlyContinue
    $disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='$($root.Substring(0,2))'" -ErrorAction SilentlyContinue
    return [pscustomobject]@{
        Root      = $root
        Full      = $full
        Label     = if ($disk) { $disk.VolumeName } else { '' }
        FileSystem= if ($disk) { $disk.FileSystem } else { '' }
        DriveType = if ($disk) { $disk.DriveType } else { 0 }   # 2=可移动 3=本地
        FreeBytes = if ($disk) { $disk.FreeSpace } else { 0 }
    }
}

if (-not (Show-Package)) { exit 1 }

if (-not $Target) {
    Write-Host ""
    Write-Host "当前是【只查看】模式，没有复制任何文件。" -ForegroundColor Yellow
    Write-Host "要真正复制，请先插好 TF 卡，然后确认卡符（例如 E:\），再运行："
    Write-Host "  powershell -ExecutionPolicy Bypass -File tools\install_v4_sd.ps1 -Target E:\"
    exit 0
}

if (-not (Test-Path $Target)) {
    Write-Host "[错误] 目标路径不存在: $Target" -ForegroundColor Red
    exit 1
}

$info = Get-DriveInfo $Target
$needBytes = (Get-ChildItem $packageRoot -File | Measure-Object Length -Sum).Sum
Write-Host ""
Write-Host "即将使用的目标：" -ForegroundColor Cyan
Write-Host ("  盘符      : {0}" -f $info.Root)
Write-Host ("  卷标      : {0}" -f ($info.Label -replace '^\s*$', '(无)'))
Write-Host ("  文件系统  : {0}" -f ($info.FileSystem -replace '^\s*$', '(未知)'))
Write-Host ("  类型      : {0}" -f $(switch ($info.DriveType) { 2 { '可移动磁盘（TF/SD/U 盘）' } 3 { '本地磁盘' } default { "其他($($info.DriveType))" } }))
Write-Host ("  剩余空间  : {0:N0} 字节（需要 {1:N0}）" -f $info.FreeBytes, $needBytes)
Write-Host ("  写入目录  : {0}" -f (Join-Path $info.Full 'littleworld'))

if ($info.DriveType -ne 2 -and -not $AllowNonRemovable) {
    Write-Host ""
    Write-Host "[已停止] 该路径不是可移动磁盘。为避免误写到系统盘，默认拒绝。" -ForegroundColor Yellow
    Write-Host "         如果确认要写本地目录（例如做测试），加 -AllowNonRemovable 再运行。"
    exit 3
}

if ($info.FreeBytes -lt $needBytes) {
    Write-Host "[错误] 剩余空间不足，复制会失败，已停止。" -ForegroundColor Red
    exit 1
}

$destDir = Join-Path $info.Full 'littleworld'
if (Test-Path $destDir) {
    Write-Host ""
    Write-Host "卡上已经有 littleworld 文件夹。" -ForegroundColor Yellow
    $existing = Get-ChildItem $destDir -File | Measure-Object
    Write-Host ("  现有文件 {0} 个；它们会被整体改名备份为 littleworld.bak-<时间戳>，不会被删除。" -f $existing.Count)
}

if (-not $Yes) {
    Write-Host ""
    $answer = Read-Host "确认把资源复制到 $destDir ？（输入 YES 继续，其他任意键取消）"
    if ($answer -ne 'YES') {
        Write-Host "已取消，没有修改 TF 卡。" -ForegroundColor Yellow
        exit 0
    }
}

# 备份旧的 littleworld（改名而不是删除）
if (Test-Path $destDir) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
     $backup = Join-Path $info.Full "littleworld.bak-$stamp"
    try {
        Rename-Item -LiteralPath $destDir -NewName (Split-Path -Leaf $backup) -ErrorAction Stop
        Write-Host "旧目录已备份为: $backup" -ForegroundColor DarkYellow
    } catch {
        Write-Host "[错误] 无法备份旧目录，已停止（没有删除任何东西）: $($_.Exception.Message)" -ForegroundColor Red
        exit 1
    }
}

New-Item -ItemType Directory -Force -Path $destDir | Out-Null
$copied = 0
foreach ($f in (Get-ChildItem $packageRoot -File)) {
    try {
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $destDir $f.Name) -ErrorAction Stop
        $copied++
    } catch {
        Write-Host "[错误] 复制失败: $($f.Name) — $($_.Exception.Message)" -ForegroundColor Red
        Write-Host "       已复制 $copied 个文件；卡上可能不完整，请重新运行或手动检查。" -ForegroundColor Yellow
        exit 1
    }
}
Write-Host ("已复制 {0} 个文件到 {1}" -f $copied, $destDir) -ForegroundColor Green

# 复制后重新校验（用同一套固件规则）
Write-Host ""
Write-Host "复制完成，正在按固件规则重新校验……" -ForegroundColor Cyan
$verify = Join-Path $PSScriptRoot 'verify_v4_sd.ps1'
& powershell -NoProfile -ExecutionPolicy Bypass -File $verify -Target $destDir
if ($LASTEXITCODE -ne 0) {
    Write-Host "[注意] 复制后的校验没有通过，请保留上面的报错信息。" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "全部完成。可以安全弹出 TF 卡并插回设备。" -ForegroundColor Green
Write-Host "如果设备启动后没有变化：确认卡里是 littleworld/littleworld-v4.json（不是多套一层目录）。"
exit 0

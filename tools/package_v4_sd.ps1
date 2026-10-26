# Little World V4.1 — 生成 TF 卡资源安装包（Windows 包装脚本）
#
# 真正的校验与打包逻辑在 tools/package_v4_sd.py（只用 Python 标准库）。
# 这个脚本只负责：找 Python、设置 UTF-8、把参数转过去、把退出码带回来。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\package_v4_sd.ps1
#   powershell -ExecutionPolicy Bypass -File tools\package_v4_sd.ps1 -Json
#   powershell -ExecutionPolicy Bypass -File tools\package_v4_sd.ps1 -Source <资源目录> -Out <输出目录>

[CmdletBinding()]
param(
    [string]$Source = "",
    [string]$Out = "",
    [switch]$Json
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$env:PYTHONIOENCODING = 'utf-8'
$env:PYTHONUTF8 = '1'

function Find-Python {
    # 1) 显式指定  2) 本机 ESP-IDF 环境  3) py 启动器  4) PATH 里的 python（需能真的跑起来）
    $candidates = @()
    if ($env:LW_PYTHON) { $candidates += $env:LW_PYTHON }
    $candidates += 'D:\Espressif\LittleWorld\idf-tools\python_env\idf5.5_py3.12_env\Scripts\python.exe'
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    if (Get-Command py -ErrorAction SilentlyContinue) { return 'py -3' }
    foreach ($name in @('python', 'python3')) {
        if (Get-Command $name -ErrorAction SilentlyContinue) {
            # Windows 应用商店的假 python 会返回 9009，必须实测
            $probe = & $name -c "print('ok')" 2>&1
            if ($LASTEXITCODE -eq 0 -and "$probe" -match 'ok') { return $name }
        }
    }
    return $null
}

$python = Find-Python
if (-not $python) {
    Write-Host "[错误] 找不到可用的 Python。" -ForegroundColor Red
    Write-Host "       请先安装 Python 3.10+，或设置环境变量 LW_PYTHON 指向 python.exe。"
    Write-Host "       本机 ESP-IDF 自带的 Python 也可用（如果已安装 ESP-IDF 工具链）。"
    exit 1
}

$script = Join-Path $PSScriptRoot 'package_v4_sd.py'
if (-not (Test-Path $script)) {
    Write-Host "[错误] 找不到 $script" -ForegroundColor Red
    exit 1
}

$argv = @($script)
if ($Source) { $argv += @('--source', $Source) }
if ($Out) { $argv += @('--out', $Out) }
if ($Json) { $argv += '--json' }

Write-Host "使用 Python: $python"
if ($python -eq 'py -3') {
    & py -3 @argv
} else {
    & $python @argv
}
$code = $LASTEXITCODE

if ($code -eq 0) {
    $outDir = if ($Out) { $Out } else { Join-Path $repoRoot 'dist\littleworld-v4-sd' }
    Write-Host ""
    Write-Host "资源包位置: $outDir" -ForegroundColor Green
    Write-Host "下一步：把其中的 littleworld 文件夹复制到 TF 卡根目录（或用 tools\install_v4_sd.ps1 辅助复制）。"
}
exit $code

# Little World V4.1 — 校验 TF 卡资源包（Windows 包装脚本）
#
# 校验规则与固件 lw_resource_verify() / lw_pack_open() 一致：
#   清单 schema/version、场景魔数与尺寸、角色包魔数/画布/clip/帧数/长度、SHA-256。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\verify_v4_sd.ps1
#   powershell -ExecutionPolicy Bypass -File tools\verify_v4_sd.ps1 -Target E:\
#   powershell -ExecutionPolicy Bypass -File tools\verify_v4_sd.ps1 -RawSource
#   powershell -ExecutionPolicy Bypass -File tools\verify_v4_sd.ps1 -Json

[CmdletBinding()]
param(
    [string]$Target = "",
    [switch]$RawSource,
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
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    if (Get-Command py -ErrorAction SilentlyContinue) { return 'py -3' }
    foreach ($name in @('python', 'python3')) {
        if (Get-Command $name -ErrorAction SilentlyContinue) {
            $probe = & $name -c "print('ok')" 2>&1
            if ($LASTEXITCODE -eq 0 -and "$probe" -match 'ok') { return $name }
        }
    }
    return $null
}

$python = Find-Python
if (-not $python) {
    Write-Host "[错误] 找不到可用的 Python（可用 LW_PYTHON 指定 python.exe）。" -ForegroundColor Red
    exit 1
}

$script = Join-Path $PSScriptRoot 'package_v4_sd.py'
$resolved = if ($Target) { $Target } else { Join-Path $repoRoot 'dist\littleworld-v4-sd' }

$argv = @($script, '--verify-only', '--target', $resolved)
if ($RawSource) { $argv += '--raw-source' }
if ($Json) { $argv += '--json' }

if ($python -eq 'py -3') { & py -3 @argv } else { & $python @argv }
$code = $LASTEXITCODE

if ($code -eq 2) {
    Write-Host ""
    Write-Host "校验未通过：上面的问题需要修掉后才建议上机。" -ForegroundColor Yellow
} elseif ($code -eq 0) {
    Write-Host ""
    Write-Host "校验通过：固件按同一套规则读取不会报错。" -ForegroundColor Green
}
exit $code

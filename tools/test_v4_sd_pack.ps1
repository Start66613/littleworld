# Little World V4.1 — 资源包工具回归测试（正向 + 负向）
#
# 正向：用仓库里真实的 V4 资源打包，然后校验生成的包。
# 负向：复制出若干"坏包"，逐个确认校验器能指出来（缺失/魔数/截断/主题越界/
#       版本不兼容/哈希不符/清单过大）。
#
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_v4_sd_pack.ps1

[CmdletBinding()]
param([string]$WorkDir = "D:\Astral\sd-pack-tests")

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$py = if ($env:LW_PYTHON) { $env:LW_PYTHON } else { 'D:\Espressif\LittleWorld\idf-tools\python_env\idf5.5_py3.12_env\Scripts\python.exe' }
if (-not (Test-Path $py)) {
    $found = Get-Command python -ErrorAction SilentlyContinue
    if (-not $found) { Write-Host "[错误] 找不到 Python" -ForegroundColor Red; exit 1 }
    $py = 'python'
}
$env:PYTHONIOENCODING = 'utf-8'

$script = Join-Path $PSScriptRoot 'package_v4_sd.py'
$pass = 0
$fail = 0

function Report([string]$name, [bool]$ok, [string]$detail = '') {
    if ($ok) {
        $script:pass++
        Write-Host ("[PASS] {0}" -f $name) -ForegroundColor Green
    } else {
        $script:fail++
        Write-Host ("[FAIL] {0} {1}" -f $name, $detail) -ForegroundColor Red
    }
}

function Invoke-Tool([string[]]$extraArgs) {
    $out = & $py $script @extraArgs 2>&1
    return [pscustomobject]@{ Code = $LASTEXITCODE; Text = ($out -join "`n") }
}

function New-Case([string]$name) {
    $dir = Join-Path $WorkDir $name
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Copy-Item (Join-Path $repoRoot 'dist\littleworld-v4-sd\littleworld\*') $dir -Recurse
    return $dir
}

if (Test-Path $WorkDir) { Remove-Item $WorkDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

Write-Host "== 正向：真实资源打包 + 校验 =="
$pkg = Invoke-Tool @('--out', (Join-Path $WorkDir 'built'))
Report '真实资源打包成功' ($pkg.Code -eq 0) $pkg.Text
$ver = Invoke-Tool @('--verify-only', '--target', (Join-Path $WorkDir 'built'))
Report '生成的包通过校验' ($ver.Code -eq 0) $ver.Text

$src = Invoke-Tool @('--verify-only', '--raw-source', '--target', (Join-Path $repoRoot 'assets\visual-v4\runtime'))
Report '仓库原始 V4 资源通过校验' ($src.Code -eq 0) $src.Text

Write-Host ""
Write-Host "== 负向：坏包必须被指出来 =="

# 1 缺文件
$d = New-Case 'missing-file'
Remove-Item (Join-Path $d 'scene-1.lwr') -Force
$r = Invoke-Tool @('--verify-only', '--target', $d)
Report '缺文件被拒绝' (($r.Code -eq 2) -and ($r.Text -match '缺少文件')) $r.Text

# 2 魔数错误
$d = New-Case 'bad-magic'
$bytes = [System.IO.File]::ReadAllBytes((Join-Path $d 'scene-0.lwr'))
$bytes[0] = [byte][char]'X'
[System.IO.File]::WriteAllBytes((Join-Path $d 'scene-0.lwr'), $bytes)
$r = Invoke-Tool @('--verify-only', '--target', $d)
Report '场景魔数错误被拒绝' (($r.Code -eq 2) -and ($r.Text -match '魔数')) $r.Text

# 3 角色包截断
$d = New-Case 'truncated-lwp'
$p = Join-Path $d 'male.lwp'
$bytes = [System.IO.File]::ReadAllBytes($p)
[System.IO.File]::WriteAllBytes($p, $bytes[0..($bytes.Length - 2000)])
$r = Invoke-Tool @('--verify-only', '--target', $d)
Report '角色包截断被拒绝' (($r.Code -eq 2) -and ($r.Text -match 'male\.lwp')) $r.Text

# 4 主题编号越界
$d = New-Case 'theme-range'
Copy-Item (Join-Path $d 'scene-0.lwr') (Join-Path $d 'scene-3.lwr')
$r = Invoke-Tool @('--verify-only', '--target', $d)
Report '主题编号越界被拒绝' (($r.Code -eq 2) -and ($r.Text -match '主题编号越界')) $r.Text

# 5 版本不兼容
$d = New-Case 'bad-version'
$m = Join-Path $d 'littleworld-v4.json'
$text = Get-Content $m -Raw
$text = $text -replace '"version"\s*:\s*\d+', '"version": 9'
[System.IO.File]::WriteAllText($m, $text, (New-Object System.Text.UTF8Encoding($false)))
$r = Invoke-Tool @('--verify-only', '--target', $d)
Report '清单版本不兼容被拒绝' (($r.Code -eq 2) -and ($r.Text -match 'version')) $r.Text

# 6 哈希不符（内容改一字节，长度不变）
$d = New-Case 'hash-mismatch'
$p = Join-Path $d 'scene-2.lwr'
$bytes = [System.IO.File]::ReadAllBytes($p)
$bytes[100] = $bytes[100] -bxor 0xFF
[System.IO.File]::WriteAllBytes($p, $bytes)
$r = Invoke-Tool @('--verify-only', '--target', $d)
Report '内容被改动被哈希清单发现' (($r.Code -eq 2) -and ($r.Text -match 'SHA-256')) $r.Text

# 7 清单过大（固件只读 1023 字节）
$d = New-Case 'manifest-too-big'
$m = Join-Path $d 'littleworld-v4.json'
$pad = '"filler": "' + ('A' * 1200) + '"'
[System.IO.File]::WriteAllText($m, '{"schema":1,"version":4,' + $pad + '}', (New-Object System.Text.UTF8Encoding($false)))
$r = Invoke-Tool @('--verify-only', '--target', $d)
Report '清单超出固件读取上限被拒绝' (($r.Code -eq 2) -and ($r.Text -match '1023|超出|上限')) $r.Text

# 8 空目录
$d = New-Case 'empty'
Get-ChildItem $d -File | Remove-Item -Force
$r = Invoke-Tool @('--verify-only', '--target', $d)
Report '空目录被拒绝' (($r.Code -eq 2) -and ($r.Text -match '缺少')) $r.Text

Write-Host ""
Write-Host ("结果：{0} 通过，{1} 失败" -f $pass, $fail)
if ($fail -gt 0) { exit 1 }
exit 0

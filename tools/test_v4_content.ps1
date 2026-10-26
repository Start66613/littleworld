# Little World V4.1 — 内容文件工具回归测试（正向 + 负向）
#
# 正向：仓库自带的示例内容必须通过校验、能生成设备文件、能再解析回来。
# 负向：每一条都对应 firmware/components/lw_content/src/lw_content.c 里
#       真实会拒绝的一种输入，确认 PC 端在烧到卡之前就能指出来。
#
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_v4_content.ps1

[CmdletBinding()]
param([string]$WorkDir = "")

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $WorkDir) { $WorkDir = Join-Path $env:TEMP 'lw-content-tests' }

$py = if ($env:LW_PYTHON) { $env:LW_PYTHON } else { 'D:\LittleWorld-Meowa\tools\venv\Scripts\python.exe' }
if (-not (Test-Path $py)) {
    $found = Get-Command python -ErrorAction SilentlyContinue
    if (-not $found) { Write-Host "[错误] 找不到 Python" -ForegroundColor Red; exit 1 }
    $py = 'python'
}
$env:PYTHONIOENCODING = 'utf-8'

$script = Join-Path $PSScriptRoot 'package_v4_content.py'
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
    # 工具的失败信息走 stderr；$ErrorActionPreference='Stop' 会把它当成终止错误，
    # 所以这里临时放宽，只按退出码判断。
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & $py $script @extraArgs 2>&1
        return [pscustomobject]@{ Code = $LASTEXITCODE; Text = ($out -join "`n") }
    } finally {
        $ErrorActionPreference = $previous
    }
}

# 写一个临时内容文件；-Bytes 用于制造非法编码
function New-Case([string]$dir, [string]$name, [string]$text) {
    $path = Join-Path $dir $name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))
    return $path
}

$header = "LWCX0001`nschema=1`ncontent_version=1`n"

if (Test-Path $WorkDir) { Remove-Item -Recurse -Force $WorkDir }
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

Write-Host "`n== 正向 ==" -ForegroundColor Cyan

# 1. 仓库自带示例必须通过
$r = Invoke-Tool @('--check', '--source', 'content')
Report "仓库示例内容通过校验" ($r.Code -eq 0) $r.Text

# 2. 示例必须能生成设备文件
$outDir = Join-Path $WorkDir 'out'
$r = Invoke-Tool @('--source', 'content', '--out', $outDir)
$target = Join-Path $outDir 'content.lwcx'
Report "生成 content.lwcx" (($r.Code -eq 0) -and (Test-Path $target)) $r.Text
Report "生成文件以魔数开头" ((Get-Content $target -TotalCount 1) -eq 'LWCX0001')

# 3. 生成的文件必须能自己再解析（往返一致）
$again = Invoke-Tool @('--check', '--source', $target)
Report "生成文件可再次解析" ($again.Code -eq 0) $again.Text

# 4. 模板必须开箱可用
$tplDir = Join-Path $WorkDir 'tpl'
$r = Invoke-Tool @('--init', $tplDir)
Report "写出模板" ($r.Code -eq 0) $r.Text
$r = Invoke-Tool @('--check', '--source', $tplDir)
Report "模板开箱即通过校验" ($r.Code -eq 0) $r.Text
$r = Invoke-Tool @('--init', $tplDir)
Report "模板已存在时拒绝覆盖" ($r.Code -eq 2) $r.Text

# 5. 目录里多个片段要按文件名合并
$multi = Join-Path $WorkDir 'multi'
New-Case $multi '01-a.lwcx' ($header + "`n[memory]`ndate=2024-01-02`ntitle=甲`nbody=一`n[/memory]`n")
New-Case $multi '02-b.lwcx' ($header + "`n[memory]`ndate=2024-03-04`ntitle=乙`nbody=二`n[/memory]`n")
$r = Invoke-Tool @('--check', '--source', $multi, '--json')
Report "多片段合并为 2 条回忆" (($r.Code -eq 0) -and ($r.Text -match '"memories":\s*2')) $r.Text

Write-Host "`n== 负向（每一条都必须在 PC 端被拦住）==" -ForegroundColor Cyan

$cases = @(
    @{ Name = '魔数错误';            Body = "NOPE0001`nschema=1`ncontent_version=1`n";                              Expect = 'first line must be' },
    @{ Name = '缺 content_version';  Body = "LWCX0001`nschema=1`n";                                                  Expect = 'content_version is missing' },
    @{ Name = 'schema 不支持';       Body = "LWCX0001`nschema=2`ncontent_version=1`n";                                Expect = 'is not supported' },
    @{ Name = 'content_version 过高'; Body = "LWCX0001`nschema=1`ncontent_version=9`n";                               Expect = 'is not supported' },
    @{ Name = '日期不存在';          Body = $header + "`n[memory]`ndate=2025-02-30`ntitle=甲`nbody=一`n[/memory]`n";  Expect = 'not a real date' },
    @{ Name = '日期早于 1970';       Body = $header + "`n[memory]`ndate=1969-12-31`ntitle=甲`nbody=一`n[/memory]`n";  Expect = 'not a real date' },
    @{ Name = 'memory 缺 body';      Body = $header + "`n[memory]`ndate=2024-01-02`ntitle=甲`n[/memory]`n";           Expect = "missing 'body'" },
    @{ Name = '段落未闭合';          Body = $header + "`n[memory]`ndate=2024-01-02`ntitle=甲`nbody=一`n";             Expect = 'is never closed' },
    @{ Name = '闭合标签不匹配';      Body = $header + "`n[memory]`ndate=2024-01-02`ntitle=甲`nbody=一`n[/letter]`n";  Expect = 'does not match' },
    @{ Name = '未知段落';            Body = $header + "`n[poem]`n[/poem]`n";                                          Expect = 'unknown section' },
    @{ Name = '未知键';              Body = $header + "`ncolour=red`n";                                               Expect = 'unknown key' },
    @{ Name = '照片路径绝对';        Body = $header + "`n[album]`ntitle=甲`nphoto=/photos/a.lwph`n[/album]`n";         Expect = 'must be relative' },
    @{ Name = '照片路径含 ..';       Body = $header + "`n[album]`ntitle=甲`nphoto=../a.lwph`n[/album]`n";              Expect = "must not contain '..'" },
    @{ Name = '照片路径非法字符';    Body = $header + "`n[album]`ntitle=甲`nphoto=a:b.lwph`n[/album]`n";               Expect = 'cannot store' },
    @{ Name = '照片后缀不对';        Body = $header + "`n[album]`ntitle=甲`nphoto=a.png`n[/album]`n";                  Expect = 'should end in .lwph' },
    @{ Name = '对话缺 speaker';      Body = $header + "`n[dialogue]`nid=10`nline=|没有名字`n[/dialogue]`n";            Expect = 'empty speaker' },
    @{ Name = '对话 id 越界';        Body = $header + "`n[dialogue]`nid=5000`nline=甲|乙`n[/dialogue]`n";              Expect = 'outside 0..' },
    @{ Name = '标题超长';            Body = $header + "`n[memory]`ndate=2024-01-02`ntitle=" + ('长' * 50) + "`nbody=一`n[/memory]`n"; Expect = 'over the 128-byte limit' }
)

foreach ($case in $cases) {
    $dir = Join-Path $WorkDir ('bad-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Case $dir 'content.lwcx' $case.Body | Out-Null
    $r = Invoke-Tool @('--check', '--source', $dir)
    $ok = ($r.Code -eq 1) -and ($r.Text -match [regex]::Escape($case.Expect))
    Report ("拒绝：" + $case.Name) $ok ($r.Text + " [期望包含: " + $case.Expect + "]")
}

# UTF-16 存盘（记事本默认之一）必须被点名
$u16 = Join-Path $WorkDir 'utf16'
New-Item -ItemType Directory -Force -Path $u16 | Out-Null
[System.IO.File]::WriteAllText((Join-Path $u16 'content.lwcx'), ($header + "`n[memory]`ndate=2024-01-02`ntitle=甲`nbody=一`n[/memory]`n"), (New-Object System.Text.UnicodeEncoding($false, $true)))
$r = Invoke-Tool @('--check', '--source', $u16)
Report "拒绝：UTF-16 存盘" (($r.Code -eq 1) -and ($r.Text -match 'UTF-8|NUL')) $r.Text

# 非法 UTF-8 字节
$bad = Join-Path $WorkDir 'badutf8'
New-Item -ItemType Directory -Force -Path $bad | Out-Null
$bytes = [System.Text.Encoding]::ASCII.GetBytes($header + "`n[memory]`ndate=2024-01-02`ntitle=") + [byte[]](0xC3, 0x28) + [System.Text.Encoding]::ASCII.GetBytes("`nbody=x`n[/memory]`n")
[System.IO.File]::WriteAllBytes((Join-Path $bad 'content.lwcx'), $bytes)
$r = Invoke-Tool @('--check', '--source', $bad)
Report "拒绝：非法 UTF-8 字节" (($r.Code -eq 1) -and ($r.Text -match 'not valid UTF-8')) $r.Text

# 重复的对话 id（跨文件才能发现）
$dup = Join-Path $WorkDir 'dup'
New-Case $dup '01.lwcx' ($header + "`n[dialogue]`nid=10`nline=甲|乙`n[/dialogue]`n") | Out-Null
New-Case $dup '02.lwcx' ($header + "`n[dialogue]`nid=10`nline=丙|丁`n[/dialogue]`n") | Out-Null
$r = Invoke-Tool @('--check', '--source', $dup)
Report "拒绝：重复对话 id" (($r.Code -eq 1) -and ($r.Text -match 'duplicate dialogue id')) $r.Text

# 总量超限
$many = Join-Path $WorkDir 'many'
$sb = New-Object System.Text.StringBuilder
[void]$sb.Append($header)
for ($i = 0; $i -lt 33; $i++) {
    [void]$sb.Append("`n[memory]`ndate=2024-01-02`ntitle=甲`nbody=一`n[/memory]`n")
}
New-Case $many 'content.lwcx' $sb.ToString() | Out-Null
$r = Invoke-Tool @('--check', '--source', $many)
Report "拒绝：回忆超过 32 条" (($r.Code -eq 1) -and ($r.Text -match 'over the limit of 32')) $r.Text

# 目录里没有内容文件
$empty = Join-Path $WorkDir 'empty'
New-Item -ItemType Directory -Force -Path $empty | Out-Null
$r = Invoke-Tool @('--check', '--source', $empty)
Report "拒绝：目录里没有 .lwcx" ($r.Code -ne 0) $r.Text

Write-Host ""
Write-Host ("结果：{0} 通过，{1} 失败" -f $pass, $fail)
if ($fail -gt 0) {
    Write-Host "CONTENT TOOL TESTS FAILED" -ForegroundColor Red
    exit 1
}
Write-Host "CONTENT TOOL TESTS PASSED" -ForegroundColor Green
exit 0

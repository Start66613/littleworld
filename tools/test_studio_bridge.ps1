# Little World V4.3 — Studio 桥（.lwpack -> .lwcx）回归测试
#
# 目标：证明 tools/studio_to_device.py 的每一条规则都是真的，而且它产出的文件
# 能被"真实固件解析器"接受（不是 Python 自己再实现一遍解析）。
#
# 覆盖：
#   正向   Studio 演示包 -> .lwcx，计数正确，固件解析器接受
#   负向   超限、非法 UTF-8、版本不兼容、缺文件、ZIP 路径穿越、坏 ZIP、
#          不支持字段在严格模式下必须失败、--allow-partial 才允许
#   约束   源包不被修改；输出是 UTF-8；device limits 与 lw_content.h 一致
#
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_studio_bridge.ps1

[CmdletBinding()]
param([string]$WorkDir = "")

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $WorkDir) { $WorkDir = Join-Path $env:TEMP 'lw-studio-bridge-tests' }

$py = if ($env:LW_PYTHON) { $env:LW_PYTHON } else { 'D:\LittleWorld-Meowa\tools\venv\Scripts\python.exe' }
if (-not (Test-Path $py)) {
    $found = Get-Command python -ErrorAction SilentlyContinue
    if (-not $found) { Write-Host "[错误] 找不到 Python" -ForegroundColor Red; exit 1 }
    $py = 'python'
}
$env:PYTHONIOENCODING = 'utf-8'

$script = Join-Path $PSScriptRoot 'studio_to_device.py'
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
    # 工具的错误走 stderr；$ErrorActionPreference='Stop' 会把它当终止错误，
    # 这里临时放宽，只按退出码判断。
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & $py $script @extraArgs 2>&1
        return [pscustomobject]@{ Code = $LASTEXITCODE; Text = ($out -join "`n") }
    } finally {
        $ErrorActionPreference = $previous
    }
}

if (Test-Path $WorkDir) { Remove-Item -Recurse -Force $WorkDir }
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

# ---------------------------------------------------------------------------
# 1. 取 Studio 演示包（来自 agent/littleworld-studio-v1）
# ---------------------------------------------------------------------------
$demo = Join-Path $WorkDir 'demo.lwpack'
$env:HTTPS_PROXY = 'http://127.0.0.1:7892/'
cmd /c "git -C `"$repoRoot`" show origin/agent/littleworld-studio-v1:studio/examples/demo.lwpack > `"$demo`"" 2>&1 | Out-Null
if (-not (Test-Path $demo) -or (Get-Item $demo).Length -lt 1000) {
    Write-Host "[错误] 取不到 Studio 演示包（需要 origin/agent/littleworld-studio-v1）" -ForegroundColor Red
    exit 1
}
$demoHash = (Get-FileHash $demo -Algorithm SHA256).Hash

Write-Host "`n== 正向 ==" -ForegroundColor Cyan

# 严格模式必须失败：演示包里有设备表示不了的字段
$out1 = Join-Path $WorkDir 'strict.lwcx'
$r = Invoke-Tool @($demo, '--out', $out1)
Report "严格模式拒绝有丢弃字段的包" (($r.Code -eq 1) -and -not (Test-Path $out1)) $r.Text

# --allow-partial 才允许
$out2 = Join-Path $WorkDir 'partial.lwcx'
$reportPath = Join-Path $WorkDir 'partial-report.json'
$r = Invoke-Tool @($demo, '--allow-partial', '--out', $out2, '--report', $reportPath)
Report "允许部分转换后成功" (($r.Code -eq 0) -and (Test-Path $out2)) $r.Text
Report "报告文件生成" (Test-Path $reportPath)

$text = [System.IO.File]::ReadAllText($out2, [System.Text.Encoding]::UTF8)
Report "输出以 LWCX0001 开头" ($text.StartsWith("LWCX0001`n"))
Report "记忆 2 条" (($text.Split("`n") | Where-Object { $_ -eq '[memory]' }).Count -eq 2)
Report "相册 2 条" (($text.Split("`n") | Where-Object { $_ -eq '[album]' }).Count -eq 2)
Report "信件 2 条" (($text.Split("`n") | Where-Object { $_ -eq '[letter]' }).Count -eq 2)
Report "对话 2 组" (($text.Split("`n") | Where-Object { $_ -eq '[dialogue]' }).Count -eq 2)
Report "纪念日来自 Studio" ($text.Contains("anniversary=2024-10-25"))
Report "照片路径改写为 .lwph" ($text.Contains('.lwph'))
Report "照片路径不含 assets/ 前缀" (-not $text.Contains('assets/'))

# 源包不能被改动
$afterHash = (Get-FileHash $demo -Algorithm SHA256).Hash
Report "源包未被修改" ($demoHash -eq $afterHash)

# 固件解析器必须接受
$checker = Join-Path $repoRoot 'build/host_tests/lw_content_tests.exe'
if (Test-Path $checker) {
    $verify = & $checker --check-content $out2 2>&1
    Report "固件解析器接受输出" ($LASTEXITCODE -eq 0) ($verify -join ' ')
} else {
    Report "固件解析器二进制存在（先跑 tools\run_backend_tests.ps1）" $false 'build/host_tests/lw_content_tests.exe 不存在'
}

# ---------------------------------------------------------------------------
# 2. 负向：构造坏包
# ---------------------------------------------------------------------------
Write-Host "`n== 负向 ==" -ForegroundColor Cyan

$mkScript = Join-Path $WorkDir 'make_packs.py'
@'
import json, shutil, sys, zipfile
from pathlib import Path

demo = Path(sys.argv[1]); out = Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
src = zipfile.ZipFile(demo)
files = {n: src.read(n) for n in src.namelist()}
src.close()

def write(name, mutate):
    data = dict(files)
    mutate(data)
    path = out / name
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as z:
        for key, value in data.items():
            z.writestr(key, value)

def bad_schema(d):
    m = json.loads(d["manifest.json"]); m["schema"] = "littleworld-content/2"
    d["manifest.json"] = json.dumps(m, ensure_ascii=False).encode()

def bad_compat(d):
    m = json.loads(d["manifest.json"]); m["compatibility"] = "firmware reader ready"
    d["manifest.json"] = json.dumps(m, ensure_ascii=False).encode()

def too_many_memories(d):
    mem = json.loads(d["memories.json"])
    one = mem[0]
    d["memories.json"] = json.dumps([dict(one, id=f"00000000-0000-0000-0000-{i:012d}") for i in range(33)],
                                    ensure_ascii=False).encode()

def long_title(d):
    mem = json.loads(d["memories.json"]); mem[0]["title"] = "x" * 200
    d["memories.json"] = json.dumps(mem, ensure_ascii=False).encode()

def bad_utf8(d):
    d["memories.json"] = b'[{"id":"x","date":"2024-10-25","title":"\xc3\x28","body":"b","photoId":null,"tags":[],"visible":true}]'

def missing_file(d):
    del d["story.json"]

def traversal(d):
    d["assets/../evil.txt"] = b"nope"

def unsupported_image(d):
    res = json.loads(d["resources.json"]); res[0]["mime"] = "image/svg+xml"
    d["resources.json"] = json.dumps(res).encode()

def bad_date(d):
    mem = json.loads(d["memories.json"]); mem[0]["date"] = "2025-02-30"
    d["memories.json"] = json.dumps(mem, ensure_ascii=False).encode()

def dangling_photo(d):
    mem = json.loads(d["memories.json"]); mem[0]["photoId"] = "ffffffff-ffff-ffff-ffff-ffffffffffff"
    d["memories.json"] = json.dumps(mem, ensure_ascii=False).encode()

write("bad-schema.lwpack", bad_schema)
write("bad-compat.lwpack", bad_compat)
write("too-many-memories.lwpack", too_many_memories)
write("long-title.lwpack", long_title)
write("bad-utf8.lwpack", bad_utf8)
write("missing-file.lwpack", missing_file)
write("traversal.lwpack", traversal)
write("unsupported-image.lwpack", unsupported_image)
write("bad-date.lwpack", bad_date)
write("dangling-photo.lwpack", dangling_photo)
print("packs written")
'@ | Set-Content -Path $mkScript -Encoding utf8
& $py $mkScript $demo (Join-Path $WorkDir 'packs') 2>&1 | Out-Null

function Expect-Fail([string]$name, [string]$file, [string]$expectText) {
    $target = Join-Path $WorkDir ('neg-' + $name + '.lwcx')
    $r = Invoke-Tool @($file, '--allow-partial', '--out', $target)
    $ok = ($r.Code -eq 1) -and (-not (Test-Path $target)) -and ($r.Text -match $expectText)
    Report $name $ok ("code=$($r.Code) " + $r.Text)
}

$packs = Join-Path $WorkDir 'packs'
Expect-Fail "版本不兼容被拒" (Join-Path $packs 'bad-schema.lwpack') 'unsupported schema'
Expect-Fail "兼容标记不符被拒" (Join-Path $packs 'bad-compat.lwpack') 'compatibility marker'
Expect-Fail "回忆超过 32 条被拒" (Join-Path $packs 'too-many-memories.lwpack') 'over the device limit'
Expect-Fail "标题超长被拒（不截断）" (Join-Path $packs 'long-title.lwpack') 'over the device limit'
Expect-Fail "非法 UTF-8 被拒" (Join-Path $packs 'bad-utf8.lwpack') 'not valid UTF-8'
Expect-Fail "缺固定 JSON 被拒" (Join-Path $packs 'missing-file.lwpack') 'missing story.json'
Expect-Fail "ZIP 路径穿越被拒" (Join-Path $packs 'traversal.lwpack') 'unsafe archive path'
Expect-Fail "不支持的图片类型被拒" (Join-Path $packs 'unsupported-image.lwpack') 'mime'
Expect-Fail "不存在的日期被拒" (Join-Path $packs 'bad-date.lwpack') 'does not exist'
Expect-Fail "悬空 photoId 被拒" (Join-Path $packs 'dangling-photo.lwpack') 'not in album.photos'

# 坏 ZIP
$notZip = Join-Path $WorkDir 'not-a-zip.lwpack'
[System.IO.File]::WriteAllText($notZip, "this is not a zip file", (New-Object System.Text.UTF8Encoding($false)))
$r = Invoke-Tool @($notZip, '--allow-partial', '--out', (Join-Path $WorkDir 'neg-zip.lwcx'))
Report "非 ZIP 文件被拒" (($r.Code -eq 1) -and ($r.Text -match 'not a ZIP')) $r.Text

# 不存在的文件
$r = Invoke-Tool @((Join-Path $WorkDir 'nope.lwpack'), '--allow-partial', '--out', (Join-Path $WorkDir 'neg-missing.lwcx'))
Report "源文件不存在被拒" (($r.Code -eq 1) -and ($r.Text -match 'archive not found')) $r.Text

# ---------------------------------------------------------------------------
# 3. 契约：device limits 必须与固件头文件一致
# ---------------------------------------------------------------------------
Write-Host "`n== 契约 ==" -ForegroundColor Cyan

$header = Join-Path $repoRoot 'firmware/components/lw_content/include/lw_content.h'
$headerText = [System.IO.File]::ReadAllText($header, [System.Text.Encoding]::UTF8)
$limits = [ordered]@{
    'LW_CONTENT_MAX_BYTES'      = 'max_bytes'
    'LW_CONTENT_MAX_MEMORIES'   = 'max_memories'
    'LW_CONTENT_MAX_PHOTOS'     = 'max_photos'
    'LW_CONTENT_MAX_LETTERS'    = 'max_letters'
    'LW_CONTENT_MAX_DIALOGUES'  = 'max_dialogues'
    'LW_CONTENT_MAX_LINES'      = 'max_lines'
    'LW_CONTENT_TITLE_MAX'      = 'title_max'
    'LW_CONTENT_BODY_MAX'       = 'body_max'
    'LW_CONTENT_PATH_MAX'       = 'path_max'
    'LW_CONTENT_SPEAKER_MAX'    = 'speaker_max'
}
$pyLimits = & $py -c "import json,sys; sys.path.insert(0,'$($PSScriptRoot -replace '\\','/')'); import studio_to_device as s; print(json.dumps(s.DEVICE_LIMITS))" 2>&1 | Out-String
try {
    $deviceLimits = $pyLimits | ConvertFrom-Json
    $mismatches = @()
    foreach ($key in $limits.Keys) {
        $match = [regex]::Match($headerText, "#define\s+$key\s+(\d+)u")
        if (-not $match.Success) { $mismatches += "$key 未在头文件中找到"; continue }
        $firmwareValue = [int]$match.Groups[1].Value
        $toolValue = [int]$deviceLimits.($limits[$key])
        if ($firmwareValue -ne $toolValue) {
            $mismatches += "$key 固件=$firmwareValue 工具=$toolValue"
        }
    }
    Report "工具限制与固件头文件一致" ($mismatches.Count -eq 0) ($mismatches -join '; ')
} catch {
    Report "工具限制与固件头文件一致" $false $pyLimits
}

Write-Host ""
if ($fail -gt 0) {
    Write-Host ("结果：{0} 通过，{1} 失败" -f $pass, $fail) -ForegroundColor Red
    Write-Host "STUDIO BRIDGE TESTS FAILED" -ForegroundColor Red
    exit 1
}
Write-Host ("结果：{0} 通过，0 失败" -f $pass) -ForegroundColor Green
Write-Host "STUDIO BRIDGE TESTS PASSED" -ForegroundColor Green
exit 0

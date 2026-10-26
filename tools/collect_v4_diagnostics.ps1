# Collects the V4 on-board diagnostics report over UART0 and turns it into a
# JSON record plus a short human-readable summary.
#
# READ-ONLY BY CONSTRUCTION. The only bytes this script ever sends are the three
# console commands below, and every one of them is a pure read:
#   diagjson  - one "LWDJ {json}" line with the full report
#   diag      - the same report as a key/value block (fallback if JSON is cut)
#   status    - heap + display geometry
# It never calls flash, erase, nvs, format, or any radio command, and it never
# reboots the board unless -Reset is passed explicitly.
#
# Three ways to use it, all of which work with no board attached:
#   -SelfTest              parse a synthetic line and check the round trip
#   -FromLog <file>        re-parse a serial log that was captured earlier
#   -Port COM5             talk to the board (needs hardware)
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/collect_v4_diagnostics.ps1 -SelfTest
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/collect_v4_diagnostics.ps1 -Port COM5
# SPDX-License-Identifier: MIT
[CmdletBinding(DefaultParameterSetName = 'Board')]
param(
    [Parameter(ParameterSetName = 'Board')]
    [string]$Port = 'COM5',
    [Parameter(ParameterSetName = 'Board')]
    [int]$Baud = 115200,
    [Parameter(ParameterSetName = 'Board')]
    [int]$Seconds = 20,
    [Parameter(ParameterSetName = 'Board')]
    [switch]$Reset,

    [Parameter(ParameterSetName = 'Replay', Mandatory = $true)]
    [string]$FromLog,

    [Parameter(ParameterSetName = 'SelfTest', Mandatory = $true)]
    [switch]$SelfTest,

    [string]$OutDir,
    [string]$Label = ''
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $root 'reports/diagnostics' }

# Every key the firmware's lw_diag_json() emits. A missing one is a real
# finding, not something to paper over: it means the report and this parser
# have drifted apart.
$RequiredKeys = @(
    'firmware', 'build', 'build_time', 'idf', 'reset_reason', 'boot_ms',
    'heap_free', 'heap_min', 'heap_largest', 'psram_free', 'psram_largest',
    'lv_available', 'lv_used', 'lv_max', 'lv_frag_pct', 'lv_note',
    'pack', 'resource', 'resource_reads', 'resource_failures',
    'frame_failures', 'asset_leases', 'asset_bytes', 'theme',
    'medium', 'medium_reason', 'wifi', 'wifi_reason',
    'last_error', 'last_error_ms'
)

function ConvertFrom-LwdjLine {
    <#
      Extracts and parses the payload of one "LWDJ {...}" console line.
      Returns $null when the line is not a report, and throws when it is a
      report but does not parse - silently dropping a malformed report is how a
      diagnostics tool starts lying.
    #>
    param([string]$Line)

    # A line that starts with the prefix but does not hold a complete object is
    # a real problem, not something to skip: it usually means the UART dropped
    # bytes. Only the closing brace is allowed to be missing here so the JSON
    # parser reports the truncation instead of this regex swallowing it.
    if ($Line -notmatch '^LWDJ\s+(\{.*)$') { return $null }
    $json = $Matches[1]
    if (-not $json.TrimEnd().EndsWith('}')) {
        throw "LWDJ line is truncated (no closing brace): $json"
    }
    try {
        $obj = $json | ConvertFrom-Json
    } catch {
        throw "LWDJ line is not valid JSON: $($_.Exception.Message)"
    }
    $missing = @()
    foreach ($k in $RequiredKeys) {
        if ($null -eq $obj.PSObject.Properties[$k]) { $missing += $k }
    }
    if ($missing.Count -gt 0) {
        throw ("LWDJ report is missing {0} key(s): {1}" -f $missing.Count, ($missing -join ', '))
    }
    return $obj
}

function Format-DiagSummary {
    <#
      Chinese summary. Every line states a measured value or says "unknown";
      nothing here guesses. The Wi-Fi line in particular refuses to translate
      "compiled" into "working".
    #>
    param($Report, [string]$Source)

    $wifiText = switch ([string]$Report.wifi) {
        '0' { '0 = 本构建未包含射频协议栈（无 Wi-Fi）' }
        '1' { '1 = 协议栈已编译进镜像，但 station 尚未启动' }
        '2' { '2 = esp_wifi_start() 已成功' }
        '3' { '3 = 协议栈启动失败' }
        default { "$($Report.wifi) = 未知取值" }
    }

    $lines = @()
    $lines += "# V4 板端诊断快照"
    $lines += ''
    $lines += "- 采集来源：$Source"
    $lines += "- 采集时间：$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
    $lines += ''
    $lines += "## 镜像"
    $lines += ''
    $lines += "| 项 | 值 |"
    $lines += "| --- | --- |"
    $lines += "| 固件版本 | $($Report.firmware) |"
    $lines += "| 构建配置 | $($Report.build) |"
    $lines += "| 编译时间 | $($Report.build_time) |"
    $lines += "| IDF | $($Report.idf) |"
    $lines += "| 复位原因码 | $($Report.reset_reason) |"
    $lines += "| 启动至今 | $($Report.boot_ms) ms |"
    $lines += ''
    $lines += "## 内存（本次实测）"
    $lines += ''
    $lines += "| 项 | 字节 |"
    $lines += "| --- | --- |"
    $lines += "| 内部堆空闲 | $($Report.heap_free) |"
    $lines += "| 内部堆历史最低 | $($Report.heap_min) |"
    $lines += "| 内部最大连续块 | $($Report.heap_largest) |"
    $lines += "| PSRAM 空闲 | $($Report.psram_free) |"
    $lines += "| PSRAM 最大连续块 | $($Report.psram_largest) |"
    $lines += ''
    if ($Report.lv_available) {
        $lines += "- LVGL 堆：已用 $($Report.lv_used) B，峰值 $($Report.lv_max) B，碎片 $($Report.lv_frag_pct)%（$($Report.lv_note)）"
    } else {
        $lines += "- LVGL 堆：**未取得测量值**（$($Report.lv_note)）。LVGL 使用 C 库分配器时 lv_mem_monitor() 是空实现，此时上表的内部堆数字才是有效数据。"
    }
    $lines += ''
    $lines += "## 资源与显示"
    $lines += ''
    $lines += "| 项 | 值 |"
    $lines += "| --- | --- |"
    $lines += "| 资源包版本 | $($Report.pack) |"
    $lines += "| 资源状态 | $($Report.resource) |"
    $lines += "| 资源读取次数 | $($Report.resource_reads) |"
    $lines += "| 资源读取失败 | $($Report.resource_failures) |"
    $lines += "| 帧加载失败 | $($Report.frame_failures) |"
    $lines += "| 素材租约 | $($Report.asset_leases) |"
    $lines += "| 素材字节 | $($Report.asset_bytes) |"
    $lines += "| 主题索引 | $($Report.theme) |"
    $lines += "| TF 卡 | $(if ($Report.medium) { '已挂载' } else { '未挂载' })：$($Report.medium_reason) |"
    $lines += ''
    $lines += "## 连接能力"
    $lines += ''
    $lines += "- Wi-Fi 状态：$wifiText"
    $lines += "- Wi-Fi 说明：$($Report.wifi_reason)"
    $lines += ''
    $lines += "## 最近错误"
    $lines += ''
    if ($Report.last_error -and $Report.last_error -ne 'none') {
        $lines += "- $($Report.last_error)（发生在 $($Report.last_error_ms) ms）"
    } else {
        $lines += "- 无记录"
    }
    $lines += ''
    return ($lines -join "`n")
}

function Save-DiagRecord {
    param($Report, [string]$Source, [string]$Tag)

    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $suffix = if ($Tag) { "-$Tag" } else { '' }
    $jsonPath = Join-Path $OutDir "diag-$stamp$suffix.json"
    $mdPath = Join-Path $OutDir "diag-$stamp$suffix.md"

    $record = [ordered]@{
        schema = 'littleworld.diag/1'
        collected_at = (Get-Date).ToString('o')
        source = $Source
        report = $Report
    }
    $json = $record | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText($jsonPath, $json, (New-Object System.Text.UTF8Encoding($false)))
    [System.IO.File]::WriteAllText($mdPath, (Format-DiagSummary -Report $Report -Source $Source), (New-Object System.Text.UTF8Encoding($false)))
    Write-Output "wrote $jsonPath"
    Write-Output "wrote $mdPath"
}

function Invoke-SelfTest {
    # A synthetic report with the same shape the firmware emits. If this stops
    # parsing, the parser and the firmware have diverged.
    $fake = 'LWDJ {"firmware":"4.1.0","build":"os","build_time":"Oct 26 2026","idf":"v5.5.5","reset_reason":3,"boot_ms":8123,"heap_free":200000,"heap_min":150000,"heap_largest":65536,"psram_free":27000000,"psram_largest":20000000,"lv_available":false,"lv_used":0,"lv_max":0,"lv_frag_pct":0,"lv_note":"no LVGL provider","pack":"lw4.1-2026.10","resource":"ok","resource_reads":1234,"resource_failures":7,"frame_failures":3,"asset_leases":2,"asset_bytes":442368,"theme":2,"medium":true,"medium_reason":"sdcard at /sdcard","wifi":1,"wifi_reason":"CONFIG_LW_NET_WIFI_ENABLE is off in this build","last_error":"none","last_error_ms":0}'
    $checks = 0
    $failures = @()

    $r = ConvertFrom-LwdjLine -Line $fake
    $checks++
    if ($null -eq $r) { $failures += 'synthetic line did not parse' }
    if ($r) {
        $checks++
        if ($r.firmware -ne '4.1.0') { $failures += "firmware round trip: $($r.firmware)" }
        $checks++
        if ($r.heap_free -ne 200000) { $failures += "heap round trip: $($r.heap_free)" }
        $checks++
        if ($r.wifi -ne 1) { $failures += "wifi round trip: $($r.wifi)" }
    }

    $checks++
    if ($null -ne (ConvertFrom-LwdjLine -Line 'I (1234) lw_diag: diagnostics ready')) {
        $failures += 'a log line was accepted as a report'
    }
    $checks++
    if ($null -ne (ConvertFrom-LwdjLine -Line 'LCAP 12 AAAA')) {
        $failures += 'a capture line was accepted as a report'
    }

    # A truncated JSON body must throw, not be silently skipped.
    $checks++
    try {
        ConvertFrom-LwdjLine -Line 'LWDJ {"firmware":"4.1.0",' | Out-Null
        $failures += 'truncated JSON did not throw'
    } catch { }

    # A report missing a key must throw.
    $checks++
    try {
        ConvertFrom-LwdjLine -Line 'LWDJ {"firmware":"4.1.0"}' | Out-Null
        $failures += 'incomplete report did not throw'
    } catch { }

    # The summary must state the LVGL limitation rather than print zeros.
    $checks++
    $md = Format-DiagSummary -Report $r -Source 'selftest'
    if ($md -notmatch '未取得测量值') { $failures += 'summary hid the missing LVGL measurement' }
    $checks++
    if ($md -notmatch '未包含射频协议栈|已编译进镜像') { $failures += 'summary did not explain the wifi state' }

    if ($failures.Count -gt 0) {
        Write-Output ("{0} checks, {1} failure(s)" -f $checks, $failures.Count)
        foreach ($f in $failures) { Write-Output "FAIL: $f" }
        throw 'DIAG COLLECTOR SELFTEST FAILED'
    }
    Write-Output ("{0} checks, 0 failure(s)" -f $checks)
    Write-Output 'DIAG COLLECTOR SELFTEST PASSED'
}

if ($SelfTest) {
    Invoke-SelfTest
    return
}

if ($FromLog) {
    if (-not (Test-Path $FromLog)) { throw "log not found: $FromLog" }
    $text = [System.IO.File]::ReadAllText((Resolve-Path $FromLog))
    $report = $null
    foreach ($line in ($text -split "`r?`n")) {
        $candidate = ConvertFrom-LwdjLine -Line $line
        if ($candidate) { $report = $candidate }
    }
    if (-not $report) { throw "no LWDJ line found in $FromLog" }
    Save-DiagRecord -Report $report -Source "replay: $FromLog" -Tag $Label
    return
}

# ---- live board -----------------------------------------------------------
# Read-only: only diagjson / diag / status are sent.
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 500
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$lines = New-Object System.Collections.Generic.List[string]
$report = $null
$plain = New-Object System.Text.StringBuilder

try {
    $sp.Open()
    if ($Reset) {
        $sp.RtsEnable = $true
        Start-Sleep -Milliseconds 200
        $sp.RtsEnable = $false
        Start-Sleep -Milliseconds 80
    }
    Start-Sleep -Milliseconds 300
    $sp.DiscardInBuffer()
    foreach ($cmd in @('diagjson', 'status')) {
        $sp.WriteLine($cmd)
        Start-Sleep -Milliseconds 400
    }
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        try { $chunk = $sp.ReadExisting() } catch { $chunk = '' }
        if (-not $chunk) { Start-Sleep -Milliseconds 120; continue }
        [void]$plain.Append($chunk)
        foreach ($line in ($chunk -split "`r?`n")) {
            $t = $line.Trim()
            if (-not $t) { continue }
            $lines.Add($t)
            $candidate = ConvertFrom-LwdjLine -Line $t
            if ($candidate) { $report = $candidate }
        }
        if ($report) { break }
    }
} finally {
    if ($sp.IsOpen) { $sp.Close() }
    $sp.Dispose()
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$rawPath = Join-Path $OutDir ("diag-{0}-console.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
[System.IO.File]::WriteAllText($rawPath, $plain.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Output "console log: $rawPath"

if (-not $report) {
    throw 'no LWDJ report received. Check the console baud (115200) and that the build has CONFIG_LW_DIAG_CONSOLE=y.'
}
Save-DiagRecord -Report $report -Source "board: $Port" -Tag $Label

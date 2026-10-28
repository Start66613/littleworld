# V4.4 30-minute soak test.
#
# Acceptance criterion from the handover: "runs 30 minutes without anomalies".
# This script cycles the six pages, and every minute records heap / PSRAM / cache
# counters / last_error from `diagjson`, so a leak shows up as a monotonic decline
# in heap_free with no recovery, and a fault shows up as a non-"none" last_error.
#
# It only reads the board: no flash, no NVS, no partition writes.

param(
    [string]$Port = 'COM5',
    [int]$Baud = 115200,
    [int]$Minutes = 30,
    [string]$LogDir = 'reports/logs/v44'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $LogDir "stress-$stamp.log"

function Send-Lines {
    param([string[]]$Lines, [int]$SettleMs = 900)
    $sp = New-Object System.IO.Ports.SerialPort $Port, $Baud
    $sp.NewLine = "`n"
    $sp.ReadTimeout = 400
    $sp.Open()
    Start-Sleep -Milliseconds 300
    $out = New-Object System.Text.StringBuilder
    foreach ($entry in $Lines) {
        # Steps use the same 'command|delayMs' shape as tools/console_seq.ps1.
        $parts = $entry -split '\|', 2
        $line = $parts[0]
        $delay = if ($parts.Count -gt 1) { [int]$parts[1] } else { $SettleMs }
        $sp.WriteLine($line)
        $deadline = (Get-Date).AddMilliseconds($delay)
        while ((Get-Date) -lt $deadline) {
            try { [void]$out.Append($sp.ReadExisting()) } catch { }
            Start-Sleep -Milliseconds 80
        }
    }
    $sp.Close()
    $sp.Dispose()
    return $out.ToString()
}

# Page navigation: dock handle, then the item centre, then (for pet/RPG) the world button.
# `Id` is the number `status` prints as "page <n>" — used to verify the taps actually
# arrived, because a row labelled by intent is worthless if navigation silently failed.
$pages = @(
    @{ Name = 'desktop';  Id = 0; Steps = @('tap 862 24|900') },
    @{ Name = 'world';    Id = 1; Steps = @('tap 512 578|700', 'tap 308 541|2200') },
    @{ Name = 'music';    Id = 6; Steps = @('tap 512 578|700', 'tap 554 541|2200') },
    @{ Name = 'pet';      Id = 8; Steps = @('tap 512 578|700', 'tap 308 541|2200', 'tap 866 438|2200') },
    @{ Name = 'rpg';      Id = 9; Steps = @('tap 862 24|900', 'tap 512 578|700', 'tap 308 541|2200', 'tap 866 372|2200') },
    @{ Name = 'memories'; Id = 5; Steps = @('tap 862 24|900', 'tap 512 578|700', 'tap 472 541|2200') }
)

"# V4.4 soak: $Minutes minutes, port $Port, started $(Get-Date -Format o)" | Set-Content -LiteralPath $log -Encoding utf8
$rows = @()
$start = Get-Date
$deadline = $start.AddMinutes($Minutes)
$m = 0
# The loop is wall-clock bounded, not iteration bounded: one cycle (6 pages, with the
# taps needed to reach each) takes a few seconds, so 30 iterations would finish in
# under 3 minutes and would not be a soak test at all.
while ((Get-Date) -lt $deadline) {
    $m++
    $page = $pages[($m - 1) % $pages.Count]
    # `sched reset` before each sample so avg_gap describes THIS page only, not the
    # running average since boot (which drifts upward as pages are rebuilt).
    $steps = $page.Steps + @('sched reset|200', 'status|6000', 'sched|400', 'diagjson|1000')
    $raw = Send-Lines -Lines $steps -SettleMs 1400
    # `status` prints "page <n>, overlay <n>, dock <...>". Capture it and compare with
    # the page this row INTENDED to visit: a soak row labelled by intent is worthless
    # if the taps silently failed to navigate (no back button on the desktop page, a
    # dock that auto-collapsed, etc.). `page_ok` makes that visible instead of implied.
    $actualPage = if ($raw -match 'page (\d+), overlay') { [int]$Matches[1] } else { $null }
    $row = [pscustomobject]@{
        minute      = $m
        at          = (Get-Date).ToString('HH:mm:ss')
        page        = $page.Name
        page_id     = $actualPage
        page_ok     = ($actualPage -eq $page.Id)
        heap_free   = if ($raw -match '"heap_free":(\d+)') { [int64]$Matches[1] } else { $null }
        heap_min    = if ($raw -match '"heap_min":(\d+)') { [int64]$Matches[1] } else { $null }
        psram_free  = if ($raw -match '"psram_free":(\d+)') { [int64]$Matches[1] } else { $null }
        cache_res   = if ($raw -match '"cache_resident":(\d+)') { [int64]$Matches[1] } else { $null }
        evictions   = if ($raw -match '"cache_evictions":(\d+)') { [int]$Matches[1] } else { $null }
        last_error  = if ($raw -match '"last_error":"([^"]*)"') { $Matches[1] } else { 'parse-fail' }
        avg_gap     = if ($raw -match 'avg_gap=(\d+) ms') { [int]$Matches[1] } else { $null }
        max_gap     = if ($raw -match 'max_gap=(\d+) ms') { [int]$Matches[1] } else { $null }
        ticks       = if ($raw -match 'ticks=(\d+)') { [int]$Matches[1] } else { $null }
        safe_mode   = if ($raw -match '"safe_mode":(\w+)') { $Matches[1] } else { $null }
    }
    $rows += $row
    $flag = if ($row.page_ok) { ' ' } else { '!' }
    $line = ('{0}  #{1,3}  {2}page {3,-9}(id {4})  heap {5}  psram {6}  resident {7}  evict {8}  sched {9}/{10} ms  ticks {11}  err {12}' -f `
        $row.at, $row.minute, $flag, $row.page, $row.page_id, $row.heap_free, $row.psram_free, $row.cache_res, $row.evictions, `
        $row.avg_gap, $row.max_gap, $row.ticks, $row.last_error)
    $line | Add-Content -LiteralPath $log -Encoding utf8
    Write-Host $line
}

$elapsed = (Get-Date) - $start
$valid = $rows | Where-Object { $_.heap_free -ne $null }
$summary = @()
$summary += "# soak finished after $([math]::Round($elapsed.TotalMinutes,1)) minutes, $($rows.Count) samples"
if ($valid.Count -ge 2) {
    $first = $valid[0].heap_free
    $last = $valid[-1].heap_free
    $minHeap = ($valid | Measure-Object -Property heap_free -Minimum).Minimum
    $summary += "heap_free first=$first last=$last delta=$($last - $first) min=$minHeap"
    $psFirst = $valid[0].psram_free
    $psLast = $valid[-1].psram_free
    $summary += "psram_free first=$psFirst last=$psLast delta=$($psLast - $psFirst)"
    $evFirst = $valid[0].evictions
    $evLast = $valid[-1].evictions
    $summary += "cache_evictions first=$evFirst last=$evLast"
}
$badErr = $valid | Where-Object { $_.last_error -ne 'none' }
$summary += "samples with non-none last_error: $($badErr.Count)"
$badSafe = $valid | Where-Object { $_.safe_mode -ne 'false' }
$summary += "samples with safe_mode true: $($badSafe.Count)"
# A row that never reached its intended page describes a different page, so report the
# mismatch count explicitly rather than letting it silently pollute the per-page numbers.
$badPage = $valid | Where-Object { -not $_.page_ok }
$summary += "samples that did not reach the intended page: $($badPage.Count) of $($valid.Count)"
if ($badPage.Count -gt 0) {
    $summary += 'misrouted: ' + (($badPage | ForEach-Object { "$($_.page)->id $($_.page_id)" }) -join ', ')
}
$summary | ForEach-Object { $_ | Add-Content -LiteralPath $log -Encoding utf8; Write-Host $_ }
Write-Host "log: $log"

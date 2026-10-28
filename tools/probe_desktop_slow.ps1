# Probe: why does the desktop page sometimes sit at ~116 ms instead of ~22 ms?
#
# The 32-minute soak (reports/logs/v44/stress-20261028-180752.log) showed desktop and
# world dropping to 114-118 ms in bursts of several minutes, while music and memories
# stayed at 20 ms the whole time. 116 ms is suspiciously close to the RPG page's
# 115-119 ms, so the working hypothesis is a leaked sprite or leaked page timer that
# keeps redrawing after its page is gone.
#
# This probe parks on the desktop page and samples, every cycle, both the LVGL timer
# gap and the live-sprite / lease counters, so a slow burst can be matched against
# sprite counts instead of guessed at.
param(
    [string]$Port = 'COM5',
    [int]$Baud = 115200,
    [int]$Minutes = 8,
    [string]$LogDir = 'reports/logs/v44'
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$log = Join-Path $LogDir ('probe-desktop-' + (Get-Date).ToString('yyyyMMdd-HHmmss') + '.log')
"# desktop slow-burst probe: $Minutes min, port $Port, started $(Get-Date -Format o)" |
    Set-Content -LiteralPath $log -Encoding utf8

function Send-Lines([string[]]$Lines, [int]$SettleMs) {
    $sp = New-Object System.IO.Ports.SerialPort $Port, $Baud
    $sp.NewLine = "`n"
    $sp.ReadTimeout = 1000
    $buf = New-Object System.Text.StringBuilder
    try {
        $sp.Open()
        Start-Sleep -Milliseconds 300
        $sp.DiscardInBuffer()
        foreach ($line in $Lines) {
            $cmd, $delay = $line -split '\|'
            $sp.WriteLine($cmd)
            Start-Sleep -Milliseconds ([int]$delay)
            $chunk = ''
            while ($true) {
                try { $chunk += $sp.ReadExisting(); Start-Sleep -Milliseconds 120 }
                catch { break }
                if ($chunk.Length -gt 0) {
                    Start-Sleep -Milliseconds 250
                    $chunk += $sp.ReadExisting()
                    break
                }
            }
            [void]$buf.Append($chunk)
        }
        Start-Sleep -Milliseconds $SettleMs
        [void]$buf.Append($sp.ReadExisting())
    } finally {
        if ($sp.IsOpen) { $sp.Close() }
        $sp.Dispose()
    }
    return $buf.ToString()
}

$start = Get-Date
$deadline = $start.AddMinutes($Minutes)
$n = 0
$rows = @()

while ((Get-Date) -lt $deadline) {
    $n++
    # Return to the desktop page (no back button there: 862,24 is inert), then measure.
    $raw = Send-Lines -Lines @('tap 862 24|900', 'sched reset|200', 'status|6000', 'sched|400', 'diagjson|1200', 'anim|900') -SettleMs 1400
    $actualPage = if ($raw -match 'page (\d+), overlay') { [int]$Matches[1] } else { $null }
    $row = [pscustomobject]@{
        at         = (Get-Date).ToString('HH:mm:ss')
        n          = $n
        page_id    = $actualPage
        avg_gap    = if ($raw -match 'avg_gap=(\d+) ms') { [int]$Matches[1] } else { $null }
        max_gap    = if ($raw -match 'max_gap=(\d+) ms') { [int]$Matches[1] } else { $null }
        ticks      = if ($raw -match 'ticks=(\d+)') { [int]$Matches[1] } else { $null }
        sprites    = if ($raw -match '"anim_sprites":(\d+)') { [int]$Matches[1] } else { $null }
        running    = if ($raw -match '"anim_running":(\d+)') { [int]$Matches[1] } else { $null }
        leases     = if ($raw -match '"asset_leases":(\d+)') { [int]$Matches[1] } else { $null }
        leaseBytes = if ($raw -match '"asset_bytes":(\d+)') { [int64]$Matches[1] } else { $null }
        resident   = if ($raw -match '"cache_resident":(\d+)') { [int64]$Matches[1] } else { $null }
        evictions  = if ($raw -match '"cache_evictions":(\d+)') { [int]$Matches[1] } else { $null }
        heap       = if ($raw -match '"heap_free":(\d+)') { [int64]$Matches[1] } else { $null }
        a0         = if ($raw -match 'actor 0: clip=(\S+) frame=(\d+)') { "$($Matches[1])/$($Matches[2])" } else { '?' }
        a1         = if ($raw -match 'actor 1: clip=(\S+) frame=(\d+)') { "$($Matches[1])/$($Matches[2])" } else { '?' }
    }
    $rows += $row
    $line = ('{0} #{1,3} page {2} sched {3}/{4} ticks {5}  sprites {6} running {7}  leases {8} bytes {9}  resident {10} evict {11}  a0 {12}  a1 {13}' -f `
        $row.at, $row.n, $row.page_id, $row.avg_gap, $row.max_gap, $row.ticks, $row.sprites, $row.running, `
        $row.leases, $row.leaseBytes, $row.resident, $row.evictions, $row.a0, $row.a1)
    $line | Add-Content -LiteralPath $log -Encoding utf8
    Write-Host $line
}

$elapsed = (Get-Date) - $start
$valid = $rows | Where-Object { $_.avg_gap -ne $null }
$slow = $valid | Where-Object { $_.avg_gap -ge 80 }
$fast = $valid | Where-Object { $_.avg_gap -lt 80 }
$summary = @()
$summary += "# probe finished after $([math]::Round($elapsed.TotalMinutes,1)) minutes, $($rows.Count) samples"
$summary += "slow (avg>=80 ms): $($slow.Count)   fast: $($fast.Count)"
if ($slow.Count -gt 0) {
    $summary += 'slow sprite counts: ' + (($slow | ForEach-Object { $_.sprites }) -join ',')
    $summary += 'slow lease counts:  ' + (($slow | ForEach-Object { $_.leases }) -join ',')
    $summary += 'slow avg_gap: ' + (($slow | ForEach-Object { $_.avg_gap }) -join ',')
}
if ($fast.Count -gt 0) {
    $summary += 'fast sprite counts: ' + (($fast | ForEach-Object { $_.sprites }) -join ',')
    $summary += 'fast lease counts:  ' + (($fast | ForEach-Object { $_.leases }) -join ',')
    $summary += 'fast avg_gap: ' + (($fast | ForEach-Object { $_.avg_gap }) -join ',')
}
$summary | ForEach-Object { $_ | Add-Content -LiteralPath $log -Encoding utf8; Write-Host $_ }
Write-Host "log: $log"

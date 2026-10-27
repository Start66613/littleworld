# Reads one base64 BMP produced by the on-board "shot" command.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\board_shot.ps1 -Port COM5 -Out reports\board\01-desktop.bmp
#
# The board numbers every line ("LCAP <index> <base64 chunk>") and ends with
# "LCAP END <count>". ESP_LOG traffic from other tasks occasionally eats an
# unnumbered line, so this script keeps what it got and re-runs the command,
# merging by index, until nothing is missing.
# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [string]$Port = 'COM5',
    [int]$Baud = 115200,
    [string]$Command = 'shot',
    [string]$Out = 'D:\LittleWorld-V3\reports\board\shot.bmp',
    [int]$Passes = 3,
    [int]$PassTimeoutMs = 150000
)

$ErrorActionPreference = 'Stop'
$chunks = @{}
$total = -1

for ($pass = 1; $pass -le $Passes; $pass++) {
    $sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    # 4 KiB is the .NET default and it overflows while the board streams base64
    # at full speed, which loses whole runs of lines (observed: 0..4 and
    # 1902..1906 missing in one pass). Read in byte batches instead of by line:
    # ReadLine() also discards the partial line whenever ReadTimeout fires.
    $sp.ReadBufferSize = 1MB
    $sp.ReadTimeout = 500
    $sp.NewLine = "`n"
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
    $sp.Open()
    try {
        Start-Sleep -Milliseconds 200
        $sp.DiscardInBuffer()
        $sp.WriteLine($Command)
        $deadline = (Get-Date).AddMilliseconds($PassTimeoutMs)
        $sawEnd = $false
        $pending = New-Object System.Text.StringBuilder
        $buf = New-Object byte[] 8192
        while (-not $sawEnd -and (Get-Date) -lt $deadline) {
            $n = 0
            try { $n = $sp.Read($buf, 0, $buf.Length) } catch { $n = 0 }
            if ($n -le 0) { continue }
            [void]$pending.Append([System.Text.Encoding]::ASCII.GetString($buf, 0, $n))
            while ($true) {
                $text = $pending.ToString()
                $nl = $text.IndexOf("`n")
                if ($nl -lt 0) { break }
                $line = $text.Substring(0, $nl).TrimEnd("`r")
                [void]$pending.Remove(0, $nl + 1)
                if ($line -match '^LCAP END (\d+)$') {
                    $total = [int]$Matches[1]
                    $sawEnd = $true
                    break
                }
                if ($line -match '^LCAP FAIL (.+)$') { throw "board reported: $line" }
                if ($line -match '^LCAP (\d+) ([A-Za-z0-9+/=]+)$') {
                    $idx = [int]$Matches[1]
                    if (-not $chunks.ContainsKey($idx)) { $chunks[$idx] = $Matches[2] }
                }
            }
        }
    } finally {
        if ($sp.IsOpen) { $sp.Close() }
        $sp.Dispose()
    }

    if ($total -lt 0) { Write-Output ("pass {0}: no LCAP END yet ({1} lines)" -f $pass, $chunks.Count); continue }
    $missing = 0
    for ($i = 0; $i -lt $total; $i++) { if (-not $chunks.ContainsKey($i)) { $missing++ } }
    Write-Output ("pass {0}: {1}/{2} lines, {3} missing" -f $pass, $chunks.Count, $total, $missing)
    if ($missing -eq 0) { break }
}

if ($total -lt 0) { throw 'no LCAP END received' }
$missingIdx = @()
for ($i = 0; $i -lt $total; $i++) { if (-not $chunks.ContainsKey($i)) { $missingIdx += $i } }
if ($missingIdx.Count -gt 0) { throw ("capture still incomplete, missing {0} of {1} lines (first: {2})" -f $missingIdx.Count, $total, ($missingIdx[0..([Math]::Min(9, $missingIdx.Count - 1))] -join ',')) }

$sb = New-Object System.Text.StringBuilder
for ($i = 0; $i -lt $total; $i++) { [void]$sb.Append($chunks[$i]) }
$bytes = [Convert]::FromBase64String($sb.ToString())

$dir = Split-Path -Parent $Out
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
[System.IO.File]::WriteAllBytes($Out, $bytes)
Write-Output ("{0}: {1} bytes, {2} lines" -f $Out, $bytes.Length, $total)
